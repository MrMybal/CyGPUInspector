// CyGPUInspectorRS — runtime control of shaders.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "RuntimeControl.hpp"

namespace cygi
{
	RuntimeControl::RuntimeControl()
		: m_pipeline_disabled(new std::atomic<uint8_t>[kMaxControlledPipelines]()),
		  m_pipeline_highlighted(new std::atomic<uint8_t>[kMaxControlledPipelines]()),
		  m_pipeline_replacement(new std::atomic<uint64_t>[kMaxControlledPipelines]())
	{
	}

	void RuntimeControl::SetShaderDisabled(uint32_t shader_id, bool disabled, const ShaderTracker &shaders)
	{
		if (shader_id == 0)
			return;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (disabled)
				m_disabled_shaders.insert(shader_id);
			else
				m_disabled_shaders.erase(shader_id);
		}
		RefreshPipelines(shader_id, shaders);
		UpdateActiveFlag();
	}

	void RuntimeControl::SetShaderHighlighted(uint32_t shader_id, bool highlighted, const ShaderTracker &shaders)
	{
		if (shader_id == 0)
			return;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (highlighted)
				m_highlighted_shaders.insert(shader_id);
			else
				m_highlighted_shaders.erase(shader_id);
		}
		RefreshPipelines(shader_id, shaders);
		UpdateActiveFlag();
	}

	void RuntimeControl::RestoreShader(uint32_t shader_id, const ShaderTracker &shaders)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_disabled_shaders.erase(shader_id);
			m_highlighted_shaders.erase(shader_id);
		}
		RefreshPipelines(shader_id, shaders);
		UpdateActiveFlag();
	}

	void RuntimeControl::RestoreAll()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_disabled_shaders.clear();
			m_highlighted_shaders.clear();
		}
		for (uint32_t i = 0; i < kMaxControlledPipelines; ++i)
		{
			m_pipeline_disabled[i].store(0, std::memory_order_relaxed);
			m_pipeline_highlighted[i].store(0, std::memory_order_relaxed);
		}
		m_active.store(false, std::memory_order_relaxed);
	}

	uint32_t RuntimeControl::SetReplacement(reshade::api::device *device, const ShaderTracker &shaders,
	                                        uint32_t shader_id, const void *code, size_t code_size,
	                                        std::string &error)
	{
		if (device == nullptr || shader_id == 0 || code == nullptr || code_size == 0)
		{
			error = "nothing to replace";
			return 0;
		}

		// Whatever was there before goes away first: a shader has one replacement at a time.
		ClearReplacement(device, shader_id);

		const std::vector<uint32_t> pipelines = shaders.PipelinesUsingShader(shader_id);
		if (pipelines.empty())
		{
			error = "no known pipeline uses this shader";
			return 0;
		}

		std::vector<std::pair<uint32_t, uint64_t>> created;
		std::string last_error;

		for (uint32_t pipeline_id : pipelines)
		{
			if (pipeline_id >= kMaxControlledPipelines)
			{
				last_error = "this pipeline is beyond the runtime control table";
				continue;
			}

			std::string build_error;
			const reshade::api::pipeline variant =
				shaders.BuildVariant(device, pipeline_id, shader_id, code, code_size, build_error);
			if (variant.handle == 0)
			{
				last_error = build_error;
				continue;
			}

			m_pipeline_replacement[pipeline_id].store(variant.handle, std::memory_order_release);
			created.emplace_back(pipeline_id, variant.handle);
		}

		if (created.empty())
		{
			error = last_error.empty() ? "no pipeline could be rebuilt" : last_error;
			return 0;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_replacements[shader_id] = created;
		}
		m_any_replacement.store(true, std::memory_order_relaxed);

		if (created.size() != pipelines.size())
			error = "only " + std::to_string(created.size()) + " of " +
				std::to_string(pipelines.size()) + " pipelines could be rebuilt: " + last_error;
		return static_cast<uint32_t>(created.size());
	}

	void RuntimeControl::ClearReplacement(reshade::api::device *device, uint32_t shader_id)
	{
		std::vector<std::pair<uint32_t, uint64_t>> created;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_replacements.find(shader_id);
			if (it == m_replacements.end())
				return;
			created = std::move(it->second);
			m_replacements.erase(it);
			m_any_replacement.store(!m_replacements.empty(), std::memory_order_relaxed);
		}

		// Unpublish before destroying: a draw must never see a handle that is already gone.
		for (const auto &entry : created)
			if (entry.first < kMaxControlledPipelines)
				m_pipeline_replacement[entry.first].store(0, std::memory_order_release);

		if (device != nullptr)
			for (const auto &entry : created)
				device->destroy_pipeline(reshade::api::pipeline{ entry.second });
	}

	void RuntimeControl::ClearAllReplacements(reshade::api::device *device)
	{
		std::vector<uint32_t> shader_ids;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			shader_ids.reserve(m_replacements.size());
			for (const auto &entry : m_replacements)
				shader_ids.push_back(entry.first);
		}
		for (uint32_t shader_id : shader_ids)
			ClearReplacement(device, shader_id);
	}

	uint32_t RuntimeControl::ReplacedShaderCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_replacements.size());
	}

	void RuntimeControl::OnPipelineRegistered(const PipelineRecord &pipeline)
	{
		if (pipeline.id == 0 || pipeline.id >= kMaxControlledPipelines)
			return;
		if (!m_active.load(std::memory_order_relaxed))
			return;

		bool disabled = false;
		bool highlighted = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (uint32_t i = 0; i < pipeline.shader_count; ++i)
			{
				if (m_disabled_shaders.count(pipeline.shader_ids[i]) != 0)
					disabled = true;
				if (m_highlighted_shaders.count(pipeline.shader_ids[i]) != 0)
					highlighted = true;
			}
		}

		m_pipeline_disabled[pipeline.id].store(disabled ? 1u : 0u, std::memory_order_relaxed);
		m_pipeline_highlighted[pipeline.id].store(highlighted ? 1u : 0u, std::memory_order_relaxed);
	}

	void RuntimeControl::RefreshPipelines(uint32_t shader_id, const ShaderTracker &shaders)
	{
		const std::vector<uint32_t> pipelines = shaders.PipelinesUsingShader(shader_id);

		for (uint32_t pipeline_id : pipelines)
		{
			if (pipeline_id == 0 || pipeline_id >= kMaxControlledPipelines)
				continue;

			PipelineRecord record;
			if (!shaders.CopyPipelineById(pipeline_id, record))
				continue;

			bool disabled = false;
			bool highlighted = false;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				for (uint32_t i = 0; i < record.shader_count; ++i)
				{
					if (m_disabled_shaders.count(record.shader_ids[i]) != 0)
						disabled = true;
					if (m_highlighted_shaders.count(record.shader_ids[i]) != 0)
						highlighted = true;
				}
			}

			m_pipeline_disabled[pipeline_id].store(disabled ? 1u : 0u, std::memory_order_relaxed);
			m_pipeline_highlighted[pipeline_id].store(highlighted ? 1u : 0u, std::memory_order_relaxed);
		}
	}

	void RuntimeControl::UpdateActiveFlag()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_active.store(!m_disabled_shaders.empty() || !m_highlighted_shaders.empty(), std::memory_order_relaxed);
	}

	bool RuntimeControl::IsShaderDisabled(uint32_t shader_id) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_disabled_shaders.count(shader_id) != 0;
	}

	uint32_t RuntimeControl::DisabledShaderCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_disabled_shaders.size());
	}

	uint32_t RuntimeControl::HighlightedShaderCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_highlighted_shaders.size());
	}
}
