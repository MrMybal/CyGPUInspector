// CyGPUInspectorApp — the messages of a shader compiler, as lines of the source.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CompilerMessages.hpp"

#include <cctype>

namespace cygi
{
	namespace
	{
		// Reads the digits at `at`, advancing it; 0 when there are none.
		size_t Number(const std::string &text, size_t &at)
		{
			size_t value = 0;
			bool any = false;
			while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])))
			{
				value = value * 10 + static_cast<size_t>(text[at] - '0');
				++at;
				any = true;
			}
			return any ? value : 0;
		}

		// "error" before any "warning" in what follows the position.
		bool IsError(const std::string &rest)
		{
			const size_t error = rest.find("error");
			const size_t warning = rest.find("warning");
			return error != std::string::npos && (warning == std::string::npos || error < warning);
		}

		bool ParseLine(const std::string &line, CompilerMessage &out)
		{
			// FXC: name(line,column) or name(line,column-column) followed by ": error" / ": warning".
			const size_t open = line.find('(');
			if (open != std::string::npos)
			{
				size_t at = open + 1;
				const size_t row = Number(line, at);
				if (row != 0 && at < line.size() && line[at] == ',')
				{
					++at;
					const size_t column = Number(line, at);
					const size_t close = line.find(')', at);
					if (close != std::string::npos && line.compare(close, 3, "): ") == 0)
					{
						out.line = row;
						out.column = column;
						out.error = IsError(line.substr(close));
						out.text = line;
						return true;
					}
				}
			}

			// DXC and clang: name:line:column: error: ...  The name can hold a drive letter, so every
			// colon is tried until one is followed by line:column:.
			for (size_t colon = line.find(':'); colon != std::string::npos; colon = line.find(':', colon + 1))
			{
				size_t at = colon + 1;
				const size_t row = Number(line, at);
				if (row == 0 || at >= line.size() || line[at] != ':')
					continue;
				++at;
				const size_t column = Number(line, at);
				if (at >= line.size() || line[at] != ':')
					continue;
				out.line = row;
				out.column = column;
				out.error = IsError(line.substr(at + 1));
				out.text = line;
				return true;
			}
			return false;
		}
	}

	std::vector<CompilerMessage> ParseCompilerMessages(const std::string &messages)
	{
		std::vector<CompilerMessage> result;
		size_t start = 0;
		while (start < messages.size())
		{
			size_t end = messages.find('\n', start);
			if (end == std::string::npos)
				end = messages.size();
			std::string line = messages.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			CompilerMessage message;
			if (ParseLine(line, message))
				result.push_back(std::move(message));
			else if (!result.empty() && !line.empty() && line.find_first_not_of(" \t~^") != std::string::npos &&
			         line[0] == ' ')
				result.back().text += "\n" + line;   // DXC's source excerpt under a message

			start = end + 1;
		}
		return result;
	}
}
