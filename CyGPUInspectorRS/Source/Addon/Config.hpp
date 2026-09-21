// CyGPUInspector — settings read from ReShade.ini.
//
// Shared by both add-ons, which is why the section name is a build setting rather than a literal:
// CyGPUInspectorRS reads [CYGPUINSPECTOR], CyGPUInjector reads [CYGPUINJECTOR], and neither can
// overwrite the other's keys by accident.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <string>

#ifndef CYGI_CONFIG_SECTION
#define CYGI_CONFIG_SECTION "CYGPUINSPECTOR"
#endif

namespace cygi::config
{
	inline constexpr const char *kSection = CYGI_CONFIG_SECTION;

	bool GetBool(const char *key, bool fallback);
	int GetInt(const char *key, int fallback);
	std::string GetString(const char *key, const char *fallback);

	void SetBool(const char *key, bool value);
	void SetInt(const char *key, int value);
}
