// CyGPUInspector — format helpers.
//
// Format values are the ones of reshade::api::format, which are DXGI_FORMAT compatible. Keeping
// them as plain uint32_t lets the standalone and the database reason about formats without
// including the ReShade SDK.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>

namespace cygi
{
	// "r16g16b16a16_float", or "format 0x1234" for an unknown value.
	const char *FormatName(uint32_t format);

	bool FormatIsDepth(uint32_t format);
	bool FormatIsStencil(uint32_t format);
	bool FormatIsCompressed(uint32_t format);
	bool FormatIsSrgb(uint32_t format);
	bool FormatIsFloat(uint32_t format);
	bool FormatIsTypeless(uint32_t format);

	// 0 for compressed or unknown formats.
	uint32_t FormatBytesPerPixel(uint32_t format);

	// Format the preview of this resource should be copied into before being shared with the
	// standalone: r8g8b8a8_unorm for LDR, r16g16b16a16_float for HDR and depth.
	uint32_t FormatCanonicalPreview(uint32_t format);

	// Reverse lookup of FormatName; returns 0 (unknown) when the name is not known.
	uint32_t FormatFromName(const char *name);

	// --- Preview sharing -------------------------------------------------------------------
	// A shared preview texture must satisfy three constraints at once: be copy compatible with
	// the source (D3D only copies inside one typeless family), be creatable as a shared resource,
	// and be viewable as a shader resource by the standalone. Depth formats satisfy none of that
	// directly, so they are shared through their typeless family member.

	// Format the add-on creates the shared texture with, for a given source format.
	uint32_t FormatShareableCopyTarget(uint32_t source_format);

	// Format the standalone creates the shader resource view with, for a given shared format.
	uint32_t FormatShaderResourceView(uint32_t shared_format);

	// False for formats a preview cannot handle: unknown, and the FourCC extensions ReShade adds
	// for APIs other than D3D10/11/12.
	bool FormatIsPreviewSupported(uint32_t format);
}
