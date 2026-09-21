// CyGPUInspector — directory of live add-on sessions.
//
// Windows cannot enumerate named kernel objects, so every CyGPUInspectorRS instance publishes
// itself into a small shared table. The standalone reads that table to build the "Connected
// applications" list, and several games can be connected at the same time.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Protocol.hpp"
#include "SharedMemory.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	inline constexpr const char *kSessionDirectoryName = "Local\\CyGPUInspector.Sessions.v1";
	inline constexpr uint32_t kSessionDirectoryMagic = 0x53495943; // 'CYIS'
	inline constexpr uint32_t kMaxSessions = 32;
	inline constexpr uint64_t kSessionStaleMilliseconds = 3000;

	struct SessionSlot
	{
		std::atomic<uint32_t> process_id;   // 0 = free slot, claimed with a CAS
		uint32_t device_index;
		GraphicsApi api;
		uint32_t protocol_version;
		uint32_t capability_flags;
		TrackingLevel level;
		uint64_t start_time_ms;
		std::atomic<uint64_t> heartbeat_ms;
		std::atomic<uint64_t> frame_index;
		char process_name[kMaxProcessNameLength];
		char ring_name[kMaxObjectNameLength];
		char signal_name[kMaxObjectNameLength];
		char pipe_name[kMaxObjectNameLength];
		uint64_t ring_capacity;
		uint8_t reserved[24];
	};

	struct SessionDirectoryHeader
	{
		uint32_t magic;
		uint32_t version;
		uint32_t slot_count;
		uint32_t reserved;
	};

	struct SessionDirectoryLayout
	{
		SessionDirectoryHeader header;
		SessionSlot slots[kMaxSessions];
	};

	// A session as seen by the standalone.
	struct SessionEntry
	{
		uint32_t process_id = 0;
		uint32_t device_index = 0;
		GraphicsApi api = GraphicsApi::unknown;
		uint32_t protocol_version = 0;
		uint32_t capability_flags = 0;
		TrackingLevel level = TrackingLevel::idle;
		uint64_t frame_index = 0;
		uint64_t ring_capacity = 0;
		uint64_t age_ms = 0;
		std::string process_name;
		std::string ring_name;
		std::string signal_name;
		std::string pipe_name;

		bool IsFresh() const { return age_ms < kSessionStaleMilliseconds; }
		// "Beyond.exe (D3D12, PID 17482)"
		std::string Describe() const;
	};

	// Milliseconds since boot, monotonic, shared between processes.
	uint64_t NowMilliseconds();

	// Side of the add-on: claims a slot and keeps it alive.
	class SessionPublisher
	{
	public:
		~SessionPublisher();

		bool Claim(uint32_t process_id, uint32_t device_index, GraphicsApi api,
		           const char *process_name, const char *ring_name, const char *signal_name,
		           const char *pipe_name, uint64_t ring_capacity, uint32_t capability_flags);
		void Heartbeat(uint64_t frame_index, TrackingLevel level);
		void Release();

		bool IsValid() const { return m_slot != nullptr; }

	private:
		SharedMemory m_memory;
		SessionSlot *m_slot = nullptr;
		uint32_t m_process_id = 0;
	};

	// Side of the standalone: reads the table.
	class SessionDirectory
	{
	public:
		bool Open();
		void Close();
		bool IsValid() const { return m_layout != nullptr; }

		// Returns only slots whose process still exists; stale slots are recycled on the way.
		std::vector<SessionEntry> List();

	private:
		SharedMemory m_memory;
		SessionDirectoryLayout *m_layout = nullptr;
	};

	// True when the process still exists (used to recycle slots left by a crashed game).
	bool IsProcessAlive(uint32_t process_id);
}
