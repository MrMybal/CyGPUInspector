// CyGPUInspector — format helpers.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/Format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace cygi
{
	namespace
	{
		struct FormatEntry
		{
			uint32_t value;
			const char *name;
		};

		// Sorted by value, generated from the ReShade SDK header.
		constexpr FormatEntry kFormats[] = {
#include "FormatNames.inc"
		};

		const FormatEntry *Find(uint32_t format)
		{
			const auto *begin = std::begin(kFormats);
			const auto *end = std::end(kFormats);
			const auto *it = std::lower_bound(begin, end, format,
				[](const FormatEntry &entry, uint32_t value) { return entry.value < value; });
			return (it != end && it->value == format) ? it : nullptr;
		}

		bool Contains(const char *name, const char *needle)
		{
			return name != nullptr && std::strstr(name, needle) != nullptr;
		}

		bool StartsWith(const char *name, const char *prefix)
		{
			return name != nullptr && std::strncmp(name, prefix, std::strlen(prefix)) == 0;
		}
	}

	const char *FormatName(uint32_t format)
	{
		if (const FormatEntry *entry = Find(format))
			return entry->name;

		// Unknown value: keep something printable and stable for the UI.
		static thread_local char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "format 0x%08X", format);
		return buffer;
	}

	bool FormatIsDepth(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		if (entry == nullptr)
			return false;
		return StartsWith(entry->name, "d16") || StartsWith(entry->name, "d24") ||
		       StartsWith(entry->name, "d32") || std::strcmp(entry->name, "intz") == 0;
	}

	bool FormatIsStencil(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		return entry != nullptr && (Contains(entry->name, "_s8") || StartsWith(entry->name, "s8"));
	}

	bool FormatIsCompressed(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		return entry != nullptr && StartsWith(entry->name, "bc");
	}

	bool FormatIsSrgb(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		return entry != nullptr && Contains(entry->name, "_srgb");
	}

	bool FormatIsFloat(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		return entry != nullptr && (Contains(entry->name, "float") || Contains(entry->name, "ufloat") ||
		                            Contains(entry->name, "sfloat"));
	}

	bool FormatIsTypeless(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		return entry != nullptr && Contains(entry->name, "typeless");
	}

	uint32_t FormatBytesPerPixel(uint32_t format)
	{
		const FormatEntry *entry = Find(format);
		if (entry == nullptr || StartsWith(entry->name, "bc"))
			return 0;

		const char *name = entry->name;
		// Sum every "<channel><bits>" group of the canonical name: r16g16b16a16 -> 64 bits.
		uint32_t bits = 0;
		for (const char *c = name; *c != '\0' && *c != '_'; ++c)
		{
			if (*c >= 'a' && *c <= 'z')
			{
				uint32_t channel_bits = 0;
				const char *digits = c + 1;
				while (*digits >= '0' && *digits <= '9')
				{
					channel_bits = channel_bits * 10 + static_cast<uint32_t>(*digits - '0');
					++digits;
				}
				bits += channel_bits;
				c = digits - 1;
			}
		}
		if (bits == 0)
			return 0;
		return (bits + 7) / 8;
	}

	uint32_t FormatFromName(const char *name)
	{
		if (name == nullptr)
			return 0;
		for (const FormatEntry &entry : kFormats)
			if (std::strcmp(entry.name, name) == 0)
				return entry.value;
		return 0;
	}

	uint32_t FormatShareableCopyTarget(uint32_t source_format)
	{
		// Depth formats can neither be shared nor viewed as shader resources. Their typeless
		// family member can do both, and a copy inside one typeless family is legal.
		switch (source_format)
		{
		case 55: return 53;  // d16_unorm            -> r16_typeless
		case 45: return 44;  // d24_unorm_s8_uint    -> r24_g8_typeless
		case 40: return 39;  // d32_float            -> r32_typeless
		case 20: return 19;  // d32_float_s8_uint    -> r32_g8_typeless
		default: return source_format;
		}
	}

	uint32_t FormatShaderResourceView(uint32_t shared_format)
	{
		// The two depth families have no plain "_unorm" / "_float" spelling: name them explicitly.
		switch (shared_format)
		{
		case 44: return 46;  // r24_g8_typeless -> r24_unorm_x8_uint
		case 19: return 21;  // r32_g8_typeless -> r32_float_x8_uint
		default: break;
		}

		const FormatEntry *entry = Find(shared_format);
		if (entry == nullptr || !Contains(entry->name, "_typeless"))
			return shared_format;

		// Every other typeless format is named "<channels>_typeless": try the concrete spellings
		// in the order that makes the most sense for a preview.
		std::string base(entry->name);
		base.resize(base.size() - std::strlen("_typeless"));

		for (const char *suffix : { "_unorm", "_float", "_uint", "_snorm", "_sint" })
		{
			const uint32_t candidate = FormatFromName((base + suffix).c_str());
			if (candidate != 0)
				return candidate;
		}
		return shared_format;
	}

	bool FormatIsPreviewSupported(uint32_t format)
	{
		// The FourCC values above 0xFFFF are ReShade extensions for D3D9, OpenGL and Vulkan
		// formats that have no DXGI equivalent: a D3D11 standalone cannot view them.
		return format != 0 && format <= 0xFFFF && Find(format) != nullptr;
	}

	uint32_t FormatCanonicalPreview(uint32_t format)
	{
		constexpr uint32_t kRgba8Unorm = 28;      // r8g8b8a8_unorm
		constexpr uint32_t kRgba16Float = 10;     // r16g16b16a16_float

		if (FormatIsDepth(format) || FormatIsFloat(format))
			return kRgba16Float;
		if (FormatBytesPerPixel(format) > 4)
			return kRgba16Float;
		return kRgba8Unorm;
	}
}
