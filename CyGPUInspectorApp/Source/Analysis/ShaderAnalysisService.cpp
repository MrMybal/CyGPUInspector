// CyGPUInspectorApp — background disassembly and reflection.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ShaderAnalysisService.hpp"

namespace cygi
{
	ShaderAnalysisService::ShaderAnalysisService()
	{
		m_store_path = m_store.Root().string();
		m_running.store(true, std::memory_order_release);
		m_worker = std::thread(&ShaderAnalysisService::WorkerMain, this);
	}

	ShaderAnalysisService::~ShaderAnalysisService()
	{
		m_running.store(false, std::memory_order_release);
		m_wakeup.notify_all();
		if (m_worker.joinable())
			m_worker.join();
	}

	void ShaderAnalysisService::Request(uint32_t shader_id, const Sha256Digest &signature,
	                                    const Sha256Digest &semantic_hash, ShaderStage stage,
	                                    ShaderFormat format, uint32_t shader_model,
	                                    std::vector<uint8_t> code, std::string process_name)
	{
		if (shader_id == 0 || code.empty())
			return;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_results.count(shader_id) != 0 || m_pending[shader_id])
				return;
			m_pending[shader_id] = true;

			Job job;
			job.shader_id = shader_id;
			job.signature = signature;
			job.semantic_hash = semantic_hash;
			job.stage = stage;
			job.format = format;
			job.shader_model = shader_model;
			job.code = std::move(code);
			job.process_name = std::move(process_name);

			// Keep the job around: a later decompilation request needs the byte code again.
			m_jobs[shader_id] = job;
			m_queue.push_back(std::move(job));
		}
		m_wakeup.notify_one();
	}

	void ShaderAnalysisService::RequestDecompilation(uint32_t shader_id)
	{
		Job job;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_results.find(shader_id);
			if (it == m_results.end() || !it->second.decompilations.empty() || m_pending[shader_id])
				return;   // not analysed yet, already decompiled, or already queued

			job = m_jobs[shader_id];
			if (job.code.empty())
				return;
			job.decompile = true;
			m_pending[shader_id] = true;
			m_queue.push_back(job);
		}
		m_wakeup.notify_one();
	}

	bool ShaderAnalysisService::Result(uint32_t shader_id, ShaderAnalysis &out) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_results.find(shader_id);
		if (it == m_results.end())
			return false;
		out = it->second;
		return true;
	}

	bool ShaderAnalysisService::IsPending(uint32_t shader_id) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_pending.find(shader_id);
		return it != m_pending.end() && it->second;
	}

	size_t ShaderAnalysisService::PendingCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	void ShaderAnalysisService::Clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.clear();
		m_results.clear();
		m_pending.clear();
		m_jobs.clear();
	}

	void ShaderAnalysisService::WorkerMain()
	{
		for (;;)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wakeup.wait(lock, [this]() {
					return !m_queue.empty() || !m_running.load(std::memory_order_acquire);
				});
				if (!m_running.load(std::memory_order_acquire) && m_queue.empty())
					return;
				if (m_queue.empty())
					continue;

				job = std::move(m_queue.front());
				m_queue.pop_front();
			}
			if (job.decompile)
				RunDecompilation(job);
			else
				Run(job);
		}
	}

	void ShaderAnalysisService::RunDecompilation(const Job &job)
	{
		// Every backend that supports this format runs, and each result is kept on its own.
		std::vector<DecompilationResult> results =
			DecompilerRegistry::Instance().DecompileAll(job.code.data(), job.code.size(), true);

		for (const DecompilationResult &result : results)
			if (result.ok)
				m_store.SaveText(job.signature, result.ArtifactName(), result.hlsl);

		std::lock_guard<std::mutex> lock(m_mutex);
		m_pending[job.shader_id] = false;
		m_results[job.shader_id].decompilations = std::move(results);
	}

	void ShaderAnalysisService::Run(const Job &job)
	{
		ShaderAnalysis analysis;
		analysis.shader_id = job.shader_id;
		analysis.signature = job.signature;

		// The store is the first stop: a shader already met in another run costs nothing.
		std::string cached;
		if (m_store.LoadText(job.signature, "disassembly.txt", cached) && !cached.empty())
		{
			analysis.disassembly.ok = true;
			analysis.disassembly.format = job.format;
			analysis.disassembly.text = std::move(cached);
			analysis.disassembly.tool = "cache";
			analysis.from_cache = true;
		}
		else
		{
			analysis.disassembly = Disassemble(job.code.data(), job.code.size());
			if (analysis.disassembly.ok)
			{
				m_store.SaveByteCode(job.signature, job.format, job.code.data(), job.code.size());
				m_store.SaveText(job.signature, "disassembly.txt", analysis.disassembly.text);
				m_store.SaveMetadata(job.signature, job.semantic_hash, job.stage, job.format,
					job.shader_model, static_cast<uint32_t>(job.code.size()), job.process_name);
			}
		}

		// Reflection is cheap and its output is structured: it is never read back from disk.
		analysis.reflection = Reflect(job.code.data(), job.code.size());
		analysis.done = true;

		std::lock_guard<std::mutex> lock(m_mutex);
		m_pending[job.shader_id] = false;
		m_results[job.shader_id] = std::move(analysis);
	}
}
