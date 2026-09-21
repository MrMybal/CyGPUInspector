// CyGPUInspectorShaderTests — the shader tool chain, on real shaders.
//
// Compiles HLSL with the actual compilers, then disassembles and reflects the result. This is
// what proves the milestone 5 chain works, rather than that it merely links.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <CyGPUInspectorCore/Sha256.hpp>
#include <CyGPUInspectorCore/ModPackage.hpp>
#include <CyGPUInspectorCore/ShaderBlob.hpp>
#include <CyGPUInspectorDatabase/ShaderStore.hpp>
#include <CyGPUInspectorDecompiler/Decompiler.hpp>
#include <CyGPUInspectorDecompiler/Disassembler.hpp>

#include "Render/CaptureBufferWriter.hpp"
#include "Render/PreviewRenderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
	int g_failures = 0;
	int g_checks = 0;
	int g_skipped = 0;

	void Check(bool condition, const char *what)
	{
		++g_checks;
		std::printf("  %-5s %s\n", condition ? "ok" : "FAIL", what);
		if (!condition)
			++g_failures;
	}

	void Skip(const char *what, const std::string &why)
	{
		++g_skipped;
		std::printf("  skip  %s (%s)\n", what, why.c_str());
	}

	bool Contains(const std::string &haystack, const char *needle)
	{
		return haystack.find(needle) != std::string::npos;
	}

	// A small but non trivial pixel shader: a constant buffer, a texture, a sampler, some maths.
	const char *const kPixelShader = R"(
cbuffer Tonemap : register(b0)
{
    float  Exposure;
    float  WhitePoint;
    float2 InvResolution;
};

Texture2D<float4> SceneColor : register(t0);
SamplerState      LinearClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 color = SceneColor.Sample(LinearClamp, uv + InvResolution).rgb * Exposure;
    color = color / (1.0f + color / WhitePoint);
    return float4(color, 1.0f);
}
)";

	const char *const kComputeShader = R"(
RWTexture2D<float4> Output : register(u0);
Texture2D<float4>   Input  : register(t0);

[numthreads(8, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = Input[id.xy] * 0.5f;
}
)";

	void TestTools()
	{
		std::printf("Tool discovery\n");

		const cygi::ToolInfo &dxbc = cygi::DxbcToolInfo();
		Check(dxbc.available, "d3dcompiler_47.dll is available");
		if (dxbc.available)
			std::printf("        %s\n", dxbc.path.c_str());
		else
			std::printf("        %s\n", dxbc.error.c_str());

		const cygi::ToolInfo &dxil = cygi::DxilToolInfo();
		if (dxil.available)
			std::printf("  ok    dxcompiler.dll found\n        %s\n", dxil.path.c_str());
		else
			std::printf("  info  dxcompiler.dll not found: %s\n", dxil.error.c_str());
	}

	void TestDxbcPixelShader()
	{
		std::printf("DXBC pixel shader (Shader Model 5)\n");

		const cygi::CompileResult compiled = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "tonemap.hlsl");
		Check(compiled.ok, "the shader compiles");
		if (!compiled.ok)
		{
			std::printf("        %s\n        %s\n", compiled.error.c_str(), compiled.messages.c_str());
			return;
		}

		cygi::ShaderBlobInfo info;
		Check(cygi::ParseShaderBlob(compiled.byte_code.data(), compiled.byte_code.size(), info),
			"the container parses");
		Check(info.format == cygi::ShaderFormat::dxbc, "it is recognised as DXBC");
		Check(info.stage == cygi::ShaderStage::pixel, "the stage is read from the container");
		Check(info.shader_model == 0x50, "the shader model is 5.0");

		const cygi::DisassemblyResult disassembly =
			cygi::Disassemble(compiled.byte_code.data(), compiled.byte_code.size());
		Check(disassembly.ok, "it disassembles");
		if (!disassembly.ok)
		{
			std::printf("        %s\n", disassembly.error.c_str());
			return;
		}

		Check(Contains(disassembly.text, "ps_5_0"), "the disassembly names the profile");
		Check(Contains(disassembly.text, "dcl_resource_texture2d"), "the texture declaration is there");
		Check(Contains(disassembly.text, "dcl_sampler"), "the sampler declaration is there");
		Check(Contains(disassembly.text, "sample"), "the sample instruction is there");
		Check(disassembly.LineCount() > 10, "the disassembly has a plausible length");
		std::printf("        %zu lines in %.2f ms\n", disassembly.LineCount(), disassembly.milliseconds);

		const cygi::ReflectionResult reflection =
			cygi::Reflect(compiled.byte_code.data(), compiled.byte_code.size());
		Check(reflection.ok, "it reflects");
		if (!reflection.ok)
		{
			std::printf("        %s\n", reflection.error.c_str());
			return;
		}

		bool found_cbuffer = false;
		bool found_texture = false;
		bool found_sampler = false;
		uint32_t cbuffer_size = 0;
		for (const cygi::ShaderBinding &binding : reflection.bindings)
		{
			if (binding.kind == cygi::BindingKind::constant_buffer && binding.name == "Tonemap")
			{
				found_cbuffer = true;
				cbuffer_size = binding.size;
			}
			if (binding.kind == cygi::BindingKind::texture && binding.name == "SceneColor")
				found_texture = true;
			if (binding.kind == cygi::BindingKind::sampler && binding.name == "LinearClamp")
				found_sampler = true;
		}
		Check(found_cbuffer, "the constant buffer is reported with its name");
		Check(cbuffer_size == 16, "the constant buffer size is read (4 floats, packed)");
		Check(found_texture, "the texture binding is reported");
		Check(found_sampler, "the sampler binding is reported");
		Check(reflection.instruction_count > 0, "the instruction count is read");

		bool has_position = false;
		bool has_uv = false;
		for (const cygi::SignatureElement &element : reflection.inputs)
		{
			if (element.semantic_name == "SV_Position")
				has_position = true;
			if (element.semantic_name == "TEXCOORD")
				has_uv = true;
		}
		Check(has_position, "SV_Position is in the input signature");
		Check(has_uv, "TEXCOORD is in the input signature");
		Check(reflection.outputs.size() == 1 && reflection.outputs[0].semantic_name == "SV_Target",
			"SV_Target is the only output");
	}

	void TestDxbcComputeShader()
	{
		std::printf("DXBC compute shader\n");

		const cygi::CompileResult compiled = cygi::CompileHlsl(kComputeShader, "main", "cs_5_0", "blit.hlsl");
		Check(compiled.ok, "the compute shader compiles");
		if (!compiled.ok)
			return;

		cygi::ShaderBlobInfo info;
		cygi::ParseShaderBlob(compiled.byte_code.data(), compiled.byte_code.size(), info);
		Check(info.stage == cygi::ShaderStage::compute, "the stage is compute");

		const cygi::ReflectionResult reflection =
			cygi::Reflect(compiled.byte_code.data(), compiled.byte_code.size());
		Check(reflection.ok, "it reflects");
		Check(reflection.thread_group[0] == 8 && reflection.thread_group[1] == 4 &&
			reflection.thread_group[2] == 1, "the thread group size is read");

		bool found_uav = false;
		for (const cygi::ShaderBinding &binding : reflection.bindings)
			if (binding.kind == cygi::BindingKind::unordered_access && binding.name == "Output")
				found_uav = true;
		Check(found_uav, "the UAV is reported");
	}

	void TestDxil()
	{
		std::printf("DXIL (Shader Model 6)\n");

		const cygi::ToolInfo &dxil = cygi::DxilToolInfo();
		if (!dxil.available)
		{
			Skip("the whole Shader Model 6 chain", dxil.error);
			return;
		}

		const cygi::CompileResult compiled = cygi::CompileHlsl(kPixelShader, "main", "ps_6_0", "tonemap.hlsl");
		Check(compiled.ok, "the shader compiles to DXIL");
		if (!compiled.ok)
		{
			std::printf("        %s\n        %s\n", compiled.error.c_str(), compiled.messages.c_str());
			return;
		}

		cygi::ShaderBlobInfo info;
		Check(cygi::ParseShaderBlob(compiled.byte_code.data(), compiled.byte_code.size(), info),
			"the container parses");
		Check(info.format == cygi::ShaderFormat::dxil, "it is recognised as DXIL");
		Check(info.stage == cygi::ShaderStage::pixel, "the stage is read from the DXIL program header");
		Check(info.shader_model == 0x60, "the shader model is 6.0");

		const cygi::DisassemblyResult disassembly =
			cygi::Disassemble(compiled.byte_code.data(), compiled.byte_code.size());
		Check(disassembly.ok, "it disassembles");
		if (!disassembly.ok)
		{
			std::printf("        %s\n", disassembly.error.c_str());
			return;
		}

		// DXIL disassembly is LLVM assembly, not the SM5 text form.
		Check(Contains(disassembly.text, "define") || Contains(disassembly.text, "dx.op"),
			"the disassembly looks like LLVM IR");
		Check(disassembly.LineCount() > 10, "the disassembly has a plausible length");
		std::printf("        %zu lines in %.2f ms\n", disassembly.LineCount(), disassembly.milliseconds);

		const cygi::ReflectionResult reflection =
			cygi::Reflect(compiled.byte_code.data(), compiled.byte_code.size());
		Check(reflection.ok, "it reflects");
		if (reflection.ok)
		{
			bool found_texture = false;
			for (const cygi::ShaderBinding &binding : reflection.bindings)
				if (binding.kind == cygi::BindingKind::texture && binding.name == "SceneColor")
					found_texture = true;
			Check(found_texture, "the texture binding is reported for DXIL too");
		}
		else
		{
			std::printf("        %s\n", reflection.error.c_str());
		}
	}

	void TestSignatureStability()
	{
		std::printf("Signature stability\n");

		const cygi::CompileResult first = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "tonemap.hlsl");
		const cygi::CompileResult second = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "other_name.hlsl");
		if (!first.ok || !second.ok)
		{
			Skip("signature stability", "the shader did not compile");
			return;
		}

		const cygi::Sha256Digest a = cygi::ComputeShaderSignature(first.byte_code.data(), first.byte_code.size());
		const cygi::Sha256Digest b = cygi::ComputeShaderSignature(second.byte_code.data(), second.byte_code.size());
		Check(a == b, "the same shader compiled twice has the same signature");

		const cygi::CompileResult different = cygi::CompileHlsl(kComputeShader, "main", "cs_5_0", "blit.hlsl");
		if (different.ok)
		{
			const cygi::Sha256Digest c =
				cygi::ComputeShaderSignature(different.byte_code.data(), different.byte_code.size());
			Check(!(a == c), "a different shader has a different signature");
		}
	}

	// The bridge between CyGPUInspector and CyGPUInjector: a modification leaves the session as a
	// package on disk, and comes back as exactly the same byte code. It is checked with real
	// compiled shaders, because the format refuses byte code that is not a valid container and
	// that refusal is the whole point of the check.
	void TestModPackage()
	{
		std::printf("Mod package\n");

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "CyGPUInspectorModTest";
		std::error_code error;
		std::filesystem::remove_all(root, error);

		const cygi::CompileResult original = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "tonemap.hlsl");
		const cygi::CompileResult compute = cygi::CompileHlsl(kComputeShader, "main", "cs_5_0", "blit.hlsl");
		if (!original.ok || !compute.ok)
		{
			Skip("mod package", "the test shaders did not compile");
			return;
		}

		const cygi::Sha256Digest hash =
			cygi::ComputeShaderSemanticHash(original.byte_code.data(), original.byte_code.size());
		const cygi::Sha256Digest compute_hash =
			cygi::ComputeShaderSemanticHash(compute.byte_code.data(), compute.byte_code.size());

		cygi::ModPackage package;
		package.name = "Test Mod";
		package.author = "CyGPUInspectorShaderTests";
		package.version = "1.0";
		package.description = "Round trip check";
		package.target_process = "FakeGame.exe";

		cygi::ModEntry replacement;
		replacement.action = cygi::ModAction::replace;
		replacement.semantic_hash = hash;
		replacement.signature =
			cygi::ComputeShaderSignature(original.byte_code.data(), original.byte_code.size());
		replacement.stage = cygi::ShaderStage::pixel;
		replacement.format = cygi::ShaderFormat::dxbc;
		replacement.shader_model = 0x50;
		replacement.byte_code = original.byte_code;
		replacement.source_hlsl = kPixelShader;
		replacement.profile = "ps_5_0";
		replacement.note = "the tonemap, replaced by itself";
		package.entries.push_back(replacement);

		cygi::ModEntry disable;
		disable.action = cygi::ModAction::disable;
		disable.semantic_hash = compute_hash;
		disable.stage = cygi::ShaderStage::compute;
		disable.format = cygi::ShaderFormat::dxbc;
		disable.shader_model = 0x50;
		package.entries.push_back(disable);

		const std::filesystem::path directory = root / "TestMod.cygimod";
		std::string save_error;
		Check(package.Save(directory.string(), save_error), "a package is written");
		if (!save_error.empty())
			std::printf("        %s\n", save_error.c_str());

		Check(std::filesystem::exists(directory / "mod.json"), "mod.json is there");
		Check(std::filesystem::exists(directory / ("shaders/" + hash.ToHex() + ".cso")),
			"the replacement byte code is stored under the hash it matches");
		Check(std::filesystem::exists(directory / ("shaders/" + hash.ToHex() + ".hlsl")),
			"the source it was compiled from travels with it");
		Check(cygi::ModPackage::LooksLikePackage(directory.string()), "the folder is recognised as a package");

		cygi::ModPackage reopened;
		std::string load_error;
		const bool loaded = cygi::ModPackage::Load(directory.string(), reopened, load_error);
		Check(loaded, "the package reopens");
		if (!loaded)
			std::printf("        %s\n", load_error.c_str());

		if (loaded)
		{
			Check(reopened.name == "Test Mod" && reopened.author == package.author,
				"the metadata survives the round trip");
			Check(reopened.target_process == "FakeGame.exe", "the game it was made on is recorded");
			Check(reopened.entries.size() == 2, "both entries come back");
			Check(reopened.CountOf(cygi::ModAction::replace) == 1 &&
				reopened.CountOf(cygi::ModAction::disable) == 1, "and keep their actions");

			const cygi::ModEntry *back = nullptr;
			for (const cygi::ModEntry &entry : reopened.entries)
				if (entry.action == cygi::ModAction::replace)
					back = &entry;

			Check(back != nullptr && back->semantic_hash == hash,
				"the matching key is exactly the hash CyGPUInjector will compute");
			Check(back != nullptr && back->byte_code == original.byte_code,
				"the byte code is byte for byte what was exported");
			Check(back != nullptr && back->stage == cygi::ShaderStage::pixel &&
				back->shader_model == 0x50, "the stage and the shader model come back");
			Check(back != nullptr && back->profile == "ps_5_0", "and the profile it was compiled for");
			Check(back != nullptr && back->note == "the tonemap, replaced by itself",
				"the author's note comes back");
			Check(back != nullptr && back->source_hlsl.find("SceneColor") != std::string::npos,
				"and so does the HLSL it was made from");
		}

		// The safety property: what goes to a graphics driver is checked on the way in, so a
		// truncated or hand-edited .cso is refused rather than handed to a game.
		{
			const std::filesystem::path blob = directory / ("shaders/" + hash.ToHex() + ".cso");
			std::ofstream damaged(blob, std::ios::binary | std::ios::trunc);
			damaged << "not a shader";
			damaged.close();

			cygi::ModPackage broken;
			std::string broken_error;
			Check(!cygi::ModPackage::Load(directory.string(), broken, broken_error),
				"a package whose byte code is not a shader container is refused");
			Check(!broken_error.empty(), "and it says which entry is at fault");
		}

		// A package with nothing in it is an error rather than a file nobody can explain.
		{
			cygi::ModPackage empty;
			empty.name = "Empty";
			std::string empty_error;
			Check(!empty.Save((root / "Empty.cygimod").string(), empty_error),
				"an empty package is refused rather than written");
		}

		std::filesystem::remove_all(root, error);
	}

	void TestShaderStore()
	{
		std::printf("Shader store\n");

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "CyGPUInspectorStoreTest";
		std::error_code error;
		std::filesystem::remove_all(root, error);

		cygi::ShaderStore store(root);
		Check(store.IsUsable(), "the store creates its directories");

		const cygi::CompileResult compiled = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "tonemap.hlsl");
		if (!compiled.ok)
		{
			Skip("the store round trip", "the shader did not compile");
			return;
		}

		const cygi::Sha256Digest signature =
			cygi::ComputeShaderSignature(compiled.byte_code.data(), compiled.byte_code.size());
		const cygi::Sha256Digest semantic =
			cygi::ComputeShaderSemanticHash(compiled.byte_code.data(), compiled.byte_code.size());

		Check(store.SaveByteCode(signature, cygi::ShaderFormat::dxbc, compiled.byte_code.data(),
			compiled.byte_code.size()), "the byte code is written");
		Check(store.SaveText(signature, "disassembly.txt", "mov r0, r1"), "a text artefact is written");
		Check(store.SaveMetadata(signature, semantic, cygi::ShaderStage::pixel, cygi::ShaderFormat::dxbc,
			0x50, static_cast<uint32_t>(compiled.byte_code.size()), "FakeGame.exe"), "metadata is written");

		Check(store.Has(signature, "disassembly.txt"), "the artefact is found again");
		Check(store.Has(signature, "metadata.json"), "the metadata is found again");
		Check(!store.Has(signature, "decompiled/nothing.hlsl"), "a missing artefact is reported missing");

		std::string text;
		Check(store.LoadText(signature, "disassembly.txt", text) && text == "mov r0, r1",
			"the artefact reads back unchanged");

		// A second store on the same root must find the work of the first: this is what makes a
		// shader met in another run of the game free to analyse.
		cygi::ShaderStore reopened(root);
		Check(reopened.Has(signature, "disassembly.txt"), "another store instance finds the cached work");

		const std::string hex = signature.ToHex();
		Check(store.DirectoryFor(signature).parent_path().filename().string() == hex.substr(0, 2),
			"the store fans out on the first two hex characters");

		std::filesystem::remove_all(root, error);
	}

	// The buffers of a deep capture, from shared textures to files: what the add-on sends for a
	// D3D11 game (legacy share handles), read back and written the way the standalone does it.
	void TestCaptureBufferWriter()
	{
		std::printf("Capture buffer writer\n");

		ID3D11Device *device = nullptr;
		ID3D11DeviceContext *context = nullptr;
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
			D3D11_SDK_VERSION, &device, nullptr, &context);
		if (FAILED(hr))
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION,
				&device, nullptr, &context);
		if (FAILED(hr))
		{
			Skip("the capture buffer writer", "no D3D11 device");
			return;
		}

		constexpr uint32_t kWidth = 8;
		constexpr uint32_t kHeight = 4;

		// Creates a shared texture the way the add-on does, and returns its legacy handle.
		std::vector<ID3D11Texture2D *> textures;
		auto make_shared = [&](DXGI_FORMAT format, const void *data, uint32_t pitch) -> uint64_t {
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = kWidth;
			desc.Height = kHeight;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
			D3D11_SUBRESOURCE_DATA initial = {};
			initial.pSysMem = data;
			initial.SysMemPitch = pitch;
			ID3D11Texture2D *texture = nullptr;
			if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)))
				return 0;
			textures.push_back(texture);
			// What the game's queue does before the standalone reads: the content has to have
			// reached the shared allocation, not be waiting in this context's queue.
			context->Flush();
			IDXGIResource *resource = nullptr;
			HANDLE handle = nullptr;
			if (SUCCEEDED(texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&resource))))
			{
				resource->GetSharedHandle(&handle);
				resource->Release();
			}
			return reinterpret_cast<uint64_t>(handle);
		};

		// An HDR colour target: 0 to 4 across, in half floats (0x4400 is 4.0, 0x3C00 is 1.0).
		uint16_t colour[kWidth * kHeight * 4] = {};
		for (uint32_t i = 0; i < kWidth * kHeight; ++i)
		{
			const uint32_t x = i % kWidth;
			colour[i * 4 + 0] = x == kWidth - 1 ? 0x4400 : 0x3C00;
			colour[i * 4 + 1] = 0x3800;   // 0.5
			colour[i * 4 + 2] = 0x0000;
			colour[i * 4 + 3] = 0x3C00;
		}
		// A depth buffer shared through its typeless family: 0.2 to 0.8, and a cleared far plane.
		float depth[kWidth * kHeight] = {};
		for (uint32_t i = 0; i < kWidth * kHeight; ++i)
			depth[i] = i == 0 ? 1.0f : 0.2f + 0.6f * static_cast<float>(i - 1) / static_cast<float>(kWidth * kHeight - 2);

		const uint64_t colour_handle = make_shared(DXGI_FORMAT_R16G16B16A16_FLOAT, colour, kWidth * 8);
		const uint64_t depth_handle = make_shared(DXGI_FORMAT_R32_TYPELESS, depth, kWidth * 4);
		Check(colour_handle != 0 && depth_handle != 0, "shared textures with legacy handles are created");
		if (colour_handle == 0 || depth_handle == 0)
		{
			for (ID3D11Texture2D *texture : textures)
				texture->Release();
			context->Release();
			device->Release();
			return;
		}

		auto record = [&](uint32_t index, uint32_t id, uint64_t handle, uint32_t format, uint32_t source_format,
		                  uint32_t roles, cygi::PreviewStatus status) {
			cygi::CaptureBufferRecord out = {};
			out.first_frame = 100;
			out.frame_index = 101;
			out.resource_id = id;
			out.status = status;
			out.shared_handle = handle;
			out.width = kWidth;
			out.height = kHeight;
			out.format = format;
			out.source_format = source_format;
			out.roles = roles;
			out.index = index;
			out.count = 3;
			return out;
		};

		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "CyGPUInspectorBufferTest";
		std::error_code error;
		std::filesystem::remove_all(folder, error);

		{
			cygi::CaptureBufferWriter writer;
			Check(writer.Initialize(device, context), "the writer has its display pass");
			writer.Begin(folder, 100, "TestGame");
			writer.Add(record(0, 11, colour_handle, 10, 10, cygi::kBufferRoleRenderTarget, cygi::PreviewStatus::ready),
				std::string());
			writer.Add(record(1, 12, depth_handle, 39, 40, cygi::kBufferRoleDepthStencil, cygi::PreviewStatus::ready),
				std::string());
			writer.Add(record(2, 13, 0, 44, 45, cygi::kBufferRoleDepthStencil, cygi::PreviewStatus::state_unknown),
				std::string());

			Check(!writer.Step(102), "nothing is read back while the frame may still be in flight");

			bool released = false;
			for (int i = 0; i < 20 && !released; ++i)
				released = writer.Step(200);
			Check(released, "once everything is read back, the add-on is told to release its copies");

			// The worker writes asynchronously; the index comes once it has finished.
			for (int i = 0; i < 200 && !std::filesystem::exists(folder / "capture.json"); ++i)
			{
				writer.Step(200);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			Check(std::filesystem::exists(folder / "capture.json"), "capture.json is written");

			const std::vector<cygi::CaptureBufferWriter::Item> items = writer.Items();
			Check(items.size() == 3, "one item per record");
			if (items.size() == 3)
			{
				Check(items[0].state == cygi::CaptureBufferWriter::State::written, "the colour target is saved");
				Check(items[1].state == cygi::CaptureBufferWriter::State::written, "the depth buffer is saved");
				Check(items[2].state == cygi::CaptureBufferWriter::State::not_copied,
					"a buffer the add-on could not copy is listed, not saved");
				Check(items[0].file_stem == "01_rt_8x4_r16g16b16a16_float_res11", "files are named by order, role, size, format");
				Check(items[0].range_min == 0.0f && items[0].range_max > 3.9f && items[0].range_max < 4.1f,
					"an HDR target's PNG is stretched to the values it holds");
				Check(items[1].depth && items[1].range_min > 0.19f && items[1].range_min < 0.21f &&
				      items[1].range_max > 0.79f && items[1].range_max < 0.81f,
					"a depth buffer's PNG spans its nearest and farthest values, the cleared ones left out");

				const std::filesystem::path dds = folder / (items[0].file_stem + ".dds");
				Check(std::filesystem::exists(folder / (items[0].file_stem + ".png")), "the PNG exists");
				Check(std::filesystem::exists(dds) &&
				      std::filesystem::file_size(dds, error) == 148u + kWidth * kHeight * 8u,
					"the DDS holds a DX10 header and the data, unchanged in size");
				std::ifstream file(dds, std::ios::binary);
				uint32_t header[37] = {};
				file.read(reinterpret_cast<char *>(header), sizeof(header));
				Check(header[0] == 0x20534444u && header[21] == 0x30315844u && header[32] == 10u,
					"the DDS says DX10, r16g16b16a16_float");
				uint16_t first = 0;
				file.read(reinterpret_cast<char *>(&first), 2);
				Check(first == 0x3C00, "the DDS data is the texture's, not a converted image");
				if (first != 0x3C00 || items[0].range_max < 3.9f)
					std::printf("        first half 0x%04X, colour range %g to %g, depth range %g to %g\n", first,
						static_cast<double>(items[0].range_min), static_cast<double>(items[0].range_max),
						static_cast<double>(items[1].range_min), static_cast<double>(items[1].range_max));

				const std::filesystem::path depth_dds = folder / (items[1].file_stem + ".dds");
				std::ifstream depth_file(depth_dds, std::ios::binary);
				depth_file.read(reinterpret_cast<char *>(header), sizeof(header));
				Check(header[32] == 41u, "a typeless depth copy is saved as r32_float");
			}

			std::ifstream index(folder / "capture.json", std::ios::binary);
			const std::string json((std::istreambuf_iterator<char>(index)), std::istreambuf_iterator<char>());
			Check(Contains(json, "\"game\": \"TestGame\"") && Contains(json, "\"saved\": false") &&
			      Contains(json, "never seen in a barrier"),
				"capture.json lists every buffer and why one was not saved");
			writer.Shutdown();
		}

		std::filesystem::remove_all(folder, error);
		for (ID3D11Texture2D *texture : textures)
			texture->Release();
		context->Release();
		device->Release();
	}

	// The preview display pass: channel selection and range remapping, checked on real pixels.
	void TestPreviewRenderer()
	{
		std::printf("Preview renderer\n");

		ID3D11Device *device = nullptr;
		ID3D11DeviceContext *context = nullptr;
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
			D3D11_SDK_VERSION, &device, nullptr, &context);
		if (FAILED(hr))
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION,
				&device, nullptr, &context);
		if (FAILED(hr))
		{
			Skip("the preview renderer", "no D3D11 device");
			return;
		}

		cygi::PreviewRenderer renderer;
		Check(renderer.Initialize(device, context), "the display shaders compile and load");
		if (!renderer.IsReady())
		{
			std::printf("        %s\n", renderer.LastError().c_str());
			context->Release();
			device->Release();
			return;
		}

		// A single known texel: red 200, green 100, blue 50, alpha 255.
		constexpr uint32_t kSize = 4;
		uint32_t pixels[kSize * kSize];
		for (uint32_t &pixel : pixels)
			pixel = 0xFF000000u | (50u << 16) | (100u << 8) | 200u;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = kSize;
		desc.Height = kSize;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initial = {};
		initial.pSysMem = pixels;
		initial.SysMemPitch = kSize * 4;

		ID3D11Texture2D *source = nullptr;
		ID3D11ShaderResourceView *source_view = nullptr;
		Check(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &source)), "the source texture is created");
		Check(source != nullptr && SUCCEEDED(device->CreateShaderResourceView(source, nullptr, &source_view)),
			"the source view is created");

		// Reads the centre texel of whatever the renderer produced.
		auto read_back = [&](ID3D11ShaderResourceView *view) -> uint32_t {
			ID3D11Resource *resource = nullptr;
			view->GetResource(&resource);

			ID3D11Texture2D *texture = nullptr;
			resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&texture));
			resource->Release();

			D3D11_TEXTURE2D_DESC staging_desc = {};
			texture->GetDesc(&staging_desc);
			staging_desc.Usage = D3D11_USAGE_STAGING;
			staging_desc.BindFlags = 0;
			staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			staging_desc.MiscFlags = 0;

			ID3D11Texture2D *staging = nullptr;
			uint32_t value = 0;
			if (SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)))
			{
				context->CopyResource(staging, texture);
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
				{
					value = *reinterpret_cast<const uint32_t *>(
						static_cast<const uint8_t *>(mapped.pData) + mapped.RowPitch);
					context->Unmap(staging, 0);
				}
				staging->Release();
			}
			texture->Release();
			return value;
		};

		auto channel = [](uint32_t pixel, int index) { return (pixel >> (index * 8)) & 0xFFu; };
		auto close_to = [](uint32_t value, uint32_t expected) {
			const uint32_t difference = value > expected ? value - expected : expected - value;
			return difference <= 2;   // the round trip through UNORM is not bit exact
		};

		cygi::PreviewSettings settings;

		settings.mode = cygi::PreviewChannelMode::color;
		uint32_t result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(close_to(channel(result, 0), 200) && close_to(channel(result, 1), 100) &&
			close_to(channel(result, 2), 50), "RGB passes the colour through");

		settings.mode = cygi::PreviewChannelMode::red;
		result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(close_to(channel(result, 0), 200) && close_to(channel(result, 1), 200) &&
			close_to(channel(result, 2), 200), "R shows the red channel as grey");

		settings.mode = cygi::PreviewChannelMode::green;
		result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(close_to(channel(result, 0), 100) && close_to(channel(result, 1), 100),
			"G shows the green channel as grey");

		settings.mode = cygi::PreviewChannelMode::blue;
		result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(close_to(channel(result, 0), 50), "B shows the blue channel as grey");

		// Range remapping: 200/255 = 0.784, stretched from [0.5, 1.0] gives 0.569 -> 145.
		settings.mode = cygi::PreviewChannelMode::red;
		settings.range_min = 0.5f;
		settings.range_max = 1.0f;
		result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(close_to(channel(result, 0), 145), "the range is remapped");

		// Everything below the range floor clamps to black rather than wrapping around.
		settings.range_min = 0.9f;
		settings.range_max = 1.0f;
		result = read_back(renderer.Render(source_view, kSize, kSize, settings));
		Check(channel(result, 0) == 0, "a value under the range clamps to black");

		if (source_view != nullptr) source_view->Release();
		if (source != nullptr) source->Release();
		context->Release();
		device->Release();
	}

	void TestDecompiler()
	{
		std::printf("Decompiler backends\n");

		const cygi::DecompilerRegistry &registry = cygi::DecompilerRegistry::Instance();
		Check(!registry.All().empty(), "at least one backend is registered");
		Check(registry.Find("cygi-dxbc") != nullptr, "the register level DXBC backend is registered");
		Check(registry.Find("hlsldecompiler") != nullptr, "the 3Dmigoto backend is registered");
		Check(registry.Find("dxbc-spirv") != nullptr, "the dxbc-spirv backend is registered");
		Check(registry.BackendsFor(cygi::ShaderFormat::dxbc).size() == 3, "three backends claim DXBC");

		for (const cygi::DecompilerBackend *backend : registry.All())
			std::printf("        %s %s\n          %s\n", backend->Info().id.c_str(),
				backend->Info().version.c_str(), backend->Info().license.c_str());

		const cygi::CompileResult compiled = cygi::CompileHlsl(kPixelShader, "main", "ps_5_0", "tonemap.hlsl");
		if (!compiled.ok)
		{
			Skip("decompilation", "the shader did not compile");
			return;
		}

		const std::vector<cygi::DecompilationResult> results =
			registry.DecompileAll(compiled.byte_code.data(), compiled.byte_code.size(), true);
		Check(results.size() == 3, "every supporting backend produced its own result");

		// Each result stands on its own: nothing is merged, nothing overwrites anything.
		const cygi::DecompilationResult *migoto = nullptr;
		const cygi::DecompilationResult *ours = nullptr;
		const cygi::DecompilationResult *spirv = nullptr;
		for (const cygi::DecompilationResult &result : results)
		{
			if (result.backend_id == "hlsldecompiler") migoto = &result;
			if (result.backend_id == "cygi-dxbc") ours = &result;
			if (result.backend_id == "dxbc-spirv") spirv = &result;
		}
		Check(migoto != nullptr, "the 3Dmigoto result is present");
		Check(spirv != nullptr, "the dxbc-spirv result is present");
		Check(spirv != nullptr && spirv->ok, "dxbc-spirv reconstructed the shader");
		if (spirv != nullptr && spirv->ok)
		{
			// A different lineage entirely: a real compiler, not a text translator. What it must
			// still deliver is the names and the registers of the original shader.
			Check(Contains(spirv->hlsl, "SceneColor"), "dxbc-spirv keeps the texture name");
			Check(Contains(spirv->hlsl, "LinearClamp"), "dxbc-spirv keeps the sampler name");
			Check(Contains(spirv->hlsl, "cbuffer Tonemap"), "dxbc-spirv keeps the constant buffer name");
			// No `space`: the shader is Shader Model 5.0, which cannot express one, so the
			// reconstruction targets 5.0 too and recompiles as ps_5_0.
			Check(Contains(spirv->hlsl, "register(t0)"), "dxbc-spirv puts the texture back on t0");
			Check(spirv->suggested_profile == "ps_5_0", "and it targets the original profile");
			Check(Contains(spirv->hlsl, ".Sample("), "dxbc-spirv reconstructs the sample call");
			Check(spirv->hlsl != migoto->hlsl && spirv->hlsl != ours->hlsl,
				"and it is a third, genuinely different reconstruction");
		}
		Check(ours != nullptr, "our own result is present");
		Check(migoto != nullptr && ours != nullptr && migoto->hlsl != ours->hlsl,
			"the two backends really produced different reconstructions");

		for (const cygi::DecompilationResult &result : results)
		{
			std::printf("        %-16s %-34s %s\n", result.backend_id.c_str(),
				cygi::ValidationVerdictName(result.verdict), result.validation_messages.c_str());
			Check(result.ok, (result.backend_id + ": the reconstruction succeeded").c_str());
			Check(!result.hlsl.empty(), (result.backend_id + ": HLSL was produced").c_str());
			Check(!result.backend_version.empty(), (result.backend_id + ": the version is named").c_str());
			Check(result.verdict != cygi::ValidationVerdict::not_run,
				(result.backend_id + ": the reconstruction was validated").c_str());
		}

		Check(ours != nullptr && ours->ArtifactName() == "decompiled/cygi-dxbc-0.1.hlsl",
			"artefact names keep backends and versions apart");
		Check(migoto != nullptr && migoto->ArtifactName() == "decompiled/hlsldecompiler-1.4.1.hlsl",
			"the 3Dmigoto artefact has its own name");

		// What 3Dmigoto brings over a register level translation: real names and real expressions.
		if (migoto != nullptr && migoto->ok)
		{
			Check(Contains(migoto->hlsl, "3Dmigoto"), "the output says which decompiler made it");
			Check(Contains(migoto->hlsl, "Tonemap"), "the constant buffer keeps its name");
			Check(Contains(migoto->hlsl, "SceneColor"), "the texture keeps its name");
			Check(Contains(migoto->hlsl, "LinearClamp"), "the sampler keeps its name");
			Check(Contains(migoto->hlsl, "Exposure") || Contains(migoto->hlsl, "WhitePoint"),
				"constant buffer members keep their names");
			Check(Contains(migoto->hlsl, ".Sample("), "the sample call is reconstructed");
			Check(!migoto->notes.empty(), "the result says what it can and cannot recover");
		}

		if (ours != nullptr && ours->ok)
			Check(ours->verdict == cygi::ValidationVerdict::equivalent_disassembly,
				"our own backend still round trips exactly");

		if (migoto != nullptr)
			std::printf("\n--- 3Dmigoto output ---\n%s--- end ---\n", migoto->hlsl.c_str());
		if (spirv != nullptr)
			std::printf("\n--- dxbc-spirv output ---\n%s--- end ---\n\n", spirv->hlsl.c_str());
	}

	void TestDxilDecompiler()
	{
		std::printf("DXIL decompilation (dxil-spirv + SPIRV-Cross)\n");

		const cygi::DecompilerRegistry &registry = cygi::DecompilerRegistry::Instance();
		Check(registry.Find("dxil-spirv") != nullptr, "the DXIL backend is registered");
		Check(registry.BackendsFor(cygi::ShaderFormat::dxil).size() == 1,
			"it is the only backend that claims DXIL");
		Check(registry.BackendsFor(cygi::ShaderFormat::dxbc).size() == 3,
			"and it does not claim DXBC, which the three other backends handle");

		const cygi::ToolInfo &dxil = cygi::DxilToolInfo();
		if (!dxil.available)
		{
			Skip("DXIL decompilation", dxil.error);
			return;
		}

		const cygi::CompileResult compiled = cygi::CompileHlsl(kPixelShader, "main", "ps_6_0", "tonemap.hlsl");
		if (!compiled.ok)
		{
			Skip("DXIL decompilation", "the shader did not compile to Shader Model 6");
			return;
		}

		const std::vector<cygi::DecompilationResult> results =
			registry.DecompileAll(compiled.byte_code.data(), compiled.byte_code.size(), true);
		Check(results.size() == 1, "only the DXIL backend ran on a Shader Model 6 shader");
		if (results.empty())
			return;

		const cygi::DecompilationResult &result = results.front();
		std::printf("        %-16s %-34s %s\n", result.backend_id.c_str(),
			cygi::ValidationVerdictName(result.verdict), result.validation_messages.c_str());

		Check(result.ok, "the reconstruction succeeded");
		if (!result.ok)
		{
			std::printf("        %s\n", result.error.c_str());
			return;
		}

		Check(!result.hlsl.empty(), "HLSL was produced");
		Check(result.suggested_profile == "ps_6_0", "the Shader Model 6 profile is carried over");
		Check(result.ArtifactName().rfind("decompiled/dxil-spirv-", 0) == 0,
			"the artefact is stored under its own backend name");
		Check(result.verdict != cygi::ValidationVerdict::not_run, "the reconstruction was validated");
		Check(!result.notes.empty(), "the result says what the round trip through SPIR-V costs");

		// The intermediate SPIR-V carries neither of these, so they are only there because the
		// backend put them back from the reflection tables. That is what is worth asserting.
		Check(Contains(result.hlsl, "SceneColor"), "the texture keeps its name");
		Check(Contains(result.hlsl, "LinearClamp"), "the sampler keeps its name");
		Check(Contains(result.hlsl, "cbuffer Tonemap"), "the constant buffer keeps its name");
		Check(Contains(result.hlsl, "register(t0, space0)"), "the texture is put back on t0");
		Check(Contains(result.hlsl, "register(s0, space0)"), "the sampler is put back on s0");
		Check(Contains(result.hlsl, "register(b0, space0)"), "the constant buffer is put back on b0");
		Check(Contains(result.hlsl, ".Sample("), "the sample call is reconstructed");

		// And this is the honest half: what the round trip destroys. The three members of the
		// constant buffer are gone, collapsed into the float4 array a Vulkan uniform block is.
		Check(!Contains(result.hlsl, "Exposure") && !Contains(result.hlsl, "WhitePoint"),
			"constant buffer members are lost, as the notes say");
		Check(Contains(result.notes, "collapsed"), "and the result says so rather than hiding it");

		std::printf("\n--- dxil-spirv output ---\n%s--- end ---\n\n", result.hlsl.c_str());

		// A compute shader goes through a different half of the chain: an unordered access view
		// rather than a texture, and a thread group size that only exists as an execution mode in
		// the intermediate SPIR-V.
		const cygi::CompileResult compute = cygi::CompileHlsl(kComputeShader, "main", "cs_6_0", "blit.hlsl");
		if (!compute.ok)
		{
			Skip("DXIL compute decompilation", "the compute shader did not compile to Shader Model 6");
			return;
		}

		const std::vector<cygi::DecompilationResult> compute_results =
			registry.DecompileAll(compute.byte_code.data(), compute.byte_code.size(), true);
		if (compute_results.empty())
		{
			Check(false, "the compute shader was decompiled");
			return;
		}

		const cygi::DecompilationResult &cs = compute_results.front();
		std::printf("        %-16s %-34s %s\n", cs.backend_id.c_str(),
			cygi::ValidationVerdictName(cs.verdict), cs.validation_messages.c_str());
		Check(cs.ok, "the compute reconstruction succeeded");
		if (!cs.ok)
		{
			std::printf("        %s\n", cs.error.c_str());
			return;
		}

		Check(cs.suggested_profile == "cs_6_0", "the compute profile is carried over");
		Check(Contains(cs.hlsl, "RWTexture2D"), "the unordered access view is typed as one");
		Check(Contains(cs.hlsl, "Output"), "the unordered access view keeps its name");
		Check(Contains(cs.hlsl, "Input"), "the read only texture keeps its name");
		Check(Contains(cs.hlsl, "register(u0, space0)"), "the unordered access view is put back on u0");
		Check(Contains(cs.hlsl, "numthreads(8, 4, 1)"), "the thread group size survives the round trip");

		std::printf("\n--- dxil-spirv compute output ---\n%s--- end ---\n\n", cs.hlsl.c_str());
	}
}

int main()
{
	std::printf("CyGPUInspectorShaderTests\n\n");

	TestTools();
	TestDxbcPixelShader();
	TestDxbcComputeShader();
	TestDxil();
	TestSignatureStability();
	TestModPackage();
	TestShaderStore();
	TestPreviewRenderer();
	TestCaptureBufferWriter();
	TestDecompiler();
	TestDxilDecompiler();

	std::printf("\n%d checks, %d failure(s), %d skipped\n", g_checks, g_failures, g_skipped);
	return g_failures == 0 ? 0 : 1;
}
