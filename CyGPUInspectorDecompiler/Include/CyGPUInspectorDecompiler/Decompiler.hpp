// CyGPUInspectorDecompiler — decompilation backends.
//
// There is no single best decompiler, so there is no single result. Every backend that can handle
// a shader runs on it, and every output is kept side by side with its own validation verdict. The
// user (or an AI) compares them and picks; nothing is silently overwritten by a "better" answer.
//
// What is produced is never the original source: that does not exist in the binary. Every result
// is labelled as a reconstruction, with the backend and version that made it.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Disassembler.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <memory>
#include <string>
#include <vector>

namespace cygi
{
	struct DecompilerInfo
	{
		std::string id;        // stable, used as the file name in the store: "cygi-dxbc"
		std::string name;
		std::string version;
		std::string license;
		std::string summary;   // what it does well, and what it does not
	};

	// How far a reconstruction was checked. Only `equivalent_disassembly` means "faithful".
	enum class ValidationVerdict : uint32_t
	{
		not_run = 0,
		compile_failed,
		compiles,
		equivalent_disassembly,
	};
	const char *ValidationVerdictName(ValidationVerdict verdict);

	struct DecompilationResult
	{
		bool ok = false;
		std::string backend_id;
		std::string backend_version;
		std::string hlsl;
		std::string error;
		std::string notes;              // what the backend could not translate, in its own words
		uint32_t untranslated_lines = 0;
		std::string suggested_profile;  // "ps_5_0", used to validate and to recompile
		double milliseconds = 0.0;

		ValidationVerdict verdict = ValidationVerdict::not_run;
		std::string validation_messages;

		// "cygi-dxbc-0.1.hlsl"
		std::string ArtifactName() const;
	};

	class DecompilerBackend
	{
	public:
		virtual ~DecompilerBackend() = default;

		virtual const DecompilerInfo &Info() const = 0;
		virtual bool Supports(ShaderFormat format) const = 0;
		virtual DecompilationResult Decompile(const void *code, size_t size) const = 0;
	};

	class DecompilerRegistry
	{
	public:
		static DecompilerRegistry &Instance();

		std::vector<const DecompilerBackend *> All() const;
		std::vector<const DecompilerBackend *> BackendsFor(ShaderFormat format) const;
		const DecompilerBackend *Find(const std::string &id) const;

		// Runs every backend that supports this shader and validates each result.
		std::vector<DecompilationResult> DecompileAll(const void *code, size_t size,
		                                              bool validate = true) const;

		// Lets a build or a plugin add a backend (3Dmigoto, dxil-spirv, ...).
		void Register(std::unique_ptr<DecompilerBackend> backend);

	private:
		DecompilerRegistry();

		std::vector<std::unique_ptr<DecompilerBackend>> m_backends;
	};

	// Recompiles the reconstruction and compares it with the original, filling `verdict` and
	// `validation_messages`. Safe to call on a failed result: it stays `compile_failed`.
	void ValidateDecompilation(DecompilationResult &result, const void *original_code,
	                           size_t original_size);
}
