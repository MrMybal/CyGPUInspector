// CyGPUInspectorRS — GPU timestamps.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "GpuTimer.hpp"

#include <algorithm>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		constexpr uint64_t kTimestampSize = sizeof(uint64_t);
	}

	bool GpuTimer::Initialize(device *device, uint32_t marks_per_frame)
	{
		Shutdown(device);
		if (device == nullptr || marks_per_frame == 0)
			return false;

		std::lock_guard<std::mutex> lock(m_mutex);

		// One fence for the whole timer, with an increasing value per closed frame: a slot is
		// readable once the fence has passed the value signalled after its copy was recorded.
		if (!device->create_fence(0, fence_flags::none, &m_fence))
			return false;

		const resource_desc readback_desc(kTimestampSize * marks_per_frame, memory_heap::readback,
			resource_usage::copy_dest);

		for (FrameSlot &frame : m_frames)
		{
			if (!device->create_query_heap(query_type::timestamp, marks_per_frame, &frame.heap) ||
			    !device->create_resource(readback_desc, nullptr, resource_usage::copy_dest,
			                             &frame.readback))
			{
				// Undo whatever was already created: a half initialised timer is worse than none.
				for (FrameSlot &created : m_frames)
				{
					if (created.heap.handle != 0)
						device->destroy_query_heap(created.heap);
					if (created.readback.handle != 0)
						device->destroy_resource(created.readback);
					created.heap = {};
					created.readback = {};
				}
				device->destroy_fence(m_fence);
				m_fence = {};
				return false;
			}
			frame.marks.reserve(marks_per_frame);
		}

		m_capacity = marks_per_frame;
		m_current = 0;
		m_dropped = 0;
		m_resolved = 0;
		m_abandoned = 0;
		m_discarded = 0;
		m_previous_close = 0;
		m_next_fence_value = 0;
		m_ready = true;
		return true;
	}

	void GpuTimer::Shutdown(device *device)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (FrameSlot &frame : m_frames)
		{
			if (device != nullptr)
			{
				if (frame.heap.handle != 0)
					device->destroy_query_heap(frame.heap);
				if (frame.readback.handle != 0)
					device->destroy_resource(frame.readback);
			}
			frame.heap = {};
			frame.readback = {};
			frame.marks.clear();
			frame.pending = false;
			frame.attempts = 0;
			frame.fence_value = 0;
		}
		if (m_fence.handle != 0 && device != nullptr)
			device->destroy_fence(m_fence);
		m_fence = {};
		m_ready = false;
		m_capacity = 0;
	}

	TimingMark GpuTimer::Mark(command_list *command_list, uint32_t local_event, uint32_t pipeline_id)
	{
		TimingMark handle;
		if (!m_ready || command_list == nullptr)
			return handle;

		std::lock_guard<std::mutex> lock(m_mutex);
		FrameSlot &frame = m_frames[m_current];
		if (frame.marks.size() >= m_capacity)
		{
			++m_dropped;
			return handle;
		}

		const uint32_t index = static_cast<uint32_t>(frame.marks.size());

		TimingResult mark = {};
		// Unattributed until the list is merged: see Attribute().
		mark.event_index = kInvalidMark;
		mark.pipeline_id = pipeline_id;
		frame.marks.push_back(mark);

		// A timestamp query is a single point in the queue, so only end_query is used.
		command_list->end_query(frame.heap, query_type::timestamp, index);

		handle.slot = m_current;
		handle.index = index;
		handle.generation = frame.generation;
		handle.local_event = local_event;
		return handle;
	}

	void GpuTimer::Attribute(const std::vector<TimingMark> &marks, uint32_t base, uint32_t accepted)
	{
		if (marks.empty())
			return;

		std::lock_guard<std::mutex> lock(m_mutex);
		for (const TimingMark &mark : marks)
		{
			if (!mark.IsValid() || mark.slot >= kTimerFramesInFlight)
				continue;
			FrameSlot &frame = m_frames[mark.slot];
			if (frame.generation != mark.generation || mark.index >= frame.marks.size())
				continue;
			// A command the frame had no room for has no index to be attributed to.
			frame.marks[mark.index].event_index =
				mark.local_event < accepted ? base + mark.local_event : kInvalidMark;
		}
	}

	void GpuTimer::EndFrame(device *device, command_queue *queue, uint64_t frame_index)
	{
		if (!m_ready || device == nullptr || queue == nullptr)
			return;

		command_list *const immediate = queue->get_immediate_command_list();

		uint64_t resolved_frame = 0;
		uint64_t resolved_begin = 0;
		uint64_t resolved_end = 0;
		std::vector<TimingResult> resolved;

		{
			std::lock_guard<std::mutex> lock(m_mutex);

			FrameSlot &frame = m_frames[m_current];
			if (!frame.marks.empty() && immediate != nullptr)
			{
				// One closing mark so the last recorded command has an interval to be measured in.
				if (frame.marks.size() < m_capacity)
				{
					TimingResult closing = {};
					closing.event_index = kInvalidMark;
					frame.marks.push_back(closing);
					immediate->end_query(frame.heap, query_type::timestamp,
						static_cast<uint32_t>(frame.marks.size()) - 1);
					frame.closed = true;
				}

				// Ask the GPU to write this frame's timestamps into our own buffer, then signal
				// the fence behind it. Both go on the queue's immediate command list, which
				// ReShade executes after the game's own work for the frame.
				immediate->copy_query_heap_results(frame.heap, query_type::timestamp, 0,
					static_cast<uint32_t>(frame.marks.size()), frame.readback, 0,
					static_cast<uint32_t>(kTimestampSize));

				frame.fence_value = ++m_next_fence_value;
				queue->signal(m_fence, frame.fence_value);

				frame.frame_index = frame_index;
				frame.pending = true;
			}
			// Closed, whether or not it held anything: marks handed out for it can no longer be
			// attributed, because its timestamps have already been asked for.
			++frame.generation;

			m_current = (m_current + 1) % kTimerFramesInFlight;

			// The slot we are about to reuse is the oldest one: read it back first.
			FrameSlot &oldest = m_frames[m_current];
			if (oldest.pending && !oldest.marks.empty())
			{
				++oldest.attempts;

				void *mapped = nullptr;
				const bool landed = oldest.fence_value != 0 &&
					device->get_completed_fence_value(m_fence) >= oldest.fence_value;

				if (landed && device->map_buffer_region(oldest.readback, 0,
						kTimestampSize * oldest.marks.size(), map_access::read_only, &mapped) &&
					mapped != nullptr)
				{
					const uint64_t *const timestamps = static_cast<const uint64_t *>(mapped);
					const size_t count = oldest.marks.size();

					// The heap is indexed in *recording* order: the order Mark() was called in,
					// across every thread the game records on. The GPU runs them in *execution*
					// order. On D3D11 the two are the same; on D3D12 they are not, and taking the
					// distance between neighbouring heap entries measured the gap between two
					// unrelated command lists. So the points are sorted by time first, and each
					// measurement runs until the next point the GPU actually reached.
					uint64_t close = 0;
					if (oldest.closed)
						close = timestamps[count - 1];
					else
						for (size_t i = 0; i < count; ++i)
							close = std::max(close, timestamps[i]);

					struct Point
					{
						uint64_t time;
						uint32_t mark;
					};
					std::vector<Point> points;
					points.reserve(count);
					for (size_t i = 0; i < count; ++i)
					{
						const uint64_t time = timestamps[i];
						// A query that never executed keeps whatever the slot held last time,
						// which is older than the previous frame's end, or zero if never used.
						if (time == 0 || time > close || (m_previous_close != 0 && time < m_previous_close))
						{
							++m_discarded;
							continue;
						}
						points.push_back({ time, static_cast<uint32_t>(i) });
					}
					std::stable_sort(points.begin(), points.end(),
						[](const Point &a, const Point &b) { return a.time < b.time; });

					if (!points.empty())
					{
						const uint64_t frame_start = points.front().time;
						resolved_begin = frame_start;
						resolved_end = close;
						resolved.reserve(points.size());
						for (size_t k = 0; k < points.size(); ++k)
						{
							const TimingResult &mark = oldest.marks[points[k].mark];
							if (mark.event_index == kInvalidMark)
								continue;

							TimingResult result = mark;
							result.gpu_start = points[k].time - frame_start;
							result.gpu_ticks = k + 1 < points.size() ? points[k + 1].time - points[k].time : 0;
							resolved.push_back(result);
						}
					}
					m_previous_close = close;

					device->unmap_buffer_region(oldest.readback);
					resolved_frame = oldest.frame_index;
					oldest.pending = false;
					++m_resolved;
				}
				else if (oldest.attempts >= kResolveAttempts)
				{
					// Give the slot up. Keeping it would be keeping its queries too, and once
					// every slot is held the heap is full, every later mark is dropped and the
					// timer never works again for the rest of the run — which is exactly what
					// happened the first time this ran against a real D3D12 game.
					oldest.pending = false;
					++m_abandoned;
				}
				// Otherwise it is merely late: keep it and try again next frame.
			}

			if (!oldest.pending)
			{
				oldest.marks.clear();
				oldest.attempts = 0;
				oldest.fence_value = 0;
				oldest.closed = false;
			}
		}

		if (!resolved.empty())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_ready_frame = resolved_frame;
			m_ready_begin = resolved_begin;
			m_ready_end = resolved_end;
			m_ready_results = std::move(resolved);
		}
	}

	bool GpuTimer::TakeResults(uint64_t &frame_index, std::vector<TimingResult> &out, uint64_t &gpu_begin,
	                           uint64_t &gpu_end)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_ready_results.empty())
			return false;

		frame_index = m_ready_frame;
		gpu_begin = m_ready_begin;
		gpu_end = m_ready_end;
		out = std::move(m_ready_results);
		m_ready_results.clear();
		return true;
	}

	uint32_t GpuTimer::ResolvedFrames() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_resolved;
	}

	uint32_t GpuTimer::AbandonedFrames() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_abandoned;
	}

	uint32_t GpuTimer::DiscardedMarks() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_discarded;
	}

	uint32_t GpuTimer::MarksThisFrame() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_frames[m_current].marks.size());
	}
}
