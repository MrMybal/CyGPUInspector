// CyGPUInjector — the mod packages loaded in this game, and the lookup the hot path uses.
//
// A package is a list of modifications keyed by the semantic hash of a shader. The game creates
// pipelines from shader byte code; this turns that byte code into the same hash and asks whether
// anything was asked of it. Everything is loaded once, at start-up, and then read only, so the
// lookup needs no lock at all after that.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/ModPackage.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	// One package as loaded, plus what happened to it in this game.
	struct LoadedPackage
	{
		ModPackage package;
		std::string directory;
		std::string error;          // why it did not load, when it did not
		bool loaded = false;
		// Turned off from the overlay. The modifications of a disabled package are skipped, but
		// only the draw suppressions take effect immediately: a shader replacement happens when
		// the game creates the pipeline, so it needs the game to create it again.
		bool enabled = true;

		uint32_t matched_shaders = 0;   // entries this game actually had a shader for
	};

	// What to do about one shader, resolved once.
	struct ModRule
	{
		ModAction action = ModAction::replace;
		const uint8_t *byte_code = nullptr;   // owned by the package, which outlives every lookup
		size_t byte_code_size = 0;
		size_t package_index = 0;
		size_t entry_index = 0;
	};

	class ModLibrary
	{
	public:
		// Loads every *.cygimod directory found in `root`. Returns the number that loaded.
		uint32_t LoadFrom(const std::string &root);

		// Hot path, called once per shader of every pipeline the game creates. Null when this
		// shader is not mentioned by any enabled package.
		const ModRule *Find(const Sha256Digest &semantic_hash) const;

		bool Empty() const { return m_rules.empty(); }
		bool AnyDisableRule() const { return m_any_disable; }

		const std::vector<LoadedPackage> &Packages() const { return m_packages; }
		std::vector<LoadedPackage> &Packages() { return m_packages; }

		// Recomputed when a package is switched on or off in the overlay.
		void Rebuild();

		const std::string &Root() const { return m_root; }

		// Counters the overlay shows, so the user can tell "nothing matched" from "nothing ran".
		uint32_t ReplacementsApplied() const { return m_replacements.load(std::memory_order_relaxed); }
		uint32_t DrawsSkipped() const { return m_draws_skipped.load(std::memory_order_relaxed); }
		void CountReplacement(size_t package_index, size_t entry_index);
		void CountSkippedDraw() { m_draws_skipped.fetch_add(1, std::memory_order_relaxed); }
		void ResetCounters();

	private:
		std::string m_root;
		std::vector<LoadedPackage> m_packages;
		std::unordered_map<Sha256Digest, ModRule, Sha256DigestHasher> m_rules;
		bool m_any_disable = false;

		std::atomic<uint32_t> m_replacements{ 0 };
		std::atomic<uint32_t> m_draws_skipped{ 0 };
	};

	// The one library of the process. An add-on has no device-independent state otherwise, and
	// packages are the same whatever device loads them.
	ModLibrary &Library();
}
