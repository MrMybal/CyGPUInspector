// CyGPUInspectorDatabase — what CyGPUInspector remembers about one game (section 63).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDatabase/GameProfile.hpp"

#include <CyGPUInspectorCore/Json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace cygi
{
	namespace
	{
		constexpr uint32_t kFormatVersion = 1;

		std::filesystem::path StatePath(const std::filesystem::path &directory)
		{
			return directory / (std::string("state") + ModPackage::kExtension);
		}
	}

	std::string GameProfile::FolderName(const std::string &process_name)
	{
		std::string name = process_name;
		const size_t slash = name.find_last_of("\\/");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		if (name.size() > 4)
		{
			std::string extension = name.substr(name.size() - 4);
			for (char &c : extension)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (extension == ".exe")
				name.resize(name.size() - 4);
		}
		// A folder name, whatever the process was called.
		for (char &c : name)
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ' '))
				c = '_';
		while (!name.empty() && (name.back() == '.' || name.back() == ' '))
			name.pop_back();
		return name.empty() ? std::string("Unknown") : name;
	}

	std::string GameProfile::Now()
	{
		SYSTEMTIME now = {};
		GetLocalTime(&now);
		char text[32];
		std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
			now.wMinute);
		return text;
	}

	bool GameProfile::Open(const std::filesystem::path &root, const std::string &process_name, std::string &error)
	{
		Close();
		m_directory = root / FolderName(process_name);
		m_executable = process_name;

		std::ifstream file(m_directory / "profile.json", std::ios::binary);
		if (!file)
			return true;   // a game seen for the first time: nothing to read yet

		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		Json root_json;
		std::string parse_error;
		if (!Json::Parse(text, root_json, &parse_error) || !root_json.IsObject())
		{
			error = "profile.json could not be read: " + parse_error;
			return false;
		}

		if (!root_json["executable"].AsString().empty())
			m_executable = root_json["executable"].AsString();
		m_first_seen = root_json["first_seen"].AsString();
		m_last_seen = root_json["last_seen"].AsString();
		m_sessions = root_json["sessions"].AsUInt32();
		m_api = root_json["api"].AsString();
		m_adapter = root_json["adapter"].AsString();
		m_reapply_on_connect = root_json["reapply_on_connect"].AsBool();

		for (const auto &member : root_json["shaders"].Members())
		{
			KnownShader shader;
			if (!Sha256Digest::FromHex(member.first, shader.signature))
				continue;
			const Json &entry = member.second;
			Sha256Digest::FromHex(entry["semantic_hash"].AsString(), shader.semantic_hash);
			shader.stage = static_cast<ShaderStage>(entry["stage_id"].AsUInt32());
			shader.format = static_cast<ShaderFormat>(entry["format_id"].AsUInt32());
			shader.shader_model = entry["shader_model"].AsUInt32();
			shader.first_seen = entry["first_seen"].AsString();
			shader.last_seen = entry["last_seen"].AsString();
			shader.sessions = entry["sessions"].AsUInt32();
			m_semantic_hashes.insert(shader.semantic_hash.ToHex());
			m_shaders[member.first] = shader;
		}
		return true;
	}

	void GameProfile::Close()
	{
		m_directory.clear();
		m_executable.clear();
		m_first_seen.clear();
		m_last_seen.clear();
		m_api.clear();
		m_adapter.clear();
		m_sessions = 0;
		m_reapply_on_connect = false;
		m_shaders.clear();
		m_noted_this_session.clear();
		m_semantic_hashes.clear();
		m_dirty = false;
	}

	bool GameProfile::Save(std::string &error)
	{
		if (!IsOpen())
			return true;

		std::error_code code;
		std::filesystem::create_directories(m_directory, code);
		if (code)
		{
			error = "the profile folder could not be created: " + code.message();
			return false;
		}

		Json shaders = Json::Object();
		for (const auto &member : m_shaders)
		{
			const KnownShader &shader = member.second;
			Json entry = Json::Object();
			entry["semantic_hash"] = Json(shader.semantic_hash.ToHex());
			// The names are for a person reading the file; the ids are what is read back.
			entry["stage"] = Json(ShaderStageName(shader.stage));
			entry["stage_id"] = Json(static_cast<uint32_t>(shader.stage));
			entry["format"] = Json(ShaderFormatName(shader.format));
			entry["format_id"] = Json(static_cast<uint32_t>(shader.format));
			entry["shader_model"] = Json(shader.shader_model);
			entry["first_seen"] = Json(shader.first_seen);
			entry["last_seen"] = Json(shader.last_seen);
			entry["sessions"] = Json(shader.sessions);
			shaders[member.first] = entry;
		}

		Json root_json = Json::Object();
		root_json["format"] = Json(kFormatVersion);
		root_json["executable"] = Json(m_executable);
		root_json["first_seen"] = Json(m_first_seen);
		root_json["last_seen"] = Json(m_last_seen);
		root_json["sessions"] = Json(m_sessions);
		root_json["api"] = Json(m_api);
		root_json["adapter"] = Json(m_adapter);
		root_json["reapply_on_connect"] = Json(m_reapply_on_connect);
		root_json["shader_count"] = Json(static_cast<uint32_t>(m_shaders.size()));
		root_json["shaders"] = shaders;

		// Written beside, then moved over: a crash while writing never leaves half a profile.
		const std::filesystem::path path = m_directory / "profile.json";
		const std::filesystem::path temporary = m_directory / "profile.json.tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			const std::string text = root_json.Write(1);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			if (!file)
			{
				error = "profile.json could not be written";
				return false;
			}
		}
		std::filesystem::rename(temporary, path, code);
		if (code)
		{
			error = "profile.json could not be replaced: " + code.message();
			return false;
		}
		m_dirty = false;
		return true;
	}

	void GameProfile::BeginSession(const std::string &api, const std::string &adapter)
	{
		++m_sessions;
		m_last_seen = Now();
		if (m_first_seen.empty())
			m_first_seen = m_last_seen;
		if (!api.empty())
			m_api = api;
		if (!adapter.empty())
			m_adapter = adapter;
		m_noted_this_session.clear();
		m_dirty = true;
	}

	GameProfile::Recognition GameProfile::Note(const KnownShader &shader)
	{
		const std::string key = shader.signature.ToHex();
		const auto noted = m_noted_this_session.find(key);
		if (noted != m_noted_this_session.end())
			return noted->second ? Recognition::known : Recognition::new_shader;

		Recognition recognition = Recognition::new_shader;
		auto existing = m_shaders.find(key);
		if (existing != m_shaders.end())
		{
			recognition = Recognition::known;
		}
		else if (!shader.semantic_hash.IsZero() && m_semantic_hashes.count(shader.semantic_hash.ToHex()) != 0)
		{
			recognition = Recognition::known_code;
		}

		const std::string now = Now();
		KnownShader &entry = m_shaders[key];
		if (recognition != Recognition::known)
		{
			entry = shader;
			entry.first_seen = now;
			entry.sessions = 0;
			m_semantic_hashes.insert(shader.semantic_hash.ToHex());
		}
		entry.last_seen = now;
		++entry.sessions;
		m_noted_this_session[key] = recognition != Recognition::new_shader;
		m_dirty = true;
		return recognition;
	}

	bool GameProfile::WasKnownBefore(const Sha256Digest &signature) const
	{
		const auto noted = m_noted_this_session.find(signature.ToHex());
		if (noted != m_noted_this_session.end())
			return noted->second;
		return m_shaders.count(signature.ToHex()) != 0;
	}

	bool GameProfile::LoadState(ModPackage &out, std::string &error) const
	{
		out = ModPackage();
		if (!IsOpen())
			return true;
		const std::filesystem::path path = StatePath(m_directory);
		std::error_code code;
		if (!std::filesystem::exists(path / "mod.json", code))
			return true;   // nothing saved for this game
		return ModPackage::Load(path.string(), out, error);
	}

	bool GameProfile::SaveState(const ModPackage &state, std::string &error) const
	{
		if (!IsOpen())
		{
			error = "no game profile is open";
			return false;
		}
		// Rewritten as a whole: entries forgotten since the last save must not stay behind.
		std::string ignored;
		ClearState(ignored);
		if (state.Empty())
			return true;
		return state.Save(StatePath(m_directory).string(), error);
	}

	bool GameProfile::ClearState(std::string &error) const
	{
		if (!IsOpen())
			return true;
		std::error_code code;
		std::filesystem::remove_all(StatePath(m_directory), code);
		if (code)
		{
			error = "the saved state could not be removed: " + code.message();
			return false;
		}
		return true;
	}

	void GameProfile::SetReapplyOnConnect(bool reapply)
	{
		if (m_reapply_on_connect != reapply)
		{
			m_reapply_on_connect = reapply;
			m_dirty = true;
		}
	}
}
