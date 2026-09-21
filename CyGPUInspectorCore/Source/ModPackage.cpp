// CyGPUInspector — reading and writing a mod package.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/ModPackage.hpp"

#include "CyGPUInspectorCore/Json.hpp"
#include "CyGPUInspectorCore/ShaderBlob.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace cygi
{
	namespace
	{
		bool WriteWholeFile(const std::filesystem::path &path, const void *data, size_t size,
		                    std::string &error)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				error = "could not write " + path.string();
				return false;
			}
			if (size != 0)
				file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
			if (!file)
			{
				error = "could not finish writing " + path.string();
				return false;
			}
			return true;
		}

		bool ReadWholeFile(const std::filesystem::path &path, std::vector<uint8_t> &out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamoff size = file.tellg();
			if (size < 0)
				return false;
			file.seekg(0);

			out.resize(static_cast<size_t>(size));
			if (size != 0)
				file.read(reinterpret_cast<char *>(out.data()), size);
			return static_cast<bool>(file);
		}

		ShaderStage StageFromName(const std::string &name)
		{
			for (uint32_t i = 0; i <= static_cast<uint32_t>(ShaderStage::callable); ++i)
				if (name == ShaderStageName(static_cast<ShaderStage>(i)))
					return static_cast<ShaderStage>(i);
			return ShaderStage::unknown;
		}

		ShaderFormat FormatFromName(const std::string &name)
		{
			for (uint32_t i = 0; i <= static_cast<uint32_t>(ShaderFormat::glsl_source); ++i)
				if (name == ShaderFormatName(static_cast<ShaderFormat>(i)))
					return static_cast<ShaderFormat>(i);
			return ShaderFormat::unknown;
		}

		// Shader model as it is written in a profile: 0x50 becomes "5_0".
		uint32_t ShaderModelFromName(const std::string &name)
		{
			if (name.size() < 3 || name[1] != '_')
				return 0;
			const int major = name[0] - '0';
			const int minor = name[2] - '0';
			if (major < 0 || major > 15 || minor < 0 || minor > 15)
				return 0;
			return static_cast<uint32_t>((major << 4) | minor);
		}
	}

	const char *ModActionName(ModAction action)
	{
		switch (action)
		{
		case ModAction::replace: return "replace";
		case ModAction::disable: return "disable";
		default: return "unknown";
		}
	}

	bool ModActionFromName(const std::string &name, ModAction &out)
	{
		if (name == "replace") { out = ModAction::replace; return true; }
		if (name == "disable") { out = ModAction::disable; return true; }
		return false;
	}

	std::string ModEntry::Describe() const
	{
		std::string text = semantic_hash.ToShortHex(8);
		text += " ";
		text += ShaderStageName(stage);
		text += ", ";
		text += ModActionName(action);
		if (action == ModAction::replace)
			text += " (" + std::to_string(byte_code.size()) + " bytes)";
		return text;
	}

	uint32_t ModPackage::CountOf(ModAction action) const
	{
		uint32_t count = 0;
		for (const ModEntry &entry : entries)
			if (entry.action == action)
				++count;
		return count;
	}

	std::string ModPackage::SuggestDirectoryName(const std::string &name)
	{
		std::string clean;
		for (const char c : name)
		{
			const unsigned char value = static_cast<unsigned char>(c);
			if (std::isalnum(value) != 0 || c == '-' || c == '_')
				clean += c;
			else if (c == ' ')
				clean += '_';
		}
		if (clean.empty())
			clean = "Mod";
		return clean + kExtension;
	}

	bool ModPackage::LooksLikePackage(const std::string &directory)
	{
		std::error_code code;
		const std::filesystem::path path(directory);
		return std::filesystem::is_directory(path, code) &&
		       std::filesystem::is_regular_file(path / "mod.json", code);
	}

	bool ModPackage::Save(const std::string &directory, std::string &error) const
	{
		if (entries.empty())
		{
			error = "there is nothing to export: no shader has been replaced or disabled";
			return false;
		}

		const std::filesystem::path root(directory);
		std::error_code code;
		std::filesystem::create_directories(root / "shaders", code);
		if (code)
		{
			error = "could not create " + root.string() + ": " + code.message();
			return false;
		}

		Json manifest = Json::Object();
		manifest["format"] = Json(std::string("cygpuinspector-mod"));
		manifest["format_version"] = Json(kFormatVersion);
		manifest["name"] = Json(name);
		manifest["author"] = Json(author);
		manifest["version"] = Json(version);
		manifest["description"] = Json(description);
		manifest["created"] = Json(created);
		manifest["tool_version"] = Json(tool_version);

		Json target = Json::Object();
		target["process_name"] = Json(target_process);
		target["api"] = Json(target_api);
		target["adapter"] = Json(target_adapter);
		manifest["target"] = target;

		Json list = Json::Array();
		for (const ModEntry &entry : entries)
		{
			if (entry.semantic_hash.IsZero())
			{
				error = "an entry has no semantic hash, so nothing could ever match it";
				return false;
			}
			if (entry.action == ModAction::replace && entry.byte_code.empty())
			{
				error = "a replacement entry carries no byte code: " + entry.Describe();
				return false;
			}

			Json item = Json::Object();
			item["action"] = Json(std::string(ModActionName(entry.action)));
			item["semantic_hash"] = Json(entry.semantic_hash.ToHex());
			item["signature"] = Json(entry.signature.ToHex());
			item["stage"] = Json(std::string(ShaderStageName(entry.stage)));
			item["format"] = Json(std::string(ShaderFormatName(entry.format)));
			item["shader_model"] = Json(std::string(ShaderModelName(entry.shader_model)));
			if (!entry.note.empty())
				item["note"] = Json(entry.note);

			if (entry.action == ModAction::replace)
			{
				// Named by the hash it matches, so the file a reader is looking at is obviously
				// the one the manifest entry above points to.
				const std::string base = entry.semantic_hash.ToHex();
				const std::string byte_code_path = "shaders/" + base + ".cso";
				if (!WriteWholeFile(root / byte_code_path, entry.byte_code.data(),
				                    entry.byte_code.size(), error))
					return false;
				item["byte_code"] = Json(byte_code_path);
				item["byte_code_size"] = Json(static_cast<uint32_t>(entry.byte_code.size()));
				if (!entry.profile.empty())
					item["profile"] = Json(entry.profile);

				if (!entry.source_hlsl.empty())
				{
					const std::string source_path = "shaders/" + base + ".hlsl";
					if (!WriteWholeFile(root / source_path, entry.source_hlsl.data(),
					                    entry.source_hlsl.size(), error))
						return false;
					item["source"] = Json(source_path);
				}
			}

			list.Push(item);
		}
		manifest["entries"] = list;

		const std::string text = manifest.Write(2);
		return WriteWholeFile(root / "mod.json", text.data(), text.size(), error);
	}

	bool ModPackage::Load(const std::string &directory, ModPackage &out, std::string &error,
	                      bool load_byte_code)
	{
		out = ModPackage();

		const std::filesystem::path root(directory);
		std::vector<uint8_t> bytes;
		if (!ReadWholeFile(root / "mod.json", bytes))
		{
			error = "mod.json is missing from " + root.string();
			return false;
		}

		Json manifest;
		std::string parse_error;
		if (!Json::Parse(std::string(bytes.begin(), bytes.end()), manifest, &parse_error))
		{
			error = "mod.json is not valid JSON: " + parse_error;
			return false;
		}

		if (manifest["format"].AsString() != "cygpuinspector-mod")
		{
			error = "this directory is not a CyGPUInspector mod package";
			return false;
		}
		if (manifest["format_version"].AsUInt32() > kFormatVersion)
		{
			error = "this package was written by a newer version of CyGPUInspector";
			return false;
		}

		out.name = manifest["name"].AsString();
		out.author = manifest["author"].AsString();
		out.version = manifest["version"].AsString();
		out.description = manifest["description"].AsString();
		out.created = manifest["created"].AsString();
		out.tool_version = manifest["tool_version"].AsString();
		out.target_process = manifest["target"]["process_name"].AsString();
		out.target_api = manifest["target"]["api"].AsString();
		out.target_adapter = manifest["target"]["adapter"].AsString();

		for (const Json &item : manifest["entries"].Items())
		{
			ModEntry entry;
			if (!ModActionFromName(item["action"].AsString(), entry.action))
				continue;   // an action a newer version understands and this one does not

			if (!Sha256Digest::FromHex(item["semantic_hash"].AsString(), entry.semantic_hash) ||
			    entry.semantic_hash.IsZero())
				continue;   // unmatched forever: skip rather than pretend it is loaded
			Sha256Digest::FromHex(item["signature"].AsString(), entry.signature);

			entry.stage = StageFromName(item["stage"].AsString());
			entry.format = FormatFromName(item["format"].AsString());
			entry.shader_model = ShaderModelFromName(item["shader_model"].AsString());
			entry.profile = item["profile"].AsString();
			entry.note = item["note"].AsString();

			if (entry.action == ModAction::replace)
			{
				const std::string byte_code_path = item["byte_code"].AsString();
				if (byte_code_path.empty())
					continue;

				if (load_byte_code)
				{
					if (!ReadWholeFile(root / byte_code_path, entry.byte_code) || entry.byte_code.empty())
					{
						error = "the byte code of an entry is missing: " + byte_code_path;
						return false;
					}

					// The file is what will be handed to the graphics driver, so it is checked
					// here rather than trusted: a truncated or edited .cso must not reach a game.
					ShaderBlobInfo info;
					if (!ParseShaderBlob(entry.byte_code.data(), entry.byte_code.size(), info) ||
					    !info.valid)
					{
						error = "the byte code of an entry is not a valid shader container: " +
							byte_code_path;
						return false;
					}
				}

				const std::string source_path = item["source"].AsString();
				if (!source_path.empty() && load_byte_code)
				{
					std::vector<uint8_t> source;
					if (ReadWholeFile(root / source_path, source))
						entry.source_hlsl.assign(source.begin(), source.end());
				}
			}

			out.entries.push_back(std::move(entry));
		}

		if (out.entries.empty())
		{
			error = "the package has no entry this version can apply";
			return false;
		}
		return true;
	}
}
