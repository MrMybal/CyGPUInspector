// CyGPUInspector — translating the interface.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/Localization.hpp"

#include "CyGPUInspectorCore/Json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

namespace cygi::i18n
{
	namespace
	{
		// A catalogue, once loaded, is never freed. That is what makes it safe for the interface
		// to hold a `const char *` from Tr for as long as it likes: the string it points at
		// outlives every frame, and switching language only changes which map is consulted.
		struct Catalogue
		{
			std::unordered_map<std::string, std::string> strings;
			bool loaded = false;
		};

		struct State
		{
			std::string root;
			std::string current = "en";
			std::vector<LanguageInfo> languages;
			std::map<std::string, Catalogue> catalogues;

			// Every string the interface has asked for this run, in the order a person would want
			// to read them. Also the source of the template a translator starts from.
			std::map<std::string, bool> seen;   // source -> was it translated

			// TrId caches "translated###source" so the pointer stays valid and the concatenation
			// does not happen sixty times a second.
			std::unordered_map<std::string, std::string> ids;
			std::string id_language;
		};

		State &Get()
		{
			static State state;
			return state;
		}

		std::filesystem::path DefaultRoot()
		{
			wchar_t executable[MAX_PATH] = {};
			if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
				return "Lang";
			return std::filesystem::path(executable).parent_path() / "Lang";
		}

		bool ReadWholeFile(const std::filesystem::path &path, std::string &out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamoff size = file.tellg();
			if (size < 0)
				return false;
			file.seekg(0);

			out.resize(static_cast<size_t>(size));
			if (size != 0)
				file.read(out.data(), size);
			return static_cast<bool>(file);
		}

		bool LoadCatalogue(State &state, const std::string &code)
		{
			Catalogue &catalogue = state.catalogues[code];
			if (catalogue.loaded)
				return true;

			const auto language = std::find_if(state.languages.begin(), state.languages.end(),
				[&](const LanguageInfo &info) { return info.code == code; });
			if (language == state.languages.end() || language->path.empty())
			{
				// English, or a language with no file: an empty catalogue means every lookup
				// falls back to the English source, which is exactly right.
				catalogue.loaded = true;
				return true;
			}

			std::string text;
			if (!ReadWholeFile(language->path, text))
				return false;

			Json root;
			if (!Json::Parse(text, root) || !root["strings"].IsObject())
				return false;

			for (const auto &member : root["strings"].Members())
			{
				// An empty translation is an untranslated entry, not an empty label. Leaving it
				// out of the map is what makes it fall back to English.
				const std::string &value = member.second.AsString();
				if (!value.empty())
					catalogue.strings.emplace(member.first, value);
			}

			catalogue.loaded = true;
			return true;
		}
	}

	void SetLanguageRoot(const std::string &directory)
	{
		Get().root = directory;
	}

	const std::string &LanguageRoot()
	{
		State &state = Get();
		if (state.root.empty())
			state.root = DefaultRoot().string();
		return state.root;
	}

	void ScanLanguages()
	{
		State &state = Get();
		state.languages.clear();

		// English is not a file. It is what the source says, and it is always available even when
		// the Lang folder is missing entirely.
		LanguageInfo english;
		english.code = "en";
		english.name = "English";
		state.languages.push_back(english);

		std::error_code code;
		const std::filesystem::path root(LanguageRoot());
		if (!std::filesystem::is_directory(root, code))
			return;

		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(root, code))
		{
			if (!entry.is_regular_file(code) || entry.path().extension() != ".json")
				continue;

			std::string text;
			if (!ReadWholeFile(entry.path(), text))
				continue;

			Json json;
			if (!Json::Parse(text, json))
				continue;

			LanguageInfo info;
			info.code = json["language"].AsString();
			if (info.code.empty() || info.code == "en")
				continue;   // the template, or a file claiming to be the base language

			info.name = json["name"].AsString();
			if (info.name.empty())
				info.name = info.code;
			info.path = entry.path().string();
			info.translated = 0;
			for (const auto &member : json["strings"].Members())
				if (!member.second.AsString().empty())
					++info.translated;

			state.languages.push_back(std::move(info));
		}

		std::sort(state.languages.begin() + 1, state.languages.end(),
			[](const LanguageInfo &a, const LanguageInfo &b) { return a.name < b.name; });
	}

	const std::vector<LanguageInfo> &Languages()
	{
		State &state = Get();
		if (state.languages.empty())
			ScanLanguages();
		return state.languages;
	}

	bool SetLanguage(const std::string &code)
	{
		State &state = Get();
		if (state.languages.empty())
			ScanLanguages();

		const auto found = std::find_if(state.languages.begin(), state.languages.end(),
			[&](const LanguageInfo &info) { return info.code == code; });
		if (found == state.languages.end())
			return false;

		if (!LoadCatalogue(state, code))
			return false;

		state.current = code;
		state.ids.clear();
		state.id_language = code;
		return true;
	}

	const std::string &CurrentLanguage() { return Get().current; }

	bool IsTranslated() { return Get().current != "en"; }

	const char *Tr(const char *source)
	{
		if (source == nullptr || source[0] == '\0')
			return source;

		State &state = Get();
		const Catalogue &catalogue = state.catalogues[state.current];

		const auto found = catalogue.strings.find(source);
		const bool translated = found != catalogue.strings.end();

		// Recorded whether or not it was found: the template a translator works from has to list
		// what the interface actually asks for, including what is still missing.
		state.seen[source] = translated;

		return translated ? found->second.c_str() : source;
	}

	const char *TrId(const char *source)
	{
		if (source == nullptr || source[0] == '\0')
			return source;

		State &state = Get();
		if (state.id_language != state.current)
		{
			state.ids.clear();
			state.id_language = state.current;
		}

		const auto cached = state.ids.find(source);
		if (cached != state.ids.end())
			return cached->second.c_str();

		std::string label = Tr(source);
		label += "###";
		label += source;   // the identity ImGui hashes: the English, whatever the language

		return state.ids.emplace(source, std::move(label)).first->second.c_str();
	}

	uint32_t SeenCount() { return static_cast<uint32_t>(Get().seen.size()); }

	uint32_t MissingCount()
	{
		uint32_t missing = 0;
		for (const auto &entry : Get().seen)
			if (!entry.second)
				++missing;
		return missing;
	}

	bool WriteTemplate(const std::string &path, std::string &error)
	{
		State &state = Get();
		if (state.seen.empty())
		{
			error = "no string has been asked for yet: open the panels you want translated first";
			return false;
		}

		Json root = Json::Object();
		root["language"] = Json(std::string("xx"));
		root["name"] = Json(std::string("Language name, as that language writes it"));
		root["note"] = Json(std::string(
			"Each key is the English source text and is also what the interface falls back to. "
			"Leave a value empty to keep the English. Do not translate a %s, a %u or anything "
			"after ### — the first two are where numbers and names are inserted, the last is how "
			"ImGui recognises a widget."));

		Json strings = Json::Object();
		for (const auto &entry : state.seen)
		{
			// The catalogue of the language in use is the better starting point when there is one:
			// a translator extending French should get the French back, not blanks.
			const Catalogue &catalogue = state.catalogues[state.current];
			const auto existing = catalogue.strings.find(entry.first);
			strings[entry.first] = Json(existing != catalogue.strings.end() ? existing->second : std::string());
		}
		root["strings"] = strings;

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			error = "could not write " + path;
			return false;
		}

		const std::string text = root.Write(2);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!file)
		{
			error = "could not finish writing " + path;
			return false;
		}
		return true;
	}
}
