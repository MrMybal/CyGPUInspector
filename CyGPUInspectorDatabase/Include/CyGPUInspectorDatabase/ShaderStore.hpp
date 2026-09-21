// CyGPUInspectorDatabase — persistent store of everything derived from a shader.
//
// Keyed by signature, so a shader met again in another run, another build or another game is
// recognised and its expensive work (disassembly, decompilation, AI analysis) is never redone.
// Metadata will move to SQLite later; the blobs stay on disk, which is what they are good at.
//
//   Database/shaders/<aa>/<signature>/metadata.json
//                                    /original.dxbc | original.dxil
//                                    /disassembly.txt
//                                    /decompiled/<backend>-<version>.hlsl
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cygi
{
	class ShaderStore
	{
	public:
		// Defaults to "Database" next to the executable.
		explicit ShaderStore(std::filesystem::path root = {});

		const std::filesystem::path &Root() const { return m_root; }
		bool IsUsable() const { return m_usable; }
		const std::string &LastError() const { return m_last_error; }

		// Database/shaders/<aa>/<signature>/
		std::filesystem::path DirectoryFor(const Sha256Digest &signature) const;

		bool SaveByteCode(const Sha256Digest &signature, ShaderFormat format, const void *code, size_t size);
		bool SaveText(const Sha256Digest &signature, const std::string &relative_name, const std::string &text);
		bool LoadText(const Sha256Digest &signature, const std::string &relative_name, std::string &out) const;
		bool Has(const Sha256Digest &signature, const std::string &relative_name) const;

		// Writes metadata.json. Kept deliberately small and human readable.
		bool SaveMetadata(const Sha256Digest &signature, const Sha256Digest &semantic_hash, ShaderStage stage,
		                  ShaderFormat format, uint32_t shader_model, uint32_t code_size,
		                  const std::string &process_name);

	private:
		bool EnsureDirectory(const std::filesystem::path &path) const;

		std::filesystem::path m_root;
		bool m_usable = false;
		mutable std::string m_last_error;
	};
}
