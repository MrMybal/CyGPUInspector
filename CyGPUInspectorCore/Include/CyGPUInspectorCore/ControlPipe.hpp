// CyGPUInspector — control channel (named pipe, message mode).
//
// The add-on runs the server, the standalone is the single client. Messages are never handled on
// the pipe thread: the add-on drains them on its render thread (at present time) so that GPU work
// and ReShade API calls stay on the thread that owns the device.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Protocol.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cygi
{
	struct ControlMessage
	{
		ControlType type = ControlType::none;
		uint64_t request_id = 0;
		std::vector<uint8_t> payload;

		template <typename T>
		const T *As() const
		{
			return payload.size() >= sizeof(T) ? reinterpret_cast<const T *>(payload.data()) : nullptr;
		}
		// Bytes that follow the fixed part of the message (shader byte code, text, ...).
		const uint8_t *Blob(size_t fixed_size) const
		{
			return payload.size() > fixed_size ? payload.data() + fixed_size : nullptr;
		}
		size_t BlobSize(size_t fixed_size) const
		{
			return payload.size() > fixed_size ? payload.size() - fixed_size : 0;
		}
	};

	// Server side (inside the game process).
	class ControlPipeServer
	{
	public:
		~ControlPipeServer();

		bool Start(const char *pipe_name);
		void Stop();

		bool IsRunning() const { return m_running.load(std::memory_order_acquire); }
		bool IsClientConnected() const { return m_connected.load(std::memory_order_acquire); }

		// Called from the render thread: returns false when nothing is pending.
		bool PopMessage(ControlMessage &out);
		// Thread safe, may be called from the render thread to answer a request.
		bool Send(ControlType type, uint64_t request_id, const void *fixed, uint32_t fixed_size,
		          const void *blob = nullptr, uint32_t blob_size = 0);

		// Convenience: success / failure acknowledgement with an optional message.
		bool SendAck(uint64_t request_id, bool succeeded, const char *message = nullptr);

	private:
		void ThreadMain();

		std::thread m_thread;
		std::atomic<bool> m_running{ false };
		std::atomic<bool> m_connected{ false };
		std::string m_pipe_name;

		void *m_pipe = nullptr;          // HANDLE, owned by the pipe thread
		void *m_stop_event = nullptr;    // HANDLE, manual reset: unblocks every overlapped wait
		void *m_write_event = nullptr;   // HANDLE, used by Send from any thread
		std::mutex m_write_mutex;        // guards m_pipe / m_write_event and serializes writes
		std::mutex m_queue_mutex;
		std::deque<ControlMessage> m_queue;
	};

	// Client side (inside the standalone).
	class ControlPipeClient
	{
	public:
		~ControlPipeClient();

		bool Connect(const char *pipe_name, uint32_t timeout_ms = 1000);
		void Disconnect();
		bool IsConnected() const { return m_pipe != nullptr; }

		bool Send(ControlType type, uint64_t request_id, const void *fixed, uint32_t fixed_size,
		          const void *blob = nullptr, uint32_t blob_size = 0);
		// Blocking read with a timeout, used for request/response pairs such as Hello.
		bool Receive(ControlMessage &out, uint32_t timeout_ms);

		uint64_t NextRequestId() { return ++m_request_id; }

	private:
		void *m_pipe = nullptr;          // HANDLE
		void *m_read_event = nullptr;    // HANDLE, for overlapped reads
		void *m_write_event = nullptr;   // HANDLE, for overlapped writes
		uint64_t m_request_id = 0;
	};
}
