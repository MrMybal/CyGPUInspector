// CyGPUInspectorDecompiler — "hlsldecompiler": the 3Dmigoto DXBC decompiler.
//
// This backend is a thin adapter. All the work is done by the vendored 3Dmigoto sources in
// ThirdParty/hlsldecompiler, which are GPL-3 and unmodified; see ORIGIN.md there for what was
// taken, from which commit, and how the licence obligations are met.
//
// Unlike our own register level backend, this one reconstructs expressions, recovers resource and
// constant buffer names from the reflection tables, and rebuilds the control flow. It is the
// reason a reconstruction can be read rather than merely compiled.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Decompiler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include "DecompileHLSL.h"
#include "log.h"

#include <chrono>
#include <mutex>

namespace cygi
{
	namespace
	{
		class MigotoDecompiler final : public DecompilerBackend
		{
		public:
			const DecompilerInfo &Info() const override
			{
				static const DecompilerInfo kInfo = {
					"hlsldecompiler",
					"3Dmigoto HLSL decompiler",
					"1.4.1",
					"GPL-3.0 (bo3b/3Dmigoto, vendored, see ThirdParty/hlsldecompiler/ORIGIN.md)",
					"Reconstructs expressions, recovers resource and constant buffer names from the "
					"reflection tables and rebuilds control flow. The most readable reconstruction "
					"available for Shader Model 4 and 5. It still recovers no local variable names "
					"and no algorithms: those were destroyed by the compiler.",
				};
				return kInfo;
			}

			bool Supports(ShaderFormat format) const override { return format == ShaderFormat::dxbc; }

			DecompilationResult Decompile(const void *code, size_t size) const override;
		};

		DecompilationResult MigotoDecompiler::Decompile(const void *code, size_t size) const
		{
			const auto start = std::chrono::steady_clock::now();

			DecompilationResult result;
			result.backend_id = Info().id;
			result.backend_version = Info().version;

			// The decompiler works from the assembly text plus the original container: it reads the
			// instructions from the text and the reflection tables from the binary. The text must
			// be the plain form: instruction numbers and byte offsets make its parser fail with
			// "No opcode".
			const DisassemblyResult disassembly = Disassemble(code, size, DisassemblyStyle::plain);
			if (!disassembly.ok)
			{
				result.error = "disassembly failed: " + disassembly.error;
				return result;
			}

			ShaderBlobInfo blob;
			ParseShaderBlob(code, size, blob);
			if (blob.format != ShaderFormat::dxbc)
			{
				result.error = "this backend only handles DXBC (Shader Model 4 and 5)";
				return result;
			}

			DecompilerSettings settings;
			// Every 3Dmigoto specific patch is off: CyGPUInspector reconstructs a shader, it does
			// not fix stereoscopic rendering.
			settings.StereoParamsReg = -1;
			settings.IniParamsReg = -1;
			settings.fixSvPosition = false;
			settings.recompileVs = false;

			ParseParameters parameters = {};
			parameters.bytecode = code;
			parameters.decompiled = disassembly.text.c_str();
			parameters.decompiledSize = disassembly.text.size();
			parameters.ZeroOutput = false;
			parameters.G = &settings;

			bool patched = false;
			bool failed = false;
			std::string shader_model;
			std::string hlsl;
			std::string messages;

			{
				// The vendored code keeps global state (its log capture, and a few statics inside
				// the parser), so one shader is decompiled at a time.
				static std::mutex mutex;
				std::lock_guard<std::mutex> lock(mutex);

				hlsldecompiler::BeginCapture();
				hlsl = DecompileBinaryHLSL(parameters, patched, shader_model, failed);
				messages = hlsldecompiler::EndCapture();
			}

			if (failed || hlsl.empty())
			{
				result.error = messages.empty()
					? "the 3Dmigoto decompiler could not reconstruct this shader"
					: "the 3Dmigoto decompiler failed: " + messages;
				return result;
			}

			result.hlsl = std::move(hlsl);

			// The decompiler reports the profile it recognised; fall back on the container.
			result.suggested_profile = shader_model;
			if (result.suggested_profile.empty() && blob.shader_model != 0)
			{
				const char *prefix = "ps";
				switch (blob.stage)
				{
				case ShaderStage::vertex: prefix = "vs"; break;
				case ShaderStage::geometry: prefix = "gs"; break;
				case ShaderStage::hull: prefix = "hs"; break;
				case ShaderStage::domain: prefix = "ds"; break;
				case ShaderStage::compute: prefix = "cs"; break;
				default: break;
				}
				result.suggested_profile = std::string(prefix) + "_" +
					std::to_string((blob.shader_model >> 4) & 0xF) + "_" +
					std::to_string(blob.shader_model & 0xF);
			}
			result.ok = true;
			result.milliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

			result.notes = "Reconstruction by the 3Dmigoto decompiler (GPL-3), vendored unmodified. "
			               "It rebuilds expressions and recovers the names of resources and constant "
			               "buffers, but local variable names, structures and algorithms were "
			               "destroyed by the original compiler and cannot be recovered.";
			if (!messages.empty())
				result.notes += " The decompiler reported: " + messages;

			return result;
		}
	}

	std::unique_ptr<DecompilerBackend> MakeMigotoDecompiler()
	{
		return std::make_unique<MigotoDecompiler>();
	}
}
