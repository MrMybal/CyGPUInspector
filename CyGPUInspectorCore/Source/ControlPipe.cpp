// CyGPUInspector — control channel implementation.
//
// Both sides use overlapped I/O with an explicit stop event. A blocking named pipe is impossible
// to shut down reliably: ConnectNamedPipe and ReadFile can park forever, and a dummy connection
// to wake them up races with the thread re-arming the pipe. The server would then never join,
// which inside a game means a hang on ReShade unload.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/ControlPipe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>

namespace cygi
{
	namespace
	{
		constexpr uint32_t kPipeBufferSize = 1u * 1024 * 1024;
		constexpr uint32_t kMaxMessageSize = 64u * 1024 * 1024;

		// Serializes a message into one contiguous buffer: a message mode pipe needs one write.
		void BuildMessage(std::vector<uint8_t> &buffer, ControlType type, uint64_t request_id,
		                  const void *fixed, uint32_t fixed_size, const void *blob, uint32_t blob_size)
		{
			ControlHeader header = {};
			header.size = static_cast<uint32_t>(sizeof(ControlHeader)) + fixed_size + blob_size;
			header.type = type;
			header.request_id = request_id;

			buffer.resize(header.size);
			std::memcpy(buffer.data(), &header, sizeof(header));
			if (fixed != nullptr && fixed_size != 0)
				std::memcpy(buffer.data() + sizeof(header), fixed, fixed_size);
			if (blob != nullptr && blob_size != 0)
				std::memcpy(buffer.data() + sizeof(header) + fixed_size, blob, blob_size);
		}

		// Overlapped write that waits for completion. `event` must be a manual reset event.
		bool WriteOverlapped(HANDLE pipe, HANDLE event, const std::vector<uint8_t> &buffer)
		{
			if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE)
				return false;

			OVERLAPPED overlapped = {};
			overlapped.hEvent = event;
			ResetEvent(event);

			DWORD written = 0;
			if (WriteFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &written, &overlapped) == 0)
			{
				if (GetLastError() != ERROR_IO_PENDING)
					return false;
				if (GetOverlappedResult(pipe, &overlapped, &written, TRUE) == 0)
					return false;
			}
			return written == buffer.size();
		}

		// Turns a completed read into a ControlMessage. Returns false when the bytes are not a
		// well formed message (a peer that died mid write, or a protocol mismatch).
		bool ParseMessage(const std::vector<uint8_t> &buffer, size_t size, ControlMessage &out)
		{
			if (size < sizeof(ControlHeader))
				return false;

			ControlHeader header = {};
			std::memcpy(&header, buffer.data(), sizeof(header));
			if (header.size < sizeof(ControlHeader) || header.size > size)
				return false;

			out.type = header.type;
			out.request_id = header.request_id;
			out.payload.assign(buffer.begin() + sizeof(ControlHeader), buffer.begin() + header.size);
			return true;
		}
	}

	ControlPipeServer::~ControlPipeServer()
	{
		Stop();
	}

	bool ControlPipeServer::Start(const char *pipe_name)
	{
		Stop();
		if (pipe_name == nullptr)
			return false;

		m_stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		if (m_stop_event == nullptr)
			return false;

		m_pipe_name = pipe_name;
		m_running.store(true, std::memory_order_release);
		m_thread = std::thread(&ControlPipeServer::ThreadMain, this);
		return true;
	}

	void ControlPipeServer::Stop()
	{
		m_running.store(false, std::memory_order_release);
		if (m_stop_event != nullptr)
			SetEvent(static_cast<HANDLE>(m_stop_event));

		if (m_thread.joinable())
			m_thread.join();

		if (m_stop_event != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_stop_event));
			m_stop_event = nullptr;
		}

		std::lock_guard<std::mutex> lock(m_queue_mutex);
		m_queue.clear();
	}

	void ControlPipeServer::ThreadMain()
	{
		const HANDLE stop_event = static_cast<HANDLE>(m_stop_event);
		const HANDLE io_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		const HANDLE write_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		if (io_event == nullptr || write_event == nullptr)
		{
			if (io_event != nullptr) CloseHandle(io_event);
			if (write_event != nullptr) CloseHandle(write_event);
			return;
		}

		{
			std::lock_guard<std::mutex> lock(m_write_mutex);
			m_write_event = write_event;
		}

		std::vector<uint8_t> buffer(kPipeBufferSize);

		while (m_running.load(std::memory_order_acquire))
		{
			const HANDLE pipe = CreateNamedPipeA(m_pipe_name.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				1, kPipeBufferSize, kPipeBufferSize, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				if (WaitForSingleObject(stop_event, 200) == WAIT_OBJECT_0)
					break;
				continue;
			}

			// Wait for a client, but never longer than the lifetime of the server.
			OVERLAPPED connect_overlapped = {};
			connect_overlapped.hEvent = io_event;
			ResetEvent(io_event);

			bool connected = ConnectNamedPipe(pipe, &connect_overlapped) != 0;
			if (!connected)
			{
				const DWORD error = GetLastError();
				if (error == ERROR_PIPE_CONNECTED)
				{
					connected = true;
				}
				else if (error == ERROR_IO_PENDING)
				{
					const HANDLE waits[] = { io_event, stop_event };
					const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
					if (wait == WAIT_OBJECT_0)
					{
						DWORD transferred = 0;
						connected = GetOverlappedResult(pipe, &connect_overlapped, &transferred, FALSE) != 0;
					}
					else
					{
						CancelIoEx(pipe, &connect_overlapped);
					}
				}
			}

			if (!connected || !m_running.load(std::memory_order_acquire))
			{
				CloseHandle(pipe);
				continue;
			}

			{
				std::lock_guard<std::mutex> lock(m_write_mutex);
				m_pipe = pipe;
			}
			m_connected.store(true, std::memory_order_release);

			// Read messages until the client goes away or the server is stopped.
			while (m_running.load(std::memory_order_acquire))
			{
				OVERLAPPED read_overlapped = {};
				read_overlapped.hEvent = io_event;
				ResetEvent(io_event);

				DWORD read = 0;
				bool completed = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
					&read_overlapped) != 0;
				bool more_data = false;

				if (!completed)
				{
					const DWORD error = GetLastError();
					if (error == ERROR_MORE_DATA)
					{
						completed = true;
						more_data = true;
					}
					else if (error == ERROR_IO_PENDING)
					{
						const HANDLE waits[] = { io_event, stop_event };
						if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
						{
							CancelIoEx(pipe, &read_overlapped);
							break;
						}
						completed = GetOverlappedResult(pipe, &read_overlapped, &read, FALSE) != 0;
						if (!completed && GetLastError() == ERROR_MORE_DATA)
						{
							completed = true;
							more_data = true;
							read = static_cast<DWORD>(buffer.size());
						}
					}
				}

				if (!completed)
					break; // client disconnected

				// A message larger than the buffer arrives in several reads.
				size_t total = read;
				while (more_data && total < kMaxMessageSize)
				{
					buffer.resize(buffer.size() * 2);

					OVERLAPPED extra_overlapped = {};
					extra_overlapped.hEvent = io_event;
					ResetEvent(io_event);

					DWORD extra = 0;
					bool extra_done = ReadFile(pipe, buffer.data() + total,
						static_cast<DWORD>(buffer.size() - total), &extra, &extra_overlapped) != 0;
					more_data = false;

					if (!extra_done)
					{
						const DWORD error = GetLastError();
						if (error == ERROR_MORE_DATA)
						{
							extra_done = true;
							more_data = true;
						}
						else if (error == ERROR_IO_PENDING)
						{
							const HANDLE waits[] = { io_event, stop_event };
							if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
							{
								CancelIoEx(pipe, &extra_overlapped);
								break;
							}
							extra_done = GetOverlappedResult(pipe, &extra_overlapped, &extra, FALSE) != 0;
							if (!extra_done && GetLastError() == ERROR_MORE_DATA)
							{
								extra_done = true;
								more_data = true;
							}
						}
					}

					if (!extra_done)
						break;
					total += extra;
				}

				ControlMessage message;
				if (ParseMessage(buffer, total, message))
				{
					std::lock_guard<std::mutex> lock(m_queue_mutex);
					// Never grow without bound if the render thread stops draining.
					if (m_queue.size() < 256)
						m_queue.push_back(std::move(message));
				}
			}

			m_connected.store(false, std::memory_order_release);
			{
				std::lock_guard<std::mutex> lock(m_write_mutex);
				m_pipe = nullptr;
			}
			CancelIoEx(pipe, nullptr);
			DisconnectNamedPipe(pipe);
			CloseHandle(pipe);
		}

		{
			std::lock_guard<std::mutex> lock(m_write_mutex);
			m_write_event = nullptr;
		}
		CloseHandle(io_event);
		CloseHandle(write_event);
	}

	bool ControlPipeServer::PopMessage(ControlMessage &out)
	{
		std::lock_guard<std::mutex> lock(m_queue_mutex);
		if (m_queue.empty())
			return false;
		out = std::move(m_queue.front());
		m_queue.pop_front();
		return true;
	}

	bool ControlPipeServer::Send(ControlType type, uint64_t request_id, const void *fixed, uint32_t fixed_size,
	                             const void *blob, uint32_t blob_size)
	{
		std::lock_guard<std::mutex> lock(m_write_mutex);
		if (m_pipe == nullptr || m_write_event == nullptr)
			return false;

		std::vector<uint8_t> buffer;
		BuildMessage(buffer, type, request_id, fixed, fixed_size, blob, blob_size);
		return WriteOverlapped(static_cast<HANDLE>(m_pipe), static_cast<HANDLE>(m_write_event), buffer);
	}

	bool ControlPipeServer::SendAck(uint64_t request_id, bool succeeded, const char *message)
	{
		ControlAck ack = {};
		ack.succeeded = succeeded ? 1u : 0u;
		const uint32_t length = message != nullptr ? static_cast<uint32_t>(std::strlen(message)) : 0u;
		ack.message_length = length;
		return Send(succeeded ? ControlType::ack : ControlType::error, request_id, &ack, sizeof(ack), message,
			length);
	}

	ControlPipeClient::~ControlPipeClient()
	{
		Disconnect();
	}

	bool ControlPipeClient::Connect(const char *pipe_name, uint32_t timeout_ms)
	{
		Disconnect();
		if (pipe_name == nullptr)
			return false;

		const ULONGLONG deadline = GetTickCount64() + timeout_ms;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		for (;;)
		{
			pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, nullptr);
			if (pipe != INVALID_HANDLE_VALUE)
				break;
			if (GetLastError() != ERROR_PIPE_BUSY || GetTickCount64() > deadline)
				return false;
			WaitNamedPipeA(pipe_name, 100);
		}

		DWORD mode = PIPE_READMODE_MESSAGE;
		if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) == 0)
		{
			CloseHandle(pipe);
			return false;
		}

		m_read_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		m_write_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		if (m_read_event == nullptr || m_write_event == nullptr)
		{
			Disconnect();
			CloseHandle(pipe);
			return false;
		}

		m_pipe = pipe;
		return true;
	}

	void ControlPipeClient::Disconnect()
	{
		if (m_pipe != nullptr && m_pipe != INVALID_HANDLE_VALUE)
		{
			CancelIoEx(static_cast<HANDLE>(m_pipe), nullptr);
			CloseHandle(static_cast<HANDLE>(m_pipe));
		}
		if (m_read_event != nullptr)
			CloseHandle(static_cast<HANDLE>(m_read_event));
		if (m_write_event != nullptr)
			CloseHandle(static_cast<HANDLE>(m_write_event));
		m_pipe = nullptr;
		m_read_event = nullptr;
		m_write_event = nullptr;
	}

	bool ControlPipeClient::Send(ControlType type, uint64_t request_id, const void *fixed, uint32_t fixed_size,
	                             const void *blob, uint32_t blob_size)
	{
		if (m_pipe == nullptr)
			return false;

		std::vector<uint8_t> buffer;
		BuildMessage(buffer, type, request_id, fixed, fixed_size, blob, blob_size);
		return WriteOverlapped(static_cast<HANDLE>(m_pipe), static_cast<HANDLE>(m_write_event), buffer);
	}

	bool ControlPipeClient::Receive(ControlMessage &out, uint32_t timeout_ms)
	{
		if (m_pipe == nullptr)
			return false;

		const HANDLE pipe = static_cast<HANDLE>(m_pipe);
		const HANDLE event = static_cast<HANDLE>(m_read_event);

		std::vector<uint8_t> buffer(kPipeBufferSize);
		OVERLAPPED overlapped = {};
		overlapped.hEvent = event;
		ResetEvent(event);

		DWORD read = 0;
		if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &overlapped) == 0)
		{
			const DWORD error = GetLastError();
			if (error == ERROR_IO_PENDING)
			{
				if (WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0)
				{
					CancelIoEx(pipe, &overlapped);
					// Let the cancellation settle so the buffer is not written after it is gone.
					GetOverlappedResult(pipe, &overlapped, &read, TRUE);
					return false;
				}
				if (GetOverlappedResult(pipe, &overlapped, &read, FALSE) == 0)
					return false;
			}
			else if (error != ERROR_MORE_DATA)
			{
				return false;
			}
		}

		return ParseMessage(buffer, read, out);
	}
}
