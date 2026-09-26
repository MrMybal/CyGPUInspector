// CyGPUInspectorApp — the messages of a shader compiler, as lines of the source.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cygi
{
	// One message of FXC ("edited.hlsl(12,5-9): error X3004: ...") or DXC
	// ("edited.hlsl:12:5: error: ..."). Lines and columns are one-based, 0 when unknown.
	struct CompilerMessage
	{
		size_t line = 0;
		size_t column = 0;
		bool error = true;       // false for a warning
		std::string text;        // the whole line, as the compiler wrote it
	};
	std::vector<CompilerMessage> ParseCompilerMessages(const std::string &messages);
}
