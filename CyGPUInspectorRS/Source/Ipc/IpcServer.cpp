// CyGPUInspectorRS — IPC server implementation.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "IpcServer.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>

namespace cygi
{
	namespace
	{
		// One frame of a heavy game is a few thousand events; chunk them so a single record
		// never needs a big contiguous hole in the ring.
		constexpr uint32_t kEventsPerRecord = 4096;

		void CopyString(char *dest, size_t dest_size, const std::string &source)
		{
			if (dest_size == 0)
				return;
			const size_t count = source.size() < dest_size - 1 ? source.size() : dest_size - 1;
			std::memcpy(dest, source.data(), count);
			dest[count] = '\0';
		}
	}

	IpcServer::~IpcServer()
	{
		Stop();
	}

	bool IpcServer::Start(uint32_t device_index, GraphicsApi api, const char *process_name,
	                      uint32_t capability_flags, uint64_t timestamp_frequency, const char *adapter_name,
	                      size_t ring_capacity)
	{
		Stop();

		m_device_index = device_index;
		m_api = api;
		m_capability_flags = capability_flags;
		m_timestamp_frequency = timestamp_frequency;
		m_process_name = process_name != nullptr ? process_name : "";
		m_adapter_name = adapter_name != nullptr ? adapter_name : "";

		const uint32_t process_id = GetCurrentProcessId();

		char ring_name[kMaxObjectNameLength] = {};
		char signal_name[kMaxObjectNameLength] = {};
		char pipe_name[kMaxObjectNameLength] = {};
		MakeRingName(process_id, device_index, ring_name, sizeof(ring_name));
		MakeSignalName(process_id, device_index, signal_name, sizeof(signal_name));
		MakePipeName(process_id, device_index, pipe_name, sizeof(pipe_name));

		if (!m_memory.Create(ring_name, RingMappingSize(ring_capacity)))
			return false;
		if (!m_writer.Initialize(m_memory.Data(), ring_capacity))
		{
			m_memory.Close();
			return false;
		}
		m_signal.Create(signal_name);
		m_pipe.Start(pipe_name);

		m_session.Claim(process_id, device_index, api, m_process_name.c_str(), ring_name, signal_name,
			pipe_name, ring_capacity, capability_flags);

		PublishSessionInfo(TrackingLevel::tracking);
		return true;
	}

	void IpcServer::Stop()
	{
		m_session.Release();
		m_pipe.Stop();
		m_writer.Shutdown();
		m_signal.Signal(); // let a waiting standalone notice the shutdown immediately
		m_signal.Close();
		m_memory.Close();
	}

	void IpcServer::PublishSessionInfo(TrackingLevel level)
	{
		SessionInfoRecord record = {};
		record.protocol_version = kProtocolVersion;
		record.process_id = GetCurrentProcessId();
		record.api = m_api;
		record.level = level;
		record.device_index = m_device_index;
		record.capability_flags = m_capability_flags;
		record.timestamp_frequency = m_timestamp_frequency;
		CopyString(record.process_name, sizeof(record.process_name), m_process_name);
		CopyString(record.addon_version, sizeof(record.addon_version), kVersionString);
		CopyString(record.adapter_name, sizeof(record.adapter_name), m_adapter_name);

		m_writer.Write(RecordType::session_info, &record, sizeof(record));
	}

	void IpcServer::PublishShader(const ShaderRecord &record, const uint8_t *code, uint32_t code_size)
	{
		ShaderCodeRecord message = {};
		message.shader_id = record.id;
		message.stage = record.stage;
		message.format = record.format;
		message.code_size = code_size;
		message.shader_model = record.shader_model;
		std::memcpy(message.signature, record.signature.bytes.data(), sizeof(message.signature));
		std::memcpy(message.semantic_hash, record.semantic_hash.bytes.data(), sizeof(message.semantic_hash));

		m_writer.Write(RecordType::shader_code, &message, sizeof(message), code, code_size);
	}

	void IpcServer::PublishResourceName(uint32_t resource_id, const std::string &name)
	{
		ResourceNamedRecord record = {};
		record.resource_id = resource_id;
		record.name_length = static_cast<uint32_t>(name.size());
		m_writer.Write(RecordType::resource_named, &record, sizeof(record),
			reinterpret_cast<const uint8_t *>(name.data()), record.name_length);
	}

	void IpcServer::PublishPipeline(const PipelineRecord &record)
	{
		PipelineInfoRecord message = {};
		message.pipeline_id = record.id;
		message.native_handle = record.native_handle;
		message.stage_mask = record.stage_mask;
		message.shader_count = record.shader_count;
		message.is_compute = record.is_compute ? 1u : 0u;

		m_writer.Write(RecordType::pipeline_info, &message, sizeof(message), record.shader_ids,
			record.shader_count * static_cast<uint32_t>(sizeof(uint32_t)));
	}

	void IpcServer::PublishResource(const ResourceRecord &record)
	{
		ResourceInfoRecord message = {};
		message.resource_id = record.id;
		message.native_handle = record.native_handle;
		message.kind = record.kind;
		message.format = record.format;
		message.width = record.width;
		message.height = record.height;
		message.depth_or_layers = record.depth_or_layers;
		message.mip_levels = record.mip_levels;
		message.samples = record.samples;
		message.usage_flags = record.usage_flags;
		message.buffer_size = record.buffer_size;
		message.created_frame = record.created_frame;
		message.created_event = record.created_event;

		m_writer.Write(RecordType::resource_info, &message, sizeof(message));
	}

	void IpcServer::PublishResourceGone(uint32_t resource_id, uint64_t frame_index, uint32_t event_index)
	{
		ResourceGoneRecord message = {};
		message.resource_id = resource_id;
		message.destroyed_event = event_index;
		message.destroyed_frame = frame_index;
		m_writer.Write(RecordType::resource_gone, &message, sizeof(message));
	}

	void IpcServer::PublishFrameBegin(uint64_t frame_index)
	{
		FrameBeginRecord message = {};
		message.frame_index = frame_index;

		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		message.cpu_timestamp_qpc = static_cast<uint64_t>(counter.QuadPart);

		m_writer.Write(RecordType::frame_begin, &message, sizeof(message));
	}

	void IpcServer::PublishFrameEvents(uint64_t frame_index, const std::vector<FrameEvent> &events)
	{
		for (size_t offset = 0; offset < events.size(); offset += kEventsPerRecord)
		{
			const uint32_t count = static_cast<uint32_t>(
				events.size() - offset < kEventsPerRecord ? events.size() - offset : kEventsPerRecord);

			FrameEventsRecord message = {};
			message.frame_index = frame_index;
			message.event_count = count;
			message.first_index = static_cast<uint32_t>(offset);

			if (!m_writer.Write(RecordType::frame_events, &message, sizeof(message), events.data() + offset,
			                    count * static_cast<uint32_t>(sizeof(FrameEvent))))
				break; // ring is full, the frame end record will report the loss
		}
	}

	void IpcServer::PublishDrawState(const DrawStateRecord &record, const DrawBinding *bindings)
	{
		m_writer.Write(RecordType::draw_state, &record, sizeof(record), bindings,
			record.binding_count * static_cast<uint32_t>(sizeof(DrawBinding)));
	}

	void IpcServer::PublishBarriers(const BarrierSetRecord &record, const BarrierEntry *entries)
	{
		m_writer.Write(RecordType::barrier_set, &record, sizeof(record), entries,
			record.count * static_cast<uint32_t>(sizeof(BarrierEntry)));
	}

	void IpcServer::PublishPipelineState(const PipelineStateRecord &record)
	{
		m_writer.Write(RecordType::pipeline_state, &record, sizeof(record));
	}

	void IpcServer::PublishCaptureState(const CaptureStateRecord &record)
	{
		m_writer.Write(RecordType::capture_state, &record, sizeof(record));
	}

	void IpcServer::PublishFrameEnd(const FrameEndRecord &record)
	{
		m_writer.Write(RecordType::frame_end, &record, sizeof(record));
	}

	void IpcServer::PublishStats(const StatsRecord &record)
	{
		m_writer.Write(RecordType::stats, &record, sizeof(record));
	}

	void IpcServer::PublishTimings(uint64_t frame_index, const std::vector<TimingResult> &timings)
	{
		TimingResultsRecord message = {};
		message.frame_index = frame_index;
		message.count = static_cast<uint32_t>(timings.size());
		message.result_size = static_cast<uint32_t>(sizeof(TimingResult));
		m_writer.Write(RecordType::timing_results, &message, sizeof(message), timings.data(),
			static_cast<uint32_t>(timings.size() * sizeof(TimingResult)));
	}

	void IpcServer::PublishFrameGpuSpan(uint64_t frame_index, uint64_t gpu_begin, uint64_t gpu_end)
	{
		FrameGpuSpanRecord record = {};
		record.frame_index = frame_index;
		record.gpu_begin = gpu_begin;
		record.gpu_end = gpu_end;
		m_writer.Write(RecordType::frame_gpu_span, &record, sizeof(record));
	}

	void IpcServer::PublishLog(LogLevel level, const char *text)
	{
		if (text == nullptr)
			return;

		LogMessageRecord message = {};
		message.level = level;
		message.text_length = static_cast<uint32_t>(std::strlen(text));
		m_writer.Write(RecordType::log_message, &message, sizeof(message), text, message.text_length);
	}

	void IpcServer::PublishPreviewReady(const PreviewReadyRecord &record)
	{
		m_writer.Write(RecordType::preview_ready, &record, sizeof(record));
	}

	void IpcServer::PublishCaptureBuffer(const CaptureBufferRecord &record)
	{
		m_writer.Write(RecordType::capture_buffer, &record, sizeof(record));
	}

	void IpcServer::PublishCaptureScope(const CaptureScopeRecord &record)
	{
		m_writer.Write(RecordType::capture_scope, &record, sizeof(record));
	}

	void IpcServer::SignalReader()
	{
		m_signal.Signal();
	}

	void IpcServer::Heartbeat(uint64_t frame_index, TrackingLevel level)
	{
		m_session.Heartbeat(frame_index, level);
	}
}
