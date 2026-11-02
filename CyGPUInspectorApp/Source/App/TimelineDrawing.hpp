// CyGPUInspectorApp — the drawing shared by the two timeline views.
//
// Domain annotations only: pass-kind text colors and hatching for unmeasured GPU waits.
// Shared CyUI modules own navigation, rulers, bars, hit testing and clipping.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cyui/Core.h>

#include <algorithm>

namespace cygi::timeline_drawing
{
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

	// Hatched annotation for time in which nothing was measured, so that an empty stretch reads as
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
