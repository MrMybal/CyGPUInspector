// CyGPUInspectorDecompiler — shader disassembly and reflection.
//
// Two backends, both loaded at run time so that a missing DLL degrades into a clear message
// instead of preventing the application from starting:
//   * DXBC (Shader Model 4/5) : d3dcompiler_47.dll, present on every Windows install
//   * DXIL (Shader Model 6.x) : dxcompiler.dll, shipped with the Windows SDK (and searched for)
//
// Neither runs inside the game: this is standalone-side work on byte code already received.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	// Where a backend was found, and why it is unusable when it is.
	struct ToolInfo
	{
		bool available = false;
		std::string name;
		std::string path;
		std::string error;
	};

	// Diagnostics for the UI: which tools this installation actually has.
	const ToolInfo &DxbcToolInfo();
	const ToolInfo &DxilToolInfo();

	struct DisassemblyResult
	{
		bool ok = false;
		ShaderFormat format = ShaderFormat::unknown;
		std::string text;
		std::string error;
		std::string tool;
		double milliseconds = 0.0;

		size_t LineCount() const;
	};

	// How the disassembly should be printed. It matters: a decompiler parses this text, and the
	// annotations a human wants in the viewer are noise a parser chokes on.
	enum class DisassemblyStyle : uint32_t
	{
		annotated = 0,   // instruction numbers and byte offsets, for reading
		plain,           // what a decompiler expects, nothing else
	};

	// Disassembles a DXBC or DXIL container. Never throws, never blocks on anything but the DLL.
	DisassemblyResult Disassemble(const void *code, size_t size,
	                              DisassemblyStyle style = DisassemblyStyle::annotated);

	// ---------------------------------------------------------------------------------------
	// Reflection: what a shader binds, and what it consumes and produces.
	// ---------------------------------------------------------------------------------------

	enum class BindingKind : uint32_t
	{
		unknown = 0,
		constant_buffer,
		texture_buffer,
		texture,
		sampler,
		unordered_access,
		structured_buffer,
		byte_address_buffer,
		append_buffer,
		consume_buffer,
		acceleration_structure,
	};
	const char *BindingKindName(BindingKind kind);

	struct ShaderBinding
	{
		BindingKind kind = BindingKind::unknown;
		std::string name;
		uint32_t bind_point = 0;
		uint32_t bind_count = 0;
		uint32_t space = 0;
		uint32_t size = 0;        // constant buffers: size in bytes
	};

	struct SignatureElement
	{
		std::string semantic_name;
		uint32_t semantic_index = 0;
		uint32_t register_index = 0;
		uint8_t mask = 0;
		std::string component_type;

		// "SV_Position0.xyzw"
		std::string Describe() const;
	};

	struct ReflectionResult
	{
		bool ok = false;
		std::string error;
		std::string tool;
		std::vector<ShaderBinding> bindings;
		std::vector<SignatureElement> inputs;
		std::vector<SignatureElement> outputs;
		uint32_t instruction_count = 0;
		uint32_t constant_buffer_count = 0;
		uint32_t thread_group[3] = { 0, 0, 0 };  // compute shaders only
	};

	ReflectionResult Reflect(const void *code, size_t size);

	// ---------------------------------------------------------------------------------------
	// Compilation: needed to validate a reconstructed HLSL (milestone 6) and to build the byte
	// code of a replacement shader (milestone 7). FXC for Shader Model 5 and below, DXC above.
	// ---------------------------------------------------------------------------------------

	struct CompileResult
	{
		bool ok = false;
		std::vector<uint8_t> byte_code;
		std::string messages;   // warnings, or the compiler errors when it failed
		std::string error;
		std::string tool;
		double milliseconds = 0.0;
	};

	// `target` is a profile such as "ps_5_0" or "cs_6_6"; the major version picks the compiler.
	CompileResult CompileHlsl(const std::string &source, const std::string &entry_point,
	                          const std::string &target, const std::string &source_name = "shader.hlsl");
}
