// CyGPUInspectorApp — the display pass of a preview.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "PreviewRenderer.hpp"

#include <CyGPUInspectorDecompiler/Disassembler.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>
#include <vector>
#include <wincodec.h>

#include <cstring>

namespace cygi
{
	namespace
	{
		// Compiled at start-up with the same compiler the tool offers its users, which is a small
		// but real proof that the compilation path works.
		const char *const kPreviewShader = R"(
cbuffer PreviewParams : register(b0)
{
    uint  Mode;
    float RangeMin;
    float RangeMax;
    float NearPlane;
    float FarPlane;
    uint  ReverseZ;
    uint  Checkerboard;
    uint  Padding;
};

Texture2D<float4> Source : register(t0);
SamplerState      PointClamp : register(s0);

struct Varyings
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

Varyings VSMain(uint id : SV_VertexID)
{
    Varyings output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float Remap(float value)
{
    float span = max(RangeMax - RangeMin, 1e-6f);
    return saturate((value - RangeMin) / span);
}

float4 PSMain(Varyings input) : SV_Target
{
    float4 texel = Source.SampleLevel(PointClamp, input.uv, 0.0f);

    if (Mode == 1) { float v = Remap(texel.r); return float4(v, v, v, 1.0f); }
    if (Mode == 2) { float v = Remap(texel.g); return float4(v, v, v, 1.0f); }
    if (Mode == 3) { float v = Remap(texel.b); return float4(v, v, v, 1.0f); }
    if (Mode == 4)
    {
        float v = Remap(texel.a);
        if (Checkerboard != 0)
        {
            // Show the alpha over a checkerboard so full transparency is not just black.
            float2 cell = floor(input.position.xy / 8.0f);
            float pattern = fmod(cell.x + cell.y, 2.0f) * 0.15f + 0.25f;
            return float4(lerp(float3(pattern, pattern, pattern), float3(v, v, v), v), 1.0f);
        }
        return float4(v, v, v, 1.0f);
    }
    if (Mode == 5) { float v = Remap(texel.r); return float4(v, v, v, 1.0f); }   // raw depth
    if (Mode == 6)
    {
        // Linearised depth. A reverse-Z projection stores 1 at the near plane.
        float depth = texel.r;
        if (ReverseZ != 0)
            depth = 1.0f - depth;

        float denominator = FarPlane - depth * (FarPlane - NearPlane);
        float linear_depth = (abs(denominator) < 1e-6f) ? FarPlane
                                                        : (NearPlane * FarPlane) / denominator;
        float v = saturate((linear_depth - NearPlane) / max(FarPlane - NearPlane, 1e-6f));
        return float4(v, v, v, 1.0f);
    }

    float3 color = float3(Remap(texel.r), Remap(texel.g), Remap(texel.b));
    return float4(color, 1.0f);
}
)";

		struct PreviewConstants
		{
			uint32_t mode;
			float range_min;
			float range_max;
			float near_plane;
			float far_plane;
			uint32_t reverse_z;
			uint32_t checkerboard;
			uint32_t padding;
		};
		static_assert(sizeof(PreviewConstants) % 16 == 0, "constant buffers are 16 byte aligned");
	}

	const char *PreviewChannelModeName(PreviewChannelMode mode)
	{
		switch (mode)
		{
		case PreviewChannelMode::color: return "RGB";
		case PreviewChannelMode::red: return "R";
		case PreviewChannelMode::green: return "G";
		case PreviewChannelMode::blue: return "B";
		case PreviewChannelMode::alpha: return "A";
		case PreviewChannelMode::depth_raw: return "Depth (raw)";
		case PreviewChannelMode::depth_linear: return "Depth (linearised)";
		default: return "?";
		}
	}

	PreviewRenderer::~PreviewRenderer()
	{
		Shutdown();
	}

	bool PreviewRenderer::Initialize(ID3D11Device *device, ID3D11DeviceContext *context)
	{
		Shutdown();
		if (device == nullptr || context == nullptr)
		{
			m_last_error = "no device";
			return false;
		}

		m_device = device;
		m_context = context;

		const CompileResult vertex = CompileHlsl(kPreviewShader, "VSMain", "vs_5_0", "preview.hlsl");
		if (!vertex.ok)
		{
			m_last_error = "preview vertex shader: " + vertex.error + " " + vertex.messages;
			return false;
		}

		const CompileResult pixel = CompileHlsl(kPreviewShader, "PSMain", "ps_5_0", "preview.hlsl");
		if (!pixel.ok)
		{
			m_last_error = "preview pixel shader: " + pixel.error + " " + pixel.messages;
			return false;
		}

		if (FAILED(device->CreateVertexShader(vertex.byte_code.data(), vertex.byte_code.size(), nullptr,
		                                      &m_vertex_shader)) ||
		    FAILED(device->CreatePixelShader(pixel.byte_code.data(), pixel.byte_code.size(), nullptr,
		                                     &m_pixel_shader)))
		{
			m_last_error = "the preview shaders could not be created";
			Shutdown();
			return false;
		}

		D3D11_BUFFER_DESC constants = {};
		constants.ByteWidth = sizeof(PreviewConstants);
		constants.Usage = D3D11_USAGE_DYNAMIC;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&constants, nullptr, &m_constants)))
		{
			m_last_error = "the preview constant buffer could not be created";
			Shutdown();
			return false;
		}

		D3D11_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // a preview must not invent pixels
		sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		if (FAILED(device->CreateSamplerState(&sampler, &m_sampler)))
		{
			m_last_error = "the preview sampler could not be created";
			Shutdown();
			return false;
		}

		m_last_error.clear();
		return true;
	}

	void PreviewRenderer::ReleaseTarget()
	{
		if (m_target_srv != nullptr) { m_target_srv->Release(); m_target_srv = nullptr; }
		if (m_target_rtv != nullptr) { m_target_rtv->Release(); m_target_rtv = nullptr; }
		if (m_target != nullptr) { m_target->Release(); m_target = nullptr; }
		m_width = 0;
		m_height = 0;
	}

	void PreviewRenderer::Shutdown()
	{
		ReleaseTarget();
		if (m_sampler != nullptr) { m_sampler->Release(); m_sampler = nullptr; }
		if (m_constants != nullptr) { m_constants->Release(); m_constants = nullptr; }
		if (m_pixel_shader != nullptr) { m_pixel_shader->Release(); m_pixel_shader = nullptr; }
		if (m_vertex_shader != nullptr) { m_vertex_shader->Release(); m_vertex_shader = nullptr; }
		m_device = nullptr;
		m_context = nullptr;
	}

	bool PreviewRenderer::EnsureTarget(uint32_t width, uint32_t height)
	{
		if (m_target != nullptr && m_width == width && m_height == height)
			return true;

		ReleaseTarget();
		if (width == 0 || height == 0)
			return false;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_target)) ||
		    FAILED(m_device->CreateRenderTargetView(m_target, nullptr, &m_target_rtv)) ||
		    FAILED(m_device->CreateShaderResourceView(m_target, nullptr, &m_target_srv)))
		{
			m_last_error = "the preview target could not be created";
			ReleaseTarget();
			return false;
		}

		m_width = width;
		m_height = height;
		return true;
	}

	ID3D11ShaderResourceView *PreviewRenderer::Render(ID3D11ShaderResourceView *source, uint32_t width,
	                                                  uint32_t height, const PreviewSettings &settings)
	{
		if (!IsReady() || source == nullptr || !EnsureTarget(width, height))
			return nullptr;

		PreviewConstants constants = {};
		constants.mode = static_cast<uint32_t>(settings.mode);
		constants.range_min = settings.range_min;
		constants.range_max = settings.range_max;
		constants.near_plane = settings.near_plane;
		constants.far_plane = settings.far_plane;
		constants.reverse_z = settings.reverse_z ? 1u : 0u;
		constants.checkerboard = settings.checkerboard_alpha ? 1u : 0u;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(m_context->Map(m_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return nullptr;
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		m_context->Unmap(m_constants, 0);

		// The ImGui backend sets its own state every frame, so nothing needs to be restored here
		// beyond unbinding what we bound: leaving the shared texture bound would stop the next
		// copy from the game process.
		const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width),
			static_cast<float>(height), 0.0f, 1.0f };
		m_context->RSSetViewports(1, &viewport);
		m_context->OMSetRenderTargets(1, &m_target_rtv, nullptr);
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_context->IASetInputLayout(nullptr);
		m_context->VSSetShader(m_vertex_shader, nullptr, 0);
		m_context->PSSetShader(m_pixel_shader, nullptr, 0);
		m_context->PSSetConstantBuffers(0, 1, &m_constants);
		m_context->PSSetShaderResources(0, 1, &source);
		m_context->PSSetSamplers(0, 1, &m_sampler);

		// One fullscreen triangle, generated from SV_VertexID: no vertex buffer needed.
		m_context->Draw(3, 0);

		ID3D11ShaderResourceView *const none = nullptr;
		m_context->PSSetShaderResources(0, 1, &none);
		ID3D11RenderTargetView *const no_target = nullptr;
		m_context->OMSetRenderTargets(1, &no_target, nullptr);

		return m_target_srv;
	}

	bool PreviewRenderer::SavePng(const std::wstring &path, std::string &error)
	{
		std::vector<uint8_t> pixels;
		uint32_t width = 0;
		uint32_t height = 0;
		return ReadPixels(pixels, width, height, error) && WritePng(path, pixels, width, height, error);
	}

	bool PreviewRenderer::ReadPixels(std::vector<uint8_t> &pixels, uint32_t &width, uint32_t &height,
	                                 std::string &error)
	{
		if (m_target == nullptr || m_device == nullptr || m_context == nullptr)
		{
			error = "nothing has been displayed yet";
			return false;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		m_target->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;

		ID3D11Texture2D *staging = nullptr;
		if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging)))
		{
			error = "could not create a staging texture";
			return false;
		}
		m_context->CopyResource(staging, m_target);

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
		{
			staging->Release();
			error = "could not read the image back";
			return false;
		}

		// BGRA with an opaque alpha: what every PNG encoder takes natively, and what was on screen
		// — the display pass composites alpha itself, so a zero in the game's alpha channel must
		// not turn into a transparent file.
		width = desc.Width;
		height = desc.Height;
		pixels.assign(static_cast<size_t>(width) * height * 4, 0);
		for (uint32_t y = 0; y < height; ++y)
		{
			const uint8_t *row = static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
			uint8_t *out = pixels.data() + static_cast<size_t>(y) * width * 4;
			for (uint32_t x = 0; x < width; ++x)
			{
				out[x * 4 + 0] = row[x * 4 + 2];
				out[x * 4 + 1] = row[x * 4 + 1];
				out[x * 4 + 2] = row[x * 4 + 0];
				out[x * 4 + 3] = 255;
			}
		}
		m_context->Unmap(staging, 0);
		staging->Release();
		return true;
	}

	bool WritePng(const std::wstring &path, const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
	              std::string &error)
	{
		if (pixels.size() < static_cast<size_t>(width) * height * 4 || width == 0 || height == 0)
		{
			error = "no pixels";
			return false;
		}

		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool uninitialize = SUCCEEDED(com);

		bool ok = false;
		IWICImagingFactory *factory = nullptr;
		IWICStream *stream = nullptr;
		IWICBitmapEncoder *encoder = nullptr;
		IWICBitmapFrameEncode *frame = nullptr;
		IPropertyBag2 *properties = nullptr;
		do
		{
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			                            IID_PPV_ARGS(&factory))))
			{
				error = "Windows Imaging Component is not available";
				break;
			}
			if (FAILED(factory->CreateStream(&stream)) ||
			    FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
			{
				error = "could not create the file";
				break;
			}
			if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
			    FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
			    FAILED(encoder->CreateNewFrame(&frame, &properties)) ||
			    FAILED(frame->Initialize(properties)) ||
			    FAILED(frame->SetSize(width, height)))
			{
				error = "the PNG encoder refused the image";
				break;
			}
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
			if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA ||
			    FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE *>(pixels.data()))) ||
			    FAILED(frame->Commit()) || FAILED(encoder->Commit()))
			{
				error = "writing the PNG failed";
				break;
			}
			ok = true;
		} while (false);

		if (properties != nullptr) properties->Release();
		if (frame != nullptr) frame->Release();
		if (encoder != nullptr) encoder->Release();
		if (stream != nullptr) stream->Release();
		if (factory != nullptr) factory->Release();
		if (uninitialize)
			CoUninitialize();
		return ok;
	}

	ID3D11ShaderResourceView *LoadImageFile(ID3D11Device *device, const std::wstring &path, uint32_t &width,
	                                        uint32_t &height, std::string &error)
	{
		width = 0;
		height = 0;
		if (device == nullptr)
		{
			error = "no device";
			return nullptr;
		}

		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool uninitialize = SUCCEEDED(com);

		std::vector<uint8_t> pixels;
		IWICImagingFactory *factory = nullptr;
		IWICBitmapDecoder *decoder = nullptr;
		IWICBitmapFrameDecode *frame = nullptr;
		IWICFormatConverter *converter = nullptr;
		do
		{
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			{
				error = "Windows Imaging Component is not available";
				break;
			}
			if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
			                                              WICDecodeMetadataCacheOnDemand, &decoder)) ||
			    FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
			    FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
			                                 0.0, WICBitmapPaletteTypeCustom)))
			{
				error = "the file could not be read";
				break;
			}
			UINT w = 0;
			UINT h = 0;
			converter->GetSize(&w, &h);
			pixels.resize(static_cast<size_t>(w) * h * 4);
			if (w == 0 || h == 0 ||
			    FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
			{
				pixels.clear();
				error = "the file could not be decoded";
				break;
			}
			width = w;
			height = h;
		} while (false);

		if (converter != nullptr) converter->Release();
		if (frame != nullptr) frame->Release();
		if (decoder != nullptr) decoder->Release();
		if (factory != nullptr) factory->Release();
		if (uninitialize)
			CoUninitialize();
		if (pixels.empty())
			return nullptr;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = pixels.data();
		data.SysMemPitch = width * 4;
		ID3D11Texture2D *texture = nullptr;
		ID3D11ShaderResourceView *view = nullptr;
		if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)))
		{
			device->CreateShaderResourceView(texture, nullptr, &view);
			texture->Release();
		}
		if (view == nullptr)
			error = "the texture could not be created";
		return view;
	}
}
