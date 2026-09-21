// CyGPUInspectorDecompiler — backend registry and validation.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Decompiler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace cygi
{
	// Defined by the backends themselves.
	std::unique_ptr<DecompilerBackend> MakeDxbcTextDecompiler();
	std::unique_ptr<DecompilerBackend> MakeMigotoDecompiler();
	std::unique_ptr<DecompilerBackend> MakeDxilSpirvDecompiler();
	std::unique_ptr<DecompilerBackend> MakeDxbcSpirvDecompiler();

	const char *ValidationVerdictName(ValidationVerdict verdict)
	{
		switch (verdict)
		{
		case ValidationVerdict::compile_failed: return "does not compile";
		case ValidationVerdict::compiles: return "compiles";
		case ValidationVerdict::equivalent_disassembly: return "compiles, equivalent disassembly";
		case ValidationVerdict::not_run:
		default: return "not validated";
		}
	}

	std::string DecompilationResult::ArtifactName() const
	{
		return "decompiled/" + backend_id + "-" + backend_version + ".hlsl";
	}

	DecompilerRegistry::DecompilerRegistry()
	{
		// Order matters only for display: every backend that supports a shader runs on it, and
		// their results are kept side by side, never merged.
		m_backends.push_back(MakeMigotoDecompiler());
		m_backends.push_back(MakeDxbcTextDecompiler());
		m_backends.push_back(MakeDxbcSpirvDecompiler());
		m_backends.push_back(MakeDxilSpirvDecompiler());
	}

	DecompilerRegistry &DecompilerRegistry::Instance()
	{
		static DecompilerRegistry registry;
		return registry;
	}

	void DecompilerRegistry::Register(std::unique_ptr<DecompilerBackend> backend)
	{
		if (backend != nullptr)
			m_backends.push_back(std::move(backend));
	}

	std::vector<const DecompilerBackend *> DecompilerRegistry::All() const
	{
		std::vector<const DecompilerBackend *> result;
		result.reserve(m_backends.size());
		for (const std::unique_ptr<DecompilerBackend> &backend : m_backends)
			result.push_back(backend.get());
		return result;
	}

	std::vector<const DecompilerBackend *> DecompilerRegistry::BackendsFor(ShaderFormat format) const
	{
		std::vector<const DecompilerBackend *> result;
		for (const std::unique_ptr<DecompilerBackend> &backend : m_backends)
			if (backend->Supports(format))
				result.push_back(backend.get());
		return result;
	}

	const DecompilerBackend *DecompilerRegistry::Find(const std::string &id) const
	{
		for (const std::unique_ptr<DecompilerBackend> &backend : m_backends)
			if (backend->Info().id == id)
				return backend.get();
		return nullptr;
	}

	std::vector<DecompilationResult> DecompilerRegistry::DecompileAll(const void *code, size_t size,
	                                                                 bool validate) const
	{
		std::vector<DecompilationResult> results;

		ShaderBlobInfo info;
		if (!ParseShaderBlob(code, size, info))
			return results;

		for (const DecompilerBackend *backend : BackendsFor(info.format))
		{
			DecompilationResult result = backend->Decompile(code, size);
			if (validate)
				ValidateDecompilation(result, code, size);
			results.push_back(std::move(result));
		}
		return results;
	}

	namespace
	{
		// Normalises a disassembly so two of them can be compared on what they do rather than on
		// how they are printed: comments, numbering and whitespace go away.
		std::vector<std::string> NormalizeDisassembly(const std::string &text)
		{
			std::vector<std::string> opcodes;
			std::istringstream lines(text);
			std::string line;

			while (std::getline(lines, line))
			{
				size_t begin = 0;
				while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin])))
					++begin;
				if (begin >= line.size() || line.compare(begin, 2, "//") == 0)
					continue;

				// Drop the instruction number prefix.
				const size_t colon = line.find(':', begin);
				if (colon != std::string::npos && colon - begin < 8)
				{
					bool numeric = colon > begin;
					for (size_t i = begin; i < colon; ++i)
						if (!std::isdigit(static_cast<unsigned char>(line[i])))
							numeric = false;
					if (numeric)
					{
						begin = colon + 1;
						while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin])))
							++begin;
					}
				}
				if (begin >= line.size())
					continue;

				size_t end = line.find(' ', begin);
				if (end == std::string::npos)
					end = line.size();

				std::string opcode = line.substr(begin, end - begin);
				const size_t parenthesis = opcode.find('(');
				if (parenthesis != std::string::npos)
					opcode = opcode.substr(0, parenthesis);
				if (opcode.compare(0, 4, "dcl_") == 0)
					continue;   // declarations differ by construction, they come from reflection

				opcodes.push_back(opcode);
			}
			return opcodes;
		}

		double OpcodeSimilarity(const std::vector<std::string> &a, const std::vector<std::string> &b)
		{
			if (a.empty() && b.empty())
				return 1.0;
			if (a.empty() || b.empty())
				return 0.0;

			// Compare the multiset of opcodes: instruction scheduling may legitimately differ.
			std::vector<std::string> sorted_a = a;
			std::vector<std::string> sorted_b = b;
			std::sort(sorted_a.begin(), sorted_a.end());
			std::sort(sorted_b.begin(), sorted_b.end());

			size_t common = 0;
			size_t i = 0;
			size_t j = 0;
			while (i < sorted_a.size() && j < sorted_b.size())
			{
				if (sorted_a[i] == sorted_b[j]) { ++common; ++i; ++j; }
				else if (sorted_a[i] < sorted_b[j]) ++i;
				else ++j;
			}

			const size_t largest = (std::max)(sorted_a.size(), sorted_b.size());
			return static_cast<double>(common) / static_cast<double>(largest);
		}
	}

	void ValidateDecompilation(DecompilationResult &result, const void *original_code, size_t original_size)
	{
		if (!result.ok || result.hlsl.empty())
		{
			result.verdict = ValidationVerdict::compile_failed;
			result.validation_messages = result.error.empty() ? "nothing was produced" : result.error;
			return;
		}

		std::string profile = result.suggested_profile;
		if (profile.empty())
		{
			ShaderBlobInfo info;
			ParseShaderBlob(original_code, original_size, info);

			const char *prefix = "ps";
			switch (info.stage)
			{
			case ShaderStage::vertex: prefix = "vs"; break;
			case ShaderStage::geometry: prefix = "gs"; break;
			case ShaderStage::hull: prefix = "hs"; break;
			case ShaderStage::domain: prefix = "ds"; break;
			case ShaderStage::compute: prefix = "cs"; break;
			default: break;
			}
			const uint32_t major = (info.shader_model >> 4) & 0xF;
			const uint32_t minor = info.shader_model & 0xF;
			profile = std::string(prefix) + "_" + std::to_string(major != 0 ? major : 5) + "_" +
				std::to_string(major != 0 ? minor : 0);
		}

		const CompileResult recompiled = CompileHlsl(result.hlsl, "main", profile, "reconstructed.hlsl");
		if (!recompiled.ok)
		{
			result.verdict = ValidationVerdict::compile_failed;
			result.validation_messages = recompiled.messages.empty() ? recompiled.error : recompiled.messages;
			return;
		}

		result.verdict = ValidationVerdict::compiles;
		result.validation_messages = "recompiled as " + profile;

		// Same instructions, whatever their order? That is as close to "faithful" as a
		// reconstruction can honestly claim to be.
		const DisassemblyResult original = Disassemble(original_code, original_size);
		const DisassemblyResult rebuilt = Disassemble(recompiled.byte_code.data(), recompiled.byte_code.size());
		if (!original.ok || !rebuilt.ok)
			return;

		const double similarity = OpcodeSimilarity(NormalizeDisassembly(original.text),
			NormalizeDisassembly(rebuilt.text));

		char buffer[128];
		std::snprintf(buffer, sizeof(buffer), "recompiled as %s, opcode similarity %.0f%%",
			profile.c_str(), similarity * 100.0);
		result.validation_messages = buffer;

		if (similarity >= 0.9)
			result.verdict = ValidationVerdict::equivalent_disassembly;
	}
}
