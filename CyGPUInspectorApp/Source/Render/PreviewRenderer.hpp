// CyGPUInspectorApp — the display pass of a preview.
//
// The add-on shares a texture as it is, in the game's own format. Turning that into something a
// human can look at — one channel at a time, a depth buffer linearised, a range stretched — is a
// rendering job, and it is done here rather than in the game: the standalone already has a D3D11
// device and a view on the texture, so the add-on keeps carrying no shader at all.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11RenderTargetView;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11VertexShader;

namespace cygi
{
	enum class PreviewChannelMode : uint32_t
	{
		color = 0,
		red,
		green,
		blue,
		alpha,
		depth_raw,
		depth_linear,
	};
	const char *PreviewChannelModeName(PreviewChannelMode mode);

	struct PreviewSettings
	{
		PreviewChannelMode mode = PreviewChannelMode::color;
		float range_min = 0.0f;
		float range_max = 1.0f;
		float near_plane = 0.1f;
		float far_plane = 1000.0f;
		bool reverse_z = false;
		bool checkerboard_alpha = true;
	};

	class PreviewRenderer
	{
	public:
		~PreviewRenderer();

		PreviewRenderer() = default;
		PreviewRenderer(const PreviewRenderer &) = delete;
		PreviewRenderer &operator=(const PreviewRenderer &) = delete;

		bool Initialize(ID3D11Device *device, ID3D11DeviceContext *context);
		void Shutdown();

		bool IsReady() const { return m_pixel_shader != nullptr; }
		const std::string &LastError() const { return m_last_error; }

		// Renders `source` through the settings and returns a view on the result, or nullptr.
		ID3D11ShaderResourceView *Render(ID3D11ShaderResourceView *source, uint32_t width, uint32_t height,
		                                 const PreviewSettings &settings);

		// Writes the last rendered image — exactly what is on screen, channels and range applied —
		// to a PNG file. One read back, in this process, when asked: never the game's.
		bool SavePng(const std::wstring &path, std::string &error);

		// The last rendered image as 8 bit BGRA with an opaque alpha, for WritePng.
		bool ReadPixels(std::vector<uint8_t> &bgra, uint32_t &width, uint32_t &height, std::string &error);

	private:
		bool EnsureTarget(uint32_t width, uint32_t height);
		void ReleaseTarget();

		ID3D11Device *m_device = nullptr;
		ID3D11DeviceContext *m_context = nullptr;
		ID3D11VertexShader *m_vertex_shader = nullptr;
		ID3D11PixelShader *m_pixel_shader = nullptr;
		ID3D11Buffer *m_constants = nullptr;
		ID3D11SamplerState *m_sampler = nullptr;

		ID3D11Texture2D *m_target = nullptr;
		ID3D11RenderTargetView *m_target_rtv = nullptr;
		ID3D11ShaderResourceView *m_target_srv = nullptr;
		uint32_t m_width = 0;
		uint32_t m_height = 0;

		std::string m_last_error;
	};

	// Encodes 8 bit BGRA pixels as a PNG file. Needs no device, so any thread can do it.
	bool WritePng(const std::wstring &path, const std::vector<uint8_t> &bgra, uint32_t width, uint32_t height,
	              std::string &error);

	// Loads an image file (PNG, or anything else the Windows Imaging Component reads) into a
	// texture, for showing a file that was saved earlier. nullptr on failure.
	ID3D11ShaderResourceView *LoadImageFile(ID3D11Device *device, const std::wstring &path, uint32_t &width,
	                                        uint32_t &height, std::string &error);
}
