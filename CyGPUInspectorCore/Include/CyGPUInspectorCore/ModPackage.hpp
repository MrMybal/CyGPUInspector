// CyGPUInspector — the mod package: what CyGPUInspector exports and CyGPUInjector applies.
//
// The two are one piece of work split across two processes and two moments. You find a shader in
// a game with the inspector, rewrite it, disable another one, and you are happy with the result —
// but everything you did lives in a session that ends when the game does. A mod package is that
// work written down: the modifications, keyed by *what the shader is* rather than by any number
// that only meant something in that one session.
//
// The key is the **semantic hash**: the SHA-256 of a shader's code chunks alone, ignoring the
// debug and reflection parts of the container. That is what makes a package survive a game patch
// that recompiles the same shader with a different compiler build, and what makes it fail cleanly
// rather than silently apply to the wrong shader when the shader really did change.
//
// The format is a directory, not an opaque blob, for the same reason a capture is: anyone can
// look inside, diff two versions, or hand-edit the manifest. It is read by this one piece of code
// on both sides, so there is no second parser to keep in sync.
//
//   MyMod.cygimod/
//     mod.json                  manifest and entries
//     shaders/<hash>.cso        replacement byte code
//     shaders/<hash>.hlsl       the source it was compiled from, when there was one
//
// This is a ReShade add-on payload, nothing more. It replaces shaders and skips draws through the
// official add-on API, exactly as the inspector does. It hides nothing, defeats nothing, and a
// game that refuses ReShade is simply not a game this works on.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Protocol.hpp"
#include "Sha256.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	// What a package asks to be done to one shader.
	enum class ModAction : uint32_t
	{
		// Substitute the byte code when the game creates a pipeline from this shader.
		replace = 0,
		// Drop every draw or dispatch that uses it. The frame renders without whatever it drew.
		disable,
	};
	const char *ModActionName(ModAction action);
	bool ModActionFromName(const std::string &name, ModAction &out);

	struct ModEntry
	{
		ModAction action = ModAction::replace;

		// The matching key. A package that cannot name this is not applicable to anything.
		Sha256Digest semantic_hash;
		// The exact container this was made from, kept so a mismatch can be explained rather than
		// merely reported. Never used for matching: a recompile changes it and nothing else.
		Sha256Digest signature;

		ShaderStage stage = ShaderStage::unknown;
		ShaderFormat format = ShaderFormat::unknown;
		uint32_t shader_model = 0;

		// `replace` only. The byte code is what gets applied; the source is provenance, so that
		// whoever opens the package in a year can see what was meant, not only what was compiled.
		std::vector<uint8_t> byte_code;
		std::string source_hlsl;
		std::string profile;        // "ps_5_0", the profile the byte code was compiled for

		std::string note;           // why this change exists, in the author's words

		// "a84df290… pixel, replace"
		std::string Describe() const;
	};

	struct ModPackage
	{
		std::string name;
		std::string author;
		std::string version;
		std::string description;
		std::string created;          // ISO-ish local time, for a human
		std::string tool_version;

		// What this was made against. Advisory: a package is applied by shader hash, not by
		// process name, so it works in any game that happens to use the same shader. The add-on
		// says when the game is not the one the package was built on rather than refusing.
		std::string target_process;
		std::string target_api;
		std::string target_adapter;

		std::vector<ModEntry> entries;

		bool Empty() const { return entries.empty(); }
		uint32_t CountOf(ModAction action) const;

		// Writes the directory. Creates it if needed, overwrites `mod.json` and the shader files
		// it owns, and leaves anything else in the directory alone.
		bool Save(const std::string &directory, std::string &error) const;

		// Reads a directory written by Save. `load_byte_code` false reads the manifest only,
		// which is what a listing needs.
		static bool Load(const std::string &directory, ModPackage &out, std::string &error,
		                 bool load_byte_code = true);

		// Cheap check that a directory looks like a package, for scanning a folder.
		static bool LooksLikePackage(const std::string &directory);

		// "MyMod.cygimod"
		static std::string SuggestDirectoryName(const std::string &name);
		static constexpr const char *kExtension = ".cygimod";
		static constexpr uint32_t kFormatVersion = 1;
	};
}
