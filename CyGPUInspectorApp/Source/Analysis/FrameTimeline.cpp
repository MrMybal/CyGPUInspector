// CyGPUInspectorApp — one frame laid out on a time axis.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "FrameTimeline.hpp"

#include <algorithm>

namespace cygi
{
	void FrameTimeline::Build(const FrameTimings &timings, const FrameGraph &graph,
	                          const std::vector<FrameEvent> &events)
	{
		m_points.clear();
		m_spans.clear();
		m_length = 0.0;
		m_timed = timings.IsValid();
		m_measured_positions = m_timed && timings.HasStarts();
		m_frame_index = m_timed ? timings.frame_index : 0;

		const std::vector<GraphPass> &passes = graph.Passes();
		m_event_end = passes.empty() ? 0 : passes.back().last_event + 1;

		if (m_timed)
		{
			m_points.reserve(timings.by_event.size());
			for (const auto &entry : timings.by_event)
			{
				Point point;
				point.event = entry.first;
				point.length = timings.Milliseconds(entry.second);
				if (m_measured_positions)
				{
					const auto start = timings.start_by_event.find(entry.first);
					if (start != timings.start_by_event.end())
						point.start = timings.Milliseconds(start->second);
				}
				m_points.push_back(point);
			}
			std::sort(m_points.begin(), m_points.end(),
				[](const Point &a, const Point &b) { return a.event < b.event; });

			if (m_measured_positions)
			{
				// Execution order and recording order can disagree on D3D12, and a pass must not
				// end before it starts: the positions are made monotonic in command order.
				for (size_t i = 1; i < m_points.size(); ++i)
					m_points[i].start = std::max(m_points[i].start, m_points[i - 1].start);
			}
			else
			{
				// Durations only, from an add-on that predates start times: end to end, which is
				// right on one queue, and the view says that is what it did.
				double cursor = 0.0;
				for (Point &point : m_points)
				{
					point.start = cursor;
					cursor += point.length;
				}
			}

			for (Point &point : m_points)
			{
				const auto found = std::lower_bound(events.begin(), events.end(), point.event,
					[](const FrameEvent &event, uint32_t index) { return event.index < index; });
				if (found != events.end() && found->index == point.event)
					point.kind = found->kind;
				m_length = std::max(m_length, point.start + point.length);
			}
		}
		else
		{
			m_length = static_cast<double>(m_event_end);
		}

		m_spans.resize(passes.size());
		for (size_t i = 0; i < passes.size(); ++i)
		{
			const GraphPass &pass = passes[i];
			Span &span = m_spans[i];
			span.start = TimeOf(pass.first_event);
			span.end = std::max(span.start, TimeOf(pass.last_event + 1));

			if (m_timed)
			{
				const auto first = std::lower_bound(m_points.begin(), m_points.end(), pass.first_event,
					[](const Point &point, uint32_t index) { return point.event < index; });
				span.measured = first != m_points.end() && first->event <= pass.last_event;
			}
		}

		if (m_length <= 0.0)
			m_length = 1.0;
	}

	double FrameTimeline::TimeOf(uint32_t event) const
	{
		if (!m_timed)
			return static_cast<double>(event);
		if (m_points.empty())
			return 0.0;

		const auto after = std::upper_bound(m_points.begin(), m_points.end(), event,
			[](uint32_t index, const Point &point) { return index < point.event; });
		if (after == m_points.begin())
			return m_points.front().start;

		const Point &point = *(after - 1);
		if (point.event == event)
			return point.start;

		// Inside a measurement: the commands it covers share its length evenly. That is an
		// estimate and the only one available — the measurement says how long the stretch took,
		// not how it was divided.
		const uint32_t next = after != m_points.end() ? after->event : m_event_end;
		const double fraction = next > point.event
			? static_cast<double>(event - point.event) / static_cast<double>(next - point.event)
			: 0.0;
		return point.start + point.length * std::min(fraction, 1.0);
	}

	const FrameTimeline::Span *FrameTimeline::SpanOfPass(uint32_t pass_id) const
	{
		return (pass_id != 0 && pass_id <= m_spans.size()) ? &m_spans[pass_id - 1] : nullptr;
	}

	double FrameTimeline::ShareOf(uint32_t pass_id) const
	{
		const Span *span = SpanOfPass(pass_id);
		return span != nullptr && m_length > 0.0 ? span->Length() / m_length : 0.0;
	}
}
