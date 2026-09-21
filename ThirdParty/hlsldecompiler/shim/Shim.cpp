// Shim for the 3Dmigoto decompiler.
//
// This file is NOT from 3Dmigoto: it provides the four symbols its sources expect from the rest of
// that project, so the vendored files themselves stay byte for byte identical to upstream.
//
// Copyright (C) 2026 CyberAlien. Licensed under the GNU GPL v3 or later.
#include "log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

// 3Dmigoto's global debug switch. Left off: the debug output is extremely verbose and says
// nothing a user of CyGPUInspector needs.
bool gLogDebug = false;

namespace cygi::hlsldecompiler
{
	namespace
	{
		// The decompiler is called from one worker thread at a time, but a mutex costs nothing
		// here and removes any doubt.
		std::mutex g_mutex;
		std::string g_captured;
		bool g_capturing = false;

		void Append(const char *format, va_list arguments)
		{
			char buffer[1024];
			std::vsnprintf(buffer, sizeof(buffer), format, arguments);

			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_capturing)
				g_captured += buffer;
		}
	}

	void BeginCapture()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_captured.clear();
		g_capturing = true;
	}

	std::string EndCapture()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_capturing = false;
		return std::move(g_captured);
	}

	void Write(const char *format, ...)
	{
		va_list arguments;
		va_start(arguments, format);
		Append(format, arguments);
		va_end(arguments);
	}

	void WriteDebug(const char *format, ...)
	{
		if (!gLogDebug)
			return;

		va_list arguments;
		va_start(arguments, format);
		Append(format, arguments);
		va_end(arguments);
	}
}

std::string LogTime()
{
	SYSTEMTIME now = {};
	GetLocalTime(&now);

	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u\n", now.wYear, now.wMonth,
		now.wDay, now.wHour, now.wMinute, now.wSecond);
	return buffer;
}
