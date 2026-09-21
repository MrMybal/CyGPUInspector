// CyGPUInspector — single producer / single consumer ring buffer living in shared memory.
//
// The producer is the game thread that calls present, the consumer is the IPC thread of the
// standalone. The producer never blocks and never allocates: when the ring is full it drops the
// record and accounts the loss in `dropped_bytes`, which the App reports in the UI.
//
// A record is always contiguous: when it would wrap, a `padding` record is written up to the end
// of the buffer first. Readers can therefore use a pointer straight into the mapping.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Protocol.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cygi
{
	inline constexpr uint32_t kRingMagic = 0x49475943; // 'CYGI'
	inline constexpr size_t kDefaultRingCapacity = 64u * 1024 * 1024;

	struct RingHeader
	{
		uint32_t magic;
		uint32_t version;
		uint64_t capacity;             // payload bytes, power of two
		std::atomic<uint64_t> write_pos;
		std::atomic<uint64_t> read_pos;
		std::atomic<uint64_t> dropped_bytes;
		std::atomic<uint64_t> dropped_records;
		std::atomic<uint64_t> sequence;
		std::atomic<uint32_t> writer_alive;
		std::atomic<uint32_t> reader_attached;
		uint8_t padding[64];
	};
	// Padded to 128 bytes so the payload area starts 16 byte aligned (records are 16 byte aligned)
	// and the header sits on its own cache lines.
	static_assert(sizeof(RingHeader) == 128, "RingHeader layout is part of the protocol");

	// Total mapping size needed for a given payload capacity.
	inline size_t RingMappingSize(size_t capacity) { return sizeof(RingHeader) + capacity; }

	class RingWriter
	{
	public:
		// `memory` points at a mapping of RingMappingSize(capacity) bytes.
		bool Initialize(void *memory, size_t capacity);
		void Shutdown();

		bool IsValid() const { return m_header != nullptr; }

		// Writes one record made of an optional fixed part and an optional trailing blob.
		// Returns false when the ring is full (the record is dropped, never partially written).
		bool Write(RecordType type, const void *fixed, uint32_t fixed_size,
		           const void *blob = nullptr, uint32_t blob_size = 0, uint16_t flags = 0);

		uint64_t DroppedBytes() const;
		uint64_t DroppedRecords() const;
		uint64_t BytesWritten() const { return m_bytes_written; }
		uint64_t RecordsWritten() const { return m_records_written; }
		size_t FreeSpace() const;

	private:
		RingHeader *m_header = nullptr;
		uint8_t *m_data = nullptr;
		uint64_t m_capacity = 0;
		uint64_t m_mask = 0;
		uint64_t m_bytes_written = 0;
		uint64_t m_records_written = 0;
	};

	class RingReader
	{
	public:
		bool Attach(void *memory, size_t expected_capacity = 0);
		void Detach();

		bool IsValid() const { return m_header != nullptr; }

		// Points `header` / `payload` at the next record. The pointers stay valid until Pop().
		bool Peek(const RecordHeader *&header, const uint8_t *&payload, uint32_t &payload_size);
		void Pop();

		uint64_t DroppedBytes() const;
		uint64_t DroppedRecords() const;
		bool WriterAlive() const;
		size_t PendingBytes() const;

	private:
		RingHeader *m_header = nullptr;
		const uint8_t *m_data = nullptr;
		uint64_t m_capacity = 0;
		uint64_t m_mask = 0;
		uint32_t m_current_size = 0;
	};
}
