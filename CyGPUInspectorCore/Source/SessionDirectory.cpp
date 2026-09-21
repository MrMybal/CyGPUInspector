// CyGPUInspector — directory of live add-on sessions.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/SessionDirectory.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace cygi
{
	namespace
	{
		void CopyString(char *dest, size_t dest_size, const char *source)
		{
			if (dest_size == 0)
				return;
			if (source == nullptr)
			{
				dest[0] = '\0';
				return;
			}
			std::strncpy(dest, source, dest_size - 1);
			dest[dest_size - 1] = '\0';
		}
	}

	uint64_t NowMilliseconds()
	{
		return GetTickCount64();
	}

	bool IsProcessAlive(uint32_t process_id)
	{
		if (process_id == 0)
			return false;

		const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
		if (process == nullptr)
			return GetLastError() == ERROR_ACCESS_DENIED; // exists, but we may not query it

		DWORD exit_code = 0;
		const bool alive = GetExitCodeProcess(process, &exit_code) != 0 && exit_code == STILL_ACTIVE;
		CloseHandle(process);
		return alive;
	}

	std::string SessionEntry::Describe() const
	{
		char buffer[192];
		std::snprintf(buffer, sizeof(buffer), "%s (%s, PID %u)", process_name.c_str(),
			GraphicsApiName(api), process_id);
		return buffer;
	}

	SessionPublisher::~SessionPublisher()
	{
		Release();
	}

	bool SessionPublisher::Claim(uint32_t process_id, uint32_t device_index, GraphicsApi api,
	                             const char *process_name, const char *ring_name, const char *signal_name,
	                             const char *pipe_name, uint64_t ring_capacity, uint32_t capability_flags)
	{
		Release();

		bool created = false;
		if (!m_memory.Create(kSessionDirectoryName, sizeof(SessionDirectoryLayout), &created))
			return false;

		SessionDirectoryLayout *layout = static_cast<SessionDirectoryLayout *>(m_memory.Data());
		if (created)
		{
			std::memset(layout, 0, sizeof(SessionDirectoryLayout));
			layout->header.magic = kSessionDirectoryMagic;
			layout->header.version = kProtocolVersion;
			layout->header.slot_count = kMaxSessions;
		}
		else if (layout->header.magic != kSessionDirectoryMagic)
		{
			// A previous incompatible build owns the mapping: refuse rather than corrupt it.
			m_memory.Close();
			return false;
		}

		for (uint32_t i = 0; i < kMaxSessions; ++i)
		{
			SessionSlot &slot = layout->slots[i];
			uint32_t owner = slot.process_id.load(std::memory_order_acquire);

			// Recycle slots left behind by a crashed game.
			if (owner != 0 && !IsProcessAlive(owner))
			{
				slot.process_id.compare_exchange_strong(owner, 0, std::memory_order_acq_rel);
				owner = slot.process_id.load(std::memory_order_acquire);
			}
			if (owner != 0)
				continue;

			uint32_t expected = 0;
			if (!slot.process_id.compare_exchange_strong(expected, process_id, std::memory_order_acq_rel))
				continue;

			slot.device_index = device_index;
			slot.api = api;
			slot.protocol_version = kProtocolVersion;
			slot.capability_flags = capability_flags;
			slot.level = TrackingLevel::tracking;
			slot.start_time_ms = NowMilliseconds();
			slot.heartbeat_ms.store(slot.start_time_ms, std::memory_order_release);
			slot.frame_index.store(0, std::memory_order_relaxed);
			slot.ring_capacity = ring_capacity;
			CopyString(slot.process_name, sizeof(slot.process_name), process_name);
			CopyString(slot.ring_name, sizeof(slot.ring_name), ring_name);
			CopyString(slot.signal_name, sizeof(slot.signal_name), signal_name);
			CopyString(slot.pipe_name, sizeof(slot.pipe_name), pipe_name);

			m_slot = &slot;
			m_process_id = process_id;
			return true;
		}

		m_memory.Close();
		return false;
	}

	void SessionPublisher::Heartbeat(uint64_t frame_index, TrackingLevel level)
	{
		if (m_slot == nullptr)
			return;
		m_slot->level = level;
		m_slot->frame_index.store(frame_index, std::memory_order_relaxed);
		m_slot->heartbeat_ms.store(NowMilliseconds(), std::memory_order_release);
	}

	void SessionPublisher::Release()
	{
		if (m_slot != nullptr)
		{
			uint32_t expected = m_process_id;
			m_slot->process_id.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
			m_slot = nullptr;
		}
		m_memory.Close();
	}

	bool SessionDirectory::Open()
	{
		Close();
		// Create rather than Open: the standalone may legitimately start before any game.
		bool created = false;
		if (!m_memory.Create(kSessionDirectoryName, sizeof(SessionDirectoryLayout), &created))
			return false;

		SessionDirectoryLayout *layout = static_cast<SessionDirectoryLayout *>(m_memory.Data());
		if (created)
		{
			std::memset(layout, 0, sizeof(SessionDirectoryLayout));
			layout->header.magic = kSessionDirectoryMagic;
			layout->header.version = kProtocolVersion;
			layout->header.slot_count = kMaxSessions;
		}
		else if (layout->header.magic != kSessionDirectoryMagic)
		{
			m_memory.Close();
			return false;
		}

		m_layout = layout;
		return true;
	}

	void SessionDirectory::Close()
	{
		m_layout = nullptr;
		m_memory.Close();
	}

	std::vector<SessionEntry> SessionDirectory::List()
	{
		std::vector<SessionEntry> entries;
		if (m_layout == nullptr)
			return entries;

		const uint64_t now = NowMilliseconds();
		for (uint32_t i = 0; i < kMaxSessions; ++i)
		{
			SessionSlot &slot = m_layout->slots[i];
			uint32_t owner = slot.process_id.load(std::memory_order_acquire);
			if (owner == 0)
				continue;

			if (!IsProcessAlive(owner))
			{
				slot.process_id.compare_exchange_strong(owner, 0, std::memory_order_acq_rel);
				continue;
			}

			SessionEntry entry;
			entry.process_id = owner;
			entry.device_index = slot.device_index;
			entry.api = slot.api;
			entry.protocol_version = slot.protocol_version;
			entry.capability_flags = slot.capability_flags;
			entry.level = slot.level;
			entry.frame_index = slot.frame_index.load(std::memory_order_relaxed);
			entry.ring_capacity = slot.ring_capacity;
			const uint64_t heartbeat = slot.heartbeat_ms.load(std::memory_order_acquire);
			entry.age_ms = now > heartbeat ? now - heartbeat : 0;
			entry.process_name = slot.process_name;
			entry.ring_name = slot.ring_name;
			entry.signal_name = slot.signal_name;
			entry.pipe_name = slot.pipe_name;
			entries.push_back(std::move(entry));
		}
		return entries;
	}
}
