// CyGPUInspectorDecompiler — can this byte code take the place of that one in the game's pipeline?
//
// A replacement shader goes into a pipeline built for the original: the same root signature (or
// the same D3D11 bindings), the same stage before and after it. Direct3D 12 checks that match when
// the pipeline is created and refuses it with no explanation an add-on can read ("the graphics API
// refused the replacement pipeline"). This compares the two shaders first and says what differs,
// the way CyRSAssistant checks a replacement before activating it:
//
//   blocking   an input the original does not have, or that reads components it does not have;
//              a resource the original does not declare (the root signature has no place for it);
//              for a stage that feeds another one, an output of the original that disappears.
//   warning    the outputs of a pixel shader that differ: allowed, but worth knowing.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cygi
{
	struct ReplacementIssue
	{
		bool blocking = true;
		std::string message;
	};

	struct ReplacementCheck
	{
		bool compatible = true;             // false when at least one issue is blocking
		std::vector<ReplacementIssue> issues;

		// The issues one per line, blocking ones first.
		std::string Describe() const;
	};

	ReplacementCheck CheckReplacement(const void *original, size_t original_size,
	                                  const void *replacement, size_t replacement_size);
}
