// CyGPUInspectorApp — assistant messages, translated where they are shown.
//
// The assistant writes its status and error messages on a worker thread, and the translation
// catalogue is not thread safe: it also records every string it is asked for. A message is
// therefore kept as its English source and its values, and translated when it is drawn. A change
// of language then applies to those messages straight away, the way the translatable messages of
// the CyRSAssistant companion do.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cygi::assistant
{
	struct Text
	{
		std::string key;          // English source with %s placeholders, or literal text
		std::vector<Text> args;   // one per %s, in order
		bool literal = false;     // text from outside (a model, a tool): never translated

		Text() = default;
		Text(const char *source) : key(source != nullptr ? source : "") {}

		static Text Literal(std::string text)
		{
			Text result;
			result.key = std::move(text);
			result.literal = true;
			return result;
		}

		static Text Format(const char *source, std::vector<Text> values)
		{
			Text result(source);
			result.args = std::move(values);
			return result;
		}

		bool Empty() const { return key.empty(); }
	};

	// The translated text. UI thread only.
	std::string Render(const Text &text);

	// The English text, from any thread: for the model and for the tests.
	std::string English(const Text &text);
}
