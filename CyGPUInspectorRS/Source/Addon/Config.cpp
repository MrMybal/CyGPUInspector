// CyGPUInspectorRS — settings read from ReShade.ini.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Config.hpp"

#include <reshade.hpp>

namespace cygi::config
{
	bool GetBool(const char *key, bool fallback)
	{
		bool value = fallback;
		// A null runtime reads the global ReShade.ini, which is what we want: the add-on settings
		// are not per effect runtime.
		reshade::get_config_value(nullptr, kSection, key, value);
		return value;
	}

	int GetInt(const char *key, int fallback)
	{
		int value = fallback;
		reshade::get_config_value(nullptr, kSection, key, value);
		return value;
	}

	std::string GetString(const char *key, const char *fallback)
	{
		char buffer[512] = {};
		size_t size = sizeof(buffer);
		if (!reshade::get_config_value(nullptr, kSection, key, buffer, &size))
			return fallback != nullptr ? fallback : "";
		return buffer;
	}

	void SetBool(const char *key, bool value)
	{
		reshade::set_config_value(nullptr, kSection, key, value);
	}

	void SetInt(const char *key, int value)
	{
		reshade::set_config_value(nullptr, kSection, key, value);
	}
}
