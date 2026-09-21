// CyGPUInspectorRS — shader and pipeline tracking.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ShaderTracker.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

namespace cygi
{
	uint32_t ShaderTracker::RegisterShader(const void *code, size_t code_size, ShaderStage stage_hint)
	{
		if (code == nullptr || code_size == 0)
			return 0;

		const Sha256Digest signature = ComputeShaderSignature(code, code_size);

		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			const auto it = m_by_signature.find(signature);
			if (it != m_by_signature.end())
				return it->second;
		}

		ShaderBlobInfo info;
		ParseShaderBlob(code, code_size, info);

		ShaderRecord record;
		record.signature = signature;
		record.semantic_hash = ComputeShaderSemanticHash(code, code_size);
		// The container knows the real stage; the pipeline sub-object type is only a fallback
		// (it is the single source of truth for GLSL and SPIR-V, which carry no program kind).
		record.stage = info.stage != ShaderStage::unknown ? info.stage : stage_hint;
		record.format = info.format;
		record.shader_model = info.shader_model;
		record.code_size = static_cast<uint32_t>(code_size);

		std::unique_lock<std::shared_mutex> lock(m_mutex);

		// Another thread may have registered the same shader while we were hashing.
		const auto it = m_by_signature.find(signature);
		if (it != m_by_signature.end())
			return it->second;

		record.id = static_cast<uint32_t>(m_shaders.size()) + 1;
		m_shaders.push_back(record);
		m_shader_code.emplace_back(static_cast<const uint8_t *>(code),
			static_cast<const uint8_t *>(code) + code_size);
		m_by_signature.emplace(signature, record.id);
		m_pending_shaders.push_back(record.id);
		return record.id;
	}

	uint32_t ShaderTracker::RegisterPipeline(uint64_t native_handle, const uint32_t *shader_ids, uint32_t count,
	                                         uint32_t stage_mask, bool is_compute)
	{
		if (native_handle == 0)
			return 0;

		std::unique_lock<std::shared_mutex> lock(m_mutex);

		const auto existing = m_pipeline_by_handle.find(native_handle);
		if (existing != m_pipeline_by_handle.end())
			return existing->second;

		PipelineRecord record;
		record.id = static_cast<uint32_t>(m_pipelines.size()) + 1;
		record.native_handle = native_handle;
		record.shader_count = count < kMaxPipelineShaders ? count : kMaxPipelineShaders;
		for (uint32_t i = 0; i < record.shader_count; ++i)
			record.shader_ids[i] = shader_ids[i];
		record.stage_mask = stage_mask;
		record.is_compute = is_compute;

		m_pipelines.push_back(record);
		m_blueprints.emplace_back();
		m_pipeline_by_handle.emplace(native_handle, record.id);
		m_pending_pipelines.push_back(record.id);
		return record.id;
	}

	void ShaderTracker::UnregisterPipeline(uint64_t native_handle)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		// The record itself is kept: draw events of past frames still refer to its id.
		m_pipeline_by_handle.erase(native_handle);
	}

	uint32_t ShaderTracker::PipelineId(uint64_t native_handle) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_pipeline_by_handle.find(native_handle);
		return it != m_pipeline_by_handle.end() ? it->second : 0;
	}

	bool ShaderTracker::CopyPipeline(uint64_t native_handle, PipelineRecord &out) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_pipeline_by_handle.find(native_handle);
		if (it == m_pipeline_by_handle.end() || it->second == 0 || it->second > m_pipelines.size())
			return false;
		out = m_pipelines[it->second - 1];
		return true;
	}

	bool ShaderTracker::CopyPipelineById(uint32_t pipeline_id, PipelineRecord &out) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (pipeline_id == 0 || pipeline_id > m_pipelines.size())
			return false;
		out = m_pipelines[pipeline_id - 1];
		return true;
	}

	bool ShaderTracker::CopyShader(uint32_t shader_id, ShaderRecord &out) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (shader_id == 0 || shader_id > m_shaders.size())
			return false;
		out = m_shaders[shader_id - 1];
		return true;
	}

	bool ShaderTracker::CopyShaderCode(uint32_t shader_id, std::vector<uint8_t> &out) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (shader_id == 0 || shader_id > m_shader_code.size())
			return false;
		out = m_shader_code[shader_id - 1];
		return !out.empty();
	}

	void ShaderTracker::SetBlueprint(uint32_t pipeline_id, PipelineBlueprint &&blueprint)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		if (pipeline_id == 0 || pipeline_id > m_blueprints.size())
			return;
		m_blueprints[pipeline_id - 1] = std::move(blueprint);
	}

	bool ShaderTracker::IsPipelineReplaceable(uint32_t pipeline_id, std::string &reason) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (pipeline_id == 0 || pipeline_id > m_blueprints.size())
		{
			reason = "unknown pipeline";
			return false;
		}
		reason = m_blueprints[pipeline_id - 1].reason;
		return m_blueprints[pipeline_id - 1].replaceable;
	}

	reshade::api::pipeline ShaderTracker::BuildVariant(reshade::api::device *device, uint32_t pipeline_id,
	                                                   uint32_t shader_id, const void *code, size_t code_size,
	                                                   std::string &error) const
	{
		// The read lock is held across the creation on purpose: the blueprint points into storage
		// this table owns, and nothing may move it while the pipeline is being built.
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (pipeline_id == 0 || pipeline_id > m_blueprints.size())
		{
			error = "unknown pipeline";
			return reshade::api::pipeline{ 0 };
		}

		const PipelineBlueprint &blueprint = m_blueprints[pipeline_id - 1];
		if (!blueprint.replaceable)
		{
			error = blueprint.reason.empty() ? "this pipeline was not captured" : blueprint.reason;
			return reshade::api::pipeline{ 0 };
		}

		// The byte code is read straight from the table here: calling CopyShaderCode would take
		// the same shared mutex a second time, which is undefined behaviour and can deadlock.
		const auto provide_code = [this](uint32_t id, std::vector<uint8_t> &out) {
			if (id == 0 || id > m_shader_code.size() || m_shader_code[id - 1].empty())
				return false;
			out = m_shader_code[id - 1];
			return true;
		};
		return blueprint.CreateVariant(device, provide_code, shader_id, code, code_size, error);
	}

	uint32_t ShaderTracker::ShaderIdBySignature(const Sha256Digest &signature) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_by_signature.find(signature);
		return it != m_by_signature.end() ? it->second : 0;
	}

	std::vector<uint32_t> ShaderTracker::PipelinesUsingShader(uint32_t shader_id) const
	{
		std::vector<uint32_t> result;
		if (shader_id == 0)
			return result;

		std::shared_lock<std::shared_mutex> lock(m_mutex);
		for (const PipelineRecord &pipeline : m_pipelines)
			if (pipeline.UsesShader(shader_id))
				result.push_back(pipeline.id);
		return result;
	}

	std::vector<PendingShaderCode> ShaderTracker::TakePendingShaders()
	{
		std::vector<PendingShaderCode> result;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		result.reserve(m_pending_shaders.size());
		for (uint32_t id : m_pending_shaders)
		{
			if (id == 0 || id > m_shader_code.size())
				continue;
			PendingShaderCode pending;
			pending.shader_id = id;
			pending.code = m_shader_code[id - 1];
			result.push_back(std::move(pending));
		}
		m_pending_shaders.clear();
		return result;
	}

	std::vector<PipelineRecord> ShaderTracker::TakePendingPipelines()
	{
		std::vector<PipelineRecord> result;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		result.reserve(m_pending_pipelines.size());
		for (uint32_t id : m_pending_pipelines)
		{
			if (id == 0 || id > m_pipelines.size())
				continue;
			result.push_back(m_pipelines[id - 1]);
		}
		m_pending_pipelines.clear();
		return result;
	}

	void ShaderTracker::QueueEverything()
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_pending_shaders.clear();
		m_pending_pipelines.clear();
		for (const ShaderRecord &shader : m_shaders)
			m_pending_shaders.push_back(shader.id);
		for (const PipelineRecord &pipeline : m_pipelines)
			m_pending_pipelines.push_back(pipeline.id);
	}

	uint32_t ShaderTracker::ShaderCount() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_shaders.size());
	}

	uint32_t ShaderTracker::PipelineCount() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_pipelines.size());
	}
}
