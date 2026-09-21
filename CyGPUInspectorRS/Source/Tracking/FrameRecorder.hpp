// CyGPUInspectorRS — per frame event accumulation.
//
// Command lists hand their local event buffers over here; the recorder renumbers them into a
// single frame timeline and hands the result to the IPC server at present time.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "CommandListState.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cygi
{
	class FrameRecorder
	{
	public:
		// Where a merged list landed in the frame: its first event became `base`, and `accepted`
		// of its events made it in before the frame was full. Local event i is frame event
		// base + i when i < accepted, and was dropped otherwise.
		struct MergeResult
		{
			uint32_t base = 0;
			uint32_t accepted = 0;
		};

		// Command lists can be executed from several threads, so merging is locked.
		MergeResult Merge(CommandListState &state);

		// Called at present: hands out the frame and starts the next one.
		std::vector<FrameEvent> TakeFrame();

		// Deep capture only: the same, for the per command state recorded alongside the events.
		// Their event indices have already been remapped onto the merged frame timeline.
		struct CapturedState
		{
			std::vector<CommandListState::RecordedDrawState> draw_states;
			std::vector<DrawBinding> bindings;
			std::vector<CommandListState::RecordedBarrierSet> barrier_sets;
			std::vector<BarrierEntry> barrier_entries;

			bool Empty() const { return draw_states.empty() && barrier_sets.empty(); }
		};
		CapturedState TakeCapturedState();

		uint64_t FrameIndex() const { return m_frame_index; }
		void AdvanceFrame() { ++m_frame_index; }

		uint32_t DrawCount() const { return m_draw_count; }
		uint32_t DispatchCount() const { return m_dispatch_count; }
		uint32_t EventCount() const;

		// Hard limit so a pathological frame cannot exhaust the game's memory.
		void SetMaxEvents(uint32_t max_events) { m_max_events = max_events; }
		uint32_t DroppedEvents() const { return m_dropped_events; }

	private:
		// Called with the mutex held, from Merge.
		void MergeCapturedState(CommandListState &state, uint32_t base, uint32_t accepted);

		mutable std::mutex m_mutex;
		std::vector<FrameEvent> m_events;
		uint64_t m_frame_index = 0;
		uint32_t m_draw_count = 0;
		uint32_t m_dispatch_count = 0;
		uint32_t m_dropped_events = 0;
		uint32_t m_max_events = 250000;
		CapturedState m_captured;
	};

	bool IsDrawEvent(EventKind kind);
	bool IsDispatchEvent(EventKind kind);
}
