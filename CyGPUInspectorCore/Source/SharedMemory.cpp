// CyGPUInspector — Windows file mapping / event wrappers.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/SharedMemory.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace cygi
{
	SharedMemory::~SharedMemory()
	{
		Close();
	}

	SharedMemory::SharedMemory(SharedMemory &&other) noexcept
		: m_mapping(other.m_mapping), m_view(other.m_view), m_size(other.m_size), m_name(std::move(other.m_name))
	{
		other.m_mapping = nullptr;
		other.m_view = nullptr;
		other.m_size = 0;
	}

	SharedMemory &SharedMemory::operator=(SharedMemory &&other) noexcept
	{
		if (this != &other)
		{
			Close();
			m_mapping = other.m_mapping;
			m_view = other.m_view;
			m_size = other.m_size;
			m_name = std::move(other.m_name);
			other.m_mapping = nullptr;
			other.m_view = nullptr;
			other.m_size = 0;
		}
		return *this;
	}

	bool SharedMemory::Create(const char *name, size_t size, bool *created)
	{
		Close();
		if (name == nullptr || size == 0)
			return false;

		const HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
			static_cast<DWORD>(static_cast<uint64_t>(size) >> 32), static_cast<DWORD>(size & 0xFFFFFFFFu), name);
		if (mapping == nullptr)
			return false;

		const bool already_existed = GetLastError() == ERROR_ALREADY_EXISTS;
		if (created != nullptr)
			*created = !already_existed;

		void *view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (view == nullptr)
		{
			CloseHandle(mapping);
			return false;
		}

		m_mapping = mapping;
		m_view = view;
		m_size = size;
		m_name = name;
		return true;
	}

	bool SharedMemory::Open(const char *name, size_t size, bool read_only)
	{
		Close();
		if (name == nullptr)
			return false;

		const DWORD access = read_only ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
		const HANDLE mapping = OpenFileMappingA(access, FALSE, name);
		if (mapping == nullptr)
			return false;

		void *view = MapViewOfFile(mapping, access, 0, 0, size);
		if (view == nullptr)
		{
			CloseHandle(mapping);
			return false;
		}

		if (size == 0)
		{
			MEMORY_BASIC_INFORMATION info = {};
			if (VirtualQuery(view, &info, sizeof(info)) != 0)
				size = info.RegionSize;
		}

		m_mapping = mapping;
		m_view = view;
		m_size = size;
		m_name = name;
		return true;
	}

	void SharedMemory::Close()
	{
		if (m_view != nullptr)
			UnmapViewOfFile(m_view);
		if (m_mapping != nullptr)
			CloseHandle(static_cast<HANDLE>(m_mapping));
		m_view = nullptr;
		m_mapping = nullptr;
		m_size = 0;
		m_name.clear();
	}

	SharedEvent::~SharedEvent()
	{
		Close();
	}

	bool SharedEvent::Create(const char *name)
	{
		Close();
		m_handle = CreateEventA(nullptr, FALSE, FALSE, name);
		return m_handle != nullptr;
	}

	bool SharedEvent::Open(const char *name)
	{
		Close();
		m_handle = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
		return m_handle != nullptr;
	}

	void SharedEvent::Close()
	{
		if (m_handle != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_handle));
			m_handle = nullptr;
		}
	}

	void SharedEvent::Signal()
	{
		if (m_handle != nullptr)
			SetEvent(static_cast<HANDLE>(m_handle));
	}

	bool SharedEvent::Wait(uint32_t timeout_ms)
	{
		if (m_handle == nullptr)
			return false;
		return WaitForSingleObject(static_cast<HANDLE>(m_handle), timeout_ms) == WAIT_OBJECT_0;
	}
}
