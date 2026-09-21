// CyGPUInspectorApp — background disassembly and reflection.
//
// Disassembling a big shader takes milliseconds, and the UI must never wait. Requests are queued,
// a worker thread runs them, and the result is cached both in memory and on disk by signature, so
// the same shader met in another run of the game is never disassembled twice.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>
#include <CyGPUInspectorDatabase/ShaderStore.hpp>
#include <CyGPUInspectorDecompiler/Decompiler.hpp>
#include <CyGPUInspectorDecompiler/Disassembler.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct ShaderAnalysis
	{
		uint32_t shader_id = 0;
		Sha256Digest signature;
		bool done = false;
		bool from_cache = false;      // the disassembly came from the store, not from a tool run
		DisassemblyResult disassembly;
		ReflectionResult reflection;
		// One entry per backend that could handle this shader, never merged, never overwritten.
		std::vector<DecompilationResult> decompilations;
	};

	class ShaderAnalysisService
	{
	public:
		ShaderAnalysisService();
		~ShaderAnalysisService();

		ShaderAnalysisService(const ShaderAnalysisService &) = delete;
		ShaderAnalysisService &operator=(const ShaderAnalysisService &) = delete;

		// Decompilation is opt-in per shader: it is much more expensive than disassembly.
		void RequestDecompilation(uint32_t shader_id);

		// Queues the work if it is not already done or pending. Returns immediately.
		void Request(uint32_t shader_id, const Sha256Digest &signature, const Sha256Digest &semantic_hash,
		             ShaderStage stage, ShaderFormat format, uint32_t shader_model,
		             std::vector<uint8_t> code, std::string process_name);

		// Copies the result out, false while the work is pending or was never requested.
		bool Result(uint32_t shader_id, ShaderAnalysis &out) const;
		bool IsPending(uint32_t shader_id) const;
		size_t PendingCount() const;

		// Called when the session changes: results belong to a session's shader ids.
		void Clear();

		ShaderStore &Store() { return m_store; }
		const std::string &StorePath() const { return m_store_path; }

	private:
		struct Job
		{
			uint32_t shader_id = 0;
			Sha256Digest signature;
			Sha256Digest semantic_hash;
			ShaderStage stage = ShaderStage::unknown;
			ShaderFormat format = ShaderFormat::unknown;
			uint32_t shader_model = 0;
			std::vector<uint8_t> code;
			std::string process_name;
			bool decompile = false;
		};

		void WorkerMain();
		void Run(const Job &job);
		void RunDecompilation(const Job &job);

		ShaderStore m_store;
		std::string m_store_path;

		mutable std::mutex m_mutex;
		std::condition_variable m_wakeup;
		std::deque<Job> m_queue;
		std::unordered_map<uint32_t, ShaderAnalysis> m_results;
		std::unordered_map<uint32_t, bool> m_pending;
		std::unordered_map<uint32_t, Job> m_jobs;   // kept so decompilation can be asked for later

		std::thread m_worker;
		std::atomic<bool> m_running{ false };
	};
}
