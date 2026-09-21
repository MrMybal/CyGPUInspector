// CyGPUInspector — a small JSON value, reader and writer.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cygi
{
	namespace
	{
		const std::string kEmptyString;
		const Json kNullValue;

		void WriteEscaped(std::string &out, const std::string &text)
		{
			out += '"';
			for (char c : text)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned char>(c));
						out += buffer;
					}
					else
					{
						out += c;
					}
					break;
				}
			}
			out += '"';
		}

		void WriteNumber(std::string &out, double value)
		{
			// Integers are by far the common case here and must not gain a ".000000" tail.
			if (value == std::floor(value) && std::fabs(value) < 9.0e15)
			{
				char buffer[32];
				std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
				out += buffer;
				return;
			}

			char buffer[40];
			std::snprintf(buffer, sizeof(buffer), "%.10g", value);
			out += buffer;
		}

		class Parser
		{
		public:
			Parser(const std::string &text) : m_text(text) {}

			bool ParseValue(Json &out)
			{
				SkipSpaces();
				if (m_cursor >= m_text.size())
					return Fail("unexpected end of input");

				switch (m_text[m_cursor])
				{
				case '{': return ParseObject(out);
				case '[': return ParseArray(out);
				case '"':
				{
					std::string value;
					if (!ParseString(value))
						return false;
					out = Json(std::move(value));
					return true;
				}
				case 't':
					if (m_text.compare(m_cursor, 4, "true") != 0)
						return Fail("expected true");
					m_cursor += 4;
					out = Json(true);
					return true;
				case 'f':
					if (m_text.compare(m_cursor, 5, "false") != 0)
						return Fail("expected false");
					m_cursor += 5;
					out = Json(false);
					return true;
				case 'n':
					if (m_text.compare(m_cursor, 4, "null") != 0)
						return Fail("expected null");
					m_cursor += 4;
					out = Json();
					return true;
				default:
					return ParseNumber(out);
				}
			}

			void SkipSpaces()
			{
				while (m_cursor < m_text.size() &&
				       std::isspace(static_cast<unsigned char>(m_text[m_cursor])))
					++m_cursor;
			}

			bool AtEnd()
			{
				SkipSpaces();
				return m_cursor >= m_text.size();
			}

			const std::string &Error() const { return m_error; }

		private:
			bool Fail(const char *message)
			{
				if (m_error.empty())
					m_error = std::string(message) + " at offset " + std::to_string(m_cursor);
				return false;
			}

			bool ParseString(std::string &out)
			{
				if (m_cursor >= m_text.size() || m_text[m_cursor] != '"')
					return Fail("expected a string");
				++m_cursor;

				out.clear();
				while (m_cursor < m_text.size())
				{
					const char c = m_text[m_cursor++];
					if (c == '"')
						return true;

					if (c != '\\')
					{
						out += c;
						continue;
					}
					if (m_cursor >= m_text.size())
						return Fail("unterminated escape");

					const char escape = m_text[m_cursor++];
					switch (escape)
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u':
					{
						if (m_cursor + 4 > m_text.size())
							return Fail("truncated \\u escape");
						const std::string digits = m_text.substr(m_cursor, 4);
						m_cursor += 4;
						const unsigned int code = static_cast<unsigned int>(
							std::strtoul(digits.c_str(), nullptr, 16));
						// Enough for the ASCII and Latin-1 range these files actually contain.
						if (code < 0x80)
						{
							out += static_cast<char>(code);
						}
						else if (code < 0x800)
						{
							out += static_cast<char>(0xC0 | (code >> 6));
							out += static_cast<char>(0x80 | (code & 0x3F));
						}
						else
						{
							out += static_cast<char>(0xE0 | (code >> 12));
							out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
							out += static_cast<char>(0x80 | (code & 0x3F));
						}
						break;
					}
					default:
						return Fail("unknown escape");
					}
				}
				return Fail("unterminated string");
			}

			bool ParseNumber(Json &out)
			{
				const size_t start = m_cursor;
				if (m_cursor < m_text.size() && (m_text[m_cursor] == '-' || m_text[m_cursor] == '+'))
					++m_cursor;
				while (m_cursor < m_text.size() &&
				       (std::isdigit(static_cast<unsigned char>(m_text[m_cursor])) ||
				        m_text[m_cursor] == '.' || m_text[m_cursor] == 'e' || m_text[m_cursor] == 'E' ||
				        m_text[m_cursor] == '-' || m_text[m_cursor] == '+'))
					++m_cursor;

				if (m_cursor == start)
					return Fail("expected a value");

				out = Json(std::strtod(m_text.substr(start, m_cursor - start).c_str(), nullptr));
				return true;
			}

			bool ParseArray(Json &out)
			{
				++m_cursor;   // '['
				out = Json::Array();

				SkipSpaces();
				if (m_cursor < m_text.size() && m_text[m_cursor] == ']')
				{
					++m_cursor;
					return true;
				}

				for (;;)
				{
					Json value;
					if (!ParseValue(value))
						return false;
					out.Push(std::move(value));

					SkipSpaces();
					if (m_cursor >= m_text.size())
						return Fail("unterminated array");
					if (m_text[m_cursor] == ',') { ++m_cursor; continue; }
					if (m_text[m_cursor] == ']') { ++m_cursor; return true; }
					return Fail("expected , or ] in array");
				}
			}

			bool ParseObject(Json &out)
			{
				++m_cursor;   // '{'
				out = Json::Object();

				SkipSpaces();
				if (m_cursor < m_text.size() && m_text[m_cursor] == '}')
				{
					++m_cursor;
					return true;
				}

				for (;;)
				{
					SkipSpaces();
					std::string key;
					if (!ParseString(key))
						return false;

					SkipSpaces();
					if (m_cursor >= m_text.size() || m_text[m_cursor] != ':')
						return Fail("expected : after a key");
					++m_cursor;

					Json value;
					if (!ParseValue(value))
						return false;
					out[key] = std::move(value);

					SkipSpaces();
					if (m_cursor >= m_text.size())
						return Fail("unterminated object");
					if (m_text[m_cursor] == ',') { ++m_cursor; continue; }
					if (m_text[m_cursor] == '}') { ++m_cursor; return true; }
					return Fail("expected , or } in object");
				}
			}

			const std::string &m_text;
			size_t m_cursor = 0;
			std::string m_error;
		};
	}

	bool Json::AsBool(bool fallback) const
	{
		return m_type == Type::boolean ? m_boolean : fallback;
	}

	double Json::AsNumber(double fallback) const
	{
		return m_type == Type::number ? m_number : fallback;
	}

	uint64_t Json::AsUInt(uint64_t fallback) const
	{
		return m_type == Type::number && m_number >= 0.0 ? static_cast<uint64_t>(m_number) : fallback;
	}

	uint32_t Json::AsUInt32(uint32_t fallback) const
	{
		return static_cast<uint32_t>(AsUInt(fallback));
	}

	const std::string &Json::AsString() const
	{
		return m_type == Type::string ? m_string : kEmptyString;
	}

	const Json &Json::operator[](const std::string &key) const
	{
		if (m_type != Type::object)
			return kNullValue;
		const auto it = m_object.find(key);
		return it != m_object.end() ? it->second : kNullValue;
	}

	Json &Json::operator[](const std::string &key)
	{
		if (m_type != Type::object)
		{
			m_type = Type::object;
			m_object.clear();
		}
		return m_object[key];
	}

	bool Json::Has(const std::string &key) const
	{
		return m_type == Type::object && m_object.find(key) != m_object.end();
	}

	void Json::Push(Json value)
	{
		if (m_type != Type::array)
		{
			m_type = Type::array;
			m_array.clear();
		}
		m_array.push_back(std::move(value));
	}

	void Json::WriteTo(std::string &out, int indent, int depth) const
	{
		const bool pretty = indent > 0;
		const std::string padding = pretty ? std::string(static_cast<size_t>((depth + 1) * indent), ' ') : "";
		const std::string closing = pretty ? std::string(static_cast<size_t>(depth * indent), ' ') : "";
		const char *separator = pretty ? "\n" : "";

		switch (m_type)
		{
		case Type::null: out += "null"; break;
		case Type::boolean: out += m_boolean ? "true" : "false"; break;
		case Type::number: WriteNumber(out, m_number); break;
		case Type::string: WriteEscaped(out, m_string); break;
		case Type::array:
			if (m_array.empty()) { out += "[]"; break; }
			out += '[';
			out += separator;
			for (size_t i = 0; i < m_array.size(); ++i)
			{
				out += padding;
				m_array[i].WriteTo(out, indent, depth + 1);
				if (i + 1 != m_array.size())
					out += ',';
				out += separator;
			}
			out += closing;
			out += ']';
			break;
		case Type::object:
			if (m_object.empty()) { out += "{}"; break; }
			out += '{';
			out += separator;
			{
				size_t index = 0;
				for (const auto &entry : m_object)
				{
					out += padding;
					WriteEscaped(out, entry.first);
					out += pretty ? ": " : ":";
					entry.second.WriteTo(out, indent, depth + 1);
					if (++index != m_object.size())
						out += ',';
					out += separator;
				}
			}
			out += closing;
			out += '}';
			break;
		}
	}

	std::string Json::Write(int indent) const
	{
		std::string out;
		WriteTo(out, indent, 0);
		return out;
	}

	bool Json::Parse(const std::string &text, Json &out, std::string *error)
	{
		// Notepad and PowerShell both put a UTF-8 byte order mark at the start of a file they
		// write, and a translator editing Lang/fr.json is going to use one of them. It is not
		// valid JSON, so it is skipped here rather than at each of the places that read a file.
		static const char kBom[] = "\xEF\xBB\xBF";
		const bool has_bom = text.size() >= 3 && text.compare(0, 3, kBom) == 0;
		// Named, because the parser keeps a reference to what it is given.
		const std::string without_bom = has_bom ? text.substr(3) : std::string();
		Parser parser(has_bom ? without_bom : text);

		Json parsed;
		if (!parser.ParseValue(parsed))
		{
			if (error != nullptr)
				*error = parser.Error();
			return false;
		}
		if (!parser.AtEnd())
		{
			if (error != nullptr)
				*error = "trailing characters after the value";
			return false;
		}

		out = std::move(parsed);
		return true;
	}
}
