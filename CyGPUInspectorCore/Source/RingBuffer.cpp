// CyGPUInspector — SPSC shared memory ring buffer.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/RingBuffer.hpp"

#include <cstring>

namespace cygi
{
	namespace
	{
		// Records are 16 byte aligned so that a padding record is never smaller than a
		// RecordHeader: the reader can always read the header of whatever it lands on.
		inline uint32_t AlignUp16(uint32_t value) { return (value + 15u) & ~15u; }

		inline bool IsPowerOfTwo(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }
	}

	bool RingWriter::Initialize(void *memory, size_t capacity)
	{
		if (memory == nullptr || !IsPowerOfTwo(capacity))
			return false;

		m_header = static_cast<RingHeader *>(memory);
		m_data = static_cast<uint8_t *>(memory) + sizeof(RingHeader);
		m_capacity = capacity;
		m_mask = capacity - 1;

		m_header->magic = kRingMagic;
		m_header->version = kProtocolVersion;
		m_header->capacity = capacity;
		m_header->write_pos.store(0, std::memory_order_relaxed);
		m_header->read_pos.store(0, std::memory_order_relaxed);
		m_header->dropped_bytes.store(0, std::memory_order_relaxed);
		m_header->dropped_records.store(0, std::memory_order_relaxed);
		m_header->sequence.store(0, std::memory_order_relaxed);
		m_header->reader_attached.store(0, std::memory_order_relaxed);
		m_header->writer_alive.store(1, std::memory_order_release);
		return true;
	}

	void RingWriter::Shutdown()
	{
		if (m_header != nullptr)
			m_header->writer_alive.store(0, std::memory_order_release);
		m_header = nullptr;
		m_data = nullptr;
	}

	size_t RingWriter::FreeSpace() const
	{
		if (m_header == nullptr)
			return 0;
		const uint64_t write_pos = m_header->write_pos.load(std::memory_order_relaxed);
		const uint64_t read_pos = m_header->read_pos.load(std::memory_order_acquire);
		return static_cast<size_t>(m_capacity - (write_pos - read_pos));
	}

	bool RingWriter::Write(RecordType type, const void *fixed, uint32_t fixed_size,
	                       const void *blob, uint32_t blob_size, uint16_t flags)
	{
		if (m_header == nullptr)
			return false;

		const uint32_t payload = fixed_size + blob_size;
		const uint32_t total = AlignUp16(static_cast<uint32_t>(sizeof(RecordHeader)) + payload);
		if (total > m_capacity)
			return false; // a single record larger than the whole ring can never be written

		const uint64_t write_pos = m_header->write_pos.load(std::memory_order_relaxed);
		const uint64_t read_pos = m_header->read_pos.load(std::memory_order_acquire);
		const uint64_t used = write_pos - read_pos;

		uint64_t offset = write_pos & m_mask;
		const uint64_t to_end = m_capacity - offset;

		// Keep records contiguous: pad up to the end of the buffer when needed.
		uint64_t needed = total;
		const bool needs_padding = to_end < total;
		if (needs_padding)
			needed += to_end;

		if (m_capacity - used < needed)
		{
			m_header->dropped_bytes.fetch_add(total, std::memory_order_relaxed);
			m_header->dropped_records.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		uint64_t cursor = write_pos;
		if (needs_padding)
		{
			// `to_end` is a multiple of 16 (capacity is a power of two and every record is
			// 16 byte aligned), so a full RecordHeader always fits in the padding.
			RecordHeader pad = {};
			pad.size = static_cast<uint32_t>(to_end);
			pad.type = RecordType::padding;
			pad.flags = 0;
			pad.sequence = 0;
			std::memcpy(m_data + offset, &pad, sizeof(pad));
			cursor += to_end;
			offset = cursor & m_mask;
		}

		RecordHeader header = {};
		header.size = total;
		header.type = type;
		header.flags = flags;
		header.sequence = m_header->sequence.fetch_add(1, std::memory_order_relaxed) + 1;

		uint8_t *dest = m_data + offset;
		std::memcpy(dest, &header, sizeof(header));
		if (fixed != nullptr && fixed_size != 0)
			std::memcpy(dest + sizeof(header), fixed, fixed_size);
		if (blob != nullptr && blob_size != 0)
			std::memcpy(dest + sizeof(header) + fixed_size, blob, blob_size);

		// Publish: everything above must be visible before the new write position is.
		m_header->write_pos.store(cursor + total, std::memory_order_release);

		m_bytes_written += total;
		++m_records_written;
		return true;
	}

	uint64_t RingWriter::DroppedBytes() const
	{
		return m_header != nullptr ? m_header->dropped_bytes.load(std::memory_order_relaxed) : 0;
	}

	uint64_t RingWriter::DroppedRecords() const
	{
		return m_header != nullptr ? m_header->dropped_records.load(std::memory_order_relaxed) : 0;
	}

	bool RingReader::Attach(void *memory, size_t expected_capacity)
	{
		if (memory == nullptr)
			return false;

		RingHeader *header = static_cast<RingHeader *>(memory);
		if (header->magic != kRingMagic || header->version != kProtocolVersion)
			return false;
		if (!IsPowerOfTwo(header->capacity))
			return false;
		if (expected_capacity != 0 && header->capacity != expected_capacity)
			return false;

		m_header = header;
		m_data = static_cast<const uint8_t *>(memory) + sizeof(RingHeader);
		m_capacity = header->capacity;
		m_mask = m_capacity - 1;
		m_current_size = 0;
		m_header->reader_attached.store(1, std::memory_order_release);
		return true;
	}

	void RingReader::Detach()
	{
		if (m_header != nullptr)
			m_header->reader_attached.store(0, std::memory_order_release);
		m_header = nullptr;
		m_data = nullptr;
	}

	bool RingReader::Peek(const RecordHeader *&out_header, const uint8_t *&payload, uint32_t &payload_size)
	{
		if (m_header == nullptr)
			return false;

		for (;;)
		{
			const uint64_t read_pos = m_header->read_pos.load(std::memory_order_relaxed);
			const uint64_t write_pos = m_header->write_pos.load(std::memory_order_acquire);
			if (read_pos == write_pos)
				return false;

			const uint64_t available = write_pos - read_pos;
			const uint64_t offset = read_pos & m_mask;
			if (available < sizeof(RecordHeader))
				return false; // torn write, the producer died mid record

			const RecordHeader *header = reinterpret_cast<const RecordHeader *>(m_data + offset);
			const uint32_t size = header->size;
			if (size < sizeof(RecordHeader) || size > available || size > m_capacity ||
			    (size & 15u) != 0)
			{
				// Corrupted stream (producer killed): give up rather than read garbage.
				m_header->read_pos.store(write_pos, std::memory_order_release);
				return false;
			}

			if (header->type == RecordType::padding)
			{
				m_header->read_pos.store(read_pos + size, std::memory_order_release);
				continue;
			}

			out_header = header;
			payload = m_data + offset + sizeof(RecordHeader);
			payload_size = size - static_cast<uint32_t>(sizeof(RecordHeader));
			m_current_size = size;
			return true;
		}
	}

	void RingReader::Pop()
	{
		if (m_header == nullptr || m_current_size == 0)
			return;
		const uint64_t read_pos = m_header->read_pos.load(std::memory_order_relaxed);
		m_header->read_pos.store(read_pos + m_current_size, std::memory_order_release);
		m_current_size = 0;
	}

	uint64_t RingReader::DroppedBytes() const
	{
		return m_header != nullptr ? m_header->dropped_bytes.load(std::memory_order_relaxed) : 0;
	}

	uint64_t RingReader::DroppedRecords() const
	{
		return m_header != nullptr ? m_header->dropped_records.load(std::memory_order_relaxed) : 0;
	}

	bool RingReader::WriterAlive() const
	{
		return m_header != nullptr && m_header->writer_alive.load(std::memory_order_acquire) != 0;
	}

	size_t RingReader::PendingBytes() const
	{
		if (m_header == nullptr)
			return 0;
		const uint64_t read_pos = m_header->read_pos.load(std::memory_order_relaxed);
		const uint64_t write_pos = m_header->write_pos.load(std::memory_order_acquire);
		return static_cast<size_t>(write_pos - read_pos);
	}
}
