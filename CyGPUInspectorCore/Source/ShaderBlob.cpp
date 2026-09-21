// CyGPUInspector — DXBC container parsing and shader signatures.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/ShaderBlob.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace cygi
{
	namespace
	{
		constexpr uint32_t kFourCC_DXBC = 0x43425844; // 'DXBC' little endian
		constexpr uint32_t kFourCC_DXIL = 0x4C495844; // 'DXIL'
		constexpr uint32_t kFourCC_SHEX = 0x58454853; // 'SHEX' (SM 4/5 code)
		constexpr uint32_t kFourCC_SHDR = 0x52444853; // 'SHDR' (SM 4 code)
		constexpr uint32_t kFourCC_SPIRV = 0x07230203; // SPIR-V magic
		constexpr uint32_t kFourCC_ILDB = 0x42444C49; // 'ILDB' (DXIL with debug info)
		constexpr uint32_t kFourCC_SDBG = 0x47424453; // 'SDBG'
		constexpr uint32_t kFourCC_ILDN = 0x4E444C49; // 'ILDN' (debug name)

		constexpr size_t kHeaderSize = 4 + 16 + 4 + 4 + 4; // magic + checksum + one + size + count
		constexpr size_t kChecksumOffset = 4;

		inline uint32_t ReadU32(const uint8_t *data, size_t offset)
		{
			uint32_t value = 0;
			std::memcpy(&value, data + offset, sizeof(value));
			return value;
		}

		// The program version token is shared by SM4/5 (SHEX/SHDR) and SM6 (DXIL program header):
		// bits 16+ hold the program kind, bits 4..7 the major version, bits 0..3 the minor one.
		ShaderStage StageFromProgramKind(uint32_t kind)
		{
			switch (kind)
			{
			case 0: return ShaderStage::pixel;
			case 1: return ShaderStage::vertex;
			case 2: return ShaderStage::geometry;
			case 3: return ShaderStage::hull;
			case 4: return ShaderStage::domain;
			case 5: return ShaderStage::compute;
			case 6: return ShaderStage::unknown; // library
			case 7: return ShaderStage::raygen;
			case 8: return ShaderStage::intersection;
			case 9: return ShaderStage::any_hit;
			case 10: return ShaderStage::closest_hit;
			case 11: return ShaderStage::miss;
			case 12: return ShaderStage::callable;
			case 13: return ShaderStage::mesh;
			case 14: return ShaderStage::amplification;
			default: return ShaderStage::unknown;
			}
		}

		struct Chunk
		{
			uint32_t fourcc;
			uint32_t offset;   // offset of the chunk payload inside the blob
			uint32_t size;
		};

		bool ReadChunks(const uint8_t *bytes, size_t size, std::vector<Chunk> &chunks)
		{
			if (size < kHeaderSize || ReadU32(bytes, 0) != kFourCC_DXBC)
				return false;

			const uint32_t total_size = ReadU32(bytes, 24);
			const uint32_t chunk_count = ReadU32(bytes, 28);
			if (total_size > size || chunk_count == 0 || chunk_count > 64)
				return false;
			if (kHeaderSize + static_cast<size_t>(chunk_count) * 4 > size)
				return false;

			chunks.reserve(chunk_count);
			for (uint32_t i = 0; i < chunk_count; ++i)
			{
				const uint32_t chunk_offset = ReadU32(bytes, kHeaderSize + i * 4);
				if (chunk_offset + 8 > size)
					return false;

				Chunk chunk = {};
				chunk.fourcc = ReadU32(bytes, chunk_offset);
				chunk.size = ReadU32(bytes, chunk_offset + 4);
				chunk.offset = chunk_offset + 8;
				if (static_cast<size_t>(chunk.offset) + chunk.size > size)
					return false;
				chunks.push_back(chunk);
			}
			return true;
		}
	}

	bool ParseShaderBlob(const void *data, size_t size, ShaderBlobInfo &out)
	{
		out = ShaderBlobInfo();
		if (data == nullptr || size < 8)
			return false;

		const uint8_t *bytes = static_cast<const uint8_t *>(data);

		if (ReadU32(bytes, 0) == kFourCC_SPIRV)
		{
			out.valid = true;
			out.format = ShaderFormat::spirv;
			return true;
		}

		std::vector<Chunk> chunks;
		if (!ReadChunks(bytes, size, chunks))
		{
			// Not a container: most likely GLSL source coming from glShaderSource.
			out.valid = false;
			return false;
		}

		out.valid = true;
		out.chunk_count = static_cast<uint32_t>(chunks.size());

		const Chunk *code = nullptr;
		for (const Chunk &chunk : chunks)
		{
			if (chunk.fourcc == kFourCC_DXIL || chunk.fourcc == kFourCC_ILDB)
			{
				code = &chunk;
				out.format = ShaderFormat::dxil;
				if (chunk.fourcc == kFourCC_ILDB)
					out.has_debug_info = true;
				break;
			}
			if ((chunk.fourcc == kFourCC_SHEX || chunk.fourcc == kFourCC_SHDR) && code == nullptr)
			{
				code = &chunk;
				out.format = ShaderFormat::dxbc;
			}
			if (chunk.fourcc == kFourCC_SDBG || chunk.fourcc == kFourCC_ILDN)
				out.has_debug_info = true;
		}

		if (code == nullptr || code->size < 4)
			return out.valid;

		out.code_chunk_offset = code->offset;
		out.code_chunk_size = code->size;

		const uint32_t version_token = ReadU32(bytes, code->offset);
		out.stage = StageFromProgramKind(version_token >> 16);
		out.shader_model = ((version_token >> 4) & 0xF) << 4 | (version_token & 0xF);
		return true;
	}

	Sha256Digest ComputeShaderSignature(const void *data, size_t size)
	{
		if (data == nullptr || size == 0)
			return Sha256Digest();

		const uint8_t *bytes = static_cast<const uint8_t *>(data);
		Sha256 sha;

		if (size >= kHeaderSize && ReadU32(bytes, 0) == kFourCC_DXBC)
		{
			// Hash the container with the checksum field replaced by zeros: the checksum is
			// derived from the rest, and different compilers write it differently.
			static const uint8_t kZeroChecksum[16] = {};
			sha.Update(bytes, kChecksumOffset);
			sha.Update(kZeroChecksum, sizeof(kZeroChecksum));
			sha.Update(bytes + kChecksumOffset + 16, size - kChecksumOffset - 16);
		}
		else
		{
			sha.Update(bytes, size);
		}
		return sha.Finish();
	}

	Sha256Digest ComputeShaderSemanticHash(const void *data, size_t size)
	{
		ShaderBlobInfo info;
		if (ParseShaderBlob(data, size, info) && info.code_chunk_size != 0)
		{
			const uint8_t *bytes = static_cast<const uint8_t *>(data);
			return Sha256::Hash(bytes + info.code_chunk_offset, info.code_chunk_size);
		}
		return ComputeShaderSignature(data, size);
	}

	const char *ShaderModelName(uint32_t shader_model)
	{
		if (shader_model == 0)
			return "?";

		static thread_local char buffer[8];
		std::snprintf(buffer, sizeof(buffer), "%u_%u", (shader_model >> 4) & 0xF, shader_model & 0xF);
		return buffer;
	}
}
