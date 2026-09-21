// CyGPUInspectorApp — connection to one CyGPUInspectorRS session.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "SessionClient.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace cygi
{
	SessionClient::~SessionClient()
	{
		Disconnect();
	}

	bool SessionClient::Connect(const SessionEntry &entry)
	{
		Disconnect();

		m_entry = entry;
		m_last_error.clear();
		m_model.Clear();

		if (entry.protocol_version != kProtocolVersion)
		{
			m_last_error = "protocol version mismatch (add-on " + std::to_string(entry.protocol_version) +
				", app " + std::to_string(kProtocolVersion) + ")";
			return false;
		}

		if (!m_memory.Open(entry.ring_name.c_str(), RingMappingSize(entry.ring_capacity)))
		{
			m_last_error = "the event ring of this session could not be opened";
			return false;
		}
		if (!m_reader.Attach(m_memory.Data(), entry.ring_capacity))
		{
			m_last_error = "the event ring has an unexpected layout";
			m_memory.Close();
			return false;
		}
		m_signal.Open(entry.signal_name.c_str());

		if (m_pipe.Connect(entry.pipe_name.c_str(), 1000))
		{
			HelloRequest hello = {};
			hello.protocol_version = kProtocolVersion;
			hello.app_process_id = GetCurrentProcessId();
			std::strncpy(hello.app_version, kVersionString, sizeof(hello.app_version) - 1);

			ControlMessage reply;
			if (SendAndWait(ControlType::hello, &hello, sizeof(hello), reply, 2000))
			{
				const HelloAck *ack = reply.As<HelloAck>();
				if (ack == nullptr || ack->accepted == 0)
					m_last_error = "the add-on refused the connection";
			}
			else
			{
				m_last_error = "the add-on did not answer the handshake (is the game rendering?)";
			}
		}
		else
		{
			m_last_error = "control pipe unavailable, read only connection";
		}

		m_running.store(true, std::memory_order_release);
		m_thread = std::thread(&SessionClient::ReaderThread, this);
		return true;
	}

	void SessionClient::Disconnect()
	{
		if (m_running.exchange(false, std::memory_order_acq_rel))
		{
			m_signal.Signal(); // wake the reader thread so it can exit
			if (m_thread.joinable())
				m_thread.join();
		}
		else if (m_thread.joinable())
		{
			m_thread.join();
		}

		m_pipe.Disconnect();
		m_reader.Detach();
		m_signal.Close();
		m_memory.Close();
	}

	void SessionClient::ReaderThread()
	{
		while (m_running.load(std::memory_order_acquire))
		{
			// Drain everything that is pending, then sleep on the signal the add-on sets once
			// per frame. A timeout keeps the loop alive even if the game stops presenting.
			bool any = false;
			const RecordHeader *header = nullptr;
			const uint8_t *payload = nullptr;
			uint32_t payload_size = 0;

			while (m_reader.Peek(header, payload, payload_size))
			{
				m_model.ApplyRecord(*header, payload, payload_size);
				m_reader.Pop();
				any = true;
			}

			if (!any)
			{
				if (!m_signal.IsValid())
					Sleep(8);
				else
					m_signal.Wait(100);
			}
		}
	}

	bool SessionClient::SendAndWait(ControlType type, const void *fixed, uint32_t fixed_size,
	                                ControlMessage &reply, uint32_t timeout_ms)
	{
		if (!m_pipe.IsConnected())
			return false;

		const uint64_t request_id = m_pipe.NextRequestId();
		if (!m_pipe.Send(type, request_id, fixed, fixed_size))
			return false;

		// The add-on answers from its present thread, so a reply can take one frame.
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			if (!m_pipe.Receive(reply, timeout_ms))
				return false;
			if (reply.request_id == request_id)
				return true;
		}
		return false;
	}

	bool SessionClient::SetLevel(TrackingLevel level)
	{
		SetLevelRequest request = {};
		request.level = level;

		ControlMessage reply;
		return SendAndWait(ControlType::set_level, &request, sizeof(request), reply);
	}

	bool SessionClient::RequestFullSync()
	{
		ControlMessage reply;
		return SendAndWait(ControlType::full_sync, nullptr, 0, reply);
	}

	bool SessionClient::StartDeepCapture(uint32_t frame_count, bool bindings, bool barriers,
	                                     bool per_draw_timing, bool buffers, std::string &message)
	{
		CaptureFrameRequest request = {};
		request.frame_count = frame_count != 0 ? frame_count : 1;
		request.mode = CaptureMode::deep;
		request.include_bindings = bindings ? 1u : 0u;
		request.include_barriers = barriers ? 1u : 0u;
		request.per_draw_timing = per_draw_timing ? 1u : 0u;
		request.max_draw_states = 0;   // the add-on picks its own ceiling
		request.include_buffers = buffers ? 1u : 0u;

		ControlMessage reply;
		if (!SendAndWait(ControlType::capture_frame, &request, sizeof(request), reply))
		{
			message = "the add-on did not answer";
			return false;
		}

		const ControlAck *ack = reply.As<ControlAck>();
		if (ack == nullptr)
		{
			message = "the add-on sent a malformed reply";
			return false;
		}
		if (ack->message_length != 0)
			message.assign(reinterpret_cast<const char *>(reply.payload.data() + sizeof(ControlAck)),
				ack->message_length);
		return ack->succeeded != 0;
	}

	bool SessionClient::SendShaderCommand(ShaderCommand command, uint32_t shader_id)
	{
		ShaderCommandRequest request = {};
		request.command = command;
		request.shader_id = shader_id;

		{
			std::lock_guard<std::mutex> lock(m_model.Mutex());
			if (const ShaderInfo *shader = m_model.ShaderById(shader_id))
				std::memcpy(request.signature, shader->signature.bytes.data(), sizeof(request.signature));
		}

		ControlMessage reply;
		if (!SendAndWait(ControlType::shader_command, &request, sizeof(request), reply))
			return false;

		const ControlAck *ack = reply.As<ControlAck>();
		const bool succeeded = reply.type == ControlType::ack && ack != nullptr && ack->succeeded != 0;
		if (succeeded)
		{
			std::lock_guard<std::mutex> lock(m_model.Mutex());
			if (ShaderInfo *shader = m_model.ShaderById(shader_id))
			{
				switch (command)
				{
				case ShaderCommand::disable: shader->disabled = true; break;
				case ShaderCommand::enable: shader->disabled = false; break;
				case ShaderCommand::highlight: shader->highlighted = true; break;
				case ShaderCommand::unhighlight: shader->highlighted = false; break;
				case ShaderCommand::restore:
					shader->disabled = false;
					shader->highlighted = false;
					shader->replaced = false;
					break;
				}
			}
		}
		return succeeded;
	}

	bool SessionClient::RequestPreview(uint32_t resource_id, uint32_t mip_level, uint32_t array_slice)
	{
		PreviewRequest request = {};
		request.request_id = ++m_next_preview_request;
		request.resource_id = resource_id;
		request.mip_level = mip_level;
		request.array_slice = array_slice;
		request.channels = PreviewChannels::rgb;

		ControlMessage reply;
		return SendAndWait(ControlType::request_preview, &request, sizeof(request), reply);
	}

	bool SessionClient::SetPreviewFrozen(bool frozen)
	{
		FreezePreviewRequest request = {};
		request.frozen = frozen ? 1u : 0u;
		ControlMessage reply;
		return SendAndWait(ControlType::freeze_preview, &request, sizeof(request), reply);
	}

	bool SessionClient::ReleaseCaptureBuffers()
	{
		ControlMessage reply;
		return SendAndWait(ControlType::release_capture_buffers, nullptr, 0, reply);
	}

	bool SessionClient::StopPreview()
	{
		PreviewRequest request = {};
		ControlMessage reply;
		return SendAndWait(ControlType::stop_preview, &request, sizeof(request), reply);
	}

	bool SessionClient::ReplaceShader(uint32_t shader_id, const std::vector<uint8_t> &code,
	                                  std::string &message)
	{
		if (shader_id == 0 || code.empty())
		{
			message = "nothing to send";
			return false;
		}

		ReplaceShaderRequest request = {};
		request.shader_id = shader_id;
		request.code_size = static_cast<uint32_t>(code.size());
		{
			std::lock_guard<std::mutex> lock(m_model.Mutex());
			if (const ShaderInfo *shader = m_model.ShaderById(shader_id))
				std::memcpy(request.signature, shader->signature.bytes.data(), sizeof(request.signature));
		}

		const uint64_t request_id = m_pipe.NextRequestId();
		if (!m_pipe.IsConnected() ||
		    !m_pipe.Send(ControlType::replace_shader, request_id, &request, sizeof(request), code.data(),
		                 static_cast<uint32_t>(code.size())))
		{
			message = "the command could not be sent";
			return false;
		}

		ControlMessage reply;
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			if (!m_pipe.Receive(reply, 2000))
			{
				message = "the add-on did not answer";
				return false;
			}
			if (reply.request_id == request_id)
				break;
		}

		const ControlAck *ack = reply.As<ControlAck>();
		const bool succeeded = reply.type == ControlType::ack && ack != nullptr && ack->succeeded != 0;
		if (ack != nullptr && ack->message_length != 0)
			message.assign(reinterpret_cast<const char *>(reply.Blob(sizeof(ControlAck))),
				(std::min)(static_cast<size_t>(ack->message_length), reply.BlobSize(sizeof(ControlAck))));
		else
			message = succeeded ? "replaced" : "refused";

		if (succeeded)
		{
			std::lock_guard<std::mutex> lock(m_model.Mutex());
			if (ShaderInfo *shader = m_model.ShaderById(shader_id))
				shader->replaced = true;
		}
		return succeeded;
	}

	void SessionClient::SampleThroughput(double seconds)
	{
		if (seconds <= 0.0)
			return;

		const uint64_t bytes = m_model.BytesApplied();
		const uint64_t delta = bytes > m_last_bytes ? bytes - m_last_bytes : 0;
		m_last_bytes = bytes;
		m_megabytes_per_second = (static_cast<double>(delta) / (1024.0 * 1024.0)) / seconds;
	}
}
