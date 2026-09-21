// CyGPUInspector — a small JSON value, reader and writer.
//
// Captures, AI snapshots and the MCP server all need JSON, and none of them needs a fast or
// complete implementation: they need one that is small, dependency free and predictable. This is
// it. It handles the whole grammar except numbers in exotic notations, which nothing here emits.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cygi
{
	class Json
	{
	public:
		enum class Type
		{
			null,
			boolean,
			number,
			string,
			array,
			object,
		};

		Json() = default;
		Json(bool value) : m_type(Type::boolean), m_boolean(value) {}
		Json(double value) : m_type(Type::number), m_number(value) {}
		Json(int value) : m_type(Type::number), m_number(value) {}
		Json(uint32_t value) : m_type(Type::number), m_number(value) {}
		Json(uint64_t value) : m_type(Type::number), m_number(static_cast<double>(value)) {}
		Json(const char *value) : m_type(Type::string), m_string(value != nullptr ? value : "") {}
		Json(std::string value) : m_type(Type::string), m_string(std::move(value)) {}

		static Json Array() { Json json; json.m_type = Type::array; return json; }
		static Json Object() { Json json; json.m_type = Type::object; return json; }

		Type GetType() const { return m_type; }
		bool IsNull() const { return m_type == Type::null; }
		bool IsObject() const { return m_type == Type::object; }
		bool IsArray() const { return m_type == Type::array; }

		// Readers. Each returns the fallback when the type does not match, so a malformed file
		// degrades into defaults instead of throwing.
		bool AsBool(bool fallback = false) const;
		double AsNumber(double fallback = 0.0) const;
		uint64_t AsUInt(uint64_t fallback = 0) const;
		uint32_t AsUInt32(uint32_t fallback = 0) const;
		const std::string &AsString() const;

		// Object access. `operator[]` on a const object returns a shared null value.
		const Json &operator[](const std::string &key) const;
		Json &operator[](const std::string &key);
		bool Has(const std::string &key) const;

		// Array access.
		const std::vector<Json> &Items() const { return m_array; }
		void Push(Json value);
		size_t Size() const { return m_type == Type::array ? m_array.size() : m_object.size(); }

		const std::map<std::string, Json> &Members() const { return m_object; }

		// `indent` < 0 writes everything on one line, which is what the MCP transport wants.
		std::string Write(int indent = 0) const;

		// Returns false and leaves `out` untouched when the text is not valid JSON.
		static bool Parse(const std::string &text, Json &out, std::string *error = nullptr);

	private:
		void WriteTo(std::string &out, int indent, int depth) const;

		Type m_type = Type::null;
		bool m_boolean = false;
		double m_number = 0.0;
		std::string m_string;
		std::vector<Json> m_array;
		std::map<std::string, Json> m_object;
	};
}
