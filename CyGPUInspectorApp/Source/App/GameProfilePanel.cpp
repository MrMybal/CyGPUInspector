// CyGPUInspectorApp — the game profile: what is remembered about the game in front of you.
//
// A profile opens by itself when a game connects (or a capture of it is opened). It counts the
// session, recognises every shader the game sends against the ones seen in earlier sessions, and
// keeps the persistent state of section 38: the shaders the user left disabled or replaced, saved
// as a mod package keyed by semantic hash, and applied again when the game comes back if the user
// asked for it.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include "LayoutHelpers.hpp"

#include <CyGPUInspectorCore/Localization.hpp>
#include <CyGPUInspectorCore/Version.hpp>
#include <CyGPUInspectorDecompiler/ReplacementCheck.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	namespace
	{
		bool ContainsText(const std::string &haystack, const char *needle)
		{
			if (needle == nullptr || needle[0] == '\0')
				return true;
			std::string a = haystack;
			std::string b = needle;
			std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a.find(b) != std::string::npos;
		}
	}

	std::filesystem::path Application::GamesRoot()
	{
		// Beside the shader database: Database/ holds what is derived from shaders, Games/ what
		// belongs to one game.
		return m_analysis.Store().Root().parent_path() / "Games";
	}

	// ------------------------------------------------------------------------------------------
	// Per frame
	// ------------------------------------------------------------------------------------------

	void Application::UpdateGameProfile(double delta_seconds)
	{
		// Which game the profile should be: the connected one, or the one an open capture came from.
		std::string process;
		std::string api;
		std::string adapter;
		const void *owner = nullptr;
		if (m_capture_model != nullptr)
		{
			process = m_capture_info.process_name;
			owner = m_capture_model.get();
		}
		else if (m_client != nullptr)
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			if (m_client->Model().HasSession())
			{
				process = m_client->Model().Session().process_name;
				api = GraphicsApiName(m_client->Model().Session().api);
				adapter = m_client->Model().Session().adapter_name;
				owner = m_client.get();
			}
		}

		if (owner != m_profile_owner)
		{
			CloseGameProfile();
			if (owner != nullptr && !process.empty())
			{
				std::string error;
				if (m_profile.Open(GamesRoot(), process, error))
				{
					m_profile_owner = owner;
					m_profile_live = m_capture_model == nullptr;
					if (m_profile_live)
						m_profile.BeginSession(api, adapter);
					m_profile.LoadState(m_profile_state, error);
					m_profile_applied.assign(m_profile_state.entries.size(), false);
					m_profile_reapplying = m_profile_live && m_profile.ReapplyOnConnect() && !m_profile_state.Empty();
					m_profile_message = error;
				}
				else
				{
					m_profile_message = error;
				}
			}
		}
		if (!m_profile.IsOpen())
			return;

		// Every shader the game sends is recognised, or remembered, as it arrives.
		if (m_profile_live && m_client != nullptr)
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());
			const std::vector<ShaderInfo> &shaders = model.Shaders();
			if (shaders.size() < m_profile_noted)
				m_profile_noted = 0;   // resynchronised: the model was rebuilt
			for (; m_profile_noted < shaders.size(); ++m_profile_noted)
			{
				const ShaderInfo &shader = shaders[m_profile_noted];
				if (shader.signature.IsZero())
					continue;
				KnownShader known;
				known.signature = shader.signature;
				known.semantic_hash = shader.semantic_hash;
				known.stage = shader.stage;
				known.format = shader.format;
				known.shader_model = shader.shader_model;
				switch (m_profile.Note(known))
				{
				case GameProfile::Recognition::new_shader: ++m_profile_new; break;
				case GameProfile::Recognition::known: ++m_profile_known; break;
				case GameProfile::Recognition::known_code: ++m_profile_known_code; break;
				}
			}
		}

		if (m_profile_reapplying)
		{
			m_profile_since_reapply += delta_seconds;
			if (m_profile_since_reapply >= 0.5)
			{
				m_profile_since_reapply = 0.0;
				ReapplyProfileState();
			}
		}

		// Saved every few seconds while something changed, and when the profile closes.
		m_profile_since_save += delta_seconds;
		if (m_profile.IsDirty() && m_profile_since_save >= 10.0)
		{
			std::string error;
			if (!m_profile.Save(error))
				m_profile_message = error;
			m_profile_since_save = 0.0;
		}
	}

	void Application::CloseGameProfile()
	{
		if (m_profile.IsOpen() && m_profile.IsDirty())
		{
			std::string error;
			m_profile.Save(error);
		}
		m_profile.Close();
		m_profile_owner = nullptr;
		m_profile_live = false;
		m_profile_noted = 0;
		m_profile_new = 0;
		m_profile_known = 0;
		m_profile_known_code = 0;
		m_profile_state = ModPackage();
		m_profile_applied.clear();
		m_profile_reapplying = false;
		m_profile_message.clear();
	}

	// ------------------------------------------------------------------------------------------
	// Persistent state (section 38)
	// ------------------------------------------------------------------------------------------

	void Application::SaveProfileState()
	{
		if (!m_profile.IsOpen() || !m_profile_live || m_client == nullptr)
			return;

		// What this session changed, plus what was saved before for shaders this session has not
		// met: those are not undone just because the game did not load them this time.
		ModPackage state;
		state.name = m_profile.Executable() + " - saved state";
		state.created = GameProfile::Now();
		state.tool_version = kVersionString;
		state.target_process = m_profile.Executable();
		state.target_api = m_profile.Api();
		state.target_adapter = m_profile.Adapter();

		std::unordered_map<std::string, bool> in_session;
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());
			for (const ShaderInfo &shader : model.Shaders())
				in_session[shader.semantic_hash.ToHex()] = true;
		}
		for (const RecordedMod &mod : m_mods.Mods())
		{
			ModEntry entry = mod.entry;
			if (entry.note.empty())
				entry.note = mod.origin;
			state.entries.push_back(std::move(entry));
		}
		for (const ModEntry &old : m_profile_state.entries)
		{
			if (in_session.count(old.semantic_hash.ToHex()) != 0)
				continue;
			bool already = false;
			for (const ModEntry &entry : state.entries)
				already = already || entry.semantic_hash == old.semantic_hash;
			if (!already)
				state.entries.push_back(old);
		}

		std::string error;
		if (!m_profile.SaveState(state, error))
		{
			m_profile_message = error;
			return;
		}
		m_profile_state = state;
		m_profile_applied.assign(m_profile_state.entries.size(), true);
		char text[160];
		std::snprintf(text, sizeof(text), Tr("%zu change(s) saved for this game."), state.entries.size());
		m_profile_message = text;
	}

	void Application::ReapplyProfileState()
	{
		if (!m_profile_live || m_client == nullptr)
		{
			m_profile_reapplying = false;
			return;
		}
		if (m_profile_applied.size() != m_profile_state.entries.size())
			m_profile_applied.assign(m_profile_state.entries.size(), false);

		// Semantic hash -> shader id and original byte code, for the shaders the game has sent so far.
		struct Present
		{
			uint32_t id = 0;
			std::vector<uint8_t> code;
		};
		std::unordered_map<std::string, Present> present;
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());
			for (const ShaderInfo &shader : model.Shaders())
				present[shader.semantic_hash.ToHex()] = { shader.id, shader.code };
		}

		bool pending = false;
		for (size_t i = 0; i < m_profile_state.entries.size(); ++i)
		{
			if (m_profile_applied[i])
				continue;
			const ModEntry &entry = m_profile_state.entries[i];
			const auto found = present.find(entry.semantic_hash.ToHex());
			if (found == present.end())
			{
				pending = true;   // the game has not loaded that shader yet
				continue;
			}
			const uint32_t shader_id = found->second.id;
			m_profile_applied[i] = true;

			if (entry.action == ModAction::disable)
			{
				if (m_client->SendShaderCommand(ShaderCommand::disable, shader_id))
					m_mods.RecordDisable(m_client->Model(), shader_id);
				continue;
			}

			// A replacement is checked against the shader the game has now, which a patch can have
			// changed, exactly as a new one would be.
			if (!found->second.code.empty())
			{
				const ReplacementCheck check = CheckReplacement(found->second.code.data(), found->second.code.size(),
					entry.byte_code.data(), entry.byte_code.size());
				if (!check.compatible)
				{
					m_profile_message = std::string(Tr("A saved replacement no longer fits its shader: ")) +
						entry.Describe() + "\n" + check.Describe();
					continue;
				}
			}
			std::string message;
			if (m_client->ReplaceShader(shader_id, entry.byte_code, message))
				m_mods.RecordReplacement(m_client->Model(), shader_id, entry.byte_code, entry.source_hlsl,
					entry.profile, entry.note.empty() ? std::string("saved state") : entry.note);
			else
				m_profile_message = std::string(Tr("A saved replacement was refused: ")) + message;
		}
		m_profile_reapplying = pending;
	}

	// ------------------------------------------------------------------------------------------
	// The panel
	// ------------------------------------------------------------------------------------------

	void Application::DrawGameProfilePanel()
	{
		if (m_profile_focus && !m_picking_tabs)
		{
			ImGui::SetNextWindowFocus();
			m_profile_focus = false;
		}
		if (!ImGui::Begin(TrId("Game profile")))
		{
			ImGui::End();
			return;
		}

		if (!m_profile.IsOpen())
		{
			ImGui::TextWrapped(Tr("Connect to a game, or open one of its captures: what CyGPUInspector remembers "
			                      "about it appears here."));
			if (!m_profile_message.empty())
				ImGui::TextWrapped("%s", m_profile_message.c_str());
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted(m_profile.Executable().c_str());
		SameLineIfFits(TrId("Open the folder"));
		if (ImGui::SmallButton(TrId("Open the folder")))
		{
			std::error_code code;
			std::filesystem::create_directories(m_profile.Directory(), code);
			ShellExecuteW(nullptr, L"open", m_profile.Directory().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		char line[256];
		std::snprintf(line, sizeof(line), Tr("First seen %s, last seen %s, %u session(s)"), m_profile.FirstSeen().c_str(),
			m_profile.LastSeen().c_str(), m_profile.Sessions());
		TextDisabledWrapped(line);
		if (!m_profile.Api().empty())
			ImGui::TextDisabled("%s, %s", m_profile.Api().c_str(), m_profile.Adapter().c_str());
		if (!m_profile_live)
			ImGui::TextDisabled("%s", Tr("A capture is open: the profile is shown, not updated."));
		if (!m_profile_message.empty())
			ImGui::TextWrapped("%s", m_profile_message.c_str());

		// ---- persistent state ---------------------------------------------------------------
		ImGui::SeparatorText(Tr("Saved state"));
		TextDisabledWrapped(Tr("The shaders you left disabled or replaced in this game, kept by what they are rather "
		                       "than by their number in one session. Saved as state.cygimod, the format CyGPUInjector "
		                       "reads."));

		ImGui::BeginDisabled(!m_profile_live || m_mods.Empty());
		if (ImGui::Button(TrId("Save this session's changes")))
			SaveProfileState();
		ImGui::EndDisabled();
		SameLineIfFits(TrId("Reapply now"));
		ImGui::BeginDisabled(!m_profile_live || m_profile_state.Empty());
		if (ImGui::Button(TrId("Reapply now")))
		{
			m_profile_applied.assign(m_profile_state.entries.size(), false);
			m_profile_reapplying = true;
			m_profile_since_reapply = 1.0;
		}
		ImGui::EndDisabled();
		SameLineIfFits(TrId("Forget the saved state"));
		ImGui::BeginDisabled(m_profile_state.Empty());
		if (ImGui::Button(TrId("Forget the saved state")))
		{
			std::string error;
			if (m_profile.ClearState(error))
			{
				m_profile_state = ModPackage();
				m_profile_applied.clear();
				m_profile_reapplying = false;
				m_profile_message.clear();
			}
			else
			{
				m_profile_message = error;
			}
		}
		ImGui::EndDisabled();

		bool reapply = m_profile.ReapplyOnConnect();
		if (ImGui::Checkbox(TrId("Reapply when this game connects"), &reapply))
			m_profile.SetReapplyOnConnect(reapply);

		if (m_profile_state.Empty())
		{
			ImGui::TextDisabled("%s", Tr("Nothing saved for this game."));
		}
		else
		{
			if (m_profile_reapplying)
				ImGui::TextDisabled("%s", Tr("Reapplying: each change is applied when the game loads its shader."));
			for (size_t i = 0; i < m_profile_state.entries.size(); ++i)
			{
				const ModEntry &entry = m_profile_state.entries[i];
				const bool applied = i < m_profile_applied.size() && m_profile_applied[i];
				ImGui::BulletText("%s  %s%s%s", entry.Describe().c_str(), entry.note.empty() ? "" : "(",
					entry.note.c_str(), entry.note.empty() ? "" : ")");
				if (m_profile_live)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", applied ? Tr("applied") : Tr("not applied"));
				}
			}
		}

		// ---- captures ----------------------------------------------------------------------
		ImGui::SeparatorText(Tr("Captures of this game"));
		{
			const std::string folder = GameProfile::FolderName(m_profile.Executable());
			int shown = 0;
			CaptureInfo to_open;
			bool open = false;
			for (const CaptureInfo &info : m_captures)
			{
				if (GameProfile::FolderName(info.process_name) != folder)
					continue;
				ImGui::PushID(shown++);
				if (ImGui::Selectable(info.Describe().c_str()))
				{
					to_open = info;
					open = true;
				}
				ImGui::PopID();
			}
			if (shown == 0)
				TextDisabledWrapped(Tr("No capture of this game yet: File > Save the frame to disk."));
			// Opened after the loop: opening one changes the list being walked.
			if (open)
				OpenCapture(to_open);
		}

		// ---- shaders -----------------------------------------------------------------------
		ImGui::SeparatorText(Tr("Shaders of this game"));
		ImGui::Text(Tr("%zu shader(s) known in this game."), m_profile.Shaders().size());
		if (m_profile_live)
		{
			std::snprintf(line, sizeof(line), Tr("This session: %u new, %u seen in an earlier session, %u with the same "
				"code as a known one."), m_profile_new, m_profile_known, m_profile_known_code);
			TextDisabledWrapped(line);
		}

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##profilefilter", Tr("filter by signature or stage"), m_profile_filter,
			sizeof(m_profile_filter));

		// Signature -> id in the current model, so a row can select the shader it names.
		std::unordered_map<std::string, uint32_t> ids;
		if (SessionModel *active = ActiveModel())
		{
			std::lock_guard<std::mutex> lock(active->Mutex());
			for (const ShaderInfo &shader : active->Shaders())
				ids[shader.signature.ToHex()] = shader.id;
		}

		std::vector<const std::pair<const std::string, KnownShader> *> rows;
		rows.reserve(m_profile.Shaders().size());
		for (const auto &member : m_profile.Shaders())
			if (ContainsText(member.first, m_profile_filter) ||
			    ContainsText(ShaderStageName(member.second.stage), m_profile_filter))
				rows.push_back(&member);

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
			ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("profileshaders", 6, kFlags, ImVec2(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y))))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(TrId("Signature"));
			ImGui::TableSetupColumn(TrId("Stage"));
			ImGui::TableSetupColumn(TrId("Sessions"));
			ImGui::TableSetupColumn(TrId("First seen"));
			ImGui::TableSetupColumn(TrId("Last seen"));
			ImGui::TableSetupColumn(TrId("Now"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::string &signature = rows[static_cast<size_t>(row)]->first;
					const KnownShader &shader = rows[static_cast<size_t>(row)]->second;
					const auto id = ids.find(signature);
					const bool here = id != ids.end();

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(row);
					const bool selected = here && id->second == m_selected_shader;
					if (ImGui::Selectable(signature.substr(0, 16).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && here)
						m_selected_shader = id->second;
					ImGui::PopID();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(ShaderStageName(shader.stage));
					ImGui::TableNextColumn();
					ImGui::Text("%u", shader.sessions);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(shader.first_seen.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(shader.last_seen.c_str());
					ImGui::TableNextColumn();
					if (!here)
						ImGui::TextDisabled("-");
					else if (m_profile_live && !m_profile.WasKnownBefore(shader.signature))
						ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.55f, 1.0f), Tr("#%u, new"), id->second);
					else
						ImGui::Text("#%u", id->second);
				}
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}
}
