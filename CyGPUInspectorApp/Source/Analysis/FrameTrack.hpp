// CyGPUInspectorApp — the frames of the last few seconds, one after another on the GPU clock.
//
// The single frame view answers "what did this frame do". This answers what a profiler's timing
// view answers: how frames follow each other, where the GPU was busy, where it waited for the
// next frame, and which pass made the one frame that spiked. Every measured frame is cut into
// passes with its own events and its own timings, then kept in compact form — names, positions,
// lengths — so that it stays inspectable seconds after its events are gone.
//
// The axis is the GPU's own clock, shifted so that the first frame kept starts at zero. It is
// only continuous when the add-on sends where each frame sits on that clock (FrameGpuSpanRecord);
// an older add-on only gives lengths, and the frames are then laid one CPU frame apart, which the
// view says rather than presenting a guess as a measurement.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"
#include "Session/SessionModel.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cygi
{
	struct TrackPass
	{
		std::string name;
		PassKind kind = PassKind::unknown;
		PassNameOrigin origin = PassNameOrigin::unknown;
		float confidence = 0.0f;
		uint32_t first_event = 0;
		uint32_t last_event = 0;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;
		double start = 0.0;     // on the track's axis, milliseconds
		double end = 0.0;
		bool measured = false;  // a timestamp starts inside it; otherwise it was placed between two

		double Length() const { return end - start; }
	};

	struct TrackCommand
	{
		uint32_t event = 0;
		EventKind kind = EventKind::none;
		double start = 0.0;
		double length = 0.0;
	};

	struct TrackFrame
	{
		uint64_t index = 0;
		// Where the GPU was busy with it, on the track's axis: from its earliest measured point
		// to its closing timestamp.
		double begin = 0.0;
		double end = 0.0;
		float cpu_ms = 0.0f;
		uint32_t event_count = 0;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;
		// False when the frame's own events were already gone and its passes were cut with the
		// events of a later frame instead. Usually identical, but not a certainty, and shown.
		bool own_structure = true;
		std::vector<TrackPass> passes;
		std::vector<TrackCommand> commands;

		double Busy() const { return end - begin; }
	};

	class FrameTrack
	{
	public:
		// Frames kept: as many as the history strip shows, so the two describe the same seconds.
		static constexpr size_t kMaxFrames = kMaxFrameHistory;
		// Measured commands kept across all frames. At Full Draw Timing a frame can carry sixteen
		// thousand; past this budget the oldest frames keep their passes and lose their commands.
		static constexpr size_t kMaxCommands = 1500000;

		// Adds one measured frame. `frame` is that frame's own events when the model still holds
		// them; `fallback` is used to cut it into passes when it does not. The caller holds the
		// model's lock.
		void Add(const FrameTimings &timings, const FrameInfo *frame, const SessionModel &model,
		         const FrameGraph &fallback);
		void Clear();

		const std::deque<TrackFrame> &Frames() const { return m_frames; }
		const TrackFrame *FrameByIndex(uint64_t index) const;
		bool Empty() const { return m_frames.empty(); }

		// Whether positions come from the GPU clock (true), or frames were laid one CPU frame
		// apart because the add-on predates FrameGpuSpanRecord (false).
		bool MeasuredAxis() const { return m_measured_axis; }
		double Start() const { return m_frames.empty() ? 0.0 : m_frames.front().begin; }
		double End() const { return m_frames.empty() ? 0.0 : m_frames.back().end; }

		// The frame whose busy span contains `time`, or the last one that starts before it.
		const TrackFrame *FrameAt(double time) const;

	private:
		std::deque<TrackFrame> m_frames;
		size_t m_command_count = 0;
		uint64_t m_origin = 0;       // GPU ticks of the axis' zero
		uint64_t m_frequency = 0;
		bool m_measured_axis = true;
	};
}
