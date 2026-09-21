// CyGPUInspectorApp — one frame laid out on a time axis.
//
// The timing view, the pass list and the selected pass all answer "how long did this pass take",
// and they used to answer it two different ways: the view measured where a pass starts and where
// it ends on the GPU, the list added up the measurements whose command fell inside the pass. When
// a measurement straddles two passes the two disagree, and a tool that shows 0.776 ms in one
// place and 0.632 ms in the next for the same pass is a tool nobody can trust. So it is computed
// once, here, and everything reads it.
//
// Positions come from the measured start of each timestamp. Between two measurements nothing is
// known, so a command that has none of its own is placed by interpolating on the commands between
// the two around it, and a pass with no measurement inside it is flagged as placed rather than
// measured. With no timings at all the same layout is made in commands instead of milliseconds,
// so the view is never empty.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"
#include "Session/SessionModel.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <vector>

namespace cygi
{
	class FrameTimeline
	{
	public:
		struct Point
		{
			uint32_t event = 0;
			double start = 0.0;     // monotonic: never before the point of the previous command
			double length = 0.0;
			EventKind kind = EventKind::none;
		};

		struct Span
		{
			double start = 0.0;
			double end = 0.0;
			// Whether a measurement starts inside the pass. When it does not, the pass was placed
			// between the measurements around it and its length is an estimate.
			bool measured = false;

			double Length() const { return end - start; }
		};

		// `events` is the frame the graph was built from, for the kind of each measured command.
		// The caller holds the model's lock for as long as this runs.
		void Build(const FrameTimings &timings, const FrameGraph &graph, const std::vector<FrameEvent> &events);

		// Where a command sits, in the timeline's unit.
		double TimeOf(uint32_t event) const;

		bool Timed() const { return m_timed; }
		// False when the add-on sent durations only: the measurements are then laid end to end.
		bool MeasuredPositions() const { return m_measured_positions; }
		// Milliseconds when Timed(), commands otherwise.
		double Length() const { return m_length; }
		uint64_t FrameIndex() const { return m_frame_index; }

		const std::vector<Point> &Points() const { return m_points; }
		// One per pass of the graph, in the same order.
		const std::vector<Span> &Spans() const { return m_spans; }
		const Span *SpanOfPass(uint32_t pass_id) const;

		// What a pass is as a share of the frame, 0 to 1.
		double ShareOf(uint32_t pass_id) const;

	private:
		std::vector<Point> m_points;
		std::vector<Span> m_spans;
		uint32_t m_event_end = 0;
		double m_length = 0.0;
		uint64_t m_frame_index = 0;
		bool m_timed = false;
		bool m_measured_positions = false;
	};
}
