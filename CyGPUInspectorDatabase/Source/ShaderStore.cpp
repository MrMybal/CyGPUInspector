// CyGPUInspectorDatabase — persistent store of everything derived from a shader.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDatabase/ShaderStore.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <fstream>

namespace cygi
{
	namespace
	{
		std::filesystem::path ExecutableDirectory()
		{
			wchar_t path[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			return std::filesystem::path(path).parent_path();
		}

		std::string JsonEscape(const std::string &text)
		{
			std::string out;
			out.reserve(text.size());
			for (char c : text)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default: out += c; break;
				}
			}
			return out;
		}
	}

	ShaderStore::ShaderStore(std::filesystem::path root)
	{
		m_root = root.empty() ? ExecutableDirectory() / "Database" : std::move(root);

		std::error_code error;
		std::filesystem::create_directories(m_root / "shaders", error);
		if (error)
		{
			m_last_error = "the database directory could not be created: " + error.message();
			return;
		}
		m_usable = true;
	}

	bool ShaderStore::EnsureDirectory(const std::filesystem::path &path) const
	{
		std::error_code error;
		std::filesystem::create_directories(path, error);
		if (error)
		{
			m_last_error = error.message();
			return false;
		}
		return true;
	}

	std::filesystem::path ShaderStore::DirectoryFor(const Sha256Digest &signature) const
	{
		const std::string hex = signature.ToHex();
		// Two hex characters of fan-out: a flat directory of thousands of shaders is painful for
		// both the file system and anyone browsing it by hand.
		return m_root / "shaders" / hex.substr(0, 2) / hex;
	}

	bool ShaderStore::SaveByteCode(const Sha256Digest &signature, ShaderFormat format, const void *code,
	                               size_t size)
	{
		if (!m_usable || code == nullptr || size == 0)
			return false;

		const std::filesystem::path directory = DirectoryFor(signature);
		if (!EnsureDirectory(directory))
			return false;

		const char *extension = format == ShaderFormat::dxil ? "original.dxil"
			: format == ShaderFormat::spirv ? "original.spv" : "original.dxbc";

		std::ofstream file(directory / extension, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			m_last_error = "the byte code could not be written";
			return false;
		}
		file.write(static_cast<const char *>(code), static_cast<std::streamsize>(size));
		return file.good();
	}

	bool ShaderStore::SaveText(const Sha256Digest &signature, const std::string &relative_name,
	                           const std::string &text)
	{
		if (!m_usable)
			return false;

		const std::filesystem::path target = DirectoryFor(signature) / relative_name;
		if (!EnsureDirectory(target.parent_path()))
			return false;

		std::ofstream file(target, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			m_last_error = "could not write " + relative_name;
			return false;
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		return file.good();
	}

	bool ShaderStore::LoadText(const Sha256Digest &signature, const std::string &relative_name,
	                           std::string &out) const
	{
		if (!m_usable)
			return false;

		std::ifstream file(DirectoryFor(signature) / relative_name, std::ios::binary);
		if (!file)
			return false;

		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	bool ShaderStore::Has(const Sha256Digest &signature, const std::string &relative_name) const
	{
		if (!m_usable)
			return false;

		std::error_code error;
		return std::filesystem::exists(DirectoryFor(signature) / relative_name, error);
	}

	bool ShaderStore::SaveMetadata(const Sha256Digest &signature, const Sha256Digest &semantic_hash,
	                               ShaderStage stage, ShaderFormat format, uint32_t shader_model,
	                               uint32_t code_size, const std::string &process_name)
	{
		SYSTEMTIME now = {};
		GetSystemTime(&now);

		char timestamp[32];
		std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ", now.wYear, now.wMonth,
			now.wDay, now.wHour, now.wMinute, now.wSecond);

		std::string json = "{\n";
		json += "  \"signature\": \"" + signature.ToHex() + "\",\n";
		json += "  \"semantic_hash\": \"" + semantic_hash.ToHex() + "\",\n";
		json += "  \"stage\": \"" + std::string(ShaderStageName(stage)) + "\",\n";
		json += "  \"format\": \"" + std::string(ShaderFormatName(format)) + "\",\n";
		json += "  \"shader_model\": \"" + std::to_string((shader_model >> 4) & 0xF) + "_" +
			std::to_string(shader_model & 0xF) + "\",\n";
		json += "  \"byte_code_size\": " + std::to_string(code_size) + ",\n";
		json += "  \"first_seen\": \"" + std::string(timestamp) + "\",\n";
		json += "  \"first_seen_process\": \"" + JsonEscape(process_name) + "\"\n";
		json += "}\n";

		return SaveText(signature, "metadata.json", json);
	}
}
