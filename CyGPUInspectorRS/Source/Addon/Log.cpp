// CyGPUInspector — logging through ReShade's log file.
//
// Shared by both add-ons. The prefix is a build setting so a line in ReShade.log always says
// which of the two wrote it.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Log.hpp"

#include <reshade.hpp>

#ifndef CYGI_ADDON_NAME
#define CYGI_ADDON_NAME "CyGPUInspector"
#endif

#include <cstdarg>
#include <cstdio>

namespace cygi::log
{
	namespace
	{
		bool g_verbose = false;

		void Write(reshade::log::level level, const char *format, va_list args)
		{
			char buffer[1024];
			std::vsnprintf(buffer, sizeof(buffer), format, args);

			char prefixed[1100];
			std::snprintf(prefixed, sizeof(prefixed), "[" CYGI_ADDON_NAME "] %s", buffer);
			reshade::log::message(level, prefixed);
		}
	}

	void SetVerbose(bool verbose) { g_verbose = verbose; }
	bool IsVerbose() { return g_verbose; }

	void Info(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		Write(reshade::log::level::info, format, args);
		va_end(args);
	}

	void Warning(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		Write(reshade::log::level::warning, format, args);
		va_end(args);
	}

	void Error(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		Write(reshade::log::level::error, format, args);
		va_end(args);
	}

	void Debug(const char *format, ...)
	{
		if (!g_verbose)
			return;

		va_list args;
		va_start(args, format);
		Write(reshade::log::level::debug, format, args);
		va_end(args);
	}
}
