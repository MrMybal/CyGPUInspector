// CyGPUInspectorDatabase — what CyGPUInspector remembers about one game (section 63).
//
//   Games/<executable>/profile.json     the game, its sessions, every shader seen in it
//   Games/<executable>/state.cygimod/   the shaders left disabled or replaced (section 38)
//
// JSON on disk, like the rest of the database: readable, diffable, and editable by hand. The shader
// store stays the place for what is derived from a shader (disassembly, decompilations, notes), by
// signature and across games; the profile is what belongs to one game: which of those shaders it
// uses, since when, and what the user chose to change in it.
//
// The persistent state is a mod package, the format CyGPUInjector reads, keyed by semantic hash.
// It survives a restart of the game, where every shader id changes, and it can be handed to
// CyGPUInjector as it is.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/ModPackage.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace cygi
{
	struct KnownShader
	{
		Sha256Digest signature;
		Sha256Digest semantic_hash;
		ShaderStage stage = ShaderStage::unknown;
		ShaderFormat format = ShaderFormat::unknown;
		uint32_t shader_model = 0;
		std::string first_seen;       // local time, "2026-09-27 14:03"
		std::string last_seen;
		uint32_t sessions = 0;        // how many sessions of this game used it
	};

	class GameProfile
	{
	public:
		// "Scorn-Win64-Shipping.exe" -> "Scorn-Win64-Shipping": the folder of a game.
		static std::string FolderName(const std::string &process_name);

		// Reads Games/<folder>/profile.json under `root`, or starts an empty profile when there is
		// none yet. Nothing is written until Save.
		bool Open(const std::filesystem::path &root, const std::string &process_name, std::string &error);
		bool IsOpen() const { return !m_directory.empty(); }
		void Close();

		// Writes profile.json. Cheap enough to call on every change that matters; the caller
		// decides when, typically every few seconds while something changed and at the end.
		bool Save(std::string &error);
		bool IsDirty() const { return m_dirty; }

		const std::filesystem::path &Directory() const { return m_directory; }
		const std::string &Executable() const { return m_executable; }
		const std::string &FirstSeen() const { return m_first_seen; }
		const std::string &LastSeen() const { return m_last_seen; }
		uint32_t Sessions() const { return m_sessions; }
		const std::string &Api() const { return m_api; }
		const std::string &Adapter() const { return m_adapter; }

		// A new session of this game starts: counted once, dated, and its shaders are counted
		// again as they are noted.
		void BeginSession(const std::string &api, const std::string &adapter);

		enum class Recognition
		{
			new_shader,      // never seen in this game
			known,           // the same byte code was seen in an earlier session
			known_code,      // the same code with different debug parts (semantic hash)
		};
		// Notes a shader met in the current session. Returns how it was recognised, as it was
		// before this call: a shader noted twice in one session is still "known" only if an
		// earlier session had it.
		Recognition Note(const KnownShader &shader);

		// Every shader seen in this game, by signature (hex).
		const std::map<std::string, KnownShader> &Shaders() const { return m_shaders; }
		// Recognition of a shader noted in the current session.
		bool WasKnownBefore(const Sha256Digest &signature) const;

		// The persistent state (section 38): Games/<folder>/state.cygimod.
		bool LoadState(ModPackage &out, std::string &error) const;
		bool SaveState(const ModPackage &state, std::string &error) const;
		bool ClearState(std::string &error) const;
		bool ReapplyOnConnect() const { return m_reapply_on_connect; }
		void SetReapplyOnConnect(bool reapply);

		// "2026-09-27 14:03", the date format of every profile field.
		static std::string Now();

	private:
		std::filesystem::path m_directory;
		std::string m_executable;
		std::string m_first_seen;
		std::string m_last_seen;
		std::string m_api;
		std::string m_adapter;
		uint32_t m_sessions = 0;
		bool m_reapply_on_connect = false;
		std::map<std::string, KnownShader> m_shaders;
		std::map<std::string, bool> m_noted_this_session;   // signature -> was known before
		std::set<std::string> m_semantic_hashes;             // of every shader in m_shaders
		bool m_dirty = false;
	};
}
