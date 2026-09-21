// CyGPUInspectorFakeSession — a synthetic CyGPUInspectorRS, without a game and without a GPU.
//
// It publishes a session in the directory, fills an event ring with a plausible frame (depth
// prepass, gbuffer, lighting, bloom chain, tonemap, UI) and answers the control pipe. This is how
// CyGPUInspectorApp can be exercised end to end on a machine with no game running, and how the
// IPC is regression tested.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/RingBuffer.hpp>
#include <CyGPUInspectorCore/SessionDirectory.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>
#include <CyGPUInspectorCore/SharedMemory.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	constexpr size_t kRingCapacity = 8u * 1024 * 1024;

	struct FakeShader
	{
		uint32_t id;
		cygi::ShaderStage stage;
		const char *purpose;
	};

	struct FakeResource
	{
		uint32_t id;
		const char *name;
		uint32_t format;
		uint32_t width;
		uint32_t height;
		uint32_t usage;
	};

	// A believable deferred renderer frame.
	const FakeShader kShaders[] = {
		{ 1, cygi::ShaderStage::vertex, "depth prepass VS" },
		{ 2, cygi::ShaderStage::pixel, "depth prepass PS" },
		{ 3, cygi::ShaderStage::vertex, "gbuffer VS" },
		{ 4, cygi::ShaderStage::pixel, "gbuffer PS" },
		{ 5, cygi::ShaderStage::compute, "lighting CS" },
		{ 6, cygi::ShaderStage::compute, "bloom downsample CS" },
		{ 7, cygi::ShaderStage::compute, "bloom blur CS" },
		{ 8, cygi::ShaderStage::pixel, "tonemap PS" },
		{ 9, cygi::ShaderStage::pixel, "ui PS" },
	};

	const FakeResource kResources[] = {
		{ 1, "SceneDepth", 40 /* d32_float */, 2560, 1440, cygi::kUsageDepthStencil | cygi::kUsageShaderResource },
		{ 2, "GBufferA", 28 /* r8g8b8a8_unorm */, 2560, 1440, cygi::kUsageRenderTarget | cygi::kUsageShaderResource },
		{ 3, "GBufferB", 28, 2560, 1440, cygi::kUsageRenderTarget | cygi::kUsageShaderResource },
		{ 4, "SceneColor", 10 /* r16g16b16a16_float */, 2560, 1440, cygi::kUsageRenderTarget | cygi::kUsageUnorderedAccess | cygi::kUsageShaderResource },
		{ 5, "BloomHalf", 10, 1280, 720, cygi::kUsageUnorderedAccess | cygi::kUsageShaderResource },
		{ 6, "BloomQuarter", 10, 640, 360, cygi::kUsageUnorderedAccess | cygi::kUsageShaderResource },
		{ 7, "BackBuffer", 28, 2560, 1440, cygi::kUsageRenderTarget | cygi::kUsageBackBuffer },
		{ 8, "GBufferC", 28, 2560, 1440, cygi::kUsageRenderTarget | cygi::kUsageShaderResource },
	};

	// pipeline id -> shader ids
	const uint32_t kPipelineShaders[][2] = {
		{ 1, 2 },   // pipeline 1: depth prepass
		{ 3, 4 },   // pipeline 2: gbuffer
		{ 5, 0 },   // pipeline 3: lighting
		{ 6, 0 },   // pipeline 4: bloom downsample
		{ 7, 0 },   // pipeline 5: bloom blur
		{ 8, 0 },   // pipeline 6: tonemap
		{ 9, 0 },   // pipeline 7: ui
	};

	// A real D3D11 device and a real shared texture: this is what lets the IPC tests prove the
	// GPU sharing path end to end, without a game and without ReShade.
	struct FakeGpu
	{
		ID3D11Device *device = nullptr;
		ID3D11DeviceContext *context = nullptr;
		ID3D11Texture2D *texture = nullptr;
		HANDLE share_handle = nullptr;
		uint32_t width = 256;
		uint32_t height = 256;

		// The buffers a deep capture hands over: a depth buffer shared through its typeless
		// family, and an HDR colour target going past 1, like the ones a game would send.
		ID3D11Texture2D *depth = nullptr;
		HANDLE depth_handle = nullptr;
		ID3D11Texture2D *hdr = nullptr;
		HANDLE hdr_handle = nullptr;

		HANDLE MakeShared(DXGI_FORMAT format, const void *data, uint32_t pitch, ID3D11Texture2D *&out)
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
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
			if (FAILED(device->CreateTexture2D(&desc, &initial, &out)))
				return nullptr;
			context->Flush();
			IDXGIResource *dxgi = nullptr;
			HANDLE handle = nullptr;
			if (SUCCEEDED(out->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgi))))
			{
				dxgi->GetSharedHandle(&handle);
				dxgi->Release();
			}
			return handle;
		}

		void CreateCaptureBuffers()
		{
			// Depth: a sphere in front of a floor, the far plane cleared to 1.
			std::vector<float> depth_values(static_cast<size_t>(width) * height, 1.0f);
			std::vector<uint16_t> hdr_values(static_cast<size_t>(width) * height * 4, 0);
			auto half = [](float value) {
				// Positive normal values only, which is all this pattern has.
				uint32_t bits = 0;
				std::memcpy(&bits, &value, 4);
				const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
				if (value <= 0.0f || exponent <= 0)
					return static_cast<uint16_t>(0);
				return static_cast<uint16_t>((exponent << 10) | ((bits >> 13) & 0x3FF));
			};
			for (uint32_t y = 0; y < height; ++y)
				for (uint32_t x = 0; x < width; ++x)
				{
					const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
					const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
					const float r2 = u * u + v * v;
					float d = 1.0f;
					if (r2 < 0.36f)
						d = 0.30f - 0.10f * std::sqrt(0.36f - r2);
					else if (v > 0.2f)
						d = 0.95f - 0.5f * (v - 0.2f);
					depth_values[y * width + x] = d;

					// A bright light in the middle, falling off: up to 3 times what a screen shows.
					const float light = 3.0f / (1.0f + 6.0f * r2);
					uint16_t *texel = &hdr_values[(static_cast<size_t>(y) * width + x) * 4];
					texel[0] = half(light);
					texel[1] = half(light * 0.7f + 0.05f);
					texel[2] = half(0.2f + 0.3f * (1.0f - static_cast<float>(y) / static_cast<float>(height)));
					texel[3] = half(1.0f);
				}
			depth_handle = MakeShared(DXGI_FORMAT_R32_TYPELESS, depth_values.data(), width * 4, depth);
			hdr_handle = MakeShared(DXGI_FORMAT_R16G16B16A16_FLOAT, hdr_values.data(), width * 8, hdr);
		}

		bool Create()
		{
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
			                             D3D11_SDK_VERSION, &device, nullptr, &context)))
			{
				if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
				                             D3D11_SDK_VERSION, &device, nullptr, &context)))
					return false;
			}

			// The pattern is deterministic so a reader can assert on exact pixels.
			std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
			for (uint32_t y = 0; y < height; ++y)
				for (uint32_t x = 0; x < width; ++x)
					pixels[y * width + x] = 0xFF000000u | (0x80u << 16) | (y << 8) | x; // ABGR in memory

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

			D3D11_SUBRESOURCE_DATA initial = {};
			initial.pSysMem = pixels.data();
			initial.SysMemPitch = width * 4;

			if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)))
				return false;

			// A shared surface is only visible to the other process once the producing device has
			// flushed: without this the receiver opens the texture but sees undefined content.
			context->Flush();

			IDXGIResource *dxgi = nullptr;
			if (FAILED(texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgi))))
				return false;
			const HRESULT hr = dxgi->GetSharedHandle(&share_handle);
			dxgi->Release();
			if (FAILED(hr) || share_handle == nullptr)
				return false;
			CreateCaptureBuffers();
			return true;
		}

		void Destroy()
		{
			if (texture != nullptr) { texture->Release(); texture = nullptr; }
			if (depth != nullptr) { depth->Release(); depth = nullptr; }
			if (hdr != nullptr) { hdr->Release(); hdr = nullptr; }
			if (context != nullptr) { context->Release(); context = nullptr; }
			if (device != nullptr) { device->Release(); device = nullptr; }
			share_handle = nullptr;
		}
	};

	std::vector<uint8_t> MakeFakeByteCode(uint32_t shader_id, cygi::ShaderStage stage)
	{
		// A structurally valid DXBC container so the standalone can parse stage and model.
		const uint32_t header_size = 4 + 16 + 4 + 4 + 4 + 4;
		const uint32_t chunk_payload = 64;
		std::vector<uint8_t> blob(header_size + 8 + chunk_payload, 0);

		std::memcpy(blob.data(), "DXBC", 4);
		const uint32_t one = 1;
		const uint32_t total_size = static_cast<uint32_t>(blob.size());
		const uint32_t chunk_count = 1;
		const uint32_t chunk_offset = header_size;
		std::memcpy(blob.data() + 20, &one, 4);
		std::memcpy(blob.data() + 24, &total_size, 4);
		std::memcpy(blob.data() + 28, &chunk_count, 4);
		std::memcpy(blob.data() + 32, &chunk_offset, 4);
		std::memcpy(blob.data() + chunk_offset, "SHEX", 4);
		std::memcpy(blob.data() + chunk_offset + 4, &chunk_payload, 4);

		uint32_t kind = 0;
		switch (stage)
		{
		case cygi::ShaderStage::pixel: kind = 0; break;
		case cygi::ShaderStage::vertex: kind = 1; break;
		case cygi::ShaderStage::compute: kind = 5; break;
		default: kind = 0; break;
		}
		const uint32_t version_token = (kind << 16) | (5u << 4) | 0u;
		std::memcpy(blob.data() + chunk_offset + 8, &version_token, 4);

		// Make each shader unique so the signatures differ.
		std::memcpy(blob.data() + chunk_offset + 12, &shader_id, 4);
		return blob;
	}
}

int main()
{
	const uint32_t process_id = GetCurrentProcessId();
	const uint32_t device_index = 0;

	char ring_name[cygi::kMaxObjectNameLength] = {};
	char signal_name[cygi::kMaxObjectNameLength] = {};
	char pipe_name[cygi::kMaxObjectNameLength] = {};
	cygi::MakeRingName(process_id, device_index, ring_name, sizeof(ring_name));
	cygi::MakeSignalName(process_id, device_index, signal_name, sizeof(signal_name));
	cygi::MakePipeName(process_id, device_index, pipe_name, sizeof(pipe_name));

	cygi::SharedMemory memory;
	if (!memory.Create(ring_name, cygi::RingMappingSize(kRingCapacity)))
	{
		std::printf("could not create the event ring\n");
		return 1;
	}

	cygi::RingWriter writer;
	if (!writer.Initialize(memory.Data(), kRingCapacity))
	{
		std::printf("could not initialize the event ring\n");
		return 1;
	}

	cygi::SharedEvent signal;
	signal.Create(signal_name);

	cygi::ControlPipeServer pipe;
	pipe.Start(pipe_name);

	const uint32_t capabilities = cygi::kCapSharedResource | cygi::kCapTimestampQueries |
		cygi::kCapDrawSkipping;

	cygi::SessionPublisher session;
	if (!session.Claim(process_id, device_index, cygi::GraphicsApi::d3d12, "FakeGame.exe", ring_name,
	                   signal_name, pipe_name, kRingCapacity, capabilities))
	{
		std::printf("could not claim a session slot\n");
		return 1;
	}

	auto publish_session_info = [&](cygi::TrackingLevel level) {
		cygi::SessionInfoRecord record = {};
		record.protocol_version = cygi::kProtocolVersion;
		record.process_id = process_id;
		record.api = cygi::GraphicsApi::d3d12;
		record.level = level;
		record.device_index = device_index;
		record.capability_flags = capabilities;
		record.timestamp_frequency = 10000000;
		std::strncpy(record.process_name, "FakeGame.exe", sizeof(record.process_name) - 1);
		std::strncpy(record.addon_version, cygi::kVersionString, sizeof(record.addon_version) - 1);
		std::strncpy(record.adapter_name, "Fake Adapter (no GPU involved)", sizeof(record.adapter_name) - 1);
		writer.Write(cygi::RecordType::session_info, &record, sizeof(record));
	};

	auto publish_static_data = [&]() {
		for (const FakeShader &shader : kShaders)
		{
			const std::vector<uint8_t> code = MakeFakeByteCode(shader.id, shader.stage);
			const cygi::Sha256Digest signature = cygi::Sha256::Hash(code.data(), code.size());

			cygi::ShaderCodeRecord record = {};
			record.shader_id = shader.id;
			record.stage = shader.stage;
			record.format = cygi::ShaderFormat::dxbc;
			record.code_size = static_cast<uint32_t>(code.size());
			record.shader_model = 0x50;
			std::memcpy(record.signature, signature.bytes.data(), sizeof(record.signature));
			std::memcpy(record.semantic_hash, signature.bytes.data(), sizeof(record.semantic_hash));
			writer.Write(cygi::RecordType::shader_code, &record, sizeof(record), code.data(),
				static_cast<uint32_t>(code.size()));
		}

		uint32_t pipeline_id = 0;
		for (const uint32_t *shaders : kPipelineShaders)
		{
			++pipeline_id;
			uint32_t ids[2] = { shaders[0], shaders[1] };
			const uint32_t count = ids[1] != 0 ? 2u : 1u;

			cygi::PipelineInfoRecord record = {};
			record.pipeline_id = pipeline_id;
			record.native_handle = 0xF000 + pipeline_id;
			record.shader_count = count;
			record.is_compute = count == 1 ? 1u : 0u;
			writer.Write(cygi::RecordType::pipeline_info, &record, sizeof(record), ids,
				count * static_cast<uint32_t>(sizeof(uint32_t)));
		}

		for (const FakeResource &resource : kResources)
		{
			cygi::ResourceInfoRecord record = {};
			record.resource_id = resource.id;
			record.native_handle = 0xA000 + resource.id;
			record.kind = cygi::ResourceKind::texture_2d;
			record.format = resource.format;
			record.width = resource.width;
			record.height = resource.height;
			record.depth_or_layers = 1;
			record.mip_levels = 1;
			record.samples = 1;
			record.usage_flags = resource.usage;
			writer.Write(cygi::RecordType::resource_info, &record, sizeof(record));
		}
	};

	publish_session_info(cygi::TrackingLevel::tracking);
	publish_static_data();

	std::printf("CyGPUInspectorFakeSession running as PID %u\n", process_id);
	std::printf("  ring : %s\n  pipe : %s\n", ring_name, pipe_name);
	std::printf("Connect CyGPUInspectorApp to \"FakeGame.exe\". Press Ctrl+C to stop.\n");

	FakeGpu gpu;
	const bool gpu_ready = gpu.Create();
	std::printf("shared texture: %s\n", gpu_ready ? "created" : "unavailable (no D3D11 device)");

	std::unordered_set<uint32_t> disabled_shaders;
	std::unordered_set<uint32_t> replaced_shaders;
	cygi::TrackingLevel level = cygi::TrackingLevel::tracking;

	// Deep capture, faked the same way the add-on does it: armed from the control pipe, recorded
	// for a few frames, then disarmed. It exists so the whole path — protocol, model, archive and
	// interface — can be exercised without a game.
	uint32_t deep_frames_left = 0;
	uint32_t deep_requested = 0;
	uint64_t deep_first_frame = 0;
	bool deep_bindings = true;
	bool deep_barriers = true;
	bool deep_buffers = false;
	uint32_t deep_draw_states = 0;
	uint32_t deep_binding_count = 0;
	uint32_t deep_barrier_count = 0;
	uint64_t frame_index = 0;

	for (;;)
	{
		// Control messages, exactly like the add-on handles them at present time.
		cygi::ControlMessage message;
		while (pipe.PopMessage(message))
		{
			switch (message.type)
			{
			case cygi::ControlType::hello:
			{
				const cygi::HelloRequest *request = message.As<cygi::HelloRequest>();
				cygi::HelloAck ack = {};
				ack.protocol_version = cygi::kProtocolVersion;
				ack.capability_flags = capabilities;
				ack.level = level;
				ack.accepted = (request != nullptr && request->protocol_version == cygi::kProtocolVersion) ? 1u : 0u;
				pipe.Send(cygi::ControlType::hello_ack, message.request_id, &ack, sizeof(ack));
				if (ack.accepted != 0)
				{
					std::printf("standalone connected (PID %u)\n", request->app_process_id);
					publish_session_info(level);
					publish_static_data();
				}
				break;
			}
			case cygi::ControlType::full_sync:
				publish_session_info(level);
				publish_static_data();
				pipe.SendAck(message.request_id, true);
				break;
			case cygi::ControlType::set_level:
				if (const cygi::SetLevelRequest *request = message.As<cygi::SetLevelRequest>())
				{
					level = request->level;
					publish_session_info(level);
					std::printf("tracking level -> %s\n", cygi::TrackingLevelName(level));
				}
				pipe.SendAck(message.request_id, true);
				break;
			case cygi::ControlType::shader_command:
				if (const cygi::ShaderCommandRequest *request = message.As<cygi::ShaderCommandRequest>())
				{
					switch (request->command)
					{
					case cygi::ShaderCommand::disable:
						disabled_shaders.insert(request->shader_id);
						std::printf("shader %u disabled\n", request->shader_id);
						break;
					case cygi::ShaderCommand::enable:
					case cygi::ShaderCommand::restore:
						disabled_shaders.erase(request->shader_id);
						std::printf("shader %u enabled\n", request->shader_id);
						break;
					default:
						break;
					}
					pipe.SendAck(message.request_id, true);
				}
				else
				{
					pipe.SendAck(message.request_id, false, "malformed request");
				}
				break;
			case cygi::ControlType::request_preview:
				if (const cygi::PreviewRequest *request = message.As<cygi::PreviewRequest>())
				{
					cygi::PreviewReadyRecord ready = {};
					ready.request_id = request->request_id;
					ready.resource_id = request->resource_id;

					if (!gpu_ready)
					{
						ready.status = cygi::PreviewStatus::sharing_unsupported;
					}
					else
					{
						ready.status = cygi::PreviewStatus::ready;
						ready.shared_handle = reinterpret_cast<uint64_t>(gpu.share_handle);
						ready.width = gpu.width;
						ready.height = gpu.height;
						ready.format = 28;      // r8g8b8a8_unorm
						ready.is_nt_handle = 0; // legacy DXGI handle, like a D3D11 game
					}

					writer.Write(cygi::RecordType::preview_ready, &ready, sizeof(ready));
					signal.Signal();
					pipe.SendAck(message.request_id, true);
					std::printf("preview requested for resource %u\n", request->resource_id);
				}
				else
				{
					pipe.SendAck(message.request_id, false, "malformed request");
				}
				break;
			case cygi::ControlType::stop_preview:
				pipe.SendAck(message.request_id, true);
				break;
			case cygi::ControlType::release_capture_buffers:
				std::printf("capture buffers released\n");
				pipe.SendAck(message.request_id, true);
				break;
			case cygi::ControlType::replace_shader:
				if (const cygi::ReplaceShaderRequest *request = message.As<cygi::ReplaceShaderRequest>())
				{
					const size_t code_size = message.BlobSize(sizeof(cygi::ReplaceShaderRequest));
					if (code_size < request->code_size || request->shader_id == 0)
					{
						pipe.SendAck(message.request_id, false, "malformed replacement");
						break;
					}
					replaced_shaders.insert(request->shader_id);
					std::printf("shader %u replaced with %zu bytes\n", request->shader_id, code_size);
					pipe.SendAck(message.request_id, true, "replaced in 1 pipeline");
				}
				else
				{
					pipe.SendAck(message.request_id, false, "malformed replacement");
				}
				break;
			case cygi::ControlType::capture_frame:
				if (const cygi::CaptureFrameRequest *request = message.As<cygi::CaptureFrameRequest>())
				{
					if (request->mode != cygi::CaptureMode::deep)
					{
						pipe.SendAck(message.request_id, true, "runtime capture needs no arming");
						break;
					}
					if (level == cygi::TrackingLevel::idle)
					{
						pipe.SendAck(message.request_id, false,
							"tracking is idle: there would be no frame to capture");
						break;
					}
					deep_frames_left = request->frame_count != 0 ? request->frame_count : 1;
					deep_requested = deep_frames_left;
					deep_bindings = request->include_bindings != 0;
					deep_barriers = request->include_barriers != 0;
					deep_buffers = request->include_buffers != 0;
					deep_first_frame = frame_index;
					std::printf("deep capture armed for %u frame(s)\n", deep_frames_left);
					pipe.SendAck(message.request_id, true);
				}
				else
				{
					pipe.SendAck(message.request_id, false, "malformed request");
				}
				break;
			case cygi::ControlType::ping:
				pipe.Send(cygi::ControlType::pong, message.request_id, nullptr, 0);
				break;
			default:
				pipe.SendAck(message.request_id, false, "not implemented in the fake session");
				break;
			}
		}

		if (level != cygi::TrackingLevel::idle)
		{
			cygi::FrameBeginRecord begin = {};
			begin.frame_index = frame_index;
			writer.Write(cygi::RecordType::frame_begin, &begin, sizeof(begin));

			std::vector<cygi::FrameEvent> events;
			uint32_t index = 0;
			uint32_t draws = 0;
			uint32_t dispatches = 0;

			auto push = [&](cygi::EventKind kind, uint32_t pipeline, uint32_t primary, uint32_t secondary,
			                uint32_t a, uint32_t b) {
				cygi::FrameEvent event = {};
				event.index = index++;
				event.kind = kind;
				event.pipeline_id = pipeline;
				event.primary_resource = primary;
				event.secondary_resource = secondary;
				event.a = a;
				event.b = b;

				uint32_t shader_a = pipeline != 0 ? kPipelineShaders[pipeline - 1][0] : 0;
				uint32_t shader_b = pipeline != 0 ? kPipelineShaders[pipeline - 1][1] : 0;
				if (disabled_shaders.count(shader_a) != 0 || disabled_shaders.count(shader_b) != 0)
					event.flags |= cygi::kEventSkipped;
				if (replaced_shaders.count(shader_a) != 0 || replaced_shaders.count(shader_b) != 0)
					event.flags |= cygi::kEventReplaced;

				if (kind == cygi::EventKind::draw_indexed || kind == cygi::EventKind::draw)
					++draws;
				else if (kind == cygi::EventKind::dispatch)
					++dispatches;

				events.push_back(event);
			};

			// A render target binding: this is what tells the standalone about a GBuffer, since a
			// draw event only carries slot 0.
			auto bind = [&](uint32_t rt0, uint32_t depth, uint32_t count, uint32_t rt1, uint32_t rt2) {
				cygi::FrameEvent event = {};
				event.index = index++;
				event.kind = cygi::EventKind::bind_render_targets;
				event.primary_resource = rt0;
				event.secondary_resource = depth;
				event.a = count;
				event.b = rt1;
				event.c = rt2;
				events.push_back(event);
			};

			// Depth prepass: depth bound, no colour target at all.
			bind(0, 1, 0, 0, 0);
			push(cygi::EventKind::clear_depth_stencil, 0, 1, 0, 0, 0);
			for (int i = 0; i < 800; ++i)
				push(cygi::EventKind::draw_indexed, 1, 0, 1, 900 + i, 1);

			// GBuffer: three colour targets plus the depth buffer written by the prepass.
			bind(2, 1, 3, 3, 8);
			push(cygi::EventKind::clear_render_target, 0, 2, 0, 0, 0);
			for (int i = 0; i < 1200; ++i)
				push(cygi::EventKind::draw_indexed, 2, 2, 1, 1200 + i, 1);

			push(cygi::EventKind::dispatch, 3, 4, 1, 160, 90);              // lighting reads depth
			push(cygi::EventKind::dispatch, 4, 5, 4, 80, 45);               // bloom downsample
			push(cygi::EventKind::dispatch, 5, 6, 5, 40, 23);               // bloom blur

			// Tonemap and UI write the back buffer.
			bind(7, 0, 1, 0, 0);
			push(cygi::EventKind::draw, 6, 7, 4, 3, 1);
			for (int i = 0; i < 40; ++i)
				push(cygi::EventKind::draw, 7, 7, 0, 6, 1);
			push(cygi::EventKind::present, 0, 7, 0, 0, 0);

			// Chunk exactly like the add-on does.
			constexpr uint32_t kChunk = 4096;
			for (size_t offset = 0; offset < events.size(); offset += kChunk)
			{
				const uint32_t count = static_cast<uint32_t>(
					events.size() - offset < kChunk ? events.size() - offset : kChunk);
				cygi::FrameEventsRecord record = {};
				record.frame_index = frame_index;
				record.event_count = count;
				record.first_index = static_cast<uint32_t>(offset);
				writer.Write(cygi::RecordType::frame_events, &record, sizeof(record), events.data() + offset,
					count * static_cast<uint32_t>(sizeof(cygi::FrameEvent)));
			}

			// GPU timings, as the add-on publishes them: a few frames late, with their own frame.
			{
				std::vector<cygi::TimingResult> timings;
				uint64_t start = 0;
				for (const cygi::FrameEvent &event : events)
				{
					if (event.kind != cygi::EventKind::dispatch && event.pipeline_id == 0)
						continue;
					if ((event.index % 256) != 0 && event.kind != cygi::EventKind::dispatch)
						continue;

					cygi::TimingResult timing = {};
					timing.event_index = event.index;
					timing.pipeline_id = event.pipeline_id;
					// 10 MHz frequency: 1234 ticks is 0.1234 ms.
					timing.gpu_ticks = 1000 + (event.index % 100) * 10;
					// Back to back, as a single queue executes them.
					timing.gpu_start = start;
					start += timing.gpu_ticks;
					timings.push_back(timing);
				}

				if (!timings.empty())
				{
					// 10 MHz clock, one frame every 16.6 ms, busy for as long as its measurements
					// add up to: what a GPU that finishes early and waits for the next frame looks like.
					cygi::FrameGpuSpanRecord span = {};
					span.frame_index = frame_index > 3 ? frame_index - 3 : 0;
					span.gpu_begin = 1000000000ull + span.frame_index * 166000ull;
					span.gpu_end = span.gpu_begin + start;
					writer.Write(cygi::RecordType::frame_gpu_span, &span, sizeof(span));

					cygi::TimingResultsRecord record = {};
					record.frame_index = frame_index > 3 ? frame_index - 3 : 0;
					record.count = static_cast<uint32_t>(timings.size());
					record.result_size = static_cast<uint32_t>(sizeof(cygi::TimingResult));
					writer.Write(cygi::RecordType::timing_results, &record, sizeof(record), timings.data(),
						static_cast<uint32_t>(timings.size() * sizeof(cygi::TimingResult)));
				}
			}

			cygi::FrameEndRecord end = {};
			end.frame_index = frame_index;
			end.draw_count = draws;
			end.dispatch_count = dispatches;
			end.event_count = static_cast<uint32_t>(events.size());
			end.cpu_frame_ms = 16.6f;
			end.addon_cpu_ms = 0.12f;
			writer.Write(cygi::RecordType::frame_end, &end, sizeof(end));

			// Deep capture payload, published after the frame it describes, exactly as the add-on
			// does: the standalone has to know the event timeline before the state can index into it.
			if (deep_frames_left != 0)
			{
				// The names the game gave its resources, as the add-on reads them during a capture.
				if (frame_index == deep_first_frame)
					for (const FakeResource &resource : kResources)
					{
						cygi::ResourceNamedRecord named = {};
						named.resource_id = resource.id;
						named.name_length = static_cast<uint32_t>(std::strlen(resource.name));
						writer.Write(cygi::RecordType::resource_named, &named, sizeof(named),
							reinterpret_cast<const uint8_t *>(resource.name), named.name_length);
					}

				// Pipeline state, sent once per pipeline the first time a capture needs it.
				if (frame_index == deep_first_frame)
				{
					for (uint32_t pipeline = 1; pipeline <= static_cast<uint32_t>(std::size(kPipelineShaders));
					     ++pipeline)
					{
						cygi::PipelineStateRecord state = {};
						state.pipeline_id = pipeline;
						state.topology = 4;                    // triangle list
						state.depth_enable = pipeline <= 2 ? 1u : 0u;
						state.depth_write = pipeline == 1 ? 1u : 0u;
						state.depth_func = 4;                  // less or equal
						state.cull_mode = 2;                   // back
						state.fill_mode = 0;
						state.blend_enable_mask = pipeline == 7 ? 1u : 0u;
						state.render_target_write_mask = 0xF;
						state.sample_count = 1;
						state.render_target_formats[0] = 28;   // R8G8B8A8_UNORM
						state.depth_stencil_format = 45;
						state.known_fields = 0xFF;
						writer.Write(cygi::RecordType::pipeline_state, &state, sizeof(state));
					}
				}

				for (const cygi::FrameEvent &event : events)
				{
					const bool is_draw = event.kind == cygi::EventKind::draw ||
						event.kind == cygi::EventKind::draw_indexed;
					const bool is_dispatch = event.kind == cygi::EventKind::dispatch;
					if (!is_draw && !is_dispatch)
						continue;
					// One in sixteen, so a fake frame stays small while still covering every pass.
					if ((event.index % 16) != 0)
						continue;

					std::vector<cygi::DrawBinding> bindings;
					if (deep_bindings)
					{
						auto bind_slot = [&](cygi::SlotKind kind, cygi::ShaderStage stage, uint16_t slot,
						                     uint32_t resource, uint32_t size) {
							cygi::DrawBinding binding = {};
							binding.kind = static_cast<uint8_t>(kind);
							binding.stage = static_cast<uint8_t>(stage);
							binding.slot = slot;
							binding.resource_id = resource;
							binding.size = size;
							bindings.push_back(binding);
						};

						const cygi::ShaderStage stage = is_dispatch
							? cygi::ShaderStage::compute : cygi::ShaderStage::pixel;
						bind_slot(cygi::SlotKind::constant_buffer, stage, 0, 9, 256);
						bind_slot(cygi::SlotKind::shader_resource, stage, 0, 2, 0);
						bind_slot(cygi::SlotKind::shader_resource, stage, 1, 1, 0);
						bind_slot(cygi::SlotKind::sampler, cygi::ShaderStage::unknown, 0, 0, 0);
						if (is_draw)
						{
							bind_slot(cygi::SlotKind::vertex_buffer, cygi::ShaderStage::unknown, 0, 10, 0);
							bind_slot(cygi::SlotKind::index_buffer, cygi::ShaderStage::unknown, 0, 11, 2);
							bind_slot(cygi::SlotKind::render_target, cygi::ShaderStage::unknown, 0,
								event.primary_resource, 0);
							if (event.secondary_resource != 0)
								bind_slot(cygi::SlotKind::depth_stencil, cygi::ShaderStage::unknown, 0,
									event.secondary_resource, 0);
						}
						else
						{
							bind_slot(cygi::SlotKind::unordered_access, stage, 0,
								event.primary_resource, 0);
						}
					}

					cygi::DrawStateRecord state = {};
					state.frame_index = frame_index;
					state.event_index = event.index;
					state.pipeline_id = event.pipeline_id;
					state.binding_count = static_cast<uint32_t>(bindings.size());
					state.topology = 4;
					state.index_format = is_draw ? 2u : 0u;
					state.viewport_count = 1;
					state.viewport[2] = 1920.0f;
					state.viewport[3] = 1080.0f;
					state.depth_range[1] = 1.0f;
					state.scissor[2] = 1920;
					state.scissor[3] = 1080;
					state.flags = cygi::kDrawStateViewportValid | cygi::kDrawStateScissorValid;
					writer.Write(cygi::RecordType::draw_state, &state, sizeof(state), bindings.data(),
						static_cast<uint32_t>(bindings.size() * sizeof(cygi::DrawBinding)));

					++deep_draw_states;
					deep_binding_count += static_cast<uint32_t>(bindings.size());
				}

				if (deep_barriers)
				{
					// One transition where the depth prepass hands the depth buffer to the lighting
					// pass, which is the dependency the frame graph also derives.
					for (const cygi::FrameEvent &event : events)
					{
						if (event.kind != cygi::EventKind::dispatch)
							continue;

						cygi::BarrierEntry entry = {};
						entry.resource_id = 1;
						entry.old_state = 0x10;   // depth write
						entry.new_state = 0x40;   // shader resource
						cygi::BarrierSetRecord record = {};
						record.frame_index = frame_index;
						record.event_index = event.index;
						record.count = 1;
						writer.Write(cygi::RecordType::barrier_set, &record, sizeof(record), &entry,
							static_cast<uint32_t>(sizeof(entry)));
						++deep_barrier_count;
						break;
					}
				}

				--deep_frames_left;

				// The buffers of the last captured frame, before the state that says it finished,
				// in the order the frame first used them: depth, GBuffer A, scene colour, and one
				// whose state no barrier revealed, as the add-on reports it on D3D12.
				if (deep_frames_left == 0 && deep_buffers)
				{
					struct Buffer { uint32_t id; HANDLE handle; uint32_t format; uint32_t source; uint32_t roles; uint32_t event; };
					const Buffer buffers[] = {
						{ 1, gpu.depth_handle, 39, 40, cygi::kBufferRoleDepthStencil, 0 },
						{ 2, gpu.share_handle, 28, 28, cygi::kBufferRoleRenderTarget, 802 },
						{ 4, gpu.hdr_handle, 10, 10, cygi::kBufferRoleRenderTarget | cygi::kBufferRoleUnorderedAccess, 2003 },
						{ 3, nullptr, 28, 28, cygi::kBufferRoleRenderTarget, 802 },
					};
					uint32_t position = 0;
					for (const Buffer &buffer : buffers)
					{
						cygi::CaptureBufferRecord record = {};
						record.first_frame = deep_first_frame;
						record.frame_index = frame_index;
						record.resource_id = buffer.id;
						record.status = buffer.handle != nullptr ? cygi::PreviewStatus::ready
						                                         : cygi::PreviewStatus::state_unknown;
						record.shared_handle = reinterpret_cast<uint64_t>(buffer.handle);
						record.width = gpu.width;
						record.height = gpu.height;
						record.format = buffer.format;
						record.source_format = buffer.source;
						record.roles = buffer.roles;
						record.first_event = buffer.event;
						record.index = position++;
						record.count = static_cast<uint32_t>(std::size(buffers));
						writer.Write(cygi::RecordType::capture_buffer, &record, sizeof(record));
					}
				}

				cygi::CaptureStateRecord capture = {};
				capture.stage = deep_frames_left == 0 ? cygi::CaptureStage::finished
				                                      : cygi::CaptureStage::capturing;
				capture.mode = cygi::CaptureMode::deep;
				capture.first_frame = deep_first_frame;
				capture.frames_requested = deep_requested;
				capture.frames_done = deep_requested - deep_frames_left;
				capture.draw_states_recorded = deep_draw_states;
				capture.bindings_recorded = deep_binding_count;
				capture.barriers_recorded = deep_barrier_count;
				writer.Write(cygi::RecordType::capture_state, &capture, sizeof(capture));
			}


			if ((frame_index % 60) == 0)
			{
				cygi::StatsRecord stats = {};
				stats.frame_index = frame_index;
				stats.bytes_written = writer.BytesWritten();
				stats.records_written = writer.RecordsWritten();
				stats.dropped_bytes = writer.DroppedBytes();
				stats.tracked_shaders = static_cast<uint32_t>(std::size(kShaders));
				stats.tracked_pipelines = static_cast<uint32_t>(std::size(kPipelineShaders));
				stats.tracked_resources = static_cast<uint32_t>(std::size(kResources));
				writer.Write(cygi::RecordType::stats, &stats, sizeof(stats));
			}
		}

		signal.Signal();
		session.Heartbeat(frame_index, level);
		++frame_index;
		Sleep(16);
	}
}
