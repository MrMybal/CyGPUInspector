// CyGPUInspector — translating the interface.
//
// English is the base language of this project: the code, the comments, the documentation and
// every string in the interface are written in English first, and that is what the source of
// truth looks like. A translation is a file that maps those English strings onto another
// language, and anything it does not cover falls back to the English it was keyed on.
//
// That fallback is the whole design. A key is **the English sentence itself**, not an invented
// identifier like `ui.shaders.title`:
//
//   * a missing or half-finished translation shows English, never a raw key and never a blank;
//   * nothing has to be named, so wrapping a new string costs one call and no bookkeeping;
//   * a translator reads the English and writes the other language beside it, with the context
//     right there rather than in a separate document.
//
// Adding a language is copying `Lang/template.json`, filling it in and dropping it in `Lang/`.
// No rebuild: the application scans the folder at start-up and lists what it finds.
//
// ImGui identifies widgets by their label, so translating a label would change a window's
// identity and break saved layouts whenever the language changes. `TrId` exists for that: it
// returns the translated label with the untranslated English appended as a hidden `###` suffix,
// which is what ImGui hashes. A French user and an English user then share one layout file.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cygi::i18n
{
	struct LanguageInfo
	{
		std::string code;   // "en", "fr", "de"
		std::string name;   // what the language calls itself: "Français", not "French"
		std::string path;   // empty for the built-in English
		uint32_t translated = 0;
	};

	// Where the catalogues live. Defaults to `Lang/` beside the executable. Call before Load.
	void SetLanguageRoot(const std::string &directory);
	const std::string &LanguageRoot();

	// Scans the folder. English is always present, whether or not anything was found.
	void ScanLanguages();
	const std::vector<LanguageInfo> &Languages();

	// Switches language, loading the catalogue the first time it is asked for. A catalogue is
	// never unloaded, so every pointer Tr has ever returned stays valid for the life of the
	// process — the interface may hold one for the duration of a frame without thinking about it.
	bool SetLanguage(const std::string &code);
	const std::string &CurrentLanguage();
	bool IsTranslated();   // false when English is active, which is the common case

	// The translation of `source`, or `source` itself when there is none.
	const char *Tr(const char *source);

	// The translation with ImGui's identity preserved: "Nuanceurs###Shaders". Use for every
	// window title and every widget whose label is its identifier.
	const char *TrId(const char *source);

	// Every string this run has asked for, translated or not. Writing a template out of a real
	// run is how a translator gets a file that matches the build in front of them, instead of one
	// that matches whatever the last person remembered to update.
	bool WriteTemplate(const std::string &path, std::string &error);
	uint32_t SeenCount();
	uint32_t MissingCount();
}
