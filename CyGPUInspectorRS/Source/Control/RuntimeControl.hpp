// CyGPUInspectorRS — runtime control of shaders (disable / highlight).
//
// The draw callbacks have to answer "is this pipeline disabled?" thousands of times per frame,
// so the answer is a lock free byte lookup in a flat table indexed by pipeline id. The table has
// a fixed capacity: pipelines beyond it stay controllable through their shader id, they just do
// not benefit from the fast path (see kMaxControlledPipelines).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Tracking/ShaderTracker.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cygi
{
	inline constexpr uint32_t kMaxControlledPipelines = 1u << 17; // 131072 pipelines, 128 KiB per table

	enum class ControlState : uint8_t
	{
		normal = 0,
		disabled = 1,
		highlighted = 2,
	};

	class RuntimeControl
	{
	public:
		RuntimeControl();

		void SetShaderDisabled(uint32_t shader_id, bool disabled, const ShaderTracker &shaders);
		void SetShaderHighlighted(uint32_t shader_id, bool highlighted, const ShaderTracker &shaders);
		void RestoreShader(uint32_t shader_id, const ShaderTracker &shaders);
		void RestoreAll();

		// Builds a replacement pipeline for every pipeline that uses this shader. Returns the
		// number of pipelines swapped, and explains itself when it swaps none.
		uint32_t SetReplacement(reshade::api::device *device, const ShaderTracker &shaders,
		                        uint32_t shader_id, const void *code, size_t code_size, std::string &error);
		void ClearReplacement(reshade::api::device *device, uint32_t shader_id);
		void ClearAllReplacements(reshade::api::device *device);
		uint32_t ReplacedShaderCount() const;

		// Called when a pipeline is registered so it inherits the state of its shaders.
		void OnPipelineRegistered(const PipelineRecord &pipeline);

		// Hot path.
		bool IsPipelineDisabled(uint32_t pipeline_id) const
		{
			return pipeline_id != 0 && pipeline_id < kMaxControlledPipelines &&
			       m_pipeline_disabled[pipeline_id].load(std::memory_order_relaxed) != 0;
		}
		bool IsPipelineHighlighted(uint32_t pipeline_id) const
		{
			return pipeline_id != 0 && pipeline_id < kMaxControlledPipelines &&
			       m_pipeline_highlighted[pipeline_id].load(std::memory_order_relaxed) != 0;
		}

		// Hot path: the pipeline to bind instead of this one, or 0.
		uint64_t ReplacementFor(uint32_t pipeline_id) const
		{
			return pipeline_id != 0 && pipeline_id < kMaxControlledPipelines
				? m_pipeline_replacement[pipeline_id].load(std::memory_order_relaxed) : 0;
		}

		bool IsShaderDisabled(uint32_t shader_id) const;
		uint32_t DisabledShaderCount() const;
		uint32_t HighlightedShaderCount() const;
		bool AnythingActive() const { return m_active.load(std::memory_order_relaxed); }
		bool AnyReplacement() const { return m_any_replacement.load(std::memory_order_relaxed); }

	private:
		void RefreshPipelines(uint32_t shader_id, const ShaderTracker &shaders);
		void UpdateActiveFlag();

		mutable std::mutex m_mutex;
		std::unordered_set<uint32_t> m_disabled_shaders;
		std::unordered_set<uint32_t> m_highlighted_shaders;
		std::atomic<bool> m_active{ false };
		std::atomic<bool> m_any_replacement{ false };

		std::unique_ptr<std::atomic<uint8_t>[]> m_pipeline_disabled;
		std::unique_ptr<std::atomic<uint8_t>[]> m_pipeline_highlighted;
		std::unique_ptr<std::atomic<uint64_t>[]> m_pipeline_replacement;

		// Which pipelines a replaced shader created, so they can all be destroyed on restore.
		std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint64_t>>> m_replacements;
	};
}
