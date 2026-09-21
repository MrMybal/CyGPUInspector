// CyGPUInspectorApp — the frames of the last few seconds, one after another on the GPU clock.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "FrameTrack.hpp"

#include "Analysis/FrameTimeline.hpp"

#include <algorithm>

namespace cygi
{
	void FrameTrack::Clear()
	{
		m_frames.clear();
		m_command_count = 0;
		m_origin = 0;
		m_frequency = 0;
		m_measured_axis = true;
	}

	void FrameTrack::Add(const FrameTimings &timings, const FrameInfo *frame, const SessionModel &model,
	                     const FrameGraph &fallback)
	{
		if (!timings.IsValid())
			return;

		// A frame that does not come after the last one kept is either a repeat, or the counter
		// of a new session starting again from zero. The first is ignored, the second starts over.
		if (!m_frames.empty() && timings.frame_index <= m_frames.back().index)
		{
			if (timings.frame_index + 1000 < m_frames.back().index)
				Clear();
			else
				return;
		}
		// A different clock, a different axis.
		if (m_frequency != 0 && m_frequency != timings.frequency)
			Clear();
		// An add-on either sends where frames sit or it does not; a change means a new session.
		if (!m_frames.empty() && m_measured_axis != timings.HasSpan())
			Clear();
		// The GPU clock going backwards means another device, or the same one reset.
		if (!m_frames.empty() && timings.HasSpan() && timings.gpu_begin < m_origin)
			Clear();

		if (m_frames.empty())
		{
			m_frequency = timings.frequency;
			m_measured_axis = timings.HasSpan();
			m_origin = timings.HasSpan() ? timings.gpu_begin : 0;
		}

		// Cut into passes with the frame's own events when they are still held. Timings arrive a
		// few frames late, and a menu frame read with the events of the gameplay frame after it
		// would be a different frame entirely.
		FrameGraph own;
		const FrameGraph *graph = &fallback;
		const std::vector<FrameEvent> *events = &model.LastFrame().events;
		bool own_structure = false;
		if (frame != nullptr && !frame->events.empty())
		{
			own.Build(model, *frame);
			graph = &own;
			events = &frame->events;
			own_structure = true;
		}

		FrameTimeline timeline;
		timeline.Build(timings, *graph, *events);

		TrackFrame out;
		out.index = timings.frame_index;
		out.own_structure = own_structure;

		if (m_measured_axis)
		{
			out.begin = timings.Milliseconds(timings.gpu_begin - m_origin);
			out.end = timings.Milliseconds(timings.gpu_end - m_origin);
		}
		else
		{
			// No position on the GPU clock: one CPU frame after the previous one, the only
			// period known. The view says so.
			const TrackFrame *previous = m_frames.empty() ? nullptr : &m_frames.back();
			out.begin = previous == nullptr ? 0.0
				: previous->begin + std::max(static_cast<double>(previous->cpu_ms), previous->Busy());
			out.end = out.begin + timeline.Length();
		}
		out.end = std::max(out.end, out.begin + timeline.Length());

		if (frame != nullptr)
		{
			out.cpu_ms = frame->cpu_frame_ms;
			out.event_count = static_cast<uint32_t>(frame->events.size());
			out.draw_count = frame->draw_count;
			out.dispatch_count = frame->dispatch_count;
		}
		else
		{
			for (auto entry = model.History().rbegin(); entry != model.History().rend(); ++entry)
			{
				if (entry->index != timings.frame_index)
					continue;
				out.cpu_ms = entry->cpu_frame_ms;
				out.event_count = entry->event_count;
				out.draw_count = entry->draw_count;
				out.dispatch_count = entry->dispatch_count;
				break;
			}
		}

		const std::vector<GraphPass> &passes = graph->Passes();
		out.passes.reserve(passes.size());
		for (size_t i = 0; i < passes.size() && i < timeline.Spans().size(); ++i)
		{
			const GraphPass &pass = passes[i];
			const FrameTimeline::Span &span = timeline.Spans()[i];
			TrackPass kept;
			kept.name = pass.name;
			kept.kind = pass.kind;
			kept.origin = pass.origin;
			kept.confidence = pass.confidence;
			kept.first_event = pass.first_event;
			kept.last_event = pass.last_event;
			kept.draw_count = pass.draw_count;
			kept.dispatch_count = pass.dispatch_count;
			kept.start = out.begin + span.start;
			kept.end = out.begin + span.end;
			kept.measured = span.measured;
			out.passes.push_back(std::move(kept));
		}

		out.commands.reserve(timeline.Points().size());
		for (const FrameTimeline::Point &point : timeline.Points())
		{
			TrackCommand command;
			command.event = point.event;
			command.kind = point.kind;
			command.start = out.begin + point.start;
			command.length = point.length;
			out.commands.push_back(command);
		}

		m_command_count += out.commands.size();
		m_frames.push_back(std::move(out));

		while (m_frames.size() > kMaxFrames)
		{
			m_command_count -= m_frames.front().commands.size();
			m_frames.pop_front();
		}
		// Past the budget, the oldest frames give up their commands first and keep their passes:
		// a spike from four seconds ago is still worth reading pass by pass.
		for (TrackFrame &old : m_frames)
		{
			if (m_command_count <= kMaxCommands)
				break;
			m_command_count -= old.commands.size();
			old.commands.clear();
			old.commands.shrink_to_fit();
		}
	}

	const TrackFrame *FrameTrack::FrameByIndex(uint64_t index) const
	{
		const auto found = std::lower_bound(m_frames.begin(), m_frames.end(), index,
			[](const TrackFrame &frame, uint64_t wanted) { return frame.index < wanted; });
		return found != m_frames.end() && found->index == index ? &*found : nullptr;
	}

	const TrackFrame *FrameTrack::FrameAt(double time) const
	{
		if (m_frames.empty())
			return nullptr;
		const auto after = std::upper_bound(m_frames.begin(), m_frames.end(), time,
			[](double wanted, const TrackFrame &frame) { return wanted < frame.begin; });
		return after == m_frames.begin() ? &m_frames.front() : &*(after - 1);
	}
}
