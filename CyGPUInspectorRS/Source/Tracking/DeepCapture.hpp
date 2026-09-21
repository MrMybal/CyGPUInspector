// CyGPUInspectorRS — the deep capture: arming, budgeting and disarming.
//
// A deep capture is a one shot. It is armed from the standalone, it runs for a few frames, and
// then it turns itself off — including when something goes wrong, because a capture that forgets
// to disarm leaves the game running the expensive path forever.
//
// This object holds no per command data. It only answers "is the expensive path on right now?",
// which the hot callbacks ask thousands of times per frame, so the answer is a relaxed atomic
// load and nothing else.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>

#include <atomic>
#include <cstdint>

namespace cygi
{
	// Beyond this many recorded commands in one frame the capture stops recording state and says
	// so, rather than growing until the game runs out of memory. A heavy frame in a modern game
	// is a few thousand draws; this leaves a wide margin and still has a ceiling.
	inline constexpr uint32_t kDefaultMaxDrawStates = 60000;

	// Bindings are the bulk of a deep capture: thirty or so per draw is normal.
	inline constexpr uint32_t kMaxBindingsPerDraw = 256;

	class DeepCapture
	{
	public:
		// Hot path: one relaxed load, called per draw.
		bool Recording() const { return m_recording.load(std::memory_order_relaxed); }
		bool WantsBindings() const { return m_bindings.load(std::memory_order_relaxed); }
		bool WantsBarriers() const { return m_barriers.load(std::memory_order_relaxed); }
		bool WantsPerDrawTiming() const { return m_per_draw_timing.load(std::memory_order_relaxed); }
		bool WantsBuffers() const { return m_buffers.load(std::memory_order_relaxed); }

		// Called from the control thread. `restore` is the level to go back to afterwards.
		void Arm(const CaptureFrameRequest &request, TrackingLevel restore);

		// Called at present, before the frame is published. Returns true when the frame that is
		// ending is a captured one, false for the present the capture was armed in: that frame was
		// recorded before the request and holds nothing. The caller publishes the state either way.
		bool BeginFrame(uint64_t frame_index);

		// Called at present, after the frame was published. Returns true when the capture just
		// ended, in which case `restore_level` says what the add-on goes back to.
		bool EndFrame(TrackingLevel &restore_level);

		// A capture limited to a part of the frame skips the frames that do not have that part —
		// an editor viewport that did not redraw — and does not count them. Returns false when it
		// has waited too long, in which case the capture has been aborted.
		bool SkipFrame(uint64_t frame_index, TrackingLevel &restore_level);

		// A draw wants to record its state: says whether the budget allows it.
		bool TakeDrawStateBudget()
		{
			if (m_draw_states.load(std::memory_order_relaxed) >= m_max_draw_states)
			{
				m_over_budget.store(true, std::memory_order_relaxed);
				return false;
			}
			m_draw_states.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void CountBindings(uint32_t count) { m_bindings_recorded.fetch_add(count, std::memory_order_relaxed); }
		void CountBarriers(uint32_t count) { m_barriers_recorded.fetch_add(count, std::memory_order_relaxed); }
		void CountDropped(uint32_t count) { m_dropped.fetch_add(count, std::memory_order_relaxed); }

		bool WentOverBudget() const { return m_over_budget.load(std::memory_order_relaxed); }

		CaptureStateRecord State() const;
		void Abort(const char *reason);
		const char *FailureReason() const { return m_failure; }

	private:
		std::atomic<bool> m_recording{ false };
		std::atomic<bool> m_bindings{ false };
		std::atomic<bool> m_barriers{ false };
		std::atomic<bool> m_per_draw_timing{ false };
		std::atomic<bool> m_buffers{ false };
		std::atomic<bool> m_over_budget{ false };

		std::atomic<CaptureStage> m_stage{ CaptureStage::idle };
		std::atomic<uint32_t> m_frames_requested{ 0 };
		std::atomic<uint32_t> m_frames_done{ 0 };
		std::atomic<uint32_t> m_draw_states{ 0 };
		std::atomic<uint32_t> m_bindings_recorded{ 0 };
		std::atomic<uint32_t> m_barriers_recorded{ 0 };
		std::atomic<uint32_t> m_dropped{ 0 };
		std::atomic<uint64_t> m_first_frame{ 0 };
		uint32_t m_skipped = 0;
		uint32_t m_max_draw_states = kDefaultMaxDrawStates;
		TrackingLevel m_restore_level = TrackingLevel::tracking;
		const char *m_failure = "";
	};
}
