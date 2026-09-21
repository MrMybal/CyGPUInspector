// CyGPUInspectorApp — connection to one CyGPUInspectorRS session.
//
// Owns the reader thread that drains the event ring and the control pipe used to send commands
// back to the add-on. Everything here tolerates the game disappearing at any moment.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "SessionModel.hpp"

#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/RingBuffer.hpp>
#include <CyGPUInspectorCore/SessionDirectory.hpp>
#include <CyGPUInspectorCore/SharedMemory.hpp>

#include <atomic>
#include <vector>
#include <string>
#include <thread>

namespace cygi
{
	class SessionClient
	{
	public:
		~SessionClient();

		bool Connect(const SessionEntry &entry);
		void Disconnect();

		bool IsAttached() const { return m_running.load(std::memory_order_acquire); }
		bool IsControlConnected() const { return m_pipe.IsConnected(); }
		bool WriterAlive() const { return m_reader.WriterAlive(); }

		SessionModel &Model() { return m_model; }
		const SessionEntry &Entry() const { return m_entry; }
		const std::string &LastError() const { return m_last_error; }

		// Commands, sent from the UI thread. They return false when the add-on did not answer.
		bool SetLevel(TrackingLevel level);
		bool RequestFullSync();

		// Arms a deep capture in the game process: the add-on records every binding and every
		// state for `frame_count` frames, then puts itself back where it was. `message` carries
		// the add-on's refusal when it declines, which it does when tracking is idle.
		// `buffers` also has the add-on copy every texture the last captured frame wrote to, for
		// the standalone to save; ReleaseCaptureBuffers gives that memory back to the game.
		bool StartDeepCapture(uint32_t frame_count, bool bindings, bool barriers, bool per_draw_timing,
		                      bool buffers, std::string &message);
		bool ReleaseCaptureBuffers();
		bool SendShaderCommand(ShaderCommand command, uint32_t shader_id);
		// Asks the add-on to keep a shared texture of this resource up to date.
		bool RequestPreview(uint32_t resource_id, uint32_t mip_level = 0, uint32_t array_slice = 0);
		bool StopPreview();
		// Keeps the shared image as it is (true) or lets it follow the game again (false).
		bool SetPreviewFrozen(bool frozen);
		// Sends replacement byte code for a shader; the add-on rebuilds every pipeline using it.
		bool ReplaceShader(uint32_t shader_id, const std::vector<uint8_t> &code, std::string &message);

		// Throughput of the event stream, refreshed by the UI once per second.
		void SampleThroughput(double seconds);
		double MegabytesPerSecond() const { return m_megabytes_per_second; }
		uint64_t DroppedBytes() const { return m_reader.DroppedBytes(); }
		size_t PendingBytes() const { return m_reader.PendingBytes(); }

	private:
		void ReaderThread();
		bool SendAndWait(ControlType type, const void *fixed, uint32_t fixed_size, ControlMessage &reply,
		                 uint32_t timeout_ms = 500);

		SharedMemory m_memory;
		RingReader m_reader;
		SharedEvent m_signal;
		ControlPipeClient m_pipe;

		std::thread m_thread;
		std::atomic<bool> m_running{ false };

		SessionModel m_model;
		SessionEntry m_entry;
		std::string m_last_error;

		uint32_t m_next_preview_request = 0;
		uint64_t m_last_bytes = 0;
		double m_megabytes_per_second = 0.0;
	};
}
