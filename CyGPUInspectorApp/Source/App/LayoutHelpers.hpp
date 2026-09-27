// CyGPUInspectorApp — small layout helpers shared by panels.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <imgui.h>

namespace cygi
{
	// SameLine when the next small button still fits on this line, a new line otherwise. Panels are
	// often narrow once docked, and a button pushed past the right edge cannot be clicked at all.
	inline void SameLineIfFits(const char *next_label)
	{
		const ImGuiStyle &style = ImGui::GetStyle();
		const float width = ImGui::CalcTextSize(next_label, nullptr, true).x + style.FramePadding.x * 2.0f;
		const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - style.WindowPadding.x -
			style.ScrollbarSize;
		if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
			ImGui::SameLine();
	}
}
