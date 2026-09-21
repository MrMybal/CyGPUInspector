// CyGPUInspectorRS — the deep capture state machine.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "DeepCapture.hpp"

namespace cygi
{
	void DeepCapture::Arm(const CaptureFrameRequest &request, TrackingLevel restore)
	{
		// Re-arming while a capture runs would leave the first one unable to disarm, so the new
		// request simply replaces the old counters and the single `m_recording` flag stays true.
		m_restore_level = restore;
		m_max_draw_states = request.max_draw_states != 0 ? request.max_draw_states : kDefaultMaxDrawStates;

		m_frames_requested.store(request.frame_count != 0 ? request.frame_count : 1,
			std::memory_order_relaxed);
		m_frames_done.store(0, std::memory_order_relaxed);
		m_draw_states.store(0, std::memory_order_relaxed);
		m_bindings_recorded.store(0, std::memory_order_relaxed);
		m_barriers_recorded.store(0, std::memory_order_relaxed);
		m_dropped.store(0, std::memory_order_relaxed);
		m_over_budget.store(false, std::memory_order_relaxed);
		m_first_frame.store(0, std::memory_order_relaxed);
		m_skipped = 0;
		m_failure = "";

		m_bindings.store(request.include_bindings != 0, std::memory_order_relaxed);
		// The buffers need the barriers: on D3D12 and Vulkan, copying a texture means taking it
		// out of the state it is in, and the recorded transitions are how that state is known.
		m_buffers.store(request.include_buffers != 0, std::memory_order_relaxed);
		m_barriers.store(request.include_barriers != 0 || request.include_buffers != 0, std::memory_order_relaxed);
		m_per_draw_timing.store(request.per_draw_timing != 0, std::memory_order_relaxed);
		m_stage.store(CaptureStage::armed, std::memory_order_relaxed);

		// Published last: it is what the callbacks read, so everything else must already be set.
		m_recording.store(true, std::memory_order_release);
	}

	bool DeepCapture::BeginFrame(uint64_t frame_index)
	{
		if (!m_recording.load(std::memory_order_acquire))
			return false;

		// Armed during this present: the frame that is ending was recorded before anyone asked,
		// so it holds nothing and must not be counted. Capturing starts with the next frame. This
		// used to count it, and a one frame capture finished in the same present it was armed in,
		// with nothing recorded.
		if (m_stage.load(std::memory_order_relaxed) == CaptureStage::armed)
		{
			m_first_frame.store(frame_index + 1, std::memory_order_relaxed);
			m_stage.store(CaptureStage::capturing, std::memory_order_relaxed);
			return false;
		}
		return true;
	}

	bool DeepCapture::EndFrame(TrackingLevel &restore_level)
	{
		if (!m_recording.load(std::memory_order_acquire))
			return false;

		const uint32_t done = m_frames_done.fetch_add(1, std::memory_order_relaxed) + 1;
		if (done < m_frames_requested.load(std::memory_order_relaxed))
			return false;

		// Stop recording first, so no callback can start writing state for a frame nobody will
		// publish, then report where the capture got to.
		m_recording.store(false, std::memory_order_release);
		m_bindings.store(false, std::memory_order_relaxed);
		m_barriers.store(false, std::memory_order_relaxed);
		m_per_draw_timing.store(false, std::memory_order_relaxed);
		m_buffers.store(false, std::memory_order_relaxed);
		m_stage.store(CaptureStage::finished, std::memory_order_relaxed);

		restore_level = m_restore_level;
		return true;
	}

	bool DeepCapture::SkipFrame(uint64_t frame_index, TrackingLevel &restore_level)
	{
		// Ten seconds or so: a viewport that has not drawn by then is not going to.
		constexpr uint32_t kMaxSkippedFrames = 600;
		if (++m_skipped > kMaxSkippedFrames)
		{
			Abort("the part of the frame to capture did not render: is the viewport visible and drawing?");
			restore_level = m_restore_level;
			return false;
		}
		// The capture is about the frames it keeps: it starts with the next one.
		if (m_frames_done.load(std::memory_order_relaxed) == 0)
			m_first_frame.store(frame_index + 1, std::memory_order_relaxed);
		return true;
	}

	void DeepCapture::Abort(const char *reason)
	{
		m_recording.store(false, std::memory_order_release);
		m_bindings.store(false, std::memory_order_relaxed);
		m_barriers.store(false, std::memory_order_relaxed);
		m_per_draw_timing.store(false, std::memory_order_relaxed);
		m_buffers.store(false, std::memory_order_relaxed);
		m_stage.store(CaptureStage::failed, std::memory_order_relaxed);
		m_failure = reason != nullptr ? reason : "unknown";
	}

	CaptureStateRecord DeepCapture::State() const
	{
		CaptureStateRecord record = {};
		record.stage = m_stage.load(std::memory_order_relaxed);
		record.mode = CaptureMode::deep;
		record.first_frame = m_first_frame.load(std::memory_order_relaxed);
		record.frames_requested = m_frames_requested.load(std::memory_order_relaxed);
		record.frames_done = m_frames_done.load(std::memory_order_relaxed);
		record.draw_states_recorded = m_draw_states.load(std::memory_order_relaxed);
		record.bindings_recorded = m_bindings_recorded.load(std::memory_order_relaxed);
		record.barriers_recorded = m_barriers_recorded.load(std::memory_order_relaxed);
		record.dropped = m_dropped.load(std::memory_order_relaxed);
		return record;
	}
}
