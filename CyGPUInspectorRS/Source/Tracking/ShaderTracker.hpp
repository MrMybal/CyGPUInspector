// CyGPUInspectorRS — shader and pipeline tracking.
//
// Every shader the game creates is hashed once (SHA-256 of the normalized byte code) and gets a
// small session local id. Pipelines keep the list of shader ids they were built from, so a
// `bind_pipeline` event only has to look up one handle to know which shaders a draw will run.
//
// Pipelines are created from several threads, so the tables are guarded by a shared mutex. The
// hot path (bind / draw) only takes the read lock, and only once per pipeline bind.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Control/PipelineBlueprint.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct ShaderRecord
	{
		uint32_t id = 0;
		Sha256Digest signature;
		Sha256Digest semantic_hash;
		ShaderStage stage = ShaderStage::unknown;
		ShaderFormat format = ShaderFormat::unknown;
		uint32_t shader_model = 0;
		uint32_t code_size = 0;
	};

	struct PipelineRecord
	{
		uint32_t id = 0;
		uint64_t native_handle = 0;
		uint32_t shader_ids[kMaxPipelineShaders] = {};
		uint32_t shader_count = 0;
		uint32_t stage_mask = 0;
		bool is_compute = false;

		bool UsesShader(uint32_t shader_id) const
		{
			for (uint32_t i = 0; i < shader_count; ++i)
				if (shader_ids[i] == shader_id)
					return true;
			return false;
		}
	};

	// A shader whose byte code still has to be sent to the standalone.
	struct PendingShaderCode
	{
		uint32_t shader_id = 0;
		std::vector<uint8_t> code;
	};

	class ShaderTracker
	{
	public:
		// Called from create_pipeline / init_pipeline. Copies the byte code (the pointer given by
		// ReShade is only valid during the callback) and returns the shader id.
		uint32_t RegisterShader(const void *code, size_t code_size, ShaderStage stage_hint);

		// Called from init_pipeline once the handle exists.
		uint32_t RegisterPipeline(uint64_t native_handle, const uint32_t *shader_ids, uint32_t count,
		                          uint32_t stage_mask, bool is_compute);
		void UnregisterPipeline(uint64_t native_handle);

		// The description a pipeline was built from, captured so a variant can be created later.
		void SetBlueprint(uint32_t pipeline_id, PipelineBlueprint &&blueprint);
		bool IsPipelineReplaceable(uint32_t pipeline_id, std::string &reason) const;
		// Builds a copy of `pipeline_id` with `shader_id` replaced by `code`, under the read lock.
		reshade::api::pipeline BuildVariant(reshade::api::device *device, uint32_t pipeline_id,
		                                    uint32_t shader_id, const void *code, size_t code_size,
		                                    std::string &error) const;

		// Hot path: id of the pipeline bound by the game, 0 when unknown.
		uint32_t PipelineId(uint64_t native_handle) const;
		bool CopyPipeline(uint64_t native_handle, PipelineRecord &out) const;
		bool CopyPipelineById(uint32_t pipeline_id, PipelineRecord &out) const;

		bool CopyShader(uint32_t shader_id, ShaderRecord &out) const;
		// The byte code kept for a shader, needed to rebuild a pipeline around a replacement.
		bool CopyShaderCode(uint32_t shader_id, std::vector<uint8_t> &out) const;
		uint32_t ShaderIdBySignature(const Sha256Digest &signature) const;

		// Does any known pipeline use this shader? Used by the runtime control to know whether
		// disabling a shader can have an effect at all.
		std::vector<uint32_t> PipelinesUsingShader(uint32_t shader_id) const;

		// Moves out the shaders and pipelines that still have to be published over IPC.
		std::vector<PendingShaderCode> TakePendingShaders();
		std::vector<PipelineRecord> TakePendingPipelines();
		// Rebuilds the full pending lists, for a FullSync after the standalone reconnected.
		void QueueEverything();

		uint32_t ShaderCount() const;
		uint32_t PipelineCount() const;

	private:
		mutable std::shared_mutex m_mutex;

		std::vector<ShaderRecord> m_shaders;                         // indexed by id - 1
		std::vector<std::vector<uint8_t>> m_shader_code;             // kept for FullSync
		std::unordered_map<Sha256Digest, uint32_t, Sha256DigestHasher> m_by_signature;

		std::vector<PipelineRecord> m_pipelines;                     // indexed by id - 1
		std::vector<PipelineBlueprint> m_blueprints;                 // indexed by id - 1
		std::unordered_map<uint64_t, uint32_t> m_pipeline_by_handle;

		std::deque<uint32_t> m_pending_shaders;
		std::deque<uint32_t> m_pending_pipelines;
	};
}
