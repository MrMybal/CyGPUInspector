// CyGPUInspectorApp — the drawing shared by the two timeline views.
//
// The single frame view and the continuous one draw the same kinds of things — a ruler, bars with
// a label clipped to them, passes coloured by what they do — and they have to look like the same
// tool. So the pieces live here once. Internal to the App directory: nothing outside the panels
// includes it.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <imgui.h>

#include <algorithm>

namespace cygi::timeline_drawing
{
	// The step between two labelled ticks of a ruler: 1, 2 or 5 times a power of ten, whichever
	// is the first to give at least `minimum` units between labels.
	inline double NiceStep(double minimum)
	{
		if (minimum <= 0.0)
			return 1.0;
		double power = 1.0;
		while (power * 10.0 <= minimum)
			power *= 10.0;
		while (power > minimum)
			power /= 10.0;
		for (const double factor : { 1.0, 2.0, 5.0, 10.0 })
			if (power * factor >= minimum)
				return power * factor;
		return power * 10.0;
	}

	inline ImU32 Colour(const ImVec4 &colour, float alpha = 1.0f)
	{
		return ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, colour.w * alpha));
	}

	// A pass is coloured by what it does, not by how long it took: length is already the bar.
	inline ImVec4 ColourOfPassKind(PassKind kind)
	{
		switch (kind)
		{
		case PassKind::compute: return ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
		case PassKind::transfer: return ImVec4(0.65f, 0.55f, 0.35f, 1.0f);
		case PassKind::graphics: return ImVec4(0.35f, 0.65f, 0.45f, 1.0f);
		default: return ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
		}
	}

	inline ImVec4 ColourOfCommand(EventKind kind)
	{
		if (kind == EventKind::draw || kind == EventKind::draw_indexed || kind == EventKind::draw_indirect)
			return ImVec4(0.38f, 0.60f, 0.86f, 1.0f);
		if (IsDrawOrDispatch(kind))
			return ImVec4(0.62f, 0.46f, 0.86f, 1.0f);
		return ImVec4(0.62f, 0.56f, 0.44f, 1.0f);
	}

	// One bar, with its label clipped to the bar so that text never spills into the neighbour it
	// does not belong to. Returns false when the bar is entirely outside the clip range.
	inline bool DrawBar(ImDrawList *draw, float x0, float x1, float y0, float y1, float clip_x0, float clip_x1,
	                    ImU32 fill, const char *label, bool outline)
	{
		const float left = std::max(x0, clip_x0);
		const float right = std::min(x1, clip_x1);
		if (right < clip_x0 || left > clip_x1)
			return false;
		// A pass shorter than a pixel still exists: it gets one.
		const float drawn_right = std::max(right, left + 1.0f);

		draw->AddRectFilled(ImVec2(left, y0), ImVec2(drawn_right, y1), fill);
		if (drawn_right - left > 4.0f)
			draw->AddRect(ImVec2(left, y0), ImVec2(drawn_right, y1), IM_COL32(0, 0, 0, 90));
		if (outline)
			draw->AddRect(ImVec2(left - 1.0f, y0 - 1.0f), ImVec2(drawn_right + 1.0f, y1 + 1.0f),
				IM_COL32(255, 255, 255, 230), 0.0f, 0, 2.0f);

		if (label != nullptr && drawn_right - left > 18.0f)
		{
			draw->PushClipRect(ImVec2(left + 3.0f, y0), ImVec2(drawn_right - 2.0f, y1), true);
			draw->AddText(ImVec2(left + 4.0f, y0 + (y1 - y0 - ImGui::GetFontSize()) * 0.5f),
				IM_COL32(245, 245, 245, 255), label);
			draw->PopClipRect();
		}
		return true;
	}

	// The same, hatched: for time in which nothing was measured, so that an empty stretch reads as
	// "waiting" rather than as a pass of its own.
	inline void DrawHatchedBar(ImDrawList *draw, float x0, float x1, float y0, float y1, float clip_x0,
	                           float clip_x1, const char *label)
	{
		const float left = std::max(x0, clip_x0);
		const float right = std::min(x1, clip_x1);
		if (right - left < 1.0f)
			return;

		draw->AddRectFilled(ImVec2(left, y0), ImVec2(right, y1), IM_COL32(38, 40, 46, 255));
		draw->PushClipRect(ImVec2(left, y0), ImVec2(right, y1), true);
		const float height = y1 - y0;
		for (float x = left - height; x < right; x += 7.0f)
			draw->AddLine(ImVec2(x, y1), ImVec2(x + height, y0), IM_COL32(255, 255, 255, 22));
		draw->PopClipRect();
		draw->AddRect(ImVec2(left, y0), ImVec2(right, y1), IM_COL32(255, 255, 255, 18));

		if (label != nullptr && right - left > 40.0f)
		{
			draw->PushClipRect(ImVec2(left + 3.0f, y0), ImVec2(right - 2.0f, y1), true);
			draw->AddText(ImVec2(left + 5.0f, y0 + (y1 - y0 - ImGui::GetFontSize()) * 0.5f),
				IM_COL32(200, 200, 200, 170), label);
			draw->PopClipRect();
		}
	}
}
