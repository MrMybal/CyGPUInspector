// CyGPUInspectorApp — remembering what you changed, so it can leave the session.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ModRecorder.hpp"

#include "Session/SessionModel.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace cygi
{
	namespace
	{
		// Reads what a shader is from the model, under its lock.
		bool DescribeShader(const SessionModel &model, uint32_t shader_id, ModEntry &out)
		{
			std::lock_guard<std::mutex> lock(model.Mutex());
			const ShaderInfo *shader = model.ShaderById(shader_id);
			if (shader == nullptr || shader->semantic_hash.IsZero())
				return false;

			out.semantic_hash = shader->semantic_hash;
			out.signature = shader->signature;
			out.stage = shader->stage;
			out.format = shader->format;
			out.shader_model = shader->shader_model;
			return true;
		}

		std::string LocalTimestamp()
		{
			SYSTEMTIME now = {};
			GetLocalTime(&now);

			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth,
				now.wDay, now.wHour, now.wMinute, now.wSecond);
			return buffer;
		}
	}

	RecordedMod *ModRecorder::FindBySemanticHash(const Sha256Digest &hash)
	{
		const auto found = std::find_if(m_mods.begin(), m_mods.end(), [&](const RecordedMod &mod) {
			return mod.entry.semantic_hash == hash;
		});
		return found != m_mods.end() ? &*found : nullptr;
	}

	void ModRecorder::RecordReplacement(const SessionModel &model, uint32_t shader_id,
	                                    std::vector<uint8_t> byte_code, std::string source_hlsl,
	                                    std::string profile, std::string origin, bool include)
	{
		ModEntry entry;
		if (!DescribeShader(model, shader_id, entry) || byte_code.empty())
			return;

		entry.action = ModAction::replace;
		entry.byte_code = std::move(byte_code);
		entry.source_hlsl = std::move(source_hlsl);
		entry.profile = std::move(profile);

		if (RecordedMod *existing = FindBySemanticHash(entry.semantic_hash))
		{
			// Keep whatever note the user had already written about this shader: they annotated
			// the shader, not the particular attempt.
			entry.note = existing->entry.note;
			existing->entry = std::move(entry);
			existing->shader_id = shader_id;
			existing->origin = std::move(origin);
			existing->include = include;
			return;
		}

		RecordedMod mod;
		mod.entry = std::move(entry);
		mod.shader_id = shader_id;
		mod.origin = std::move(origin);
		mod.include = include;
		m_mods.push_back(std::move(mod));
	}

	void ModRecorder::RecordDisable(const SessionModel &model, uint32_t shader_id)
	{
		ModEntry entry;
		if (!DescribeShader(model, shader_id, entry))
			return;

		entry.action = ModAction::disable;

		if (RecordedMod *existing = FindBySemanticHash(entry.semantic_hash))
		{
			entry.note = existing->entry.note;
			existing->entry = std::move(entry);
			existing->shader_id = shader_id;
			existing->origin = "disabled";
			return;
		}

		RecordedMod mod;
		mod.entry = std::move(entry);
		mod.shader_id = shader_id;
		mod.origin = "disabled";
		m_mods.push_back(std::move(mod));
	}

	void ModRecorder::Forget(const SessionModel &model, uint32_t shader_id)
	{
		ModEntry entry;
		if (!DescribeShader(model, shader_id, entry))
			return;

		m_mods.erase(std::remove_if(m_mods.begin(), m_mods.end(), [&](const RecordedMod &mod) {
			return mod.entry.semantic_hash == entry.semantic_hash;
		}), m_mods.end());
	}

	uint32_t ModRecorder::CountIncluded() const
	{
		uint32_t count = 0;
		for (const RecordedMod &mod : m_mods)
			if (mod.include)
				++count;
		return count;
	}

	bool ModRecorder::BuildPackage(const SessionModel &model, ModPackage &out, std::string &error) const
	{
		out.entries.clear();
		for (const RecordedMod &mod : m_mods)
			if (mod.include)
				out.entries.push_back(mod.entry);

		if (out.entries.empty())
		{
			error = "nothing is ticked: a package with no entry would do nothing";
			return false;
		}

		out.created = LocalTimestamp();
		out.tool_version = kVersionString;

		// Which game this was made on. Advisory only — the package matches on shader hashes — but
		// it is what explains a package that loads and never matches anything.
		std::lock_guard<std::mutex> lock(model.Mutex());
		if (model.HasSession())
		{
			out.target_process = model.Session().process_name;
			out.target_api = GraphicsApiName(model.Session().api);
			out.target_adapter = model.Session().adapter_name;
		}
		return true;
	}
}
