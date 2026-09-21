// CyGPUInspectorApp — remembering what you changed, so it can leave the session.
//
// Everything the inspector does to a running game is undone when the game exits: the add-on holds
// the replacements, and the ids they use mean nothing in the next run. What survives is what gets
// written down here — each modification keyed by the semantic hash of the shader it applies to,
// which is the same key CyGPUInjector matches on later.
//
// It records the intent, not the outcome. A replacement is remembered when it is sent to the game,
// because that is the moment the user decided; whether that particular run of the game still has
// the pipeline in memory is beside the point for something meant to be applied next time.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/ModPackage.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	class SessionModel;

	// One modification, as the user made it.
	struct RecordedMod
	{
		ModEntry entry;
		uint32_t shader_id = 0;          // the session it was made in, for the interface only
		std::string origin;              // "edited HLSL", "highlight", "disabled"
		bool include = true;             // unticked in the export list
	};

	class ModRecorder
	{
	public:
		// Both take the shader from the model, so that the signature, the stage and the shader
		// model come from what the game really has rather than from whatever the UI last showed.
		// `include` is false for changes made to look at something rather than to keep, such as
		// a magenta highlight: they are recorded, so nothing is lost, but they are not ticked and
		// so cannot end up in a package by inattention.
		void RecordReplacement(const SessionModel &model, uint32_t shader_id,
		                       std::vector<uint8_t> byte_code, std::string source_hlsl,
		                       std::string profile, std::string origin, bool include = true);
		void RecordDisable(const SessionModel &model, uint32_t shader_id);

		// Undone in the game: the entry goes, rather than being exported as something the user
		// decided against.
		void Forget(const SessionModel &model, uint32_t shader_id);
		void Clear() { m_mods.clear(); }

		bool Empty() const { return m_mods.empty(); }
		size_t Count() const { return m_mods.size(); }
		uint32_t CountIncluded() const;

		const std::vector<RecordedMod> &Mods() const { return m_mods; }
		std::vector<RecordedMod> &Mods() { return m_mods; }

		// Builds the package from the ticked entries. `error` says why when it returns false.
		bool BuildPackage(const SessionModel &model, ModPackage &out, std::string &error) const;

	private:
		// A shader modified twice keeps only the last decision: that is what the user sees in the
		// game, so it is what an export has to mean.
		RecordedMod *FindBySemanticHash(const Sha256Digest &hash);

		std::vector<RecordedMod> m_mods;
	};
}
