// CyGPUInspector — version constants shared by every component.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

// The resource compiler includes this header for the version macros below: keep the C++ parts
// out of its way, RC only understands the #define block.
#ifndef RC_INVOKED
#include <cstdint>
#endif

#define CYGI_VERSION_MAJOR 0
#define CYGI_VERSION_MINOR 1
#define CYGI_VERSION_PATCH 0
#define CYGI_VERSION_STRING "0.1.0"

#ifndef RC_INVOKED
namespace cygi
{
	inline constexpr const char *kVersionString = CYGI_VERSION_STRING;
	inline constexpr uint32_t kVersionMajor = CYGI_VERSION_MAJOR;
	inline constexpr uint32_t kVersionMinor = CYGI_VERSION_MINOR;
	inline constexpr uint32_t kVersionPatch = CYGI_VERSION_PATCH;
}
#endif
