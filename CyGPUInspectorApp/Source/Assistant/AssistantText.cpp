// CyGPUInspectorApp — assistant messages, translated where they are shown.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "AssistantText.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

namespace cygi::assistant
{
	namespace
	{
		// Replaces each %s with the next value. Not printf: a value containing a percent sign, or
		// a translation that dropped a placeholder, can never read past the arguments.
		template <typename Resolve>
		std::string Fill(const std::string &pattern, const std::vector<Text> &args, Resolve resolve)
		{
			std::string out;
			out.reserve(pattern.size());
			size_t next = 0;
			for (size_t i = 0; i < pattern.size(); ++i)
			{
				if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 's')
				{
					if (next < args.size())
						out += resolve(args[next]);
					++next;
					++i;
					continue;
				}
				out += pattern[i];
			}
			return out;
		}
	}

	std::string Render(const Text &text)
	{
		if (text.literal)
			return text.key;
		return Fill(i18n::Tr(text.key.c_str()), text.args, [](const Text &value) { return Render(value); });
	}

	std::string English(const Text &text)
	{
		if (text.literal)
			return text.key;
		return Fill(text.key, text.args, [](const Text &value) { return English(value); });
	}
}
