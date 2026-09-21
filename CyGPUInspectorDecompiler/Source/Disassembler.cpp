// CyGPUInspectorDecompiler — shader disassembly and reflection.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Disassembler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11shader.h>
#include <d3d12shader.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

#include <chrono>
#include <cstdio>
#include <mutex>

namespace cygi
{
	namespace
	{
		using PFN_Disassemble = HRESULT(WINAPI *)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob **);
		using PFN_Reflect = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void **);

		std::string ModulePath(HMODULE module)
		{
			char path[MAX_PATH] = {};
			if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
				return {};
			return path;
		}

		// dxcompiler.dll is not a system DLL: look next to us first, then in the Windows SDK.
		HMODULE LoadDxCompiler(std::string &where, std::string &error)
		{
			if (const HMODULE already = GetModuleHandleA("dxcompiler.dll"))
			{
				where = ModulePath(already);
				return already;
			}

			// 1. Next to the executable: what a packaged build ships.
			char exe_path[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
			std::string directory(exe_path);
			const size_t slash = directory.find_last_of("\\/");
			if (slash != std::string::npos)
			{
				const std::string local = directory.substr(0, slash + 1) + "dxcompiler.dll";
				if (const HMODULE module = LoadLibraryA(local.c_str()))
				{
					where = local;
					return module;
				}
			}

			// 2. The Windows SDK, newest version first.
			static const char *const kRoots[] = {
				"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\",
				"C:\\Program Files\\Windows Kits\\10\\bin\\",
			};
			static const char *const kVersions[] = {
				"10.0.26100.0", "10.0.22621.0", "10.0.22000.0", "10.0.20348.0", "10.0.19041.0",
			};
			for (const char *root : kRoots)
			{
				for (const char *version : kVersions)
				{
					const std::string candidate = std::string(root) + version + "\\x64\\dxcompiler.dll";
					if (const HMODULE module = LoadLibraryA(candidate.c_str()))
					{
						where = candidate;
						return module;
					}
				}
			}

			// 3. Whatever the PATH offers.
			if (const HMODULE module = LoadLibraryA("dxcompiler.dll"))
			{
				where = ModulePath(module);
				return module;
			}

			error = "dxcompiler.dll was not found next to the application, in the Windows SDK, or on "
			        "the PATH. Shader Model 6 shaders cannot be disassembled without it.";
			return nullptr;
		}

		// Owns the two DLLs for the whole process, loaded once, on first use.
		class ToolHost
		{
		public:
			static ToolHost &Instance()
			{
				static ToolHost host;
				return host;
			}

			const ToolInfo &Dxbc()
			{
				std::call_once(m_dxbc_once, [this]() { LoadDxbc(); });
				return m_dxbc;
			}

			const ToolInfo &Dxil()
			{
				std::call_once(m_dxil_once, [this]() { LoadDxil(); });
				return m_dxil;
			}

			PFN_Disassemble DisassembleProc() { Dxbc(); return m_disassemble; }
			PFN_Reflect ReflectProc() { Dxbc(); return m_reflect; }
			DxcCreateInstanceProc DxcCreate() { Dxil(); return m_dxc_create; }

		private:
			void LoadDxbc()
			{
				m_dxbc.name = "d3dcompiler_47.dll";

				const HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
				if (module == nullptr)
				{
					m_dxbc.error = "d3dcompiler_47.dll could not be loaded";
					return;
				}

				m_disassemble = reinterpret_cast<PFN_Disassemble>(
					reinterpret_cast<void *>(GetProcAddress(module, "D3DDisassemble")));
				m_reflect = reinterpret_cast<PFN_Reflect>(
					reinterpret_cast<void *>(GetProcAddress(module, "D3DReflect")));

				if (m_disassemble == nullptr)
				{
					m_dxbc.error = "D3DDisassemble is missing from d3dcompiler_47.dll";
					return;
				}

				m_dxbc.path = ModulePath(module);
				m_dxbc.available = true;
			}

			void LoadDxil()
			{
				m_dxil.name = "dxcompiler.dll";

				std::string where;
				std::string error;
				const HMODULE module = LoadDxCompiler(where, error);
				if (module == nullptr)
				{
					m_dxil.error = error;
					return;
				}

				m_dxc_create = reinterpret_cast<DxcCreateInstanceProc>(
					reinterpret_cast<void *>(GetProcAddress(module, "DxcCreateInstance")));
				if (m_dxc_create == nullptr)
				{
					m_dxil.error = "DxcCreateInstance is missing from " + where;
					return;
				}

				m_dxil.path = where;
				m_dxil.available = true;
			}

			ToolInfo m_dxbc;
			ToolInfo m_dxil;
			std::once_flag m_dxbc_once;
			std::once_flag m_dxil_once;
			PFN_Disassemble m_disassemble = nullptr;
			PFN_Reflect m_reflect = nullptr;
			DxcCreateInstanceProc m_dxc_create = nullptr;
		};

		double ElapsedMilliseconds(std::chrono::steady_clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		}

		std::string HResultText(const char *what, HRESULT hr)
		{
			char buffer[160];
			std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
			return buffer;
		}
	}

	const ToolInfo &DxbcToolInfo() { return ToolHost::Instance().Dxbc(); }
	const ToolInfo &DxilToolInfo() { return ToolHost::Instance().Dxil(); }

	size_t DisassemblyResult::LineCount() const
	{
		if (text.empty())
			return 0;
		size_t lines = 1;
		for (char c : text)
			if (c == '\n')
				++lines;
		return lines;
	}

	const char *BindingKindName(BindingKind kind)
	{
		switch (kind)
		{
		case BindingKind::constant_buffer: return "Constant buffer";
		case BindingKind::texture_buffer: return "Texture buffer";
		case BindingKind::texture: return "Texture";
		case BindingKind::sampler: return "Sampler";
		case BindingKind::unordered_access: return "UAV";
		case BindingKind::structured_buffer: return "Structured buffer";
		case BindingKind::byte_address_buffer: return "Byte address buffer";
		case BindingKind::append_buffer: return "Append buffer";
		case BindingKind::consume_buffer: return "Consume buffer";
		case BindingKind::acceleration_structure: return "Acceleration structure";
		case BindingKind::unknown:
		default: return "Unknown";
		}
	}

	std::string SignatureElement::Describe() const
	{
		std::string text = semantic_name;
		if (semantic_index != 0 || semantic_name.empty())
			text += std::to_string(semantic_index);

		const char *const kComponents = "xyzw";
		std::string swizzle;
		for (int i = 0; i < 4; ++i)
			if (mask & (1u << i))
				swizzle += kComponents[i];
		if (!swizzle.empty())
			text += "." + swizzle;
		return text;
	}

	namespace
	{
		BindingKind ToBindingKind(D3D_SHADER_INPUT_TYPE type)
		{
			switch (type)
			{
			case D3D_SIT_CBUFFER: return BindingKind::constant_buffer;
			case D3D_SIT_TBUFFER: return BindingKind::texture_buffer;
			case D3D_SIT_TEXTURE: return BindingKind::texture;
			case D3D_SIT_SAMPLER: return BindingKind::sampler;
			case D3D_SIT_UAV_RWTYPED: return BindingKind::unordered_access;
			case D3D_SIT_STRUCTURED:
			case D3D_SIT_UAV_RWSTRUCTURED:
			case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: return BindingKind::structured_buffer;
			case D3D_SIT_BYTEADDRESS:
			case D3D_SIT_UAV_RWBYTEADDRESS: return BindingKind::byte_address_buffer;
			case D3D_SIT_UAV_APPEND_STRUCTURED: return BindingKind::append_buffer;
			case D3D_SIT_UAV_CONSUME_STRUCTURED: return BindingKind::consume_buffer;
			case D3D_SIT_RTACCELERATIONSTRUCTURE: return BindingKind::acceleration_structure;
			default: return BindingKind::unknown;
			}
		}

		// Register spaces only exist in the D3D12 / DXIL reflection; SM 5.x always uses space 0.
		uint32_t BindSpace(const D3D11_SHADER_INPUT_BIND_DESC &) { return 0; }
		uint32_t BindSpace(const D3D12_SHADER_INPUT_BIND_DESC &desc) { return desc.Space; }

		const char *ComponentTypeName(D3D_REGISTER_COMPONENT_TYPE type)
		{
			switch (type)
			{
			case D3D_REGISTER_COMPONENT_UINT32: return "uint";
			case D3D_REGISTER_COMPONENT_SINT32: return "int";
			case D3D_REGISTER_COMPONENT_FLOAT32: return "float";
			default: return "unknown";
			}
		}

		// ID3D11ShaderReflection and ID3D12ShaderReflection expose the same methods with the same
		// names; only the description structs differ, and their fields are named identically.
		template <typename Reflection, typename ShaderDesc, typename BindDesc, typename ParamDesc,
		          typename BufferDesc>
		void FillReflection(Reflection *reflection, ReflectionResult &out)
		{
			ShaderDesc desc = {};
			if (FAILED(reflection->GetDesc(&desc)))
			{
				out.error = "GetDesc failed";
				return;
			}

			out.instruction_count = desc.InstructionCount;
			out.constant_buffer_count = desc.ConstantBuffers;

			UINT x = 0, y = 0, z = 0;
			reflection->GetThreadGroupSize(&x, &y, &z);
			out.thread_group[0] = x;
			out.thread_group[1] = y;
			out.thread_group[2] = z;

			out.bindings.reserve(desc.BoundResources);
			for (UINT i = 0; i < desc.BoundResources; ++i)
			{
				BindDesc bind = {};
				if (FAILED(reflection->GetResourceBindingDesc(i, &bind)))
					continue;

				ShaderBinding binding;
				binding.kind = ToBindingKind(bind.Type);
				binding.name = bind.Name != nullptr ? bind.Name : "";
				binding.bind_point = bind.BindPoint;
				binding.bind_count = bind.BindCount;
				binding.space = BindSpace(bind);
				out.bindings.push_back(std::move(binding));
			}

			// Constant buffer sizes are only reachable through the buffer objects.
			for (ShaderBinding &binding : out.bindings)
			{
				if (binding.kind != BindingKind::constant_buffer && binding.kind != BindingKind::texture_buffer)
					continue;

				auto *buffer = reflection->GetConstantBufferByName(binding.name.c_str());
				if (buffer == nullptr)
					continue;

				BufferDesc buffer_desc = {};
				if (SUCCEEDED(buffer->GetDesc(&buffer_desc)))
					binding.size = buffer_desc.Size;
			}

			out.inputs.reserve(desc.InputParameters);
			for (UINT i = 0; i < desc.InputParameters; ++i)
			{
				ParamDesc param = {};
				if (FAILED(reflection->GetInputParameterDesc(i, &param)))
					continue;

				SignatureElement element;
				element.semantic_name = param.SemanticName != nullptr ? param.SemanticName : "";
				element.semantic_index = param.SemanticIndex;
				element.register_index = param.Register;
				element.mask = param.Mask;
				element.component_type = ComponentTypeName(param.ComponentType);
				out.inputs.push_back(std::move(element));
			}

			out.outputs.reserve(desc.OutputParameters);
			for (UINT i = 0; i < desc.OutputParameters; ++i)
			{
				ParamDesc param = {};
				if (FAILED(reflection->GetOutputParameterDesc(i, &param)))
					continue;

				SignatureElement element;
				element.semantic_name = param.SemanticName != nullptr ? param.SemanticName : "";
				element.semantic_index = param.SemanticIndex;
				element.register_index = param.Register;
				element.mask = param.Mask;
				element.component_type = ComponentTypeName(param.ComponentType);
				out.outputs.push_back(std::move(element));
			}

			out.ok = true;
		}
	}

	DisassemblyResult Disassemble(const void *code, size_t size, DisassemblyStyle style)
	{
		DisassemblyResult result;
		const auto start = std::chrono::steady_clock::now();

		ShaderBlobInfo info;
		if (!ParseShaderBlob(code, size, info) || info.format == ShaderFormat::unknown)
		{
			result.error = "the byte code is not a DXBC or DXIL container";
			return result;
		}
		result.format = info.format;

		if (info.format == ShaderFormat::dxbc)
		{
			ToolHost &host = ToolHost::Instance();
			const ToolInfo &tool = host.Dxbc();
			if (!tool.available)
			{
				result.error = tool.error;
				return result;
			}

			// D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS is what every decompiler that parses this
			// text expects; the numbering and offsets are only useful to a human reading it.
			const UINT flags = style == DisassemblyStyle::plain
				? D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS
				: (D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING | D3D_DISASM_ENABLE_INSTRUCTION_OFFSET);

			ID3DBlob *blob = nullptr;
			const HRESULT hr = host.DisassembleProc()(code, size, flags, nullptr, &blob);
			if (FAILED(hr) || blob == nullptr)
			{
				result.error = HResultText("D3DDisassemble", hr);
				if (blob != nullptr)
					blob->Release();
				return result;
			}

			result.text.assign(static_cast<const char *>(blob->GetBufferPointer()), blob->GetBufferSize());
			blob->Release();

			// The blob is null terminated: do not keep the terminator inside a std::string.
			while (!result.text.empty() && result.text.back() == '\0')
				result.text.pop_back();

			result.tool = "D3DDisassemble (" + tool.path + ")";
			result.ok = true;
			result.milliseconds = ElapsedMilliseconds(start);
			return result;
		}

		// Shader Model 6: DXIL, through the DirectX Shader Compiler.
		ToolHost &host = ToolHost::Instance();
		const ToolInfo &tool = host.Dxil();
		if (!tool.available)
		{
			result.error = tool.error;
			return result;
		}

		IDxcCompiler3 *compiler = nullptr;
		HRESULT hr = host.DxcCreate()(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		if (FAILED(hr) || compiler == nullptr)
		{
			result.error = HResultText("DxcCreateInstance(DxcCompiler)", hr);
			return result;
		}

		DxcBuffer buffer = {};
		buffer.Ptr = code;
		buffer.Size = size;
		buffer.Encoding = DXC_CP_ACP;

		IDxcResult *dxc_result = nullptr;
		hr = compiler->Disassemble(&buffer, IID_PPV_ARGS(&dxc_result));
		compiler->Release();

		if (FAILED(hr) || dxc_result == nullptr)
		{
			result.error = HResultText("IDxcCompiler3::Disassemble", hr);
			if (dxc_result != nullptr)
				dxc_result->Release();
			return result;
		}

		IDxcBlobUtf8 *text = nullptr;
		hr = dxc_result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&text), nullptr);
		dxc_result->Release();

		if (FAILED(hr) || text == nullptr)
		{
			result.error = HResultText("IDxcResult::GetOutput(DISASSEMBLY)", hr);
			if (text != nullptr)
				text->Release();
			return result;
		}

		result.text.assign(text->GetStringPointer(), text->GetStringLength());
		text->Release();

		result.tool = "IDxcCompiler3::Disassemble (" + tool.path + ")";
		result.ok = true;
		result.milliseconds = ElapsedMilliseconds(start);
		return result;
	}

	ReflectionResult Reflect(const void *code, size_t size)
	{
		ReflectionResult result;

		ShaderBlobInfo info;
		if (!ParseShaderBlob(code, size, info) || info.format == ShaderFormat::unknown)
		{
			result.error = "the byte code is not a DXBC or DXIL container";
			return result;
		}

		ToolHost &host = ToolHost::Instance();

		if (info.format == ShaderFormat::dxbc)
		{
			const ToolInfo &tool = host.Dxbc();
			if (!tool.available || host.ReflectProc() == nullptr)
			{
				result.error = tool.available ? "D3DReflect is missing from d3dcompiler_47.dll" : tool.error;
				return result;
			}

			ID3D11ShaderReflection *reflection = nullptr;
			const HRESULT hr = host.ReflectProc()(code, size, IID_PPV_ARGS(&reflection));
			if (FAILED(hr) || reflection == nullptr)
			{
				result.error = HResultText("D3DReflect", hr);
				return result;
			}

			result.tool = "D3DReflect";
			FillReflection<ID3D11ShaderReflection, D3D11_SHADER_DESC, D3D11_SHADER_INPUT_BIND_DESC,
				D3D11_SIGNATURE_PARAMETER_DESC, D3D11_SHADER_BUFFER_DESC>(reflection, result);
			reflection->Release();
			return result;
		}

		const ToolInfo &tool = host.Dxil();
		if (!tool.available)
		{
			result.error = tool.error;
			return result;
		}

		IDxcUtils *utils = nullptr;
		HRESULT hr = host.DxcCreate()(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		if (FAILED(hr) || utils == nullptr)
		{
			result.error = HResultText("DxcCreateInstance(DxcUtils)", hr);
			return result;
		}

		DxcBuffer buffer = {};
		buffer.Ptr = code;
		buffer.Size = size;
		buffer.Encoding = DXC_CP_ACP;

		ID3D12ShaderReflection *reflection = nullptr;
		hr = utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection));
		utils->Release();

		if (FAILED(hr) || reflection == nullptr)
		{
			result.error = HResultText("IDxcUtils::CreateReflection", hr);
			return result;
		}

		result.tool = "IDxcUtils::CreateReflection";
		FillReflection<ID3D12ShaderReflection, D3D12_SHADER_DESC, D3D12_SHADER_INPUT_BIND_DESC,
			D3D12_SIGNATURE_PARAMETER_DESC, D3D12_SHADER_BUFFER_DESC>(reflection, result);
		reflection->Release();
		return result;
	}
	namespace
	{
		using PFN_Compile = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *,
		                                      LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

		// "ps_6_0" -> 6. Returns 0 when the profile cannot be read.
		uint32_t TargetMajorVersion(const std::string &target)
		{
			const size_t underscore = target.find('_');
			if (underscore == std::string::npos || underscore + 1 >= target.size())
				return 0;
			const char digit = target[underscore + 1];
			return (digit >= '0' && digit <= '9') ? static_cast<uint32_t>(digit - '0') : 0;
		}

		std::wstring Widen(const std::string &text)
		{
			if (text.empty())
				return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
				nullptr, 0);
			std::wstring wide(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
			return wide;
		}

		CompileResult CompileWithFxc(const std::string &source, const std::string &entry_point,
		                             const std::string &target, const std::string &source_name)
		{
			CompileResult result;
			const auto start = std::chrono::steady_clock::now();

			ToolHost &host = ToolHost::Instance();
			const ToolInfo &tool = host.Dxbc();
			if (!tool.available)
			{
				result.error = tool.error;
				return result;
			}

			const auto compile = reinterpret_cast<PFN_Compile>(
				reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("d3dcompiler_47.dll"), "D3DCompile")));
			if (compile == nullptr)
			{
				result.error = "D3DCompile is missing from d3dcompiler_47.dll";
				return result;
			}

			ID3DBlob *code = nullptr;
			ID3DBlob *errors = nullptr;
			const HRESULT hr = compile(source.data(), source.size(), source_name.c_str(), nullptr, nullptr,
				entry_point.c_str(), target.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);

			if (errors != nullptr)
			{
				result.messages.assign(static_cast<const char *>(errors->GetBufferPointer()),
					errors->GetBufferSize());
				while (!result.messages.empty() && result.messages.back() == '\0')
					result.messages.pop_back();
				errors->Release();
			}

			if (FAILED(hr) || code == nullptr)
			{
				result.error = HResultText("D3DCompile", hr);
				if (code != nullptr)
					code->Release();
				return result;
			}

			const uint8_t *bytes = static_cast<const uint8_t *>(code->GetBufferPointer());
			result.byte_code.assign(bytes, bytes + code->GetBufferSize());
			code->Release();

			result.tool = "D3DCompile (" + tool.path + ")";
			result.ok = true;
			result.milliseconds = ElapsedMilliseconds(start);
			return result;
		}

		CompileResult CompileWithDxc(const std::string &source, const std::string &entry_point,
		                             const std::string &target, const std::string &source_name)
		{
			CompileResult result;
			const auto start = std::chrono::steady_clock::now();

			ToolHost &host = ToolHost::Instance();
			const ToolInfo &tool = host.Dxil();
			if (!tool.available)
			{
				result.error = tool.error;
				return result;
			}

			IDxcCompiler3 *compiler = nullptr;
			HRESULT hr = host.DxcCreate()(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
			if (FAILED(hr) || compiler == nullptr)
			{
				result.error = HResultText("DxcCreateInstance(DxcCompiler)", hr);
				return result;
			}

			const std::wstring wide_entry = Widen(entry_point);
			const std::wstring wide_target = Widen(target);
			const std::wstring wide_name = Widen(source_name);
			// IDxcCompiler3::Compile takes a non const array, so this one cannot be const.
			LPCWSTR arguments[] = {
				wide_name.c_str(),
				L"-E", wide_entry.c_str(),
				L"-T", wide_target.c_str(),
				L"-O3",
			};

			DxcBuffer buffer = {};
			buffer.Ptr = source.data();
			buffer.Size = source.size();
			buffer.Encoding = DXC_CP_UTF8;

			IDxcResult *dxc_result = nullptr;
			hr = compiler->Compile(&buffer, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
				IID_PPV_ARGS(&dxc_result));
			compiler->Release();

			if (FAILED(hr) || dxc_result == nullptr)
			{
				result.error = HResultText("IDxcCompiler3::Compile", hr);
				if (dxc_result != nullptr)
					dxc_result->Release();
				return result;
			}

			IDxcBlobUtf8 *messages = nullptr;
			if (SUCCEEDED(dxc_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&messages), nullptr)) &&
			    messages != nullptr)
			{
				result.messages.assign(messages->GetStringPointer(), messages->GetStringLength());
				messages->Release();
			}

			HRESULT status = E_FAIL;
			dxc_result->GetStatus(&status);
			if (FAILED(status))
			{
				result.error = HResultText("shader compilation", status);
				dxc_result->Release();
				return result;
			}

			IDxcBlob *object = nullptr;
			hr = dxc_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
			dxc_result->Release();

			if (FAILED(hr) || object == nullptr)
			{
				result.error = HResultText("IDxcResult::GetOutput(OBJECT)", hr);
				if (object != nullptr)
					object->Release();
				return result;
			}

			const uint8_t *bytes = static_cast<const uint8_t *>(object->GetBufferPointer());
			result.byte_code.assign(bytes, bytes + object->GetBufferSize());
			object->Release();

			result.tool = "IDxcCompiler3::Compile (" + tool.path + ")";
			result.ok = true;
			result.milliseconds = ElapsedMilliseconds(start);
			return result;
		}
	}

	CompileResult CompileHlsl(const std::string &source, const std::string &entry_point,
	                          const std::string &target, const std::string &source_name)
	{
		const uint32_t major = TargetMajorVersion(target);
		if (major == 0)
		{
			CompileResult result;
			result.error = "\"" + target + "\" is not a shader profile such as ps_5_0 or cs_6_6";
			return result;
		}

		// Shader Model 6 only exists in DXC; everything below it only exists in FXC.
		return major >= 6 ? CompileWithDxc(source, entry_point, target, source_name)
		                  : CompileWithFxc(source, entry_point, target, source_name);
	}
}
