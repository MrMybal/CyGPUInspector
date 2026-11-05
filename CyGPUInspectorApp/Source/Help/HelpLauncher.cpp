// Copyright (C) 2026 Cyberalien, AGPL-3.0-or-later.
#include "HelpLauncher.hpp"
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/Localization.hpp>
#include <Windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cyui/Core.h>
#include <fstream>
#include <iterator>
#include "cydoc_link.hpp"

namespace cygi
{
	namespace
	{
		std::wstring Wide(const std::string &text)
		{
			const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
			if (!size) return {};
			std::wstring out(size, 0); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size); return out;
		}
		std::string Utf8(const std::wstring &text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(size, 0); WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr); return out;
		}
	}
	std::wstring HelpLauncher::BuildUrl(const std::filesystem::path &index, const std::string &requested,
		const std::string &language, std::string &error)
	{
		error.clear();
		std::ifstream file(index.parent_path() / "manifest.json", std::ios::binary);
		Json manifest;
		if (!std::filesystem::is_regular_file(index) || !Json::Parse({std::istreambuf_iterator<char>(file), {}}, manifest) ||
			manifest["schemaVersion"].AsUInt() != 1)
		{ error = "The offline documentation is missing or invalid. Restore the complete Help folder beside the application."; return {}; }
		std::string page = requested.empty() ? "welcome" : requested, anchor;
		if (page.rfind("help:", 0) == 0) page.erase(0, 5);
		if (page.rfind("doc:", 0) == 0) page.erase(0, 4);
		std::string query;
		if (const auto pos = page.find('?'); pos != std::string::npos) { query = page.substr(pos + 1); page.erase(pos); }
		if (const auto pos = page.find('#'); pos != std::string::npos) { anchor = page.substr(pos + 1); page.erase(pos); }
		const auto &alias = manifest["aliases"][page].AsString();
		if (!alias.empty()) page = alias;
		const Json *found = nullptr;
		const std::string locale = language == "fr" ? "fr" : "en";
		for (const auto &item : manifest["pages"].Items())
			if (item["id"].AsString() == page && item["locale"].AsString() == locale) { found = &item; break; }
		if (!found)
			for (const auto &item : manifest["pages"].Items())
				if (item["id"].AsString() == page && item["locale"].AsString() == "en") { found = &item; break; }
		if (!found) { error = "The requested documentation page is not available."; return {}; }
		if (!anchor.empty())
		{
			std::ifstream mapping(index.parent_path() / "legacy-routes.json"); Json routes;
			if (Json::Parse({std::istreambuf_iterator<char>(mapping), {}}, routes))
			{
				const auto &translated = routes[page][locale][anchor].AsString();
				if (!translated.empty()) anchor = translated;
			}
			bool exists = false;
			for (const auto &heading : (*found)["headings"].Items()) exists |= heading["id"].AsString() == anchor;
			for (const auto &id : (*found)["anchors"].Items()) exists |= id.AsString() == anchor;
			if (!exists) { error = "The requested documentation section is not available."; return {}; }
		}
		std::vector<std::pair<std::string, std::string>> state;
		if (!query.empty())
		{
			// Explicit stable media IDs; the viewer owns zoom/pan and reading-state persistence.
			if (query.rfind("media=", 0) != 0 || query.find('&') != std::string::npos)
			{ error = "The requested documentation media route is invalid."; return {}; }
			const auto media = query.substr(6);
			if ((*found)["html"].AsString().find("data-cydoc-id=\"" + media + "\"") == std::string::npos)
			{ error = "The requested documentation media is not available."; return {}; }
			state.push_back({"media", media});
		}
		// UrlCreateFromPathW escapes non-ASCII paths using legacy bytes on some Windows versions.
		// Normalize the path first, then explicitly request UTF-8 URI escaping.
		const auto local = std::filesystem::absolute(index).lexically_normal().generic_wstring();
		const auto base = (local.rfind(L"//", 0) == 0 ? L"file:" : L"file:///") + local;
		wchar_t url[32768] = {}; DWORD count = 32768;
		if (FAILED(UrlEscapeW(base.c_str(), url, &count, URL_ESCAPE_AS_UTF8 | URL_ESCAPE_PERCENT)))
		{ error = "The local documentation path could not be converted to a file URL."; return {}; }
		return Wide(cydoc_native::link(Utf8(url), locale, page, anchor, state));
	}
	void HelpLauncher::Open(const std::string &topic, const std::string &language)
	{
		m_topic = topic; if (!language.empty()) m_language = language == "fr" ? "fr" : "en";
		m_pending = true;
	}
	std::string HelpLauncher::ContextTopic(const std::string &window)
	{
		const auto hash = window.find("###");
		const auto name = hash == std::string::npos ? window : window.substr(hash + 3);
		if (name == "Shader code" || name.find("HLSL") != std::string::npos) return "hlsl";
		if (name == "Shaders") return "shaders";
		if (name == "MCP") return "mcp";
		if (name == "AI assistant") return "ai";
		if (name == "Resources" || name == "Preview") return "resources";
		if (name == "Frame timeline") return "timeline";
		if (name == "Deep capture") return "capture";
		if (name == "Captures") return "offline";
		if (name == "Game profile") return "profiles";
		if (name == "Mod export") return "mods";
		if (name == "Connections") return "installation";
		if (name == "Frame graph" || name == "Frame events" || name == "Details") return "graph";
		if (name == "Status") return "troubleshooting";
		return "welcome";
	}
	void HelpLauncher::Draw()
	{
		if (m_pending)
		{
			m_pending = false;
			wchar_t executable[32768] = {}; GetModuleFileNameW(nullptr, executable, 32768);
			const auto url = BuildUrl(std::filesystem::path(executable).parent_path() / "Help/index.html", m_topic, m_language, m_error);
			if (!url.empty())
			{
				if (!m_output.empty()) { std::ofstream output(m_output, std::ios::binary); output << Utf8(url); if (!output) m_error = "The documentation URL file could not be written."; }
				else if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					m_error = "The local documentation could not be opened. Open Help/index.html with a modern browser.";
			}
			if (!m_error.empty()) ImGui::OpenPopup("Offline help###help-error");
		}
		if (ImGui::BeginPopupModal("Offline help###help-error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::PushTextWrapPos(cyui::dp(540)); ImGui::TextUnformatted(i18n::Tr(m_error.c_str())); ImGui::PopTextWrapPos();
			if (ImGui::Button(i18n::Tr("Close"))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}
