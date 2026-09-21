// CyGPUInspectorApp — exporting the modifications as a package CyGPUInjector can apply.
//
// This panel is the bridge between the two halves of the project. On the left of it, everything
// you did to a running game exists only in that game's memory and dies with it. On the right,
// there is a folder anyone can drop next to their own copy of the game and get the same result,
// with no inspector, no standalone and no IPC.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace cygi
{
	// Every user visible string goes through Tr, which returns the translation when there is one
	// and the English source when there is not. TrId does the same for a label ImGui also uses as
	// an identifier, appending the English as a hidden ### suffix so a layout survives a language
	// change. See CyGPUInspectorCore/Localization.hpp.
	using i18n::Tr;
	using i18n::TrId;
	namespace
	{
		void Help(const char *text)
		{
			ImGui::TextDisabled("(?)");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
				ImGui::TextUnformatted(text);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		// Where a package goes by default: beside the captures, so the two kinds of output of the
		// tool sit together.
		std::filesystem::path DefaultModRoot()
		{
			return CaptureArchive::DefaultRoot().parent_path() / "Mods";
		}
	}

	// ------------------------------------------------------------------------------------
	// Language preference
	//
	// Small enough to live here rather than in a settings subsystem nobody else would use: one
	// key, next to the executable, beside the layout ImGui already writes there.
	// ------------------------------------------------------------------------------------

	namespace
	{
		// Beside the executable, not in the working directory: a shortcut, a launcher or a
		// console started somewhere else would otherwise scatter the user's state across folders
		// and make the language look as though it had not been remembered.
		std::filesystem::path PreferencesPath()
		{
			wchar_t executable[MAX_PATH] = {};
			if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
				return std::filesystem::path("CyGPUInspectorApp.settings.json");
			return std::filesystem::path(executable).parent_path() / "CyGPUInspectorApp.settings.json";
		}
	}

	std::string Application::LoadPreferredLanguage() const
	{
		std::ifstream file(PreferencesPath(), std::ios::binary);
		if (!file)
			return "en";

		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		Json root;
		if (!Json::Parse(text, root))
			return "en";

		const std::string code = root["language"].AsString();
		return code.empty() ? "en" : code;
	}

	void Application::SavePreferredLanguage(const std::string &code) const
	{
		Json root = Json::Object();
		root["language"] = Json(code);

		std::ofstream file(PreferencesPath(), std::ios::binary | std::ios::trunc);
		if (!file)
			return;
		const std::string text = root.Write(2);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
	}

	void Application::WriteTranslationTemplate()
	{
		const std::filesystem::path path =
			std::filesystem::path(i18n::LanguageRoot()) / "template.json";

		std::error_code code;
		std::filesystem::create_directories(path.parent_path(), code);

		std::string error;
		if (i18n::WriteTemplate(path.string(), error))
			m_status = "Translation template written to " + path.string() + " (" +
				std::to_string(i18n::SeenCount()) + " strings)";
		else
			m_status = "Could not write the translation template: " + error;
	}

	void Application::ExportModPackage()
	{
		m_mod_message.clear();

		const SessionModel *model = ActiveModel();
		if (model == nullptr)
		{
			m_mod_message = "No session: connect to a game, or open a capture the modifications "
			                "were made against.";
			return;
		}

		ModPackage package;
		package.name = m_mod_name[0] != '\0' ? m_mod_name : "Untitled";
		package.author = m_mod_author;
		package.version = m_mod_version;
		package.description = m_mod_description;

		std::string error;
		if (!m_mods.BuildPackage(*model, package, error))
		{
			m_mod_message = error;
			return;
		}

		std::filesystem::path directory = m_mod_directory[0] != '\0'
			? std::filesystem::path(m_mod_directory)
			: DefaultModRoot() / ModPackage::SuggestDirectoryName(package.name);

		if (!package.Save(directory.string(), error))
		{
			m_mod_message = error;
			return;
		}

		char message[768];
		std::snprintf(message, sizeof(message),
			"Exported %u replacement(s) and %u disable(s) to %s. Copy that folder into "
			"CyGPUInjector/ next to the game and load CyGPUInjector.addon64 with ReShade.",
			package.CountOf(ModAction::replace), package.CountOf(ModAction::disable),
			directory.string().c_str());
		m_mod_message = message;
	}

	void Application::DrawModExportPanel()
	{
		if (!ImGui::Begin(TrId("Mod export")))
		{
			ImGui::End();
			return;
		}

		ImGui::TextWrapped(Tr("What you changed in the game, written down so it can be applied again "
		                      "without CyGPUInspector."));
		ImGui::SameLine();
		Help(Tr("A package matches shaders by the hash of their code, not by any number from this "
		        "session, so it keeps working in the next run and on someone else's machine. It stops "
		        "working when the game patches that shader — which is the correct behaviour: it will "
		        "match nothing rather than apply your change to something else.\n\n"
		        "CyGPUInjector.addon64 is what applies it. It is a ReShade add-on like this one's "
		        "capture agent: it replaces shaders and skips draws through the official add-on API, "
		        "and it defeats nothing. A game that refuses ReShade is out of scope."));

		ImGui::Separator();

		if (m_mods.Empty())
		{
			ImGui::TextDisabled(Tr("Nothing recorded yet."));
			ImGui::TextWrapped(Tr("Replace a shader from the Shader code panel, or disable one from "
			                      "Details, and it appears here."));
			ImGui::End();
			return;
		}

		ImGui::Text(Tr("%zu modification(s), %u ticked for export"), m_mods.Count(), m_mods.CountIncluded());
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Tick all")))
			for (RecordedMod &mod : m_mods.Mods())
				mod.include = true;
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Untick all")))
			for (RecordedMod &mod : m_mods.Mods())
				mod.include = false;
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Forget all")))
		{
			m_mods.Clear();
			m_mod_message.clear();
		}

		if (ImGui::BeginTable("mods", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		                                 ImGuiTableFlags_SizingStretchProp |
		                                 ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
			ImGui::TableSetupColumn(TrId("Shader"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn(TrId("Action"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn(TrId("Origin"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableSetupColumn(TrId("Note"));
			ImGui::TableHeadersRow();

			for (size_t i = 0; i < m_mods.Mods().size(); ++i)
			{
				RecordedMod &mod = m_mods.Mods()[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Checkbox("##include", &mod.include);

				ImGui::TableNextColumn();
				// Selecting a row selects the shader everywhere else, when this session still has it.
				char label[96];
				std::snprintf(label, sizeof(label), "%s##row", mod.entry.semantic_hash.ToShortHex(8).c_str());
				if (ImGui::Selectable(label, m_selected_shader == mod.shader_id) && mod.shader_id != 0)
					m_selected_shader = mod.shader_id;
				if (ImGui::BeginItemTooltip())
				{
					ImGui::TextUnformatted(Tr("Semantic hash, the key CyGPUInjector matches on:"));
					ImGui::TextUnformatted(mod.entry.semantic_hash.ToHex().c_str());
					ImGui::Separator();
					ImGui::Text(Tr("%s, %s %s"), ShaderStageName(mod.entry.stage),
						ShaderFormatName(mod.entry.format), ShaderModelName(mod.entry.shader_model));
					if (!mod.entry.profile.empty())
						ImGui::Text(Tr("compiled as %s"), mod.entry.profile.c_str());
					ImGui::EndTooltip();
				}

				ImGui::TableNextColumn();
				if (mod.entry.action == ModAction::replace)
					ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), Tr("replace"));
				else
					ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.45f, 1.0f), Tr("disable"));

				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", mod.origin.c_str());

				ImGui::TableNextColumn();
				char note[256] = {};
				std::snprintf(note, sizeof(note), "%s", mod.entry.note.c_str());
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##note", note, sizeof(note)))
					mod.entry.note = note;

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Separator();
		ImGui::TextUnformatted(Tr("Package"));

		ImGui::InputText(TrId("Name"), m_mod_name, sizeof(m_mod_name));
		ImGui::InputText(TrId("Author"), m_mod_author, sizeof(m_mod_author));
		ImGui::InputText(TrId("Version"), m_mod_version, sizeof(m_mod_version));
		ImGui::InputTextMultiline(TrId("Description"), m_mod_description, sizeof(m_mod_description),
			ImVec2(-1.0f, 60.0f));

		ImGui::InputTextWithHint(TrId("Folder"), Tr("left empty: next to the captures"), m_mod_directory,
			sizeof(m_mod_directory));

		if (ImGui::Button(TrId("Export package"), ImVec2(160.0f, 0.0f)))
			ExportModPackage();
		ImGui::SameLine();
		Help(Tr("Writes a .cygimod folder: a readable mod.json plus one .cso per replaced shader, and "
		        "the HLSL each was compiled from. Nothing about it is opaque — you can diff two "
		        "versions of a mod, or hand-edit a note, without this application."));

		if (!m_mod_message.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("%s", m_mod_message.c_str());
		}

		ImGui::End();
	}
}
