// CyGPUInspectorApp — searching the shaders of a frame by what they do (section 46).
//
// A query is a line of words, each one narrowing the list:
//
//   stage:compute        stage:ps,vs            the stage (vs ps cs gs hs ds ms as, or full names)
//   writes:rgba16f       writes:depth  writes:#12   a target the shader's draws write: a format, depth,
//                                                    hdr (any float format) or a resource id
//   reads:depth          reads:#7               a resource bound for reading (needs a deep capture)
//   ms>0.2   ms<0.01                            GPU time in the analysed frame (needs timing)
//   calls>1000                                  draws and dispatches in the frame
//   format:dxil   sm:6   sm:5_0                 byte code format, shader model
//   tag:bloom   name:tonemap                    tags and names from the shader's notes
//   used   disabled   replaced   new            used this frame, changed, new in this game's profile
//   anything else                               a piece of the signature or of the stage name
//
// "Find the shaders writing RGBA16F" is `writes:rgba16f`; "compute shaders writing this resource"
// is `stage:cs writes:#12`. The same text works in the Shaders panel and in the find_shaders MCP
// tool, so an agent and a person search the same way.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Session/SessionModel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct ShaderQuery
	{
		struct Comparison
		{
			bool active = false;
			bool greater = true;      // > or >=, otherwise < or <=
			bool or_equal = false;
			bool exact = false;       // =
			double value = 0.0;
			bool Accepts(double measured) const;
		};

		std::vector<ShaderStage> stages;          // any of them
		std::vector<std::string> writes;          // every pattern must match something written
		std::vector<std::string> reads;
		Comparison gpu_ms;
		Comparison calls;
		std::string format;                        // "dxbc", "dxil"
		std::string shader_model;                  // a prefix of "6_6"
		std::vector<std::string> tags;
		std::vector<std::string> names;
		bool used = false;
		bool disabled = false;
		bool replaced = false;
		bool new_only = false;
		std::vector<std::string> words;            // free text

		std::vector<std::string> errors;           // words that looked like a criterion but are not one
		bool structured = false;                   // anything beyond free text

		bool NeedsBindings() const { return !reads.empty(); }
		bool NeedsNotes() const { return !tags.empty() || !names.empty(); }
		bool NeedsTiming() const { return gpu_ms.active; }

		static ShaderQuery Parse(const std::string &text);
	};

	// What the analysed frame says a shader did.
	struct ShaderFrameFacts
	{
		std::vector<uint32_t> written;   // render targets of its draws, UAVs and targets bound in a deep capture
		std::vector<uint32_t> read;      // shader resources bound in a deep capture
		bool depth_target = false;       // one of its draws had a depth target
		bool described = false;          // a deep capture described at least one of its commands
	};

	// Per shader id, from the model's last frame. The model lock is the caller's.
	std::unordered_map<uint32_t, ShaderFrameFacts> CollectShaderFacts(const SessionModel &model);

	struct ShaderNotesView
	{
		std::string name;
		std::vector<std::string> tags;
	};

	// "rgba16f" matches r16g16b16a16_float, "rgba8" r8g8b8a8_unorm, "d32" d32_float, "srgb" any
	// sRGB format; a longer piece of the full name matches too.
	bool FormatMatches(uint32_t format, const std::string &pattern);

	// `facts` and `notes` may be null when the query does not need them.
	bool MatchesShaderQuery(const ShaderQuery &query, const ShaderInfo &shader, const ShaderFrameFacts *facts,
	                        const SessionModel &model, const ShaderNotesView *notes, bool is_new);
}
