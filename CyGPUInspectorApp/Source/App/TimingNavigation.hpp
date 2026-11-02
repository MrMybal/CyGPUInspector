// Compatibility input policy for the application's read-only GPU timelines.
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once
#include <cyui/modules/Timeline.h>

namespace cygi
{
	struct TimingNavigation
	{
		bool right = false, dragged = false;

		// Call immediately after the shared timeline item. CyUI keeps wheel/middle navigation;
		// this preserves the old left/right drag shortcuts without enabling timing edits.
		bool Update(cyui::TimelineState &view)
		{
			const bool hovered = ImGui::IsItemHovered();
			if (ImGui::IsItemActivated() || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
				dragged = false;
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) right = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) right = false;
			const bool pan = (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, cyui::dp(3))) ||
				(right && ImGui::IsMouseDragging(ImGuiMouseButton_Right, cyui::dp(1)));
			if (pan)
			{
				const float width = std::max(cyui::dp(1), ImGui::GetItemRectSize().x);
				const double delta = ImGui::GetIO().MouseDelta.x / width * view.view.span();
				const cyui::Range next{view.view.min - delta, view.view.max - delta};
				if (next.valid()) view.view = next;
				view.selection = {}; view.mode = 0; // blank-space drags are pan, not a timing edit
				dragged = true;
			}
			return pan;
		}
	};
}
