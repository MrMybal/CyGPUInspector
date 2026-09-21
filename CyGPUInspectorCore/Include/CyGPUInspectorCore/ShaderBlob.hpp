// CyGPUInspector — DXBC container parsing and shader signatures.
//
// Both sides need this: the add-on computes the signature of every shader it sees, and the
// standalone re-reads the same containers to pick a disassembler and a decompiler backend.
//
// Container layout (DXBC, also used as the envelope of DXIL):
//   char     magic[4]        "DXBC"
//   uint8    checksum[16]    MD5-like digest computed by the compiler over the rest
//   uint32   one             always 1
//   uint32   total_size
//   uint32   chunk_count
//   uint32   chunk_offsets[chunk_count]
//   chunks:  char fourcc[4]; uint32 size; uint8 data[size]
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Protocol.hpp"
#include "Sha256.hpp"

#include <cstddef>
#include <cstdint>

namespace cygi
{
	struct ShaderBlobInfo
	{
		bool valid = false;
		ShaderFormat format = ShaderFormat::unknown;
		ShaderStage stage = ShaderStage::unknown;
		uint32_t shader_model = 0;   // 0xMN, e.g. 0x50 = SM 5.0, 0x66 = SM 6.6
		uint32_t chunk_count = 0;
		uint32_t code_chunk_offset = 0;
		uint32_t code_chunk_size = 0;
		bool has_debug_info = false;
	};

	// Reads the container header. Never dereferences past `size`.
	bool ParseShaderBlob(const void *data, size_t size, ShaderBlobInfo &out);

	// SHA-256 of the byte code with the container checksum zeroed: identifies the exact shader
	// while staying stable across compilers that recompute the checksum differently.
	Sha256Digest ComputeShaderSignature(const void *data, size_t size);

	// SHA-256 of the code chunk alone (SHEX / SHDR / DXIL): matches shaders that only differ by
	// debug or reflection parts, which is what "I have already seen this shader" should mean.
	Sha256Digest ComputeShaderSemanticHash(const void *data, size_t size);

	// "5_0", "6_6", or "?" when the model could not be read.
	const char *ShaderModelName(uint32_t shader_model);
}
