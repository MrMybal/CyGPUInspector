// CyGPUInspectorDecompiler — the shared second half of every SPIR-V based backend.
//
// Two backends reach HLSL through SPIR-V: `dxil-spirv` for Shader Model 6 and `dxbc-spirv` for
// Shader Model 4 and 5. What happens after the SPIR-V exists is identical for both, including the
// part that matters most — putting back the Direct3D registers and resource names that a Vulkan
// module does not carry — so it lives here rather than twice.
//
// Internal to the library: not part of the public decompiler interface.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "CyGPUInspectorDecompiler/Disassembler.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace spv
{
	enum ExecutionModel;
}

namespace cygi
{
	struct SpirvToHlslRequest
	{
		const std::vector<uint32_t> *words = nullptr;

		// Execution model of the shader, as SPIR-V spells it. Needed because a resource binding
		// is per stage in the HLSL backend of SPIRV-Cross.
		int execution_model = 0;

		// Target model, as SPIRV-Cross counts it: 60 for Shader Model 6.0, 51 for 5.1.
		uint32_t shader_model = 51;

		// The Direct3D reflection of the *original* shader. Its registers and names are what the
		// intermediate SPIR-V lost; nothing here is invented, and a resource the reflection does
		// not name keeps whatever name the SPIR-V had.
		const ReflectionResult *reflection = nullptr;

		// Vertex stage only: restores the input semantics from the input signature.
		bool restore_vertex_semantics = false;
	};

	struct SpirvToHlslResult
	{
		bool ok = false;
		std::string hlsl;
		std::string error;
	};

	SpirvToHlslResult SpirvToHlsl(const SpirvToHlslRequest &request);
}
