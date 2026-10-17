// CyGPUInspector — CyUI shell, navigation and additive appearance preferences.
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"

#include <CyGPUInspectorCore/Localization.hpp>
#include <CyGPUInspectorCore/Version.hpp>
#include <cyui/CyUI.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	void Application::LoadAppearance()
	{
		m_display_dpi = cyui::dpi();
		const Json appearance = LoadPreferences()["appearance"];
		SetAppearance(appearance["theme"].AsString(), appearance["comfortable"].AsBool() ? 1 : 0,
			static_cast<float>(appearance["scale"].AsNumber(1.0)), false);
	}

	void Application::SetAppearance(const std::string &theme, int density, float scale, bool save)
	{
		const std::string requested_theme = theme;
		m_theme = "navy";
		for (const auto &candidate : cyui::builtInThemes())
			if (candidate.key == requested_theme)
				m_theme = requested_theme;
		m_density = density == 1 ? 1 : 0;
		m_ui_scale = std::isfinite(scale) ? std::clamp(scale, 0.85f, 1.75f) : 1.0f;
		cyui::Config config;
		config.themeKey = m_theme.c_str();
		config.density = m_density ? cyui::Density::comfortable : cyui::Density::compact;
		config.dpiScale = m_display_dpi * m_ui_scale;
		config.squareWindows = true;
		cyui::apply(config);
		if (save)
		{
			Json preferences = LoadPreferences();
			preferences["appearance"]["theme"] = Json(m_theme);
			preferences["appearance"]["comfortable"] = Json(m_density == 1);
			preferences["appearance"]["scale"] = Json(static_cast<double>(m_ui_scale));
			SavePreferences(preferences);
			cyui::notify(Tr("Appearance saved"), cyui::Tone::success);
		}
	}

	void Application::UpdateDisplayDpi(float dpi)
	{
		m_display_dpi = std::max(0.5f, dpi);
		if (std::abs(cyui::dpi() - m_display_dpi * m_ui_scale) > 0.001f)
			cyui::applyDpi(m_display_dpi * m_ui_scale);
	}

	void Application::DrawAppearanceMenu()
	{
		if (!ImGui::BeginMenu(TrId("Appearance")))
			return;
		cyui::sectionTitle(Tr("Theme"));
		for (const auto &theme : cyui::builtInThemes())
		{
			if (ImGui::MenuItem(theme.name.c_str(), nullptr, m_theme == theme.key))
				SetAppearance(theme.key, m_density, m_ui_scale);
		}
		ImGui::Separator();
		cyui::sectionTitle(Tr("Density"));
		if (ImGui::MenuItem(TrId("Compact"), nullptr, m_density == 0))
			SetAppearance(m_theme, 0, m_ui_scale);
		if (ImGui::MenuItem(TrId("Comfortable"), nullptr, m_density == 1))
			SetAppearance(m_theme, 1, m_ui_scale);
		ImGui::Separator();
		cyui::sectionTitle(Tr("Interface size"));
		for (const int percent : { 85, 100, 110, 125, 150, 175 })
		{
			char label[20];
			std::snprintf(label, sizeof(label), "%d%%", percent);
			if (ImGui::MenuItem(label, nullptr, std::abs(m_ui_scale * 100.0f - percent) < 0.1f))
				SetAppearance(m_theme, m_density, percent / 100.0f);
		}
		ImGui::EndMenu();
	}

	void Application::FocusPanel(const char *name)
	{
		for (auto &request : m_panel_focus_requests)
			if (request.first == name)
			{
				request.second = 3;
				return;
			}
		m_panel_focus_requests.emplace_back(name, 3);
	}

	bool Application::BeginPanel(const char *name)
	{
		for (auto &request : m_panel_focus_requests)
			if (request.first == name && request.second > 0)
			{
				ImGui::SetNextWindowFocus();
				--request.second;
			}
		return ImGui::Begin(TrId(name));
	}

	void Application::FocusWorkspace(int workspace)
	{
		m_workspace = workspace;
		// Select existing tabs; do not move windows or replace the user's saved docking layout.
		FocusPanel(workspace == 1 ? "Captures" : "Connections");
		FocusPanel(workspace == 1 ? "Resources" : "Shaders");
		FocusPanel(workspace == 2 ? "Preview" : "Frame timeline");
		FocusPanel(workspace == 1 ? "Deep capture" : "Frame events");
		FocusPanel(workspace == 3 ? "AI assistant" : "Details");
		FocusPanel("Shader code");
	}

	void Application::DrawActionBar()
	{
		if (cyui::beginTopBar("##cygi-actions"))
		{
			const char *workspaces[] = { Tr("Inspect"), Tr("Captures"), Tr("Shaders"), Tr("AI assistant") };
			int workspace = m_workspace;
			if (ImGui::GetContentRegionAvail().x < cyui::dp(860))
			{
				if (cyui::combo("##workspace", &workspace, workspaces, IM_ARRAYSIZE(workspaces), cyui::dp(170)))
					FocusWorkspace(workspace);
			}
			else if (cyui::segmented("##workspace", workspaces, IM_ARRAYSIZE(workspaces), &workspace))
				FocusWorkspace(workspace);
			ImGui::SameLine();
			cyui::toolbarSeparator();
			if (cyui::iconButton(cyui::icon::refresh, Tr("Refresh connections (F5)")))
				RefreshSessions();
			ImGui::SameLine();
			if (cyui::iconButton(cyui::icon::folder, Tr("Browse saved captures")))
				FocusPanel("Captures");
			ImGui::SameLine();
			cyui::toolbarSeparator();
			{
				cyui::ScopedDisabled disabled(ActiveModel() == nullptr);
				if (cyui::iconTextButton(cyui::icon::save, TrId("Capture frame"), cyui::Variant::primary))
					SaveCapture();
				cyui::tooltip(Tr("Save the current frame for offline analysis (Ctrl+S)"));
			}
			ImGui::SameLine();
			{
				cyui::ScopedDisabled disabled(!IsLive());
				if (cyui::iconButton(cyui::icon::photo, Tr("Capture bindings, barriers and buffers")))
				{
					FocusPanel("Deep capture");
					StartDeepCapture();
				}
			}
			ImGui::SameLine();
			if (cyui::iconButton(cyui::icon::settings, Tr("Appearance and interface size")))
				ImGui::OpenPopup("##appearance-popup");
			if (ImGui::BeginPopup("##appearance-popup"))
			{
				DrawAppearanceMenu();
				ImGui::EndPopup();
			}
		}
		cyui::endTopBar();

		// Shortcuts never intercept text entry or the focused HLSL editor's own save shortcut.
		ImGuiWindow *code = ImGui::FindWindowByName(TrId("Shader code"));
		ImGuiWindow *focused = ImGui::GetCurrentContext()->NavWindow;
		const bool code_focused = code && focused && ImGui::IsWindowChildOf(focused, code, true, false);
		if (!ImGui::GetIO().WantTextInput && !m_hlsl_window && !code_focused)
		{
			if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal))
				RefreshSessions();
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal) && ActiveModel())
				SaveCapture();
		}
	}

	void Application::DrawSessionBar()
	{
		if (cyui::beginStatusBar("##cygi-session"))
		{
			const bool live = IsLive();
			const bool stopped = live && !m_client->WriterAlive();
			const auto tone = stopped ? cyui::Tone::warning : live ? cyui::Tone::success : cyui::Tone::neutral;
			cyui::statusDot(stopped ? cyui::palette().warning : live ? cyui::palette().success : cyui::palette().textFaint);
			ImGui::SameLine();
			cyui::statusText(tone, "%s", Tr(stopped ? "SESSION STOPPED" : live ? "LIVE" :
				m_capture_model ? "OFFLINE CAPTURE" : "DISCONNECTED"));
			ImGui::SameLine();
			if (ActiveModel())
			{
				cyui::statusText(cyui::Tone::neutral, "%s", m_capture_model ?
					m_capture_info.process_name.c_str() : m_client->Entry().process_name.c_str());
				ImGui::SameLine();
			}
			if (SessionModel *model = ActiveModel())
			{
				std::lock_guard<std::mutex> lock(model->Mutex());
				const auto &frame = model->LastFrame();
				cyui::statusText(cyui::Tone::neutral, Tr("Frame %llu  |  %u draws  |  %u dispatches"),
					static_cast<unsigned long long>(frame.index), frame.draw_count, frame.dispatch_count);
				if (model->Timings().IsValid() && ImGui::GetContentRegionAvail().x > cyui::dp(200))
				{
					ImGui::SameLine();
					cyui::statusText(cyui::Tone::accent, "GPU %.2f ms", model->Timings().Milliseconds(model->Timings().total_ticks));
				}
			}
			else
				cyui::statusText(cyui::Tone::neutral, "%s", Tr("Connect a running application or open a capture"));
			if (!m_status.empty() && ImGui::GetContentRegionAvail().x > cyui::dp(220))
			{
				ImGui::SameLine();
				cyui::statusText(cyui::Tone::neutral, " | %s", m_status.c_str());
				cyui::tooltip(m_status.c_str());
			}
		}
		cyui::endStatusBar();
	}

	void Application::DrawNoSessionState()
	{
		const ImGuiID window = ImGui::GetCurrentWindow()->ID;
		const bool timeline = window == ImHashStr(TrId("Frame timeline"));
		const char *title = "No frame to inspect";
		if (window == ImHashStr(TrId("Shaders"))) title = "No shaders to inspect";
		else if (window == ImHashStr(TrId("Resources"))) title = "No resources to inspect";
		else if (window == ImHashStr(TrId("Details"))) title = "Nothing selected";
		else if (window == ImHashStr(TrId("Shader code"))) title = "No shader selected";
		else if (window == ImHashStr(TrId("Frame events"))) title = "No frame events";
		cyui::emptyState(Tr(title), Tr("Connect a game or open a capture"), cyui::icon::search);
		if (!timeline)
			return;
		const float width = std::max(cyui::dp(1), ImGui::GetContentRegionAvail().x);
		if (cyui::button(TrId("Connections"), cyui::Variant::secondary, ImVec2(width, 0)))
			FocusPanel("Connections");
		if (cyui::ghostButton(TrId("Browse saved captures"), ImVec2(width, 0)))
			FocusPanel("Captures");
	}
}
