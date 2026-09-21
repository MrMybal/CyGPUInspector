// CyGPUInspectorApp — saving and reopening a frame without the game.
//
// A capture is a directory, not an opaque blob: metadata in readable JSON, the frame events in one
// flat binary file, and the shader byte code beside it. Anyone can look inside, and a future
// version can still read an old capture.
//
// Loading does not parse into the model directly. It rebuilds the same IPC records the add-on
// would have sent and feeds them to SessionModel::ApplyRecord, so an opened capture goes through
// exactly the same code as a live session. There is no second deserialiser to keep in sync.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"
#include "Session/SessionModel.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cygi
{
	struct CaptureInfo
	{
		std::filesystem::path path;
		std::string process_name;
		std::string api;
		std::string created;
		std::string tool_version;
		uint64_t frame_index = 0;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;
		uint32_t event_count = 0;
		uint32_t shader_count = 0;
		uint32_t resource_count = 0;
		uint32_t pass_count = 0;
		double gpu_milliseconds = 0.0;

		std::string Describe() const;
	};

	class CaptureArchive
	{
	public:
		// `model` must be locked by the caller. `graph` may be empty.
		static bool Save(const SessionModel &model, const FrameGraph &graph,
		                 const std::filesystem::path &directory, std::string &error);

		// Fills a fresh model from a capture directory.
		static bool Load(const std::filesystem::path &directory, SessionModel &model, CaptureInfo &info,
		                 std::string &error);

		// Cheap header read, for listing captures.
		static bool ReadInfo(const std::filesystem::path &directory, CaptureInfo &info);

		// Default location: Captures/ next to the executable.
		static std::filesystem::path DefaultRoot();
		// "Captures/FakeGame_2026-09-17_01-12-45"
		static std::filesystem::path SuggestDirectory(const std::string &process_name);
		static std::vector<CaptureInfo> List(const std::filesystem::path &root);
	};

	// What changed between two captures (section 44 of the brief).
	struct CaptureDiff
	{
		int32_t draw_delta = 0;
		int32_t dispatch_delta = 0;
		int32_t event_delta = 0;
		int32_t shader_delta = 0;
		int32_t resource_delta = 0;
		double gpu_delta_ms = 0.0;

		std::vector<std::string> only_in_a;   // shader signatures, short form
		std::vector<std::string> only_in_b;
		std::vector<std::string> notes;
	};

	CaptureDiff CompareCaptures(const SessionModel &a, const SessionModel &b);
}
