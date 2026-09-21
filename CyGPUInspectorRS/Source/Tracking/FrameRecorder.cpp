// CyGPUInspectorRS — per frame event accumulation.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "FrameRecorder.hpp"

namespace cygi
{
	bool IsDrawEvent(EventKind kind)
	{
		return kind == EventKind::draw || kind == EventKind::draw_indexed || kind == EventKind::draw_indirect;
	}

	bool IsDispatchEvent(EventKind kind)
	{
		return kind == EventKind::dispatch || kind == EventKind::dispatch_indirect ||
		       kind == EventKind::dispatch_mesh || kind == EventKind::dispatch_rays;
	}

	FrameRecorder::MergeResult FrameRecorder::Merge(CommandListState &state)
	{
		MergeResult result;
		if (state.events.empty())
			return result;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			// Events are appended in order, so a command list's local event index maps onto the
			// merged timeline by a single offset — as long as none of its events were dropped,
			// which is what `accepted` tracks.
			const uint32_t base = static_cast<uint32_t>(m_events.size());
			uint32_t accepted = 0;
			for (size_t i = 0; i < state.events.size(); ++i)
			{
				if (m_events.size() >= m_max_events)
				{
					m_dropped_events += static_cast<uint32_t>(state.events.size() - i);
					break;
				}

				FrameEvent &event = state.events[i];
				event.index = static_cast<uint32_t>(m_events.size());
				if (IsDrawEvent(event.kind))
					++m_draw_count;
				else if (IsDispatchEvent(event.kind))
					++m_dispatch_count;
				m_events.push_back(event);
				++accepted;
			}

			MergeCapturedState(state, base, accepted);
			result.base = base;
			result.accepted = accepted;
		}

		state.events.clear();
		state.ResetCapture();
		return result;
	}

	// Called with the mutex held.
	void FrameRecorder::MergeCapturedState(CommandListState &state, uint32_t base, uint32_t accepted)
	{
		for (const CommandListState::RecordedDrawState &recorded : state.draw_states)
		{
			if (recorded.local_event >= accepted)
				continue;   // the event itself was dropped, so its state has nothing to attach to

			CommandListState::RecordedDrawState merged = recorded;
			merged.record.event_index = base + recorded.local_event;
			merged.binding_offset = static_cast<uint32_t>(m_captured.bindings.size());
			m_captured.bindings.insert(m_captured.bindings.end(),
				state.binding_pool.begin() + recorded.binding_offset,
				state.binding_pool.begin() + recorded.binding_offset + recorded.record.binding_count);
			m_captured.draw_states.push_back(merged);
		}

		for (const CommandListState::RecordedBarrierSet &recorded : state.barrier_sets)
		{
			if (recorded.local_event >= accepted)
				continue;

			CommandListState::RecordedBarrierSet merged = recorded;
			merged.record.event_index = base + recorded.local_event;
			merged.entry_offset = static_cast<uint32_t>(m_captured.barrier_entries.size());
			m_captured.barrier_entries.insert(m_captured.barrier_entries.end(),
				state.barrier_pool.begin() + recorded.entry_offset,
				state.barrier_pool.begin() + recorded.entry_offset + recorded.record.count);
			m_captured.barrier_sets.push_back(merged);
		}
	}

	FrameRecorder::CapturedState FrameRecorder::TakeCapturedState()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		CapturedState taken;
		taken.draw_states.swap(m_captured.draw_states);
		taken.bindings.swap(m_captured.bindings);
		taken.barrier_sets.swap(m_captured.barrier_sets);
		taken.barrier_entries.swap(m_captured.barrier_entries);
		return taken;
	}

	std::vector<FrameEvent> FrameRecorder::TakeFrame()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<FrameEvent> events;
		events.swap(m_events);
		// Keep the capacity of the swapped-out vector for the next frame to avoid reallocating
		// every frame: reserve the same size on the fresh one.
		m_events.reserve(events.size());
		m_draw_count = 0;
		m_dispatch_count = 0;
		m_dropped_events = 0;
		return events;
	}

	uint32_t FrameRecorder::EventCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_events.size());
	}
}
