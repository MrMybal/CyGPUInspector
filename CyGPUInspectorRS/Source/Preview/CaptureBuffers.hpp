// CyGPUInspectorRS — the buffers of a deep capture.
//
// At the end of the last captured frame, every texture that frame rendered into — render targets,
// depth buffers, unordered access targets, copy destinations — is copied into a shared texture of
// its own, on the game's own queue, and the handles go to the standalone, which reads them back
// and saves them to disk. Nothing goes through the game's system memory.
//
// Each copy shows its texture as it was at the end of the frame: a texture several passes render
// into in turn shows the result of the last of them, and a texture reused for something else
// later in the frame shows that. The standalone says so next to every file.
//
// This is the one place where the add-on holds a lot of GPU memory on purpose, so it is bounded
// (a count and a size) and given back as soon as the standalone says it has what it needed, when
// a new capture starts, or after a while if nobody ever asks.
//
// Everything here runs on the render thread, inside the present event.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Tracking/FrameRecorder.hpp"
#include "Tracking/ResourceTracker.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cygi
{
	class CaptureBuffers
	{
	public:
		static constexpr uint32_t kMaxBuffers = 64;
		static constexpr uint64_t kMaxBytes = 1536ull << 20;       // 1.5 GiB of copies at most
		static constexpr uint64_t kReleaseAfterFrames = 3600;      // a minute at 60 fps
		// A released copy may still be being written by the GPU: it is destroyed a few frames
		// later, once the frames in flight that could touch it have certainly finished.
		static constexpr uint64_t kRetireFrames = 8;

		// A capture is starting: whatever the previous one left is given back.
		void BeginCapture(uint64_t frame_index);

		// Every frame of a capture, kept or skipped: the transitions it recorded, in the order the
		// queue executes them, which is how the state of each texture is known at the end.
		void NoteStates(const FrameRecorder::CapturedState &captured);
		// Every kept frame: the textures its commands from first_event to last_event wrote to.
		void NoteFrame(const std::vector<FrameEvent> &events, const FrameRecorder::CapturedState &captured,
		               uint32_t first_event, uint32_t last_event);

		// At the present that ends the capture: copies the textures of the last frame and returns
		// what the standalone has to be told, one record per texture, failures included.
		std::vector<CaptureBufferRecord> Snapshot(reshade::api::device *device, reshade::api::command_queue *queue,
		                                          const ResourceTracker &resources, uint64_t first_frame,
		                                          uint64_t frame_index, uint32_t app_process_id);

		// The standalone is done with them.
		void Release(uint64_t frame_index);
		// Every present: destroys what was released long enough ago, and releases what nobody
		// has asked for in too long.
		void Update(reshade::api::device *device, uint64_t frame_index);
		// The device is going away: everything goes, now.
		void Shutdown(reshade::api::device *device);

		uint64_t HeldBytes() const { return m_held_bytes; }
		size_t HeldCount() const { return m_held.size(); }

	private:
		struct Candidate
		{
			uint32_t resource_id = 0;
			uint32_t roles = 0;
			uint32_t first_event = 0;
		};

		std::vector<Candidate> m_candidates;                     // of the last noted frame
		std::unordered_map<uint32_t, uint32_t> m_states;         // resource id -> resource_usage

		struct Retired
		{
			reshade::api::resource texture;
			uint64_t frame_index;
		};

		std::vector<reshade::api::resource> m_held;
		std::vector<Retired> m_retired;
		uint64_t m_held_bytes = 0;
		uint64_t m_held_since = 0;
	};
}
