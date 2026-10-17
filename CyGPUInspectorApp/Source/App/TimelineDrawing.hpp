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

#include <cyui/CyUI.h>

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
		case PassKind::compute: return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(0), 0.6f));
		case PassKind::transfer: return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(1), 0.6f));
		case PassKind::graphics: return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(2), 0.6f));
		default: return cyui::vec(cyui::palette().bg4);
		}
	}

	inline ImVec4 ColourOfCommand(EventKind kind)
	{
		if (kind == EventKind::draw || kind == EventKind::draw_indexed || kind == EventKind::draw_indirect)
			return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(0), 0.6f));
		if (IsDrawOrDispatch(kind))
			return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(2), 0.6f));
		return cyui::vec(cyui::mix(cyui::palette().text, cyui::categorical(1), 0.6f));
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
		const float drawn_right = std::max(right, left + cyui::dp(1.0f));

		draw->AddRectFilled(ImVec2(left, y0), ImVec2(drawn_right, y1),
			cyui::mix(cyui::palette().bg3, fill, 0.35f));
		if (drawn_right - left > cyui::dp(4.0f))
			draw->AddRect(ImVec2(left, y0), ImVec2(drawn_right, y1), cyui::palette().lineStrong);
		if (outline)
			draw->AddRect(ImVec2(left - cyui::dp(1.0f), y0 - cyui::dp(1.0f)), ImVec2(drawn_right + cyui::dp(1.0f), y1 + cyui::dp(1.0f)),
				cyui::palette().accent, 0.0f, 0, cyui::dp(2.0f));

		if (label != nullptr && drawn_right - left > cyui::dp(18.0f))
		{
			draw->PushClipRect(ImVec2(left + cyui::dp(3.0f), y0), ImVec2(drawn_right - cyui::dp(2.0f), y1), true);
			draw->AddText(ImVec2(left + cyui::dp(4.0f), y0 + (y1 - y0 - ImGui::GetFontSize()) * 0.5f),
				cyui::palette().text, label);
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
		if (right - left < cyui::dp(1.0f))
			return;

		draw->AddRectFilled(ImVec2(left, y0), ImVec2(right, y1), cyui::palette().bg2);
		draw->PushClipRect(ImVec2(left, y0), ImVec2(right, y1), true);
		const float height = y1 - y0;
		for (float x = left - height; x < right; x += cyui::dp(7.0f))
			draw->AddLine(ImVec2(x, y1), ImVec2(x + height, y0), cyui::palette().grid);
		draw->PopClipRect();
		draw->AddRect(ImVec2(left, y0), ImVec2(right, y1), cyui::palette().line);

		if (label != nullptr && right - left > cyui::dp(40.0f))
		{
			draw->PushClipRect(ImVec2(left + cyui::dp(3.0f), y0), ImVec2(right - cyui::dp(2.0f), y1), true);
			draw->AddText(ImVec2(left + cyui::dp(5.0f), y0 + (y1 - y0 - ImGui::GetFontSize()) * 0.5f),
				cyui::palette().textDim, label);
			draw->PopClipRect();
		}
	}
}
