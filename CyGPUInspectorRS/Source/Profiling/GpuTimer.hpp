// CyGPUInspectorRS — GPU timestamps.
//
// ReShade calls us *before* a command, never after, so a command cannot be bracketed. What is
// measured instead is the interval between one recorded command and the next: the time attributed
// to a draw is the time from its start until the following recorded command starts. That is what
// every before-only interceptor can honestly report, and it is stated plainly in the UI.
//
// The read back does not use `device::get_query_heap_results`. On D3D12 that function checks a
// per-query fence that ReShade only signals for queries its own runtime issued, so for queries an
// add-on records on the game's command lists it returns false for ever: the first run against a
// real D3D12 game resolved exactly zero frames out of several hundred. The route that works, and
// that is just as official, is `command_list::copy_query_heap_results` into a readback buffer of
// our own, a fence signalled on the game's queue so we know when that copy has actually landed,
// and `device::map_buffer_region` to read it. That is what this does.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <reshade.hpp>

#include "Tracking/CommandListState.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cygi
{
	inline constexpr uint32_t kTimerFramesInFlight = 4;
	inline constexpr uint32_t kInvalidMark = 0xFFFFFFFFu;
	// How many frames a slot may stay unresolved before it is given up. Four slots and four
	// attempts each means a driver has sixteen frames to answer, which is generous, while a
	// driver that never answers costs a quarter of the timestamps rather than all of them: a
	// slot that is never released is a slot whose queries are never reusable either.
	inline constexpr uint32_t kResolveAttempts = 4;

	class GpuTimer
	{
	public:
		bool Initialize(reshade::api::device *device, uint32_t marks_per_frame);
		void Shutdown(reshade::api::device *device);

		bool IsReady() const { return m_ready; }
		uint32_t Capacity() const { return m_capacity; }

		// Records a timestamp for the command at `local_event` of the list being recorded. The
		// mark measures nothing until Attribute() says which command of the frame that is: on
		// D3D12 every command list numbers its commands from zero, and taking the local number as
		// the frame's put every measurement of a frame on its first few commands.
		TimingMark Mark(reshade::api::command_list *command_list, uint32_t local_event, uint32_t pipeline_id);

		// Called when a list is merged into the frame, with where it landed. A mark whose slot has
		// been closed since (the list was executed a frame later than it was recorded) is left
		// alone: its timestamp was already copied out before the command ran.
		void Attribute(const std::vector<TimingMark> &marks, uint32_t base, uint32_t accepted);

		// Called at present: closes the frame, asks the GPU to copy its timestamps out, then reads
		// back whatever has landed. The queue is needed for the fence that says when that is.
		void EndFrame(reshade::api::device *device, reshade::api::command_queue *queue,
		              uint64_t frame_index);

		// Moves out the results that finished, with the frame they belong to and where that frame
		// begins and ends on the GPU clock, in absolute ticks.
		bool TakeResults(uint64_t &frame_index, std::vector<TimingResult> &out, uint64_t &gpu_begin,
		                 uint64_t &gpu_end);

		uint32_t MarksThisFrame() const;
		uint32_t DroppedMarks() const { return m_dropped; }

		// How the read back is going, for the diagnostic line the add-on logs. A slot that never
		// resolves is the failure that matters: it used to jam the timer for the rest of the run.
		uint32_t ResolvedFrames() const;
		uint32_t AbandonedFrames() const;
		// Timestamps thrown away because they could not belong to the frame they were read for.
		uint32_t DiscardedMarks() const;

	private:
		struct FrameSlot
		{
			reshade::api::query_heap heap = {};
			// Where the GPU copies this slot's timestamps, and the fence value that says the copy
			// has been executed rather than merely recorded.
			reshade::api::resource readback = {};
			uint64_t fence_value = 0;
			std::vector<TimingResult> marks;
			uint64_t frame_index = 0;
			bool pending = false;
			// Bumped every time the slot is closed, so a mark handed out before cannot be
			// attributed into a frame that is no longer the one it was taken in.
			uint32_t generation = 0;
			// Whether the last mark is the closing one, recorded at present on the queue's own
			// command list: the one timestamp of the frame that is certain to have executed.
			bool closed = false;
			// How many times this slot has been asked for its results. A driver that is simply
			// late gets more frames; one that never answers must not keep the slot forever, or
			// the heap fills up and every later mark is dropped.
			uint32_t attempts = 0;
		};

		mutable std::mutex m_mutex;
		FrameSlot m_frames[kTimerFramesInFlight];
		reshade::api::fence m_fence = {};
		uint64_t m_next_fence_value = 0;
		uint32_t m_current = 0;
		uint32_t m_capacity = 0;
		uint32_t m_dropped = 0;
		bool m_ready = false;

		uint64_t m_ready_frame = 0;
		uint64_t m_ready_begin = 0;
		uint64_t m_ready_end = 0;
		std::vector<TimingResult> m_ready_results;
		uint32_t m_resolved = 0;
		uint32_t m_abandoned = 0;
		uint32_t m_discarded = 0;
		// The closing timestamp of the last frame resolved: anything of a later frame older than
		// it is a value left in the heap by an earlier use of the slot, not a measurement.
		uint64_t m_previous_close = 0;
	};
}
