// CyGPUInspectorDecompiler — "dxbc-spirv": a third, independent reconstruction of Shader Model 4
// and 5, through SPIR-V.
//
// Why a third DXBC backend at all: the whole point of the multi-backend design is that two
// reconstructions which disagree tell you something. 3Dmigoto is a 2014 pattern matcher over
// assembly text; `cygi-dxbc` is our own line by line translation; this one comes from an entirely
// different lineage — dxbc-spirv (MIT, Philip Rebohle), the DXBC front end being built for DXVK.
// It parses the byte code, lowers it into an SSA intermediate representation, runs real compiler
// passes over it, and emits SPIR-V. SPIRV-Cross then emits HLSL from that.
//
// This is the backend the brief asked for under the name `vkd3d-shader`. That library cannot be
// built with this toolchain — see Docs/ShaderDecompiler.md for the list of code generators it
// needs — and dxbc-spirv covers the same ground from the same world with none of that cost: it is
// already vendored, because dxil-spirv itself depends on it.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Decompiler.hpp"

#include "SpirvToHlsl.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <dxbc/dxbc_api.h>
#include <spirv/spirv_builder.h>
#include <util/util_log.h>

#include <spirv.hpp>

#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace cygi
{
	namespace
	{
		// dxbc-spirv talks to a thread-local logger. Without one it writes to stderr, which is
		// useless inside a windowed application, so its diagnostics are collected and attached to
		// the result where the user will actually read them.
		class CapturingLogger final : public dxbc_spv::util::Logger
		{
		public:
			void message(dxbc_spv::util::LogLevel severity, const char *text) override
			{
				if (text == nullptr || severity < dxbc_spv::util::LogLevel::eWarn)
					return;
				if (!m_messages.empty())
					m_messages.push_back('\n');
				m_messages.append(severity == dxbc_spv::util::LogLevel::eError ? "error: " : "warning: ");
				m_messages.append(text);
			}

			dxbc_spv::util::LogLevel getMinimumSeverity() override
			{
				return dxbc_spv::util::LogLevel::eWarn;
			}

			const std::string &Messages() const { return m_messages; }

		private:
			std::string m_messages;
		};

		// The identity mapping: descriptor set = register space, binding = register index. It is
		// what lets the Direct3D registers be read back out of the SPIR-V afterwards. The default
		// mapping dxbc-spirv ships numbers descriptors sequentially, which would lose them.
		class IdentityMapping final : public dxbc_spv::spirv::ResourceMapping
		{
		public:
			dxbc_spv::spirv::DescriptorBinding mapDescriptor(dxbc_spv::ir::ScalarType /*type*/,
			                                                 uint32_t space, uint32_t index) override
			{
				return { space, index };
			}

			uint32_t mapPushData(dxbc_spv::ir::ShaderStageMask /*stages*/) override { return 0; }
		};

		// dxbc-spirv always declares the `PhysicalStorageBuffer64` addressing model and the Vulkan
		// memory model, because it targets Vulkan. SPIRV-Cross refuses to emit HLSL from anything
		// but the `Logical` model, so the two do not meet without help.
		//
		// The help is this: the declarations are rewritten to the logical model. That is only
		// sound as long as the shader does not actually use a physical pointer, so the module is
		// checked for one first and the backend gives up loudly if it finds any. Rewriting a
		// module that really needs physical addressing would produce a reconstruction that is
		// quietly wrong, which is worse than no reconstruction at all.
		bool MakeLogicalAddressing(std::vector<uint32_t> &words, std::string &error)
		{
			constexpr size_t kHeaderWords = 5;
			if (words.size() < kHeaderWords || words[0] != spv::MagicNumber)
			{
				error = "dxbc-spirv produced something that is not a SPIR-V module";
				return false;
			}

			std::vector<uint32_t> out(words.begin(), words.begin() + kHeaderWords);

			for (size_t i = kHeaderWords; i < words.size();)
			{
				const uint32_t length = words[i] >> 16;
				const auto opcode = static_cast<spv::Op>(words[i] & 0xFFFFu);
				if (length == 0 || i + length > words.size())
				{
					error = "the SPIR-V module is truncated";
					return false;
				}

				const uint32_t *operands = &words[i] + 1;
				const uint32_t operand_count = length - 1;

				// Does anything really use a physical pointer? If so, stop here.
				switch (opcode)
				{
				case spv::OpTypePointer:
					if (operand_count >= 2 &&
					    operands[1] == static_cast<uint32_t>(spv::StorageClassPhysicalStorageBuffer))
					{
						error = "this shader uses buffer device addresses, which have no HLSL "
						        "equivalent; the 3Dmigoto and cygi-dxbc reconstructions still apply";
						return false;
					}
					break;
				case spv::OpTypeForwardPointer:
					if (operand_count >= 2 &&
					    operands[1] == static_cast<uint32_t>(spv::StorageClassPhysicalStorageBuffer))
					{
						error = "this shader uses buffer device addresses, which have no HLSL equivalent";
						return false;
					}
					break;
				case spv::OpConvertUToPtr:
				case spv::OpConvertPtrToU:
					error = "this shader converts between integers and pointers, which HLSL cannot express";
					return false;
				default:
					break;
				}

				// Drop what only exists to support those two models.
				bool keep = true;
				if (opcode == spv::OpCapability && operand_count >= 1)
				{
					const auto capability = static_cast<spv::Capability>(operands[0]);
					keep = capability != spv::CapabilityPhysicalStorageBufferAddresses &&
						capability != spv::CapabilityVulkanMemoryModel &&
						capability != spv::CapabilityVulkanMemoryModelDeviceScope;
				}
				else if (opcode == spv::OpExtension)
				{
					const char *name = reinterpret_cast<const char *>(operands);
					const size_t bytes = static_cast<size_t>(operand_count) * sizeof(uint32_t);
					const std::string extension(name, strnlen(name, bytes));
					keep = extension != "SPV_KHR_physical_storage_buffer" &&
						extension != "SPV_KHR_vulkan_memory_model";
				}

				if (keep)
				{
					out.insert(out.end(), &words[i], &words[i] + length);
					if (opcode == spv::OpMemoryModel && operand_count >= 2)
					{
						uint32_t *written = &out[out.size() - length] + 1;
						written[0] = static_cast<uint32_t>(spv::AddressingModelLogical);
						written[1] = static_cast<uint32_t>(spv::MemoryModelGLSL450);
					}
				}

				i += length;
			}

			words = std::move(out);
			return true;
		}

		int ExecutionModelFor(ShaderStage stage)
		{
			switch (stage)
			{
			case ShaderStage::vertex: return spv::ExecutionModelVertex;
			case ShaderStage::hull: return spv::ExecutionModelTessellationControl;
			case ShaderStage::domain: return spv::ExecutionModelTessellationEvaluation;
			case ShaderStage::geometry: return spv::ExecutionModelGeometry;
			case ShaderStage::compute: return spv::ExecutionModelGLCompute;
			case ShaderStage::pixel: return spv::ExecutionModelFragment;
			default: return spv::ExecutionModelMax;
			}
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
			default: return "ps";
			}
		}

		class DxbcSpirvDecompiler final : public DecompilerBackend
		{
		public:
			const DecompilerInfo &Info() const override
			{
				static const DecompilerInfo kInfo = {
					"dxbc-spirv",
					"dxbc-spirv + SPIRV-Cross",
					"0.1+spirv-cross-be71ee8c",
					"MIT (dxbc-spirv, Philip Rebohle) + Apache-2.0 / MIT (SPIRV-Cross, Khronos), "
					"vendored, see ThirdParty/dxil-spirv/ORIGIN.md",
					"Lowers Shader Model 4 and 5 byte code into an SSA intermediate representation, "
					"optimises it, and emits SPIR-V, which SPIRV-Cross turns back into HLSL. Its "
					"value is that it agrees or disagrees with 3Dmigoto from a completely different "
					"lineage: two backends that reconstruct the same shader differently are telling "
					"you where the reconstruction is uncertain.",
				};
				return kInfo;
			}

			bool Supports(ShaderFormat format) const override { return format == ShaderFormat::dxbc; }

			DecompilationResult Decompile(const void *code, size_t size) const override;
		};

		DecompilationResult DxbcSpirvDecompiler::Decompile(const void *code, size_t size) const
		{
			const auto start = std::chrono::steady_clock::now();

			DecompilationResult result;
			result.backend_id = Info().id;
			result.backend_version = Info().version;

			ShaderBlobInfo blob;
			ParseShaderBlob(code, size, blob);
			if (blob.format != ShaderFormat::dxbc)
			{
				result.error = "this backend only handles DXBC (Shader Model 4 and 5)";
				return result;
			}

			// The logger installs itself on this thread and removes itself on destruction.
			CapturingLogger logger;

			const auto fail = [&](const std::string &what) {
				result.error = logger.Messages().empty() ? what : what + ": " + logger.Messages();
				return result;
			};

			dxbc_spv::dxbc::Converter::Options convert_options = {};
			// The whole reason this backend is readable: it keeps the resource names and the I/O
			// semantics from the signature chunks, which the SPIR-V then carries.
			convert_options.includeDebugNames = true;
			convert_options.boundCheckShaderIo = true;
			convert_options.lowerIcb = false;

			dxbc_spv::ir::CompileOptions compile_options = {};

			std::vector<uint32_t> words;
			try
			{
				std::optional<dxbc_spv::ir::Builder> ir =
					dxbc_spv::dxbc::compileShaderToLegalizedIr(code, size, convert_options, compile_options);
				if (!ir.has_value())
					return fail("dxbc-spirv could not lower this shader into its intermediate representation");

				IdentityMapping mapping;
				dxbc_spv::spirv::SpirvBuilder::Options spirv_options = {};
				spirv_options.includeDebugNames = true;

				dxbc_spv::spirv::SpirvBuilder builder(*ir, mapping, spirv_options);
				builder.buildSpirvBinary();
				words = builder.getSpirvBinary();
			}
			catch (const std::exception &error)
			{
				return fail(std::string("dxbc-spirv failed: ") + error.what());
			}

			if (words.empty())
				return fail("dxbc-spirv produced no SPIR-V");

			std::string rewrite_error;
			if (!MakeLogicalAddressing(words, rewrite_error))
				return fail(rewrite_error);

			SpirvToHlslRequest request;
			request.words = &words;
			request.execution_model = ExecutionModelFor(blob.stage);
			request.shader_model = ((blob.shader_model >> 4) & 0xF) * 10 + (blob.shader_model & 0xF);
			const ReflectionResult reflection = Reflect(code, size);
			request.reflection = &reflection;
			request.restore_vertex_semantics = blob.stage == ShaderStage::vertex;

			const SpirvToHlslResult hlsl = SpirvToHlsl(request);
			if (!hlsl.ok)
				return fail(hlsl.error);

			result.hlsl = hlsl.hlsl;
			result.suggested_profile = std::string(ProfilePrefix(blob.stage)) + "_" +
				std::to_string((blob.shader_model >> 4) & 0xF) + "_" +
				std::to_string(blob.shader_model & 0xF);
			result.ok = true;
			result.milliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

			result.notes =
				"Reconstruction obtained by lowering the byte code into the SSA intermediate "
				"representation of dxbc-spirv, emitting SPIR-V from it, and emitting HLSL from "
				"that with SPIRV-Cross. It is a compiler, not a text translator, so the result is "
				"the shader as an optimiser sees it: expressions are rebuilt and control flow is "
				"restructured, but instructions may be reordered or folded away, and the "
				"temporaries are numbered. Compare it with the 3Dmigoto reconstruction rather than "
				"choosing blindly — where the two disagree is where a reconstruction is uncertain. "
				"The members of a constant buffer are collapsed into one array, which is how a "
				"Vulkan uniform block is shaped, and the intermediate module was rewritten from "
				"the Vulkan memory model to the logical one so that HLSL could be emitted at all.";
			if (!logger.Messages().empty())
				result.notes += " The chain reported: " + logger.Messages();

			return result;
		}
	}

	std::unique_ptr<DecompilerBackend> MakeDxbcSpirvDecompiler()
	{
		return std::make_unique<DxbcSpirvDecompiler>();
	}
}
