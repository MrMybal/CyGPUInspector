// CyDoc file-URL integration. Copyright (C) 2026 Cyberalien, AGPL-3.0-or-later.
#pragma once
#include <filesystem>
#include <string>

namespace cygi
{
	// Launch the local manual using stable product topic IDs.
	class HelpLauncher
	{
	public:
		void Open(const std::string &topic = "welcome", const std::string &language = {});
		void SetUrlOutput(const std::filesystem::path &path) { m_output = path; }
		void Draw();
		static std::string ContextTopic(const std::string &window);
		// Normalizes the installed path and escapes UTF-8 with Windows' API, then uses CyDoc's route helper.
		static std::wstring BuildUrl(const std::filesystem::path &index, const std::string &topic,
			const std::string &language, std::string &error);
	private:
		bool m_pending = false;
		std::string m_topic = "welcome", m_language = "en", m_error;
		std::filesystem::path m_output;
	};
}
