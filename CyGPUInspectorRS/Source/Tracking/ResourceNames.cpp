// CyGPUInspectorRS — the names a game gives its resources.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ResourceNames.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3d12.h>

#include <vector>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		// The two well-known private data keys of d3dcommon.h, spelled out so that nothing has to
		// link dxguid.lib for them.
		constexpr GUID kDebugObjectName = { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };
		constexpr GUID kDebugObjectNameW = { 0x4cca5fd8, 0x921f, 0x42c8, { 0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41 } };

		// A name is a label, not a document: anything longer is cut.
		constexpr UINT kMaxNameBytes = 512;

		std::string FromWide(const wchar_t *text, size_t length)
		{
			while (length != 0 && text[length - 1] == L'\0')
				--length;
			if (length == 0)
				return std::string();
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
			if (bytes <= 0)
				return std::string();
			std::string out(static_cast<size_t>(bytes), ' ');
			WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), bytes, nullptr, nullptr);
			return out;
		}

		std::string FromNarrow(const char *text, size_t length)
		{
			while (length != 0 && text[length - 1] == '\0')
				--length;
			return std::string(text, length);
		}

		// Both interfaces have the same GetPrivateData; the template only saves writing it twice.
		template <typename Object>
		std::string ReadName(Object *object)
		{
			UINT size = 0;
			if (SUCCEEDED(object->GetPrivateData(kDebugObjectNameW, &size, nullptr)) && size != 0 && size <= kMaxNameBytes)
			{
				std::vector<wchar_t> name(size / sizeof(wchar_t) + 1, L'\0');
				if (SUCCEEDED(object->GetPrivateData(kDebugObjectNameW, &size, name.data())))
					return FromWide(name.data(), size / sizeof(wchar_t));
			}
			size = 0;
			if (SUCCEEDED(object->GetPrivateData(kDebugObjectName, &size, nullptr)) && size != 0 && size <= kMaxNameBytes)
			{
				std::vector<char> name(size + 1, '\0');
				if (SUCCEEDED(object->GetPrivateData(kDebugObjectName, &size, name.data())))
					return FromNarrow(name.data(), size);
			}
			return std::string();
		}
	}

	std::string ReadNativeName(device *device, resource resource)
	{
		if (device == nullptr || resource.handle == 0)
			return std::string();

		// ReShade's resource handle is the native object on both Direct3D versions.
		switch (device->get_api())
		{
		case device_api::d3d12:
			return ReadName(reinterpret_cast<ID3D12Resource *>(resource.handle));
		case device_api::d3d11:
			return ReadName(reinterpret_cast<ID3D11Resource *>(resource.handle));
		default:
			return std::string();
		}
	}
}
