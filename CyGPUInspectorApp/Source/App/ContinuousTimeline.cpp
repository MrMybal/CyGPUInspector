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
	using timeline_drawing::Colour;
	using timeline_drawing::ColourOfCommand;
	using timeline_drawing::ColourOfPassKind;
	using timeline_drawing::DrawBar;
	using timeline_drawing::DrawHatchedBar;
	using timeline_drawing::NiceStep;

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

	namespace
	{
		// Seconds on the ruler, with as many decimals as the step between ticks needs.
		void FormatSeconds(char *out, size_t size, double milliseconds, double step)
		{
			int decimals = 0;
			if (step < 1000.0)
				decimals = 1;
			if (step < 100.0)
				decimals = 2;
			if (step < 10.0)
				decimals = 3;
			if (step < 1.0)
				decimals = 4;
			if (step < 0.1)
				decimals = 5;
			if (step < 0.01)
				decimals = 6;
			std::snprintf(out, size, "%.*f s", decimals, milliseconds / 1000.0);
		}
	}

	void Application::DrawContinuousView()
	{
		const std::deque<TrackFrame> &frames = m_track.Frames();
		if (frames.empty())
		{
			ImGui::TextDisabled(Tr("No measured frame yet. The continuous view is built from GPU timings: raise "
			                       "the tracking level to Pass Timing."));
			return;
		}
		if (!m_track.MeasuredAxis())
			ImGui::TextDisabled(Tr("Frames laid one CPU frame apart: this add-on does not send where frames sit "
			                       "on the GPU clock, so the waits between them are not measured."));

		const double track_start = m_track.Start();
		const double track_end = m_track.End();

		// ---- the window onto the track ---------------------------------------------------------
		if (m_track_view_span <= 0.0)
		{
			// About four frames, whatever the frame rate: a frame, its wait and its neighbours.
			const size_t back = std::min<size_t>(frames.size() - 1, 4);
			const double period = back > 0
				? (frames.back().begin - frames[frames.size() - 1 - back].begin) / static_cast<double>(back)
				: frames.back().Busy();
			m_track_view_span = std::max(period * 4.0, 0.05);
		}

		if (m_track_center_request != 0)
		{
			// The frame picked in the strip, or the closest one that was measured.
			const TrackFrame *target = nullptr;
			for (const TrackFrame &frame : frames)
			{
				if (target == nullptr || (frame.index > m_track_center_request
					? frame.index - m_track_center_request : m_track_center_request - frame.index) <
					(target->index > m_track_center_request
					? target->index - m_track_center_request : m_track_center_request - target->index))
					target = &frame;
			}
			if (target != nullptr)
			{
				m_track_view_start = target->begin - (m_track_view_span - target->Busy()) * 0.5;
				m_track_selected_frame = target->index;
			}
			m_track_center_request = 0;
		}
		if (m_track_follow)
			m_track_view_start = track_end - m_track_view_span * 0.97;

		// ---- what is visible -------------------------------------------------------------------
		const double view_start = m_track_view_start;
		const double view_end = m_track_view_start + m_track_view_span;

		auto first_visible = std::upper_bound(frames.begin(), frames.end(), view_start,
			[](double time, const TrackFrame &frame) { return time < frame.begin; });
		if (first_visible != frames.begin())
			--first_visible;

		// Passes can overlap — two queues, or a frame whose work started before the previous one
		// closed — so they are packed into as many lanes as it takes, up to four.
		constexpr int kMaxLanes = 4;
		struct Placed
		{
			const TrackFrame *frame;
			size_t pass;
			int lane;
		};
		std::vector<Placed> placed;
		double lane_end[kMaxLanes];
		for (double &end : lane_end)
			end = -std::numeric_limits<double>::infinity();
		int lanes = 1;
		bool show_commands = false;

		for (auto frame = first_visible; frame != frames.end() && frame->begin <= view_end; ++frame)
		{
			size_t measured = 0;
			for (size_t i = 0; i < frame->passes.size(); ++i)
			{
				const TrackPass &pass = frame->passes[i];
				measured += pass.measured ? 1 : 0;
				if (pass.end < view_start || pass.start > view_end)
					continue;
				int lane = 0;
				while (lane < kMaxLanes - 1 && pass.start < lane_end[lane] - 1e-9)
					++lane;
				lane_end[lane] = std::max(lane_end[lane], pass.end);
				lanes = std::max(lanes, lane + 1);
				placed.push_back({ &*frame, i, lane });
			}
			// The commands row only when it says more than the passes row.
			if (frame->commands.size() > measured)
				show_commands = true;
		}

		// ---- geometry --------------------------------------------------------------------------
		constexpr float kLabels = 74.0f;
		constexpr float kRuler = 18.0f;
		constexpr float kRow = 22.0f;
		constexpr float kGap = 3.0f;
		const int rows = 1 + lanes + (show_commands ? 1 : 0);
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = kRuler + static_cast<float>(rows) * (kRow + kGap) + 2.0f;

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("continuous-view", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		const bool owns_wheel = ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

		const float area_x0 = origin.x + kLabels;
		const float area_x1 = origin.x + width;
		const float area_width = std::max(area_x1 - area_x0, 1.0f);

		ImGuiIO &io = ImGui::GetIO();
		const double length = std::max(track_end - track_start, 1.0);
		if (hovered && owns_wheel && io.MouseWheel != 0.0f && io.MousePos.x >= area_x0)
		{
			const double anchor = (io.MousePos.x - area_x0) / area_width;
			const double at = m_track_view_start + anchor * m_track_view_span;
			m_track_view_span = std::clamp(m_track_view_span * std::pow(0.8, static_cast<double>(io.MouseWheel)),
				0.005, length * 1.2);
			m_track_view_start = at - anchor * m_track_view_span;
			m_track_follow = false;
		}
		if (ImGui::IsItemActivated())
			m_track_dragged = false;
		if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) ||
		                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f) ||
		                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)))
		{
			m_track_view_start -= static_cast<double>(io.MouseDelta.x) / area_width * m_track_view_span;
			m_track_dragged = true;
			m_track_follow = false;
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			m_track_follow = true;
		m_track_view_start = std::clamp(m_track_view_start, track_start - m_track_view_span * 0.5,
			std::max(track_end - m_track_view_span * 0.1, track_start));

		const auto x_of = [&](double time) {
			return area_x0 + static_cast<float>((time - m_track_view_start) / m_track_view_span) * area_width;
		};

		ImDrawList *draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, ImVec2(area_x1, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg));

		// ---- ruler -----------------------------------------------------------------------------
		const double step = NiceStep(m_track_view_span * 110.0 / area_width);
		draw->PushClipRect(ImVec2(area_x0, origin.y), ImVec2(area_x1, origin.y + height), true);
		for (double tick = std::floor(m_track_view_start / step) * step; tick <= view_end; tick += step)
		{
			const float x = x_of(tick);
			draw->AddLine(ImVec2(x, origin.y + kRuler - 5.0f), ImVec2(x, origin.y + kRuler),
				IM_COL32(255, 255, 255, 60));
			if (x + 3.0f < area_x0)
				continue;
			char text[32];
			FormatSeconds(text, sizeof(text), tick, step);
			draw->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), IM_COL32(255, 255, 255, 140), text);
		}

		// Where each frame starts, across every row: the boundaries a profiler draws.
		for (auto frame = first_visible; frame != frames.end() && frame->begin <= view_end; ++frame)
		{
			const float x = x_of(frame->begin);
			draw->AddLine(ImVec2(x, origin.y + kRuler), ImVec2(x, origin.y + height), IM_COL32(255, 255, 255, 40));
		}
		draw->PopClipRect();

		// ---- rows ------------------------------------------------------------------------------
		const float row_frames = origin.y + kRuler + 1.0f;
		const float row_passes = row_frames + kRow + kGap;
		const float row_commands = row_passes + static_cast<float>(lanes) * (kRow + kGap);

		const ImU32 label_colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		draw->AddText(ImVec2(origin.x + 4.0f, origin.y + 1.0f), label_colour, Tr("GPU"));
		draw->AddText(ImVec2(origin.x + 4.0f, row_frames + 3.0f), label_colour, Tr("Frames"));
		draw->AddText(ImVec2(origin.x + 4.0f, row_passes + 3.0f), label_colour, Tr("Passes"));
		if (show_commands)
			draw->AddText(ImVec2(origin.x + 4.0f, row_commands + 3.0f), label_colour, Tr("Commands"));

		char label[192];
		enum class Hit
		{
			none,
			frame,
			wait,
			pass,
			command
		};
		Hit hit = Hit::none;
		const TrackFrame *hit_frame = nullptr;
		const TrackFrame *hit_next = nullptr;
		size_t hit_item = 0;

		const auto inside = [&](float x0, float x1, float y0) {
			return hovered && io.MousePos.y >= y0 && io.MousePos.y < y0 + kRow &&
				io.MousePos.x >= std::max(x0, area_x0) && io.MousePos.x < std::max(x1, x0 + 1.0f);
		};

		// Frames, and the waits between them.
		for (auto frame = first_visible; frame != frames.end() && frame->begin <= view_end; ++frame)
		{
			const float x0 = x_of(frame->begin);
			const float x1 = x_of(frame->end);
			std::snprintf(label, sizeof(label), Tr("Frame %llu  %.2f ms"),
				static_cast<unsigned long long>(frame->index), frame->Busy());
			DrawBar(draw, x0, x1, row_frames, row_frames + kRow, area_x0, area_x1, IM_COL32(70, 78, 96, 255),
				label, frame->index == m_track_selected_frame);
			if (inside(x0, x1, row_frames))
			{
				hit = Hit::frame;
				hit_frame = &*frame;
			}

			const auto next = frame + 1;
			if (next != frames.end() && next->begin > frame->end)
			{
				const float w0 = x_of(frame->end);
				const float w1 = x_of(next->begin);
				const uint64_t missing = next->index > frame->index + 1 ? next->index - frame->index - 1 : 0;
				if (missing != 0)
					std::snprintf(label, sizeof(label), Tr("%llu frames not measured"),
						static_cast<unsigned long long>(missing));
				else
					std::snprintf(label, sizeof(label), Tr("wait %.2f ms"), next->begin - frame->end);
				DrawHatchedBar(draw, w0, w1, row_frames, row_frames + kRow, area_x0, area_x1, label);
				if (inside(w0, w1, row_frames))
				{
					hit = Hit::wait;
					hit_frame = &*frame;
					hit_next = &*next;
				}
			}
		}

		// Passes.
		for (const Placed &item : placed)
		{
			const TrackPass &pass = item.frame->passes[item.pass];
			const float y = row_passes + static_cast<float>(item.lane) * (kRow + kGap);
			const float x0 = x_of(pass.start);
			const float x1 = x_of(pass.end);
			std::snprintf(label, sizeof(label), "%s  %.3f ms", pass.name.c_str(), pass.Length());
			const bool selected = item.frame->index == m_track_selected_frame &&
				static_cast<int>(item.pass) == m_track_selected_pass;
			DrawBar(draw, x0, x1, y, y + kRow, area_x0, area_x1,
				Colour(ColourOfPassKind(pass.kind), pass.measured ? 1.0f : 0.45f), label, selected);
			if (inside(x0, x1, y))
			{
				hit = Hit::pass;
				hit_frame = item.frame;
				hit_item = item.pass;
			}
		}

		// Measured commands.
		if (show_commands)
		{
			for (auto frame = first_visible; frame != frames.end() && frame->begin <= view_end; ++frame)
			{
				for (size_t i = 0; i < frame->commands.size(); ++i)
				{
					const TrackCommand &command = frame->commands[i];
					const float x0 = x_of(command.start);
					const float x1 = x_of(command.start + command.length);
					if (x1 < area_x0 || x0 > area_x1)
						continue;
					std::snprintf(label, sizeof(label), "%u %s", command.event, EventKindName(command.kind));
					DrawBar(draw, x0, x1, row_commands, row_commands + kRow, area_x0, area_x1,
						Colour(ColourOfCommand(command.kind)), label, false);
					if (inside(x0, x1, row_commands))
					{
						hit = Hit::command;
						hit_frame = &*frame;
						hit_item = i;
					}
				}
			}
		}

		// ---- a scrollbar over the whole track ---------------------------------------------------
		// Dragging the view moves it too, but a bar that shows where the window is among the
		// seconds kept, and can be grabbed, is what makes it obvious that there is more to see.
		{
			constexpr float kBar = 12.0f;
			const ImVec2 bar_origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("continuous-scrollbar", ImVec2(width, kBar), ImGuiButtonFlags_MouseButtonLeft);
			const bool bar_active = ImGui::IsItemActive();
			const bool bar_hovered = ImGui::IsItemHovered();

			const double whole = std::max(track_end - track_start, m_track_view_span);
			const float bar_x0 = bar_origin.x + kLabels;
			const float bar_width = std::max(width - kLabels, 1.0f);
			const float thumb_x0 = bar_x0 + static_cast<float>((m_track_view_start - track_start) / whole) * bar_width;
			const float thumb_x1 = thumb_x0 + std::max(static_cast<float>(m_track_view_span / whole) * bar_width, 12.0f);

			if (bar_active)
			{
				if (ImGui::IsItemActivated() && (io.MousePos.x < thumb_x0 || io.MousePos.x > thumb_x1))
				{
					// A click beside the thumb brings that moment to the middle of the view.
					const double at = track_start + (io.MousePos.x - bar_x0) / bar_width * whole;
					m_track_view_start = at - m_track_view_span * 0.5;
				}
				else
				{
					m_track_view_start += static_cast<double>(io.MouseDelta.x) / bar_width * whole;
				}
				m_track_view_start = std::clamp(m_track_view_start, track_start - m_track_view_span * 0.5,
					std::max(track_end - m_track_view_span * 0.1, track_start));
				m_track_follow = false;
			}

			draw->AddRectFilled(ImVec2(bar_x0, bar_origin.y + 2.0f), ImVec2(bar_x0 + bar_width, bar_origin.y + kBar - 2.0f),
				ImGui::GetColorU32(ImGuiCol_ScrollbarBg), 4.0f);
			draw->AddRectFilled(ImVec2(std::max(thumb_x0, bar_x0), bar_origin.y + 2.0f),
				ImVec2(std::min(thumb_x1, bar_x0 + bar_width), bar_origin.y + kBar - 2.0f),
				ImGui::GetColorU32(bar_active ? ImGuiCol_ScrollbarGrabActive
					: (bar_hovered ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab)), 4.0f);
			if (bar_hovered && !bar_active)
				ImGui::SetTooltip("%s", Tr("Drag to move through the frames kept; click beside the bar to jump there."));
		}

		// ---- hover and click -------------------------------------------------------------------
		const bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_track_dragged;

		switch (hit)
		{
		case Hit::frame:
			if (ImGui::BeginTooltip())
			{
				ImGui::Text(Tr("Frame %llu"), static_cast<unsigned long long>(hit_frame->index));
				ImGui::Separator();
				ImGui::Text(Tr("GPU busy %.3f ms, from %.3f s"), hit_frame->Busy(), hit_frame->begin / 1000.0);
				ImGui::Text(Tr("CPU %.2f ms"), static_cast<double>(hit_frame->cpu_ms));
				ImGui::Text(Tr("%u draws, %u dispatches, %u commands"), hit_frame->draw_count,
					hit_frame->dispatch_count, hit_frame->event_count);
				ImGui::EndTooltip();
			}
			if (clicked)
			{
				m_track_selected_frame = hit_frame->index;
				m_track_selected_pass = -1;
			}
			break;

		case Hit::wait:
			if (ImGui::BeginTooltip())
			{
				if (hit_next->index > hit_frame->index + 1)
					ImGui::Text(Tr("%llu frames not measured between frame %llu and frame %llu"),
						static_cast<unsigned long long>(hit_next->index - hit_frame->index - 1),
						static_cast<unsigned long long>(hit_frame->index),
						static_cast<unsigned long long>(hit_next->index));
				else
					ImGui::Text(Tr("%.3f ms between frame %llu and frame %llu"), hit_next->begin - hit_frame->end,
						static_cast<unsigned long long>(hit_frame->index),
						static_cast<unsigned long long>(hit_next->index));
				ImGui::TextDisabled(Tr("No measured GPU work: the GPU was waiting for the next frame, or running "
				                       "commands that come before its first timestamp."));
				ImGui::EndTooltip();
			}
			break;

		case Hit::pass:
		{
			const TrackPass &pass = hit_frame->passes[hit_item];
			if (ImGui::BeginTooltip())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ColourOfPassKind(pass.kind));
				ImGui::TextUnformatted(pass.name.c_str());
				ImGui::PopStyleColor();
				ImGui::TextDisabled(Tr("frame %llu, name: %s, confidence %.0f%%"),
					static_cast<unsigned long long>(hit_frame->index), PassNameOriginName(pass.origin),
					static_cast<double>(pass.confidence) * 100.0);
				ImGui::Separator();
				ImGui::Text(Tr("starts %.3f ms into the frame, lasts %.3f ms (%.1f%% of it)"),
					pass.start - hit_frame->begin, pass.Length(),
					hit_frame->Busy() > 0.0 ? pass.Length() / hit_frame->Busy() * 100.0 : 0.0);
				if (!pass.measured)
					ImGui::TextDisabled(Tr("no measurement of its own: placed between the two around it"));
				ImGui::Text(Tr("%u draws, %u dispatches, events %u-%u"), pass.draw_count, pass.dispatch_count,
					pass.first_event, pass.last_event);
				ImGui::EndTooltip();
			}
			if (clicked)
			{
				m_track_selected_frame = hit_frame->index;
				m_track_selected_pass = static_cast<int>(hit_item);
			}
			break;
		}

		case Hit::command:
		{
			const TrackCommand &command = hit_frame->commands[hit_item];
			if (ImGui::BeginTooltip())
			{
				ImGui::Text(Tr("Event %u — %s"), command.event, EventKindName(command.kind));
				ImGui::TextDisabled(Tr("frame %llu"), static_cast<unsigned long long>(hit_frame->index));
				ImGui::Separator();
				ImGui::Text(Tr("starts %.3f ms into the frame, lasts %.3f ms (%.1f%% of it)"),
					command.start - hit_frame->begin, command.length,
					hit_frame->Busy() > 0.0 ? command.length / hit_frame->Busy() * 100.0 : 0.0);
				ImGui::EndTooltip();
			}
			if (clicked)
			{
				m_track_selected_frame = hit_frame->index;
				m_track_selected_pass = -1;
			}
			break;
		}

		case Hit::none:
			break;
		}
	}

	// The frame picked in the strip or in the view, pass by pass: the continuous view says which
	// frame was slow, this says why.
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
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
				Tr("Its own commands were no longer held when its timings arrived: its passes were cut with the "
				   "commands of a later frame."));

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("track-passes", 5, kFlags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(TrId("Pass"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn(TrId("Cmds"), ImGuiTableColumnFlags_WidthFixed, 46.0f);
		ImGui::TableSetupColumn(TrId("Starts at"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn(TrId("GPU ms"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 62.0f);
		ImGui::TableSetupColumn(TrId("%"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_PreferSortDescending, 44.0f);
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
