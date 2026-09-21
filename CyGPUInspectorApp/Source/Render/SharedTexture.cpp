// CyGPUInspectorApp — a texture of the game process, opened in ours.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "SharedTexture.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11_1.h>

#include <cstdio>

namespace cygi
{
	namespace
	{
		std::string HResultText(const char *what, HRESULT hr)
		{
			char buffer[160];
			std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
			return buffer;
		}
	}

	SharedTexture::~SharedTexture()
	{
		Close();
	}

	bool SharedTexture::Open(ID3D11Device *device, uint64_t handle, bool nt_handle, uint32_t format,
	                         uint32_t width, uint32_t height)
	{
		Close();

		// An NT handle is this process's to close from here on, whether it opens or not.
		m_handle = handle;
		m_is_nt_handle = nt_handle;

		if (device == nullptr || handle == 0)
		{
			m_last_error = "no share handle";
			Close();
			return false;
		}

		HRESULT hr = S_OK;
		if (nt_handle)
		{
			// NT handles need the 11.1 entry point; the handle was duplicated into this process
			// by the add-on, so it is ours to close.
			ID3D11Device1 *device1 = nullptr;
			hr = device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&device1));
			if (FAILED(hr) || device1 == nullptr)
			{
				m_last_error = HResultText("ID3D11Device1", hr);
				Close();
				return false;
			}

			hr = device1->OpenSharedResource1(reinterpret_cast<HANDLE>(handle), __uuidof(ID3D11Texture2D),
				reinterpret_cast<void **>(&m_texture));
			device1->Release();
		}
		else
		{
			hr = device->OpenSharedResource(reinterpret_cast<HANDLE>(handle), __uuidof(ID3D11Texture2D),
				reinterpret_cast<void **>(&m_texture));
		}

		if (FAILED(hr) || m_texture == nullptr)
		{
			m_last_error = HResultText("OpenSharedResource", hr);
			Close();
			return false;
		}

		// The shared texture may be typeless (that is how depth formats are shared): the view
		// decides how the bits are read.
		D3D11_TEXTURE2D_DESC desc = {};
		m_texture->GetDesc(&desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
		view.Format = static_cast<DXGI_FORMAT>(FormatShaderResourceView(format));
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(m_texture, &view, &m_srv);
		if (FAILED(hr) || m_srv == nullptr)
		{
			m_last_error = HResultText("CreateShaderResourceView", hr);
			Close();
			return false;
		}

		m_format = format;
		m_width = desc.Width != 0 ? desc.Width : width;
		m_height = desc.Height != 0 ? desc.Height : height;
		m_last_error.clear();
		return true;
	}

	void SharedTexture::Close()
	{
		if (m_srv != nullptr)
		{
			m_srv->Release();
			m_srv = nullptr;
		}
		if (m_texture != nullptr)
		{
			m_texture->Release();
			m_texture = nullptr;
		}
		// A duplicated NT handle belongs to this process; a legacy handle does not.
		if (m_is_nt_handle && m_handle != 0)
			CloseHandle(reinterpret_cast<HANDLE>(m_handle));

		m_handle = 0;
		m_is_nt_handle = false;
		m_width = 0;
		m_height = 0;
		m_format = 0;
	}
}
