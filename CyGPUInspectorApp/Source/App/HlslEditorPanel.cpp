// CyGPUInspectorApp — HLSL: the coloured HLSL tab and the HLSL editor window.
//
// One document, shown in one of two places: the HLSL tab of the shader code panel, or a window of
// its own that opens as a separate Windows window, which can be maximised on any screen. Either
// way it is the same editor with the same undo history: syntax colouring, line numbers, find and
// replace, the compiler's errors marked on their lines, test compilation for any profile, and
// Compile & Inject into the running game.
//
// The document belongs to one shader. Selecting another shader loads that one's reconstruction,
// unless the document was edited: work in progress is never replaced behind someone's back.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <fstream>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	namespace
	{
		std::wstring Widen(const std::string &text)
		{
			if (text.empty())
				return {};
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
			return out;
		}

		std::string Narrow(const std::wstring &text)
		{
			if (text.empty())
				return {};
			const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
				nullptr, nullptr);
			std::string out(static_cast<size_t>(length), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
			return out;
		}

		constexpr wchar_t kFilter[] = L"HLSL (*.hlsl;*.hlsli;*.fx;*.fxh)\0*.hlsl;*.hlsli;*.fx;*.fxh\0All files (*.*)\0*.*\0";

		// The standard Windows file dialogs. Modal: the interface waits, which is what a person
		// choosing a file expects.
		bool AskPath(bool save, const std::string &suggested, std::string &out)
		{
			wchar_t path[MAX_PATH * 4] = {};
			const std::wstring initial = Widen(suggested);
			wcsncpy_s(path, initial.c_str(), _TRUNCATE);

			OPENFILENAMEW dialog = {};
			dialog.lStructSize = sizeof(dialog);
			dialog.hwndOwner = GetActiveWindow();
			dialog.lpstrFilter = kFilter;
			dialog.lpstrFile = path;
			dialog.nMaxFile = static_cast<DWORD>(std::size(path));
			dialog.lpstrDefExt = L"hlsl";
			dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
			if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)))
				return false;
			out = Narrow(path);
			return true;
		}

		std::string FileName(const std::string &path)
		{
			const size_t slash = path.find_last_of("\\/");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}
	}

	// ------------------------------------------------------------------------------------------
	// The document
	// ------------------------------------------------------------------------------------------

	void Application::OpenHlslEditor(uint32_t shader_id, const std::string &text, const std::string &origin,
	                                 bool open_window)
	{
		m_hlsl_editor.SetText(text);
		m_hlsl_shader = shader_id;
		m_hlsl_origin = origin;
		m_hlsl_messages.clear();
		m_hlsl_status.clear();
		const std::string profile = shader_id != 0 ? ProfileOf(shader_id) : std::string();
		if (!profile.empty())
			std::snprintf(m_hlsl_profile, sizeof(m_hlsl_profile), "%s", profile.c_str());
		if (open_window)
		{
			m_hlsl_window = true;
			m_hlsl_window_focus = true;
		}
	}

	bool Application::HlslModified() const
	{
		return m_hlsl_editor.IsModified();
	}

	void Application::CompileEditorHlsl()
	{
		const std::string profile = m_hlsl_profile[0] != '\0' ? std::string(m_hlsl_profile) : ProfileOf(m_hlsl_shader);
		if (profile.empty())
		{
			m_hlsl_status = Tr("Give a profile to compile for, such as ps_5_0 or cs_6_6.");
			return;
		}
		const std::string entry = m_hlsl_entry[0] != '\0' ? std::string(m_hlsl_entry) : std::string("main");

		const CompileResult result = CompileHlsl(m_hlsl_editor.GetText(), entry, profile, "edited.hlsl");
		m_hlsl_messages = m_hlsl_editor.MarkCompilerMessages(result.messages.empty() ? result.error : result.messages);

		char status[256];
		if (result.ok)
			std::snprintf(status, sizeof(status), Tr("Compiles as %s with %s in %.1f ms, %zu warning(s)."),
				profile.c_str(), result.tool.c_str(), result.milliseconds, m_hlsl_messages.size());
		else
			std::snprintf(status, sizeof(status), Tr("Compilation failed as %s: %zu message(s)."), profile.c_str(),
				m_hlsl_messages.size());
		m_hlsl_status = status;
		m_inject_message = result.ok ? "compiles as " + profile
			: "compilation failed: " + (result.messages.empty() ? result.error : result.messages);
	}

	void Application::InjectEditorHlsl()
	{
		if (!IsLive() || m_hlsl_shader == 0)
			return;
		// Checked here first, so a failure lands on its lines instead of in a sentence.
		const std::string profile = ProfileOf(m_hlsl_shader);
		const std::string entry = m_hlsl_entry[0] != '\0' ? std::string(m_hlsl_entry) : std::string("main");
		const CompileResult check = CompileHlsl(m_hlsl_editor.GetText(), entry, profile, "edited.hlsl");
		m_hlsl_messages = m_hlsl_editor.MarkCompilerMessages(check.messages.empty() ? check.error : check.messages);
		if (!check.ok)
		{
			char status[256];
			std::snprintf(status, sizeof(status), Tr("Compilation failed as %s: %zu message(s)."), profile.c_str(),
				m_hlsl_messages.size());
			m_hlsl_status = status;
			return;
		}

		const std::string origin = m_hlsl_origin.empty() ? std::string("edited HLSL")
			: m_hlsl_origin + (HlslModified() ? ", edited" : "");
		InjectHlsl(m_hlsl_shader, m_hlsl_editor.GetText(), origin.c_str(), true, entry);
		m_hlsl_status = m_inject_message;
	}

	void Application::OpenHlslFile()
	{
		std::string path;
		if (!AskPath(false, m_hlsl_file, path))
			return;
		std::ifstream file(Widen(path), std::ios::binary);
		if (!file)
		{
			m_hlsl_status = Tr("The file could not be read.");
			return;
		}
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
			text.erase(0, 3);
		const uint32_t shader = m_hlsl_shader;
		OpenHlslEditor(shader, text, "file " + FileName(path), false);
		m_hlsl_file = path;
	}

	void Application::SaveHlslFile()
	{
		std::string path = m_hlsl_file;
		if (path.empty())
			path = m_hlsl_shader != 0 ? "shader_" + std::to_string(m_hlsl_shader) + ".hlsl" : "edited.hlsl";
		if (!AskPath(true, path, path))
			return;
		std::ofstream file(Widen(path), std::ios::binary | std::ios::trunc);
		const std::string text = m_hlsl_editor.GetText();
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!file)
		{
			m_hlsl_status = Tr("The file could not be written.");
			return;
		}
		m_hlsl_file = path;
		m_hlsl_status = std::string(Tr("Saved to ")) + path;
	}

	// ------------------------------------------------------------------------------------------
	// The HLSL tab
	// ------------------------------------------------------------------------------------------

	void Application::DrawHlslTab(const ShaderAnalysis &analysis, bool has_analysis)
	{
		if (!has_analysis)
		{
			ImGui::TextDisabled(Tr("Waiting for the disassembly of shader #%u..."), m_selected_shader);
			return;
		}

		if (analysis.decompilations.empty())
		{
			ImGui::TextWrapped(Tr("Nothing shown here will ever be the original source: that does "
			                      "not exist in the compiled shader. Every backend that can handle "
			                      "this shader produces its own reconstruction, and they are kept "
			                      "side by side so they can be compared."));
			ImGui::Spacing();
			if (m_analysis.IsPending(m_selected_shader))
				ImGui::TextDisabled(Tr("Decompiling..."));
			else if (ImGui::Button(TrId("Decompile")))
				m_analysis.RequestDecompilation(m_selected_shader);

			ImGui::Spacing();
			ImGui::TextDisabled(Tr("Available backends:"));
			for (const DecompilerBackend *backend : DecompilerRegistry::Instance().All())
				ImGui::BulletText(Tr("%s %s (%s)"), backend->Info().name.c_str(),
					backend->Info().version.c_str(), backend->Info().license.c_str());
			return;
		}

		if (m_selected_backend >= static_cast<int>(analysis.decompilations.size()))
			m_selected_backend = 0;

		// A new shader brings its own reconstruction into the document, unless the document holds
		// edits of another one: those are kept, and loading is a click away.
		const bool other_shader = m_hlsl_shader != m_selected_shader;
		if (other_shader && !HlslModified())
		{
			const DecompilationResult &first = analysis.decompilations[static_cast<size_t>(m_selected_backend)];
			OpenHlslEditor(m_selected_shader, first.hlsl, first.backend_id + " " + first.backend_version, false);
		}

		// One tab per backend: no result is ever merged into or hidden by another.
		if (!ImGui::BeginTabBar("backends"))
			return;
		for (size_t i = 0; i < analysis.decompilations.size(); ++i)
		{
			const DecompilationResult &result = analysis.decompilations[i];
			if (!ImGui::BeginTabItem(result.backend_id.c_str()))
				continue;
			m_selected_backend = static_cast<int>(i);

			ImVec4 colour(0.9f, 0.6f, 0.3f, 1.0f);
			if (result.verdict == ValidationVerdict::equivalent_disassembly)
				colour = ImVec4(0.55f, 0.9f, 0.55f, 1.0f);
			else if (result.verdict == ValidationVerdict::compiles)
				colour = ImVec4(0.9f, 0.85f, 0.5f, 1.0f);

			ImGui::TextColored(colour, "%s", ValidationVerdictName(result.verdict));
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("(%s)"), result.validation_messages.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Copy")))
				ImGui::SetClipboardText(result.hlsl.c_str());

			ImGui::TextDisabled(Tr("AI reconstruction: none. This is a decompiler output, "
			                       "version %s, in %.1f ms."), result.backend_version.c_str(), result.milliseconds);
			if (!result.notes.empty())
				ImGui::TextWrapped("%s", result.notes.c_str());

			if (m_hlsl_shader != m_selected_shader)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
					m_hlsl_shader != 0 ? Tr("The HLSL editor holds your edits of shader #%u.")
					                   : Tr("The HLSL editor holds edits of a shader from an earlier session."),
					m_hlsl_shader);
				if (ImGui::SmallButton(TrId("Load this shader instead")))
					OpenHlslEditor(m_selected_shader, result.hlsl, result.backend_id + " " + result.backend_version, false);
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Show the HLSL editor")))
				{
					m_hlsl_window = true;
					m_hlsl_window_focus = true;
				}
			}
			else
			{
				// Edit, compile and inject: the point of a reconstruction is to change it.
				if (ImGui::SmallButton(TrId("Reset to backend output")))
					OpenHlslEditor(m_selected_shader, result.hlsl, result.backend_id + " " + result.backend_version, false);
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Compile")))
					CompileEditorHlsl();
				ImGui::SameLine();
				ImGui::BeginDisabled(!IsLive());
				if (ImGui::SmallButton(TrId("Compile & Inject")))
					InjectEditorHlsl();
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Restore original")))
				{
					m_client->SendShaderCommand(ShaderCommand::restore, m_selected_shader);
					m_inject_message = "the original shader is back";
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Open in a window")))
				{
					m_hlsl_window = true;
					m_hlsl_window_focus = true;
				}

				if (!m_inject_message.empty())
					ImGui::TextWrapped("%s", m_inject_message.c_str());
			}

			ImGui::Separator();
			if (m_hlsl_window)
			{
				ImGui::TextDisabled(Tr("Being edited in the HLSL editor window."));
				if (ImGui::Button(TrId("Show the HLSL editor")))
					m_hlsl_window_focus = true;
			}
			else if (m_hlsl_shader == m_selected_shader)
			{
				m_hlsl_editor.Render("hlsl_tab");
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	// ------------------------------------------------------------------------------------------
	// The HLSL editor window
	// ------------------------------------------------------------------------------------------

	void Application::DrawHlslEditorWindow()
	{
		if (!m_hlsl_window)
			return;

		// A window of its own, with the system's title bar: it can be moved to another screen,
		// maximised and closed like any other window, and never merges back into the main one.
		ImGuiWindowClass window_class;
		window_class.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
		window_class.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
		ImGui::SetNextWindowClass(&window_class);

		const ImGuiViewport *main = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(main->WorkSize.x * 0.7f, main->WorkSize.y * 0.8f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(main->WorkPos.x + main->WorkSize.x * 0.15f, main->WorkPos.y + main->WorkSize.y * 0.1f),
			ImGuiCond_FirstUseEver);
		if (m_hlsl_window_focus)
		{
			ImGui::SetNextWindowFocus();
			m_hlsl_window_focus = false;
		}

		// The part after ### keeps the window's identity, and its place, whatever the title says.
		char title[160];
		if (m_hlsl_shader != 0)
			std::snprintf(title, sizeof(title), "%s - %s #%u%s###HLSL editor", Tr("HLSL editor"), Tr("shader"),
				m_hlsl_shader, HlslModified() ? " *" : "");
		else
			std::snprintf(title, sizeof(title), "%s%s###HLSL editor", Tr("HLSL editor"), HlslModified() ? " *" : "");

		const bool visible = ImGui::Begin(title, &m_hlsl_window,
			ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse);
		if (!visible)
		{
			ImGui::End();
			return;
		}

		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool live = IsLive() && m_hlsl_shader != 0;

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu(TrId("File")))
			{
				if (ImGui::MenuItem(TrId("Open a file..."), "Ctrl+O"))
					OpenHlslFile();
				if (ImGui::MenuItem(TrId("Save as..."), "Ctrl+S"))
					SaveHlslFile();
				ImGui::Separator();
				if (ImGui::MenuItem(TrId("Close the editor")))
					m_hlsl_window = false;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(TrId("Edit")))
			{
				if (ImGui::MenuItem(TrId("Undo"), "Ctrl+Z", false, m_hlsl_editor.CanUndo()))
					m_hlsl_editor.Undo();
				if (ImGui::MenuItem(TrId("Redo"), "Ctrl+Y", false, m_hlsl_editor.CanRedo()))
					m_hlsl_editor.Redo();
				ImGui::Separator();
				if (ImGui::MenuItem(TrId("Find and replace"), "Ctrl+F"))
					m_hlsl_editor.OpenFind();
				if (ImGui::MenuItem(TrId("Select all"), "Ctrl+A"))
					m_hlsl_editor.SelectAll();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(TrId("View")))
			{
				bool wrap = m_hlsl_editor.IsWordWrap();
				if (ImGui::MenuItem(TrId("Word wrap"), nullptr, &wrap))
					m_hlsl_editor.SetWordWrap(wrap);
				bool spaces = m_hlsl_editor.IsShowWhitespace();
				if (ImGui::MenuItem(TrId("Show whitespace"), nullptr, &spaces))
					m_hlsl_editor.SetShowWhitespace(spaces);
				bool minimap = m_hlsl_editor.IsShowMiniMap();
				if (ImGui::MenuItem(TrId("Minimap"), nullptr, &minimap))
					m_hlsl_editor.SetShowMiniMap(minimap);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(TrId("Build")))
			{
				if (ImGui::MenuItem(TrId("Compile"), "F7"))
					CompileEditorHlsl();
				if (ImGui::MenuItem(TrId("Compile & Inject"), "F5", false, live))
					InjectEditorHlsl();
				if (ImGui::MenuItem(TrId("Restore original"), nullptr, false, live))
				{
					m_client->SendShaderCommand(ShaderCommand::restore, m_hlsl_shader);
					m_inject_message = "the original shader is back";
					m_hlsl_status = m_inject_message;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		if (focused)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
				CompileEditorHlsl();
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && live)
				InjectEditorHlsl();
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				SaveHlslFile();
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
				OpenHlslFile();
		}

		// What the document is, and what it is compiled for.
		if (m_hlsl_shader != 0)
			ImGui::Text(Tr("Shader #%u, profile %s"), m_hlsl_shader, ProfileOf(m_hlsl_shader).c_str());
		else
			ImGui::TextDisabled(Tr("No shader: compile for the profile below, or save to a file."));
		ImGui::SameLine();
		ImGui::TextDisabled(Tr("Source: %s"), m_hlsl_origin.empty() ? Tr("typed") : m_hlsl_origin.c_str());

		// Every reconstruction of this shader can be loaded here, decompilers and AI clean-ups alike.
		ShaderAnalysis analysis;
		const bool has_analysis = m_hlsl_shader != 0 && m_analysis.Result(m_hlsl_shader, analysis);
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("##load", Tr("Load a reconstruction..."), ImGuiComboFlags_HeightLarge))
		{
			if (has_analysis)
				for (const DecompilationResult &result : analysis.decompilations)
				{
					const std::string label = result.backend_id + " " + result.backend_version + " (" +
						ValidationVerdictName(result.verdict) + ")";
					if (ImGui::Selectable(label.c_str()))
						OpenHlslEditor(m_hlsl_shader, result.hlsl, result.backend_id + " " + result.backend_version, false);
				}
			if (m_hlsl_shader == m_notes_shader)
				for (const Annotation &annotation : m_notes.annotations)
				{
					const std::string fence = "```hlsl\n";
					const size_t open = annotation.text.find(fence);
					const size_t close = annotation.text.rfind("\n```");
					if (annotation.kind != AnnotationKind::cleanup || open == std::string::npos || close == std::string::npos ||
					    close < open + fence.size())
						continue;
					const std::string label = std::string(Tr("AI clean-up")) + " (" + annotation.author + ", " +
						annotation.created + ")";
					if (ImGui::Selectable(label.c_str()))
						OpenHlslEditor(m_hlsl_shader, annotation.text.substr(open + fence.size(), close - open - fence.size()),
							"AI clean-up (" + annotation.author + ")", false);
				}
			if (!has_analysis || analysis.decompilations.empty())
				ImGui::TextDisabled(Tr("Nothing decompiled for this shader yet."));
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputTextWithHint("##entry", Tr("entry point"), m_hlsl_entry, sizeof(m_hlsl_entry));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputTextWithHint("##profile", Tr("profile"), m_hlsl_profile, sizeof(m_hlsl_profile));
		ImGui::SameLine();
		if (ImGui::Button(TrId("Compile")))
			CompileEditorHlsl();
		ImGui::SameLine();
		ImGui::BeginDisabled(!live);
		if (ImGui::Button(TrId("Compile & Inject")))
			InjectEditorHlsl();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", Tr("Injects into the running game with the shader's own profile. Needs a game "
			                           "connected, not a capture."));

		// The editor takes the room left above the output and the status line.
		const float output_height = m_hlsl_messages.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 6.0f;
		const float status_height = ImGui::GetFrameHeightWithSpacing();
		m_hlsl_editor.Render("hlsl_window", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - output_height - status_height));

		if (!m_hlsl_messages.empty())
		{
			ImGui::BeginChild("output", ImVec2(0.0f, output_height), ImGuiChildFlags_Borders);
			for (size_t i = 0; i < m_hlsl_messages.size(); ++i)
			{
				const CompilerMessage &message = m_hlsl_messages[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::PushStyleColor(ImGuiCol_Text, message.error ? ImVec4(1.0f, 0.5f, 0.45f, 1.0f)
					: ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
				const std::string first_line = message.text.substr(0, message.text.find('\n'));
				if (ImGui::Selectable(first_line.c_str()) && message.line != 0)
					m_hlsl_editor.GoToLine(message.line);
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered() && message.text.find('\n') != std::string::npos)
					ImGui::SetTooltip("%s", message.text.c_str());
				ImGui::PopID();
			}
			ImGui::EndChild();
		}

		ImGui::Text(Tr("Ln %zu, Col %zu | %zu lines"), m_hlsl_editor.CursorLine(), m_hlsl_editor.CursorColumn(),
			m_hlsl_editor.LineCount());
		if (HlslModified())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", Tr("| modified"));
		}
		if (!m_hlsl_status.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("| %s", m_hlsl_status.c_str());
		}
		ImGui::End();
	}
}
