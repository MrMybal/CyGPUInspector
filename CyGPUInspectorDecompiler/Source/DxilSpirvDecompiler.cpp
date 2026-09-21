// CyGPUInspectorDecompiler — "dxil-spirv": the Shader Model 6 chain, DXIL -> SPIR-V -> HLSL.
//
// There is no direct DXIL decompiler worth having. What exists, and is used by every Direct3D 12
// translation layer on Linux, is dxil-spirv (MIT): it reads the LLVM bitcode a Shader Model 6
// shader is made of and emits SPIR-V. SPIRV-Cross (Apache-2.0 / MIT) then emits HLSL from that
// SPIR-V. Two hops, both battle tested, and nothing of our own in between.
//
// The detour costs something and the user has to be told: the intermediate SPIR-V is a Vulkan
// module, so bindings become descriptor sets and interpolants become numbered locations. Both are
// mapped back here — registers from the SPIR-V decorations, vertex semantics from the D3D
// reflection — but what the mapping cannot recover is said plainly in the notes.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Decompiler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include "SpirvToHlsl.hpp"

#include <dxil_spirv_c.h>
#include <spirv.hpp>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace cygi
{
	namespace
	{
		const char *DxilSpirvErrorName(dxil_spv_result result)
		{
			switch (result)
			{
			case DXIL_SPV_SUCCESS: return "success";
			case DXIL_SPV_ERROR_OUT_OF_MEMORY: return "out of memory";
			case DXIL_SPV_ERROR_UNSUPPORTED_FEATURE: return "the shader uses a feature dxil-spirv does not translate";
			case DXIL_SPV_ERROR_PARSER: return "the DXIL bitcode could not be parsed";
			case DXIL_SPV_ERROR_FAILED_VALIDATION: return "the generated SPIR-V failed validation";
			case DXIL_SPV_ERROR_INVALID_ARGUMENT: return "invalid argument";
			case DXIL_SPV_ERROR_NO_DATA: return "no data";
			default: return "generic failure";
			}
		}

		// dxil-spirv keeps a per thread allocator and a per thread log callback. Both have to be
		// torn down on every exit path, including the early returns below.
		class ThreadContext
		{
		public:
			ThreadContext() { dxil_spv_begin_thread_allocator_context(); }
			~ThreadContext()
			{
				dxil_spv_set_thread_log_callback(nullptr, nullptr);
				dxil_spv_end_thread_allocator_context();
			}
			ThreadContext(const ThreadContext &) = delete;
			ThreadContext &operator=(const ThreadContext &) = delete;
		};

		// Frees whatever dxil-spirv handed us, whichever way we leave the function.
		template <typename Handle, void (*Free)(Handle)>
		class Owned
		{
		public:
			~Owned()
			{
				if (m_handle != nullptr)
					Free(m_handle);
			}
			Owned() = default;
			Owned(const Owned &) = delete;
			Owned &operator=(const Owned &) = delete;

			Handle *Receive() { return &m_handle; }
			Handle Get() const { return m_handle; }

		private:
			Handle m_handle = nullptr;
		};

		using OwnedBlob = Owned<dxil_spv_parsed_blob, dxil_spv_parsed_blob_free>;
		using OwnedConverter = Owned<dxil_spv_converter, dxil_spv_converter_free>;

		void AppendLog(void *userdata, dxil_spv_log_level level, const char *message)
		{
			auto *messages = static_cast<std::string *>(userdata);
			if (messages == nullptr || message == nullptr)
				return;

			// Debug chatter would bury the two lines that matter.
			if (level == DXIL_SPV_LOG_LEVEL_DEBUG)
				return;

			if (!messages->empty())
				messages->push_back('\n');
			messages->append(level == DXIL_SPV_LOG_LEVEL_ERROR ? "error: " : "warning: ");
			messages->append(message);
		}

		const char *ProfilePrefix(ShaderStage stage)
		{
			switch (stage)
			{
			case ShaderStage::vertex: return "vs";
			case ShaderStage::hull: return "hs";
			case ShaderStage::domain: return "ds";
			case ShaderStage::geometry: return "gs";
			case ShaderStage::compute: return "cs";
			case ShaderStage::amplification: return "as";
			case ShaderStage::mesh: return "ms";
			default: return "ps";
			}
		}

		int ExecutionModelFor(dxil_spv_shader_stage stage)
		{
			switch (stage)
			{
			case DXIL_SPV_STAGE_VERTEX: return spv::ExecutionModelVertex;
			case DXIL_SPV_STAGE_HULL: return spv::ExecutionModelTessellationControl;
			case DXIL_SPV_STAGE_DOMAIN: return spv::ExecutionModelTessellationEvaluation;
			case DXIL_SPV_STAGE_GEOMETRY: return spv::ExecutionModelGeometry;
			case DXIL_SPV_STAGE_PIXEL: return spv::ExecutionModelFragment;
			case DXIL_SPV_STAGE_COMPUTE: return spv::ExecutionModelGLCompute;
			case DXIL_SPV_STAGE_AMPLIFICATION: return spv::ExecutionModelTaskEXT;
			case DXIL_SPV_STAGE_MESH: return spv::ExecutionModelMeshEXT;
			default: return spv::ExecutionModelMax;
			}
		}

		class DxilSpirvDecompiler final : public DecompilerBackend
		{
		public:
			const DecompilerInfo &Info() const override;
			bool Supports(ShaderFormat format) const override { return format == ShaderFormat::dxil; }
			DecompilationResult Decompile(const void *code, size_t size) const override;
		};

		const DecompilerInfo &DxilSpirvDecompiler::Info() const
		{
			static const DecompilerInfo kInfo = [] {
				unsigned major = 0;
				unsigned minor = 0;
				unsigned patch = 0;
				dxil_spv_get_version(&major, &minor, &patch);

				DecompilerInfo info;
				info.id = "dxil-spirv";
				info.name = "dxil-spirv + SPIRV-Cross";
				// SPIRV-Cross publishes no version macro, so its pinned commit is named instead;
				// ThirdParty/SPIRV-Cross/ORIGIN.md has the full hash.
				info.version = std::to_string(major) + "." + std::to_string(minor) + "." +
					std::to_string(patch) + "+spirv-cross-be71ee8c";
				info.license = "MIT (dxil-spirv, Hans-Kristian Arntzen) + Apache-2.0 / MIT "
				               "(SPIRV-Cross, Khronos), vendored, see ThirdParty/*/ORIGIN.md";
				info.summary =
					"Translates Shader Model 6 byte code to SPIR-V, then back to HLSL. The only "
					"reconstruction available for DXIL. It recovers resource names and the original "
					"registers, but the round trip through the Vulkan model renames interpolants and "
					"reshapes control flow, so the result reads like machine output.";
				return info;
			}();
			return kInfo;
		}

		DecompilationResult DxilSpirvDecompiler::Decompile(const void *code, size_t size) const
		{
			const auto start = std::chrono::steady_clock::now();

			DecompilationResult result;
			result.backend_id = Info().id;
			result.backend_version = Info().version;

			ShaderBlobInfo blob_info;
			ParseShaderBlob(code, size, blob_info);
			if (blob_info.format != ShaderFormat::dxil)
			{
				result.error = "this backend only handles DXIL (Shader Model 6)";
				return result;
			}

			ThreadContext context;
			std::string messages;
			dxil_spv_set_thread_log_callback(AppendLog, &messages);

			const auto fail = [&](const std::string &what) {
				result.error = messages.empty() ? what : what + ": " + messages;
				return result;
			};

			OwnedBlob blob;
			dxil_spv_result status = dxil_spv_parse_dxil_blob(code, size, blob.Receive());
			if (status != DXIL_SPV_SUCCESS)
				return fail(std::string("dxil-spirv could not read the container (") +
				            DxilSpirvErrorName(status) + ")");

			const dxil_spv_shader_stage stage = dxil_spv_parsed_blob_get_shader_stage(blob.Get());

			OwnedConverter converter;
			status = dxil_spv_create_converter(blob.Get(), converter.Receive());
			if (status != DXIL_SPV_SUCCESS)
				return fail(std::string("dxil-spirv could not create a converter (") +
				            DxilSpirvErrorName(status) + ")");

			// No remapping is installed on purpose: the default is the identity mapping that
			// RestoreRegisters() relies on to put the D3D registers back.
			status = dxil_spv_converter_run(converter.Get());
			if (status != DXIL_SPV_SUCCESS)
				return fail(std::string("dxil-spirv could not translate this shader (") +
				            DxilSpirvErrorName(status) + ")");

			dxil_spv_compiled_spirv spirv = {};
			status = dxil_spv_converter_get_compiled_spirv(converter.Get(), &spirv);
			if (status != DXIL_SPV_SUCCESS || spirv.data == nullptr || spirv.size < sizeof(uint32_t))
				return fail("dxil-spirv produced no SPIR-V");

			if (const char *warnings = dxil_spv_converter_get_analysis_warnings(converter.Get()))
			{
				if (warnings[0] != '\0')
				{
					if (!messages.empty())
						messages.push_back('\n');
					messages.append("warning: ").append(warnings);
				}
			}

			std::vector<uint32_t> words(spirv.size / sizeof(uint32_t));
			std::memcpy(words.data(), spirv.data, words.size() * sizeof(uint32_t));

			SpirvToHlslRequest request;
			request.words = &words;
			request.execution_model = ExecutionModelFor(stage);
			request.shader_model = ((blob_info.shader_model >> 4) & 0xF) * 10 + (blob_info.shader_model & 0xF);
			// Everything dxil-spirv dropped and the reflection tables still hold.
			const ReflectionResult reflection = Reflect(code, size);
			request.reflection = &reflection;
			request.restore_vertex_semantics = stage == DXIL_SPV_STAGE_VERTEX;

			const SpirvToHlslResult hlsl = SpirvToHlsl(request);
			if (!hlsl.ok)
				return fail(hlsl.error);
			result.hlsl = hlsl.hlsl;

			if (result.hlsl.empty())
				return fail("the chain produced no HLSL");

			std::string entry_point;
			if (const char *name = nullptr;
			    dxil_spv_parsed_blob_get_entry_point_demangled_name(blob.Get(), 0, &name) == DXIL_SPV_SUCCESS &&
			    name != nullptr)
				entry_point = name;

			result.suggested_profile = std::string(ProfilePrefix(blob_info.stage)) + "_" +
				std::to_string((blob_info.shader_model >> 4) & 0xF) + "_" +
				std::to_string(blob_info.shader_model & 0xF);
			result.ok = true;
			result.milliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

			result.notes =
				"Reconstruction obtained by translating the DXIL to SPIR-V with dxil-spirv, then "
				"back to HLSL with SPIRV-Cross. The intermediate SPIR-V is a Vulkan module and "
				"carries neither resource names nor Direct3D registers, so both were put back "
				"afterwards from this shader's own reflection tables. What the detour still costs: "
				"the members of a constant buffer are collapsed into one array, interpolant "
				"semantics become TEXCOORD<n>, control flow is the one the structurizer rebuilt "
				"rather than the one that was written, and temporaries are numbered. Local variable "
				"names, structures and algorithms were destroyed by the original compiler and "
				"cannot be recovered by any backend.";
			if (!entry_point.empty() && entry_point != "main")
				result.notes += " The entry point was named `" + entry_point +
					"`; it is emitted as `main` so that the reconstruction can be validated by "
					"recompiling it.";
			if (!messages.empty())
				result.notes += " The chain reported: " + messages;

			return result;
		}
	}

	std::unique_ptr<DecompilerBackend> MakeDxilSpirvDecompiler()
	{
		return std::make_unique<DxilSpirvDecompiler>();
	}
}
