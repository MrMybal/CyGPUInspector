// CyGPUInspectorRS — everything needed to rebuild a pipeline with one shader swapped.
//
// ReShade only lets an add-on substitute a shader at `create_pipeline`, which is too late for a
// shader the game created minutes ago. So the description of every pipeline is captured when it
// is created, and a replacement pipeline is built from it on demand.
//
// The shader byte code is NOT stored here: it already lives once per signature in the
// ShaderTracker, and duplicating it per pipeline would cost hundreds of megabytes in a real game.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <reshade.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cygi
{

	struct PipelineBlueprint
	{
		// False when a sub-object type we cannot copy was met: the pipeline is then simply not
		// replaceable, and the user is told why rather than getting a silent failure.
		bool replaceable = false;
		std::string reason;

		reshade::api::pipeline_layout layout = {};
		std::vector<reshade::api::pipeline_subobject> subobjects;

		// Which sub-objects are shaders, and which tracked shader each one is.
		struct ShaderSlot
		{
			uint32_t subobject_index = 0;
			uint32_t shader_id = 0;
			std::string entry_point;
		};
		std::vector<ShaderSlot> shaders;

		// Backing storage the sub-object `data` pointers point into.
		std::vector<std::vector<uint8_t>> blobs;
		std::vector<std::string> strings;

		// Captures the description ReShade gave us. Copies everything it can reach.
		bool Capture(uint32_t subobject_count, const reshade::api::pipeline_subobject *subobjects,
		             reshade::api::pipeline_layout layout, const std::vector<uint32_t> &shader_ids);

		// Supplies the byte code of an untouched stage. It is a callback rather than a tracker
		// reference so the caller can already hold the tracker lock without taking it twice.
		using CodeProvider = std::function<bool(uint32_t shader_id, std::vector<uint8_t> &out)>;

		// Builds a pipeline identical to the original except for `shader_id`, replaced by `code`.
		// Returns a zero handle and fills `error` on failure.
		reshade::api::pipeline CreateVariant(reshade::api::device *device, const CodeProvider &provide_code,
		                                     uint32_t replaced_shader_id, const void *code, size_t code_size,
		                                     std::string &error) const;
	};
}
