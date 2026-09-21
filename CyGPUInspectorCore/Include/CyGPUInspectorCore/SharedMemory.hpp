// CyGPUInspector — thin RAII wrapper around a Windows file mapping.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cygi
{
	class SharedMemory
	{
	public:
		SharedMemory() = default;
		~SharedMemory();

		SharedMemory(const SharedMemory &) = delete;
		SharedMemory &operator=(const SharedMemory &) = delete;
		SharedMemory(SharedMemory &&other) noexcept;
		SharedMemory &operator=(SharedMemory &&other) noexcept;

		// Creates the mapping, or opens it when it already exists (`created` says which happened).
		bool Create(const char *name, size_t size, bool *created = nullptr);
		// Opens an existing mapping only.
		bool Open(const char *name, size_t size, bool read_only = false);
		void Close();

		bool IsValid() const { return m_view != nullptr; }
		void *Data() const { return m_view; }
		size_t Size() const { return m_size; }
		const std::string &Name() const { return m_name; }

	private:
		void *m_mapping = nullptr;   // HANDLE
		void *m_view = nullptr;
		size_t m_size = 0;
		std::string m_name;
	};

	// Auto-reset event used to wake the App when the add-on wrote something.
	class SharedEvent
	{
	public:
		SharedEvent() = default;
		~SharedEvent();

		SharedEvent(const SharedEvent &) = delete;
		SharedEvent &operator=(const SharedEvent &) = delete;

		bool Create(const char *name);
		bool Open(const char *name);
		void Close();

		bool IsValid() const { return m_handle != nullptr; }
		void Signal();
		// Returns true when signalled, false on timeout.
		bool Wait(uint32_t timeout_ms);

	private:
		void *m_handle = nullptr;
	};
}
