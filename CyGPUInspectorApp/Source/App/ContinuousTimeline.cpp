// CyGPUInspectorApp — the continuous timeline.
//
// Every measured frame of the last few seconds, one after another on the GPU clock, the way a
// profiler's timing view shows them: a row of frames with the time the GPU waited between two of
// them, the passes of each frame under it, and the commands measured inside those passes under
// that. Picking a frame in the strip above brings it into view, and the frame picked is taken
// apart pass by pass below — which is how a spike is read after it has happened.
//
// It is the second view of the runtime capture, not a replacement for the first: the single frame
// view shows everything about the latest frame, this one shows how frames follow each other.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include <cyui/Core.h>
#include "TimelineDrawing.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;
	using timeline_drawing::ColourOfPassKind;

	void Application::UpdateFrameTrack()
	{
		SessionModel *model = ActiveModel();
		if (model != m_track_model)
		{
			m_track.Clear();
			m_track_model = model;
			m_track_selected_frame = 0;
			m_track_selected_pass = -1;
			m_track_follow = true;
			m_track_view_span = 0.0;
		}
		if (model == nullptr)
			return;

		std::lock_guard<std::mutex> lock(model->Mutex());
		std::deque<FrameTimings> queued = model->TakeQueuedTimings();

		// Paused: what arrives is let go, so the frames on screen stay the ones being read.
		if (!m_timeline_follow || queued.empty())
			return;

		// Each frame is cut into passes with its own events, which is a frame graph per frame.
		// Bounded per interface frame, so that a game running far faster than the interface draws
		// cannot stall it: past the bound, the oldest of the batch are the ones left out, and the
		// view shows them as frames not measured rather than pretending they were not there.
		constexpr size_t kPerInterfaceFrame = 6;
		const size_t first = queued.size() > kPerInterfaceFrame ? queued.size() - kPerInterfaceFrame : 0;
		for (size_t i = first; i < queued.size(); ++i)
			m_track.Add(queued[i], model->FrameByIndex(queued[i].frame_index), *model, m_graph);
	}


	void Application::DrawTrackFrameDetail()
	{
		const TrackFrame *frame = m_track_selected_frame != 0 ? m_track.FrameByIndex(m_track_selected_frame) : nullptr;
		if (frame == nullptr && !m_track.Empty())
			frame = &m_track.Frames().back();
		if (frame == nullptr)
			return;

		const TrackFrame *next = m_track.FrameByIndex(frame->index + 1);

		ImGui::Text(Tr("Frame %llu"), static_cast<unsigned long long>(frame->index));
		ImGui::SameLine();
		ImGui::TextDisabled(Tr("| GPU busy %.3f ms | CPU %.2f ms | %u draws, %u dispatches"), frame->Busy(),
			static_cast<double>(frame->cpu_ms), frame->draw_count, frame->dispatch_count);
		if (next != nullptr && next->begin > frame->end)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("| then %.3f ms waiting"), next->begin - frame->end);
		}
		if (m_track_selected_frame == 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("(the newest: click a frame to pick another)"));
		}
		if (!frame->own_structure)
			ImGui::TextColored(cyui::vec(cyui::palette().warning),
				Tr("Its own commands were no longer held when its timings arrived: its passes were cut with the "
				   "commands of a later frame."));

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("track-passes", 5, kFlags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(TrId("Pass"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn(TrId("Cmds"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(46.0f));
		ImGui::TableSetupColumn(TrId("Starts at"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(72.0f));
		ImGui::TableSetupColumn(TrId("GPU ms"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(62.0f));
		ImGui::TableSetupColumn(TrId("%"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(44.0f));
		ImGui::TableHeadersRow();

		std::vector<size_t> order(frame->passes.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		const ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs();
		if (specs != nullptr && specs->SpecsCount > 0)
		{
			const ImGuiTableColumnSortSpecs &spec = specs->Specs[0];
			const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
				const TrackPass &left = frame->passes[a];
				const TrackPass &right = frame->passes[b];
				double l = 0.0;
				double r = 0.0;
				if (spec.ColumnIndex == 1)
				{
					l = static_cast<double>(left.last_event - left.first_event + 1);
					r = static_cast<double>(right.last_event - right.first_event + 1);
				}
				else if (spec.ColumnIndex == 2)
				{
					l = left.start;
					r = right.start;
				}
				else
				{
					l = left.Length();
					r = right.Length();
				}
				return ascending ? l < r : r < l;
			});
		}

		for (const size_t i : order)
		{
			const TrackPass &pass = frame->passes[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::PushStyleColor(ImGuiCol_Text, ColourOfPassKind(pass.kind));
			const bool selected = frame->index == m_track_selected_frame && static_cast<int>(i) == m_track_selected_pass;
			if (ImGui::Selectable(pass.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
			{
				m_track_selected_frame = frame->index;
				m_track_selected_pass = static_cast<int>(i);
				// Bring it into view if it is not.
				if (pass.end < m_track_view_start || pass.start > m_track_view_start + m_track_view_span)
				{
					m_track_view_start = pass.start - m_track_view_span * 0.3;
					m_track_follow = false;
				}
			}
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::Text("%u", pass.last_event - pass.first_event + 1);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", pass.start - frame->begin);
			ImGui::TableNextColumn();
			if (pass.measured)
				ImGui::Text("%.3f", pass.Length());
			else
				ImGui::TextDisabled("~%.3f", pass.Length());
			ImGui::TableNextColumn();
			const double share = frame->Busy() > 0.0 ? pass.Length() / frame->Busy() * 100.0 : 0.0;
			if (pass.measured)
				ImGui::Text("%.1f", share);
			else
				ImGui::TextDisabled("~%.1f", share);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}
}
