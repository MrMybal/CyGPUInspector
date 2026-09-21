// CyGPUInspectorApp — a texture of the game process, opened in ours.
//
// The add-on copies the resource the user selected into a shared D3D texture and sends us its
// share handle. Opening it here gives a shader resource view over the very same GPU memory: no
// readback, no copy through system memory, no IPC bandwidth for the pixels.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace cygi
{
	class SharedTexture
	{
	public:
		~SharedTexture();

		SharedTexture() = default;
		SharedTexture(const SharedTexture &) = delete;
		SharedTexture &operator=(const SharedTexture &) = delete;

		// `handle` is what PreviewReadyRecord carries: a legacy DXGI handle (D3D11 games) or an
		// NT handle already duplicated into this process (D3D12 games).
		bool Open(ID3D11Device *device, uint64_t handle, bool nt_handle, uint32_t format,
		          uint32_t width, uint32_t height);
		void Close();

		bool IsValid() const { return m_srv != nullptr; }
		ID3D11ShaderResourceView *View() const { return m_srv; }
		// The underlying texture, for the rare paths that need the pixels (export, save to disk).
		ID3D11Texture2D *Texture() const { return m_texture; }
		uint64_t Handle() const { return m_handle; }
		uint32_t Width() const { return m_width; }
		uint32_t Height() const { return m_height; }
		uint32_t Format() const { return m_format; }
		const std::string &LastError() const { return m_last_error; }

	private:
		ID3D11Texture2D *m_texture = nullptr;
		ID3D11ShaderResourceView *m_srv = nullptr;
		uint64_t m_handle = 0;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint32_t m_format = 0;
		bool m_is_nt_handle = false;
		std::string m_last_error;
	};
}
