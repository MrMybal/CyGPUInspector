// CyGPUInspectorApp — the buffers of a deep capture, from the game's GPU to files.
//
// The add-on copies every texture the last captured frame wrote to into a shared texture and
// sends the handles. This opens them one per interface frame, reads each back once, and hands the
// pixels to a worker thread that writes two files per buffer:
//
//   NN_<name>.dds   the data itself, in the game's format (depth through its typeless family),
//                   for tools that want the real values — HDR, depth, normals, IDs;
//   NN_<name>.png   something a person can look at: colour clamped or stretched to the range
//                   actually used, depth stretched between its nearest and farthest values.
//
// plus capture.json, the list of what was saved and why some were not. Once everything is read
// back the add-on is told to let go of its copies, which gives the game its memory back.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Render/PreviewRenderer.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cygi
{
	class CaptureBufferWriter
	{
	public:
		enum class State : uint32_t
		{
			waiting = 0,   // shared, not read back yet
			writing,       // read back, the worker has it
			written,
			failed,        // shared, but reading or writing it failed
			not_copied,    // the add-on could not copy it (the record says why)
		};

		struct Item
		{
			CaptureBufferRecord record = {};
			std::string name;              // the game's name for it, or empty
			std::string file_stem;         // "03_depth_2560x1440_d32_float_res812", no extension
			State state = State::waiting;
			std::string error;
			bool depth = false;
			float range_min = 0.0f;        // what the PNG was stretched between
			float range_max = 1.0f;
		};

		CaptureBufferWriter() = default;
		~CaptureBufferWriter();
		CaptureBufferWriter(const CaptureBufferWriter &) = delete;
		CaptureBufferWriter &operator=(const CaptureBufferWriter &) = delete;

		bool Initialize(ID3D11Device *device, ID3D11DeviceContext *context);
		void Shutdown();

		// A capture's buffers start arriving: they go to `folder`, which is created.
		void Begin(const std::filesystem::path &folder, uint64_t first_frame, const std::string &game);
		// One record of that capture; records of another capture are ignored.
		void Add(const CaptureBufferRecord &record, const std::string &name);

		// Once per interface frame. Reads back at most one buffer, and only once the game has run
		// far enough past the frame the copies were recorded in for the GPU to have made them.
		// Returns true once, when every buffer has been read back: the moment to tell the add-on
		// to release its copies.
		bool Step(uint64_t game_frame);

		bool Active() const { return m_active; }
		uint64_t FirstFrame() const { return m_first_frame; }
		const std::filesystem::path &Folder() const { return m_folder; }
		// A copy, safe to draw from while the worker writes.
		std::vector<Item> Items() const;
		// Counts, for a progress line.
		void Progress(uint32_t &done, uint32_t &total) const;

	private:
		struct Job
		{
			size_t index = 0;
			uint64_t generation = 0;         // which capture it belongs to
			std::filesystem::path dds_path;
			std::vector<uint8_t> dds;        // header and data, ready to write
			std::filesystem::path png_path;
			std::vector<uint8_t> bgra;
			uint32_t width = 0;
			uint32_t height = 0;
		};

		bool ReadBack(Item &item, Job &job, std::string &error);
		void WorkerLoop();
		void WriteIndex();

		ID3D11Device *m_device = nullptr;
		ID3D11DeviceContext *m_context = nullptr;
		PreviewRenderer m_renderer;
		bool m_renderer_ready = false;

		bool m_active = false;
		bool m_release_sent = false;
		bool m_index_written = false;
		uint64_t m_first_frame = 0;
		uint64_t m_copied_at_frame = 0;
		uint32_t m_expected = 0;
		std::string m_game;
		std::filesystem::path m_folder;

		mutable std::mutex m_mutex;          // m_items and the queue
		std::vector<Item> m_items;
		std::deque<Job> m_jobs;
		std::condition_variable m_wake;
		std::thread m_worker;
		bool m_stop = false;
		uint64_t m_generation = 0;
	};
}
