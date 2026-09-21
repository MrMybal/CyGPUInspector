// CyGPUInspectorApp — the two captures, and the two panels that show them.
//
//   Frame timeline    the runtime capture. What the game can afford every frame: a strip of the
//                     last few hundred frames so a spike is visible before it is gone, then one
//                     frame on a time axis — the frame, its passes, the commands measured inside
//                     them — the way a profiler's timing view lays it out. This is the panel to
//                     leave open while playing.
//
//   Deep capture      the one shot. Arms the add-on for a few frames, during which it records
//                     every descriptor bound to every command, the fixed function state of every
//                     pipeline, the barriers, and a GPU timestamp per command rather than per
//                     pass. Then it disarms itself and this panel shows what came back.
//
// The panel says what the deep capture cannot do as plainly as what it can. Where a frame
// debugger replays the command stream on its own device and can therefore re-run a draw, this
// observes the real one through the official ReShade add-on API and never injects a device of its
// own — that is a decision in the brief, not a gap to be papered over.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include "TimelineDrawing.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#include <CyGPUInspectorCore/Format.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cygi
{
	// Every user visible string goes through Tr, which returns the translation when there is one
	// and the English source when there is not. TrId does the same for a label ImGui also uses as
	// an identifier, appending the English as a hidden ### suffix so a layout survives a language
	// change. See CyGPUInspectorCore/Localization.hpp.
	using i18n::Tr;
	using i18n::TrId;
	namespace
	{
		void Help(const char *text)
		{
			ImGui::TextDisabled("(?)");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
				ImGui::TextUnformatted(text);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		ImVec4 ColourOfPass(const GraphPass &pass)
		{
			return timeline_drawing::ColourOfPassKind(pass.kind);
		}

		const char *TopologyName(uint32_t topology)
		{
			// reshade::api::primitive_topology, spelled out here so the standalone never has to
			// include the ReShade headers.
			switch (topology)
			{
			case 1: return "point list";
			case 2: return "line list";
			case 3: return "line strip";
			case 4: return "triangle list";
			case 5: return "triangle strip";
			case 6: return "triangle fan";
			case 10: return "line list adj";
			case 11: return "line strip adj";
			case 12: return "triangle list adj";
			case 13: return "triangle strip adj";
			default: return topology == 0 ? "undefined" : "patch list";
			}
		}

		const char *CompareOpName(uint32_t op)
		{
			switch (op)
			{
			case 1: return "never";
			case 2: return "less";
			case 3: return "equal";
			case 4: return "less or equal";
			case 5: return "greater";
			case 6: return "not equal";
			case 7: return "greater or equal";
			case 8: return "always";
			default: return "?";
			}
		}

		const char *CullModeName(uint32_t mode)
		{
			switch (mode)
			{
			case 0: return "none";
			case 1: return "front";
			case 2: return "back";
			default: return "?";
			}
		}

		// The register letter a slot is addressed by, which is how anyone reading HLSL thinks of it.
		char RegisterLetter(SlotKind kind)
		{
			switch (kind)
			{
			case SlotKind::constant_buffer: return 'b';
			case SlotKind::shader_resource: return 't';
			case SlotKind::unordered_access: return 'u';
			case SlotKind::sampler: return 's';
			default: return '#';
			}
		}
	}

	// ---------------------------------------------------------------------------------------
	// Runtime capture: the frame timeline
	// ---------------------------------------------------------------------------------------

	void Application::DrawTimelinePanel()
	{
		if (!ImGui::Begin(TrId("Frame timeline")))
		{
			ImGui::End();
			return;
		}

		const SessionModel *model = ActiveModel();
		if (model == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		ImGui::Checkbox(TrId("Follow"), &m_timeline_follow);
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Fit")))
		{
			m_view_fit = true;
			m_track_follow = true;
			m_track_view_span = 0.0;
		}
		ImGui::SameLine();
		// Two views of the same capture, each with its own layout: one frame taken apart, or the
		// last few seconds of frames one after another the way a profiler's timing view shows them.
		if (ImGui::RadioButton(TrId("One frame"), !m_timeline_continuous))
			m_timeline_continuous = false;
		ImGui::SameLine();
		if (ImGui::RadioButton(TrId("Continuous"), m_timeline_continuous))
			m_timeline_continuous = true;
		ImGui::SameLine();
		Help(Tr("This is the runtime capture: it costs the game a few percent and can be left on. "
		        "The strip at the top is the last few hundred frames. One frame shows the latest "
		        "frame on a time axis: the whole frame, its passes, then the commands measured "
		        "inside them. Continuous shows every measured frame one after another on the GPU "
		        "clock, with the time the GPU waited between two frames; click a frame in the strip "
		        "to go to it. Wheel to zoom, drag to pan, double-click to fit or to follow the "
		        "newest frame again. Lengths need the tracking level to be Pass Timing or higher. "
		        "Names are derived, never read from the game: no graphics API exposes its debug "
		        "markers to a ReShade add-on."));

		FrameTimings timings;
		FrameHistoryEntry latest = {};
		bool has_history = false;
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			// Follow off freezes everything shown here, the timings included: the passes of one
			// frame drawn with the lengths of another would describe neither.
			if (m_timeline_follow || !m_frozen_timings.IsValid())
				m_frozen_timings = model->Timings();
			timings = m_frozen_timings;
			if (!model->History().empty())
			{
				latest = model->History().back();
				has_history = true;
			}
		}

		if (m_timeline_follow)
			RefreshFrameGraph(false);

		// One layout of the frame on a time axis, read by the view, the list and the selected
		// pass alike, so the three never disagree about how long a pass took.
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			m_timeline.Build(timings, m_graph, model->LastFrame().events);
		}

		if (has_history)
		{
			ImGui::SameLine();
			ImGui::Text(Tr("Frame %llu: %u draws, %u dispatches, %u commands"),
				static_cast<unsigned long long>(latest.index), latest.draw_count,
				latest.dispatch_count, latest.event_count);
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("| CPU %.2f ms | tool %.2f ms"),
				static_cast<double>(latest.cpu_frame_ms), static_cast<double>(latest.addon_cpu_ms));
		}

		if (m_timeline_continuous)
		{
			// Its own layout: the strip, the continuous view, and under it the frame that was
			// picked, pass by pass — which is how a spike gets read after the fact.
			uint64_t visible_first = 0;
			uint64_t visible_last = 0;
			if (!m_track.Empty() && m_track_view_span > 0.0)
			{
				if (const TrackFrame *first = m_track.FrameAt(m_track_view_start))
					visible_first = first->index;
				if (const TrackFrame *last = m_track.FrameAt(m_track_view_start + m_track_view_span))
					visible_last = last->index;
			}
			DrawFrameStrip(m_track_selected_frame, visible_first, visible_last, true);
			DrawContinuousView();
			// The frame picked, pass by pass, and what was on screen beside it.
			const float detail_width = ImGui::GetContentRegionAvail().x * 0.58f;
			if (ImGui::BeginChild("track-frame-detail", ImVec2(detail_width, 0.0f)))
				DrawTrackFrameDetail();
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("track-frame-image", ImVec2(0.0f, 0.0f)))
				DrawFrameImage(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
			ImGui::EndChild();
			ImGui::End();
			return;
		}

		DrawFrameStrip(timings.IsValid() ? timings.frame_index : 0, 0, 0, false);

		if (!m_graph.IsValid())
		{
			ImGui::TextDisabled(Tr("No frame analysed yet."));
			ImGui::End();
			return;
		}

		if (!timings.IsValid())
			ImGui::TextDisabled(Tr("No GPU timings: raise the tracking level to Pass Timing to give the "
			                       "passes a length. Until then they are laid out by command count."));

		DrawTimingView();

		// Under the view, side by side and both filling the rest of the panel: the list of passes,
		// because a bar too short to read is still a row you can sort and click, and everything
		// about the one that is selected.
		const float lower_width = ImGui::GetContentRegionAvail().x;
		if (ImGui::BeginChild("pass-list", ImVec2(lower_width * 0.38f, 0.0f)))
			DrawPassList(timings);
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("pass-detail-pane", ImVec2(lower_width * 0.32f, 0.0f)))
			DrawSelectedPass(timings);
		ImGui::EndChild();
		ImGui::SameLine();
		// What the frame looked like: the final image, frozen with the timeline when Follow is off.
		if (ImGui::BeginChild("frame-image-pane", ImVec2(0.0f, 0.0f)))
			DrawFrameImage(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		ImGui::EndChild();

		ImGui::End();
	}

	using timeline_drawing::Colour;
	using timeline_drawing::ColourOfCommand;
	using timeline_drawing::DrawBar;
	using timeline_drawing::NiceStep;

	// The strip of recent frames, one bar per frame and newest on the right, the way a profiler
	// shows them. A spike stays visible for a few seconds instead of being a number that has
	// already changed, and the frame whose GPU timings are in the view below is outlined.
	void Application::DrawFrameStrip(uint64_t measured_frame, uint64_t visible_first, uint64_t visible_last,
	                                 bool pick)
	{
		const SessionModel *model = ActiveModel();
		if (model == nullptr)
			return;

		std::vector<FrameHistoryEntry> history;
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			history = model->History();
		}
		if (history.size() < 2)
			return;

		constexpr float kHeight = 46.0f;
		constexpr float kPitch = 3.0f;
		const float width = ImGui::GetContentRegionAvail().x;
		const size_t shown = std::min(history.size(), static_cast<size_t>(std::max(1.0f, width / kPitch)));
		const size_t first = history.size() - shown;

		// The scale is a high percentile, not the peak: one loading stall of four seconds would
		// otherwise flatten every other frame into the bottom pixel. Anything taller is drawn full
		// height with a red cap, and its real value is in the tooltip.
		std::vector<float> values;
		values.reserve(shown);
		float peak = 0.0f;
		for (size_t i = first; i < history.size(); ++i)
		{
			values.push_back(history[i].cpu_frame_ms);
			peak = std::max(peak, history[i].cpu_frame_ms);
		}
		const size_t percentile = (values.size() * 19) / 20;
		std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(percentile), values.end());
		const float scale = std::max(values[percentile] * 1.3f, 1.0f);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("frame-strip", ImVec2(width, kHeight));
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList *draw = ImGui::GetWindowDrawList();
		const float bottom = origin.y + kHeight;
		draw->AddRectFilled(origin, ImVec2(origin.x + width, bottom), ImGui::GetColorU32(ImGuiCol_FrameBg));

		// The budgets a player feels: 60 and 30 frames per second, drawn when they are on scale.
		for (const float budget : { 1000.0f / 60.0f, 1000.0f / 30.0f })
		{
			if (budget >= scale)
				continue;
			const float y = bottom - budget / scale * kHeight;
			draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), IM_COL32(255, 255, 255, 40));
			char text[16];
			std::snprintf(text, sizeof(text), "%.0f fps", 1000.0f / budget);
			draw->AddText(ImVec2(origin.x + width - ImGui::CalcTextSize(text).x - 4.0f, y - ImGui::GetFontSize()),
				IM_COL32(255, 255, 255, 90), text);
		}

		const float right_edge = origin.x + width;
		int hovered_index = -1;
		float window_x0 = 0.0f;
		float window_x1 = 0.0f;
		for (size_t i = first; i < history.size(); ++i)
		{
			const FrameHistoryEntry &entry = history[i];
			const float x1 = right_edge - static_cast<float>(history.size() - 1 - i) * kPitch;
			const float x0 = x1 - (kPitch - 1.0f);

			const float cpu = entry.cpu_frame_ms;
			const ImU32 fill = cpu <= 1000.0f / 60.0f + 0.5f ? IM_COL32(88, 170, 104, 220)
				: cpu <= 1000.0f / 30.0f + 0.5f ? IM_COL32(214, 164, 64, 230)
				: IM_COL32(214, 78, 64, 240);
			const float height = std::min(cpu / scale, 1.0f) * kHeight;
			draw->AddRectFilled(ImVec2(x0, bottom - height), ImVec2(x1, bottom), fill);
			if (cpu > scale)
				draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + 3.0f), IM_COL32(255, 60, 40, 255));

			// The GPU cost of the same frame, as a tick at its height: whether the frame was bound
			// by the CPU or by the GPU reads straight off the gap between the two.
			if (entry.gpu_ms > 0.0)
			{
				const float y = bottom - std::min(static_cast<float>(entry.gpu_ms) / scale, 1.0f) * kHeight;
				draw->AddRectFilled(ImVec2(x0, y - 1.0f), ImVec2(x1, y + 1.0f), IM_COL32(120, 190, 255, 255));
			}

			if (visible_first != 0 && entry.index >= visible_first && entry.index <= visible_last)
			{
				window_x0 = window_x0 == 0.0f ? x0 : std::min(window_x0, x0);
				window_x1 = std::max(window_x1, x1);
			}

			if (entry.index == measured_frame && measured_frame != 0)
				draw->AddRect(ImVec2(x0 - 1.0f, origin.y), ImVec2(x1 + 1.0f, bottom), IM_COL32(255, 255, 255, 220));

			if (hovered && ImGui::GetIO().MousePos.x >= x0 - 0.5f && ImGui::GetIO().MousePos.x < x1 + 1.0f)
				hovered_index = static_cast<int>(i);
		}

		// What the strip says in numbers, in its top left corner.
		char caption[128];
		std::snprintf(caption, sizeof(caption), "CPU %.2f ms, peak %.2f ms", static_cast<double>(history.back().cpu_frame_ms),
			static_cast<double>(peak));
		const ImVec2 caption_size = ImGui::CalcTextSize(caption);
		draw->AddRectFilled(ImVec2(origin.x + 2.0f, origin.y + 1.0f),
			ImVec2(origin.x + 6.0f + caption_size.x, origin.y + 3.0f + caption_size.y), IM_COL32(20, 22, 28, 190));
		draw->AddText(ImVec2(origin.x + 4.0f, origin.y + 2.0f), IM_COL32(255, 255, 255, 210), caption);

		// Which frames the continuous view below is showing, the way a profiler marks its
		// selection on the frame graph.
		if (window_x1 > window_x0)
		{
			draw->AddRectFilled(ImVec2(window_x0 - 1.0f, origin.y), ImVec2(window_x1 + 1.0f, bottom),
				IM_COL32(255, 255, 255, 30));
			draw->AddRect(ImVec2(window_x0 - 1.0f, origin.y), ImVec2(window_x1 + 1.0f, bottom),
				IM_COL32(255, 255, 255, 120));
		}

		// Press and drag along the strip to scrub: the frame under the mouse is picked and brought
		// into the view below as it moves, the way a profiler's frame graph is scrubbed.
		if (pick && ImGui::IsItemActive())
		{
			const float offset = (right_edge - ImGui::GetIO().MousePos.x) / kPitch;
			const float clamped = std::clamp(offset, 0.0f, static_cast<float>(shown - 1));
			const size_t picked_row = history.size() - 1 - static_cast<size_t>(clamped);
			const uint64_t picked = history[picked_row].index;
			if (picked != m_track_selected_frame)
			{
				m_track_selected_frame = picked;
				m_track_selected_pass = -1;
				m_track_center_request = picked;
			}
			m_track_follow = false;
		}

		if (hovered_index >= 0)
		{
			const FrameHistoryEntry &entry = history[static_cast<size_t>(hovered_index)];
			if (ImGui::BeginTooltip())
			{
				ImGui::Text(Tr("Frame %llu"), static_cast<unsigned long long>(entry.index));
				ImGui::Separator();
				ImGui::Text(Tr("CPU %.2f ms"), static_cast<double>(entry.cpu_frame_ms));
				if (entry.gpu_ms > 0.0)
					ImGui::Text(Tr("GPU %.2f ms, measured passes only"), entry.gpu_ms);
				else
					ImGui::TextDisabled(Tr("GPU not measured for this frame"));
				ImGui::Text(Tr("%u draws, %u dispatches, %u commands"), entry.draw_count,
					entry.dispatch_count, entry.event_count);
				if (pick)
					ImGui::TextDisabled(Tr("Click to show this frame in the continuous view."));
				ImGui::EndTooltip();
			}
		}
	}

	// One frame on a time axis: a ruler, then the whole frame, then its passes, then the commands
	// that were measured inside them — the layout of a profiler's timing view, drawn from what
	// the runtime capture measures.
	//
	// Where a pass sits comes from the measured start of the timestamps inside it. Between two
	// measurements nothing is known, so a pass that has none of its own is placed by interpolating
	// on its commands, and drawn dimmed with a tooltip saying so. When no timings exist at all, the
	// same view is laid out by command count instead, and the ruler says "commands", not "ms".
	void Application::DrawTimingView()
	{
		const std::vector<GraphPass> &passes = m_graph.Passes();
		if (passes.empty())
			return;

		const FrameTimeline &timeline = m_timeline;
		const std::vector<FrameTimeline::Point> &points = timeline.Points();
		const std::vector<FrameTimeline::Span> &spans = timeline.Spans();
		const bool timed = timeline.Timed();
		const double frame_length = timeline.Length();

		// The commands row only earns its place when it says more than the passes row: at Pass
		// Timing there is one measurement per pass and the two rows would be the same bars twice.
		size_t measured_passes = 0;
		for (const FrameTimeline::Span &span : spans)
			measured_passes += span.measured ? 1 : 0;
		const bool show_commands = timed && points.size() > measured_passes;

		if (timed && !timeline.MeasuredPositions())
			ImGui::TextDisabled(Tr("Positions laid end to end: this add-on predates measured start times."));

		// ---- geometry --------------------------------------------------------------------------
		constexpr float kLabels = 74.0f;
		constexpr float kRuler = 18.0f;
		constexpr float kRow = 22.0f;
		constexpr float kGap = 3.0f;
		const int rows = show_commands ? 3 : 2;
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = kRuler + static_cast<float>(rows) * (kRow + kGap) + 2.0f;

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("timing-view", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		// The wheel belongs to the view while the mouse is over it, not to the panel's scrollbar.
		const bool owns_wheel = ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

		const float area_x0 = origin.x + kLabels;
		const float area_x1 = origin.x + width;
		const float area_width = std::max(area_x1 - area_x0, 1.0f);

		if (m_view_fit || m_view_span <= 0.0)
		{
			m_view_start = 0.0;
			m_view_span = frame_length * 1.02;
		}

		ImGuiIO &io = ImGui::GetIO();
		if (hovered && owns_wheel && io.MouseWheel != 0.0f && io.MousePos.x >= area_x0)
		{
			// Zoom around the mouse, so what is under the cursor stays under it.
			const double anchor = (io.MousePos.x - area_x0) / area_width;
			const double at = m_view_start + anchor * m_view_span;
			const double minimum = timed ? 0.0005 : 4.0;
			m_view_span = std::clamp(m_view_span * std::pow(0.8, static_cast<double>(io.MouseWheel)),
				minimum, std::max(frame_length * 1.5, minimum));
			m_view_start = at - anchor * m_view_span;
			m_view_fit = false;
		}
		if (ImGui::IsItemActivated())
			m_view_dragged = false;
		if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) ||
		                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f) ||
		                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)))
		{
			m_view_start -= static_cast<double>(io.MouseDelta.x) / area_width * m_view_span;
			m_view_dragged = true;
			m_view_fit = false;
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			m_view_fit = true;
		m_view_start = std::clamp(m_view_start, -m_view_span * 0.1,
			std::max(frame_length - m_view_span * 0.1, 0.0));

		const auto x_of = [&](double time) {
			return area_x0 + static_cast<float>((time - m_view_start) / m_view_span) * area_width;
		};

		ImDrawList *draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, ImVec2(area_x1, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg));

		// ---- ruler -----------------------------------------------------------------------------
		const double step = NiceStep(m_view_span * 90.0 / area_width);
		const double first_tick = std::floor(m_view_start / step) * step;
		draw->PushClipRect(ImVec2(area_x0, origin.y), ImVec2(area_x1, origin.y + height), true);
		for (double tick = first_tick; tick <= m_view_start + m_view_span; tick += step)
		{
			const float x = x_of(tick);
			draw->AddLine(ImVec2(x, origin.y + kRuler - 5.0f), ImVec2(x, origin.y + height),
				IM_COL32(255, 255, 255, 22));
			char text[32];
			if (!timed)
				std::snprintf(text, sizeof(text), "%.0f", tick);
			else if (step >= 1.0)
				std::snprintf(text, sizeof(text), "%.0f ms", tick);
			else if (step >= 0.1)
				std::snprintf(text, sizeof(text), "%.1f ms", tick);
			else if (step >= 0.01)
				std::snprintf(text, sizeof(text), "%.2f ms", tick);
			else
				std::snprintf(text, sizeof(text), "%.3f ms", tick);
			// A tick just left of the area would have its label cut to "ms": the line is enough.
			if (x + 3.0f >= area_x0)
				draw->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), IM_COL32(255, 255, 255, 140), text);
		}
		draw->PopClipRect();

		// ---- rows ------------------------------------------------------------------------------
		const float row_frame = origin.y + kRuler + 1.0f;
		const float row_passes = row_frame + kRow + kGap;
		const float row_commands = row_passes + kRow + kGap;

		const ImU32 label_colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		draw->AddText(ImVec2(origin.x + 4.0f, origin.y + 1.0f), label_colour, timed ? Tr("GPU") : Tr("commands"));
		draw->AddText(ImVec2(origin.x + 4.0f, row_frame + 3.0f), label_colour, Tr("Frame"));
		draw->AddText(ImVec2(origin.x + 4.0f, row_passes + 3.0f), label_colour, Tr("Passes"));
		if (show_commands)
			draw->AddText(ImVec2(origin.x + 4.0f, row_commands + 3.0f), label_colour, Tr("Commands"));

		char label[192];

		// The frame itself.
		if (timed)
			std::snprintf(label, sizeof(label), Tr("Frame %llu, %.3f ms of measured GPU time"),
				static_cast<unsigned long long>(timeline.FrameIndex()), frame_length);
		else
			std::snprintf(label, sizeof(label), Tr("Frame, %.0f commands"), frame_length);
		DrawBar(draw, x_of(0.0), x_of(frame_length), row_frame, row_frame + kRow, area_x0, area_x1,
			IM_COL32(70, 78, 96, 255), label, false);

		// The passes.
		int hovered_pass = -1;
		for (size_t i = 0; i < passes.size() && i < spans.size(); ++i)
		{
			const GraphPass &pass = passes[i];
			const float x0 = x_of(spans[i].start);
			const float x1 = x_of(spans[i].end);
			if (timed)
				std::snprintf(label, sizeof(label), "%s  %.3f ms", pass.name.c_str(), spans[i].Length());
			else
				std::snprintf(label, sizeof(label), "%s", pass.name.c_str());
			DrawBar(draw, x0, x1, row_passes, row_passes + kRow, area_x0, area_x1,
				Colour(ColourOfPass(pass), spans[i].measured || !timed ? 1.0f : 0.45f), label,
				pass.id == m_selected_pass);

			if (hovered && io.MousePos.y >= row_passes && io.MousePos.y < row_passes + kRow &&
			    io.MousePos.x >= std::max(x0, area_x0) && io.MousePos.x < std::max(x1, x0 + 1.0f))
				hovered_pass = static_cast<int>(i);
		}

		// The measured commands.
		int hovered_point = -1;
		if (show_commands)
		{
			for (size_t i = 0; i < points.size(); ++i)
			{
				const float x0 = x_of(points[i].start);
				const float x1 = x_of(points[i].start + points[i].length);
				// Culled before formatting: at Full Draw Timing there can be sixteen thousand.
				if (x1 < area_x0 || x0 > area_x1)
					continue;
				std::snprintf(label, sizeof(label), "%u %s", points[i].event, EventKindName(points[i].kind));
				DrawBar(draw, x0, x1, row_commands, row_commands + kRow, area_x0, area_x1,
					Colour(ColourOfCommand(points[i].kind)), label, points[i].event == m_selected_event);

				if (hovered && io.MousePos.y >= row_commands && io.MousePos.y < row_commands + kRow &&
				    io.MousePos.x >= std::max(x0, area_x0) && io.MousePos.x < std::max(x1, x0 + 1.0f))
					hovered_point = static_cast<int>(i);
			}
		}

		// ---- hover and click -------------------------------------------------------------------
		const bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_view_dragged;

		if (hovered_pass >= 0)
		{
			const GraphPass &pass = passes[static_cast<size_t>(hovered_pass)];
			const FrameTimeline::Span &span = spans[static_cast<size_t>(hovered_pass)];
			if (ImGui::BeginTooltip())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ColourOfPass(pass));
				ImGui::TextUnformatted(pass.name.c_str());
				ImGui::PopStyleColor();
				if (pass.origin != PassNameOrigin::user)
					ImGui::TextDisabled(Tr("name: %s, confidence %.0f%%"), PassNameOriginName(pass.origin),
						static_cast<double>(pass.confidence) * 100.0);
				ImGui::Separator();
				if (timed)
				{
					ImGui::Text(Tr("starts at %.3f ms, lasts %.3f ms (%.1f%% of the frame)"), span.start,
						span.Length(), span.Length() / frame_length * 100.0);
					if (!span.measured)
						ImGui::TextDisabled(Tr("no measurement of its own: placed between the two around it"));
				}
				ImGui::Text(Tr("%u draws, %u dispatches, events %u-%u"), pass.draw_count, pass.dispatch_count,
					pass.first_event, pass.last_event);
				ImGui::EndTooltip();
			}
			if (clicked)
			{
				m_selected_pass = pass.id;
				SelectEvent(pass.first_event);
			}
		}
		else if (hovered_point >= 0)
		{
			const FrameTimeline::Point &point = points[static_cast<size_t>(hovered_point)];
			if (ImGui::BeginTooltip())
			{
				ImGui::Text(Tr("Event %u — %s"), point.event, EventKindName(point.kind));
				ImGui::Separator();
				ImGui::Text(Tr("starts at %.3f ms, lasts %.3f ms (%.1f%% of the frame)"),
					point.start, point.length, point.length / frame_length * 100.0);
				ImGui::TextDisabled(Tr("until the next measured command, which is what a before-only "
				                       "interceptor can measure"));
				ImGui::EndTooltip();
			}
			if (clicked)
			{
				SelectEvent(point.event);
				if (const GraphPass *owner = m_graph.PassOfEvent(point.event))
					m_selected_pass = owner->id;
			}
		}
	}

	// The passes as a sortable list. The bars answer "where did the frame go"; this answers "which
	// pass is the expensive one", which is a different question and needs a sort, not a picture.
	void Application::DrawPassList(const FrameTimings &timings)
	{
		const std::vector<GraphPass> &passes = m_graph.Passes();
		if (passes.empty())
			return;

		const std::vector<FrameTimeline::Span> &spans = m_timeline.Spans();
		const bool timed = m_timeline.Timed() && spans.size() == passes.size();
		const auto length_of = [&](size_t i) { return timed ? spans[i].Length() : 0.0; };

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

		if (!ImGui::BeginTable("passes", 4, kFlags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(TrId("Pass"), ImGuiTableColumnFlags_WidthStretch |
			ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn(TrId("Cmds"), ImGuiTableColumnFlags_WidthFixed, 46.0f);
		ImGui::TableSetupColumn(TrId("GPU ms"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 62.0f);
		ImGui::TableSetupColumn(TrId("%"), ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_PreferSortDescending, 44.0f);
		ImGui::TableHeadersRow();

		// Sorted as an index list, so a row still points at the right pass whatever the order.
		std::vector<size_t> order(passes.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = i;

		const ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs();
		if (specs != nullptr && specs->SpecsCount > 0)
		{
			const ImGuiTableColumnSortSpecs &spec = specs->Specs[0];
			const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
				double left = 0.0;
				double right = 0.0;
				if (spec.ColumnIndex == 1)
				{
					left = static_cast<double>(passes[a].event_count);
					right = static_cast<double>(passes[b].event_count);
				}
				else
				{
					// The percentage is that same time, so both columns sort by it.
					left = length_of(a);
					right = length_of(b);
				}
				return ascending ? left < right : right < left;
			});
		}

		for (const size_t i : order)
		{
			const GraphPass &pass = passes[i];
			ImGui::PushID(static_cast<int>(pass.id));
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			// The bar's colour, so a row and a bar are recognisably the same pass.
			ImGui::PushStyleColor(ImGuiCol_Text, ColourOfPass(pass));
			if (ImGui::Selectable(pass.name.c_str(), pass.id == m_selected_pass,
				ImGuiSelectableFlags_SpanAllColumns))
			{
				m_selected_pass = pass.id;
				SelectEvent(pass.first_event);
			}
			ImGui::PopStyleColor();
			if (ImGui::BeginItemTooltip())
			{
				if (pass.origin != PassNameOrigin::user)
					ImGui::TextDisabled(Tr("name: %s, confidence %.0f%%"), PassNameOriginName(pass.origin),
						static_cast<double>(pass.confidence) * 100.0);
				else
					ImGui::TextDisabled(Tr("name: given by you"));
				ImGui::Text(Tr("events %u to %u"), pass.first_event, pass.last_event);
				ImGui::Text(Tr("%u draws, %u dispatches"), pass.draw_count, pass.dispatch_count);
				ImGui::EndTooltip();
			}

			ImGui::TableNextColumn();
			ImGui::Text("%u", pass.event_count);

			// A pass with no measurement of its own still has a length on the timeline, placed
			// between the two around it: shown, but dimmed, because it is an estimate.
			ImGui::TableNextColumn();
			if (timed && spans[i].measured)
				ImGui::Text("%.3f", length_of(i));
			else if (timed && length_of(i) > 0.0)
				ImGui::TextDisabled("~%.3f", length_of(i));
			else
				ImGui::TextDisabled("-");

			ImGui::TableNextColumn();
			if (timed && length_of(i) > 0.0)
			{
				const double share = 100.0 * m_timeline.ShareOf(pass.id);
				if (spans[i].measured)
					ImGui::Text("%.1f", share);
				else
					ImGui::TextDisabled("~%.1f", share);
			}
			else
				ImGui::TextDisabled("-");

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Everything about the pass whose bar is selected, and the way into the rest of the tool:
	// clicking a shader or a resource here selects it in every other panel.
	void Application::DrawSelectedPass(const FrameTimings &timings)
	{
		const GraphPass *pass = m_graph.PassById(m_selected_pass);
		if (pass == nullptr)
		{
			ImGui::TextDisabled(Tr("Click a pass to see what it did, what it wrote and which shaders "
			                       "it ran."));
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, ColourOfPass(*pass));
		ImGui::TextUnformatted(pass->name.c_str());
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (pass->origin == PassNameOrigin::user)
			ImGui::TextDisabled(Tr("name: given by you"));
		else
			ImGui::TextDisabled(Tr("name: %s, confidence %.0f%%"), PassNameOriginName(pass->origin),
				static_cast<double>(pass->confidence) * 100.0);

		const FrameTimeline::Span *span = m_timeline.Timed() ? m_timeline.SpanOfPass(pass->id) : nullptr;
		if (span != nullptr && span->Length() > 0.0)
		{
			ImGui::Text(Tr("%u draws, %u dispatches, events %u-%u, GPU %.3f ms"), pass->draw_count,
				pass->dispatch_count, pass->first_event, pass->last_event, span->Length());
			if (!span->measured)
			{
				ImGui::SameLine();
				ImGui::TextDisabled(Tr("(estimated)"));
			}
		}
		else
			ImGui::Text(Tr("%u draws, %u dispatches, events %u-%u"), pass->draw_count,
				pass->dispatch_count, pass->first_event, pass->last_event);

		if (ImGui::SmallButton(TrId("Show its commands")))
		{
			SelectEvent(pass->first_event);
			ImGui::SetWindowFocus("Frame events");
		}

		const SessionModel *model = ActiveModel();
		if (model == nullptr)
			return;

		// What it wrote, what it read, what it ran, one under the other and each across the whole
		// width. Three columns side by side looked tidier and was useless: a resource description
		// is "#8 Texture2D 2560x1440 R16G16B16A16_FLOAT", which does not fit in a third of a pane.
		//
		// Everything here is clickable, because "which shader drew this" is the question this
		// block exists to answer, and a click selects it in every other panel.
		std::lock_guard<std::mutex> lock(model->Mutex());

		ImGui::Spacing();
		ImGui::TextDisabled(Tr("writes"));
		if (pass->render_targets.empty() && pass->depth_target == 0)
		{
			ImGui::TextDisabled("    -");
		}
		else
		{
			ImGui::Indent();
			for (const uint32_t id : pass->render_targets)
			{
				if (id == 0)
					continue;
				const ResourceInfo *resource = model->ResourceById(id);
				char label[224];
				std::snprintf(label, sizeof(label), "%s##w%u",
					resource != nullptr ? resource->Describe().c_str() : "#?", id);
				if (ImGui::Selectable(label, m_selected_resource == id))
					m_selected_resource = id;
			}
			if (pass->depth_target != 0)
			{
				const ResourceInfo *resource = model->ResourceById(pass->depth_target);
				char label[224];
				std::snprintf(label, sizeof(label), "%s  (depth)##d",
					resource != nullptr ? resource->Describe().c_str() : "#?");
				if (ImGui::Selectable(label, m_selected_resource == pass->depth_target))
					m_selected_resource = pass->depth_target;
			}
			ImGui::Unindent();
		}

		ImGui::Spacing();
		ImGui::TextDisabled(Tr("reads"));
		if (pass->reads.empty())
		{
			ImGui::TextDisabled("    -");
		}
		else
		{
			ImGui::Indent();
			for (const uint32_t id : pass->reads)
			{
				const ResourceInfo *resource = model->ResourceById(id);
				char label[224];
				std::snprintf(label, sizeof(label), "%s##r%u",
					resource != nullptr ? resource->Describe().c_str() : "#?", id);
				if (ImGui::Selectable(label, m_selected_resource == id))
					m_selected_resource = id;
			}
			ImGui::Unindent();
		}

		ImGui::Spacing();
		ImGui::TextDisabled(Tr("shaders"));
		if (pass->shaders.empty())
		{
			ImGui::TextDisabled("    -");
		}
		else
		{
			ImGui::Indent();
			for (const uint32_t id : pass->shaders)
			{
				const ShaderInfo *shader = model->ShaderById(id);
				char label[224];
				if (shader != nullptr)
					std::snprintf(label, sizeof(label), Tr("#%u %s, %s, %u draws##s%u"), id,
						ShaderStageName(shader->stage), ShaderFormatName(shader->format),
						shader->draws_this_frame, id);
				else
					std::snprintf(label, sizeof(label), "#%u##s%u", id, id);
				if (ImGui::Selectable(label, m_selected_shader == id))
					m_selected_shader = id;
				ImGui::SetItemTooltip(Tr("Select it, then read it in Shader code."));
			}
			ImGui::Unindent();
		}
	}

	// ---------------------------------------------------------------------------------------
	// Deep capture
	// ---------------------------------------------------------------------------------------

	void Application::StartDeepCapture()
	{
		m_deep_message.clear();
		if (!IsLive())
		{
			m_deep_message = "A deep capture has to be armed in a running game: an opened capture "
			                 "is a recording, and nothing can be asked of it.";
			return;
		}

		// The picture of the captured frame is kept through the preview: start the final image if
		// nothing is being previewed, so every capture comes back with what was on screen.
		if (m_preview_resource == 0)
			StartPreview(kPreviewFinalImage);

		std::string message;
		const bool started = m_client->StartDeepCapture(static_cast<uint32_t>(m_deep_frames),
			m_deep_bindings, m_deep_barriers, m_deep_per_draw_timing, m_deep_buffers, message);
		if (started)
		{
			ReleaseBufferView();
			// Seen as armed from here, so a capture that finishes before the next interface frame
			// is still recognised as one that just finished.
			m_last_capture_stage = CaptureStage::armed;
			m_preview_capture_hold = false;
			m_capture_image_path.clear();
		}
		if (started)
			m_deep_message = message.empty()
				? "Armed. The add-on is recording; it will disarm itself when it is done."
				: message;
		else
			m_deep_message = message.empty() ? "The add-on refused the capture." : message;
	}

	void Application::DrawDeepCapturePanel()
	{
		if (!ImGui::Begin(TrId("Deep capture")))
		{
			ImGui::End();
			return;
		}

		const SessionModel *model = ActiveModel();
		if (model == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		ImGui::TextWrapped(Tr("The deep capture records everything the ReShade add-on API can report "
		                      "about a frame, for a handful of frames, and then stops. It is expensive "
		                      "on purpose. The frame timeline is the one to leave running."));
		ImGui::SameLine();
		Help(Tr("What it records: every descriptor bound to every draw and dispatch, the viewport "
		        "and scissor, the vertex and index buffers, the render target set, the fixed "
		        "function state of each pipeline, the resource barriers, and a GPU timestamp per "
		        "command instead of per pass.\n\n"
		        "What it cannot record: the contents of a Direct3D 12 descriptor table, which is "
		        "bound by handle and never handed to an add-on; and anything requiring replay, such "
		        "as pixel history or re-running a draw, because CyGPUInspector observes the real "
		        "frame rather than re-executing it on a device of its own."));

		ImGui::Separator();

		ImGui::BeginDisabled(!IsLive());
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderInt(TrId("Frames"), &m_deep_frames, 1, 8);
		ImGui::Checkbox(TrId("Descriptors"), &m_deep_bindings);
		ImGui::SameLine();
		ImGui::Checkbox(TrId("Barriers"), &m_deep_barriers);
		ImGui::SameLine();
		ImGui::Checkbox(TrId("Timestamp per command"), &m_deep_per_draw_timing);
		ImGui::SameLine();
		ImGui::Checkbox(TrId("Save the buffers"), &m_deep_buffers);
		ImGui::SameLine();
		Help(Tr("At the end of the last captured frame, the add-on copies every texture that frame "
		        "wrote to (render targets, depth buffers, UAV textures, copy destinations) on the "
		        "game's GPU, and this application saves each one as a PNG to look at and a DDS with "
		        "the data itself, in the capture's folder under Images, with capture.json listing "
		        "them.\n\n"
		        "Each buffer is as it was at the END of the frame: a texture several passes write to "
		        "shows the last of them.\n\n"
		        "The copies take GPU memory in the game (at most 64 buffers and 1.5 GiB) until they "
		        "are saved, a few seconds. On Direct3D 12 a texture whose state no barrier revealed is "
		        "not copied, rather than guessed."));

		const bool running = [&] {
			std::lock_guard<std::mutex> lock(model->Mutex());
			return model->DeepCaptureRunning();
		}();

		ImGui::BeginDisabled(running);
		if (ImGui::Button(TrId("Capture now"), ImVec2(140.0f, 0.0f)))
			StartDeepCapture();
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		if (!IsLive())
			ImGui::TextDisabled(Tr("Read only: this is an opened capture, not a running game."));

		if (!m_deep_message.empty())
			ImGui::TextWrapped("%s", m_deep_message.c_str());

		CaptureStateRecord capture = {};
		CaptureScopeRecord scope = {};
		FrameInfo frame;
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			capture = model->CaptureState();
			scope = model->CaptureScope();
			// The captured frame, not the live one: a deep capture is a one shot and the frame it
			// recorded would otherwise be replaced before anyone could look at it.
			frame = model->HasDeepFrame() ? model->DeepFrame() : model->LastFrame();
		}

		ImGui::Separator();
		ImGui::Text(Tr("Capture: %s"), CaptureStageName(capture.stage));
		if (capture.stage != CaptureStage::idle)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("| frame %llu | %u of %u frames | %u commands, %u bindings, %u barriers"),
				static_cast<unsigned long long>(capture.first_frame), capture.frames_done,
				capture.frames_requested, capture.draw_states_recorded, capture.bindings_recorded,
				capture.barriers_recorded);
		}
		if (capture.dropped != 0)
			ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
				Tr("%u bindings were dropped: a command bound more than the per command ceiling."),
				capture.dropped);

		// Where this capture went on disk, and how far the saving has got.
		if (!m_capture_folder.empty() && (m_preview_capture_hold || m_buffer_writer.Active()))
		{
			if (ImGui::SmallButton(TrId("Open folder")))
				OpenCaptureFolder();
			ImGui::SameLine();
			ImGui::TextDisabled("%s", CaptureFolderText().c_str());
			if (m_buffer_writer.Active() && m_buffer_writer.FirstFrame() == m_capture_folder_first)
			{
				uint32_t done = 0;
				uint32_t total = 0;
				m_buffer_writer.Progress(done, total);
				ImGui::TextDisabled(Tr("Buffers: %u of %u handled"), done, total);
			}
		}
		else if (!m_capture_image_path.empty())
		{
			ImGui::TextDisabled("%s", m_capture_image_path.c_str());
		}

		// Two tabs, each with the whole width, which neither has to spare: what the capture left on
		// disk — the image of the frame and its buffers — and the commands it recorded.
		if (!ImGui::BeginTabBar("deep-tabs"))
		{
			ImGui::End();
			return;
		}

		if (m_preview_capture_hold || m_buffer_writer.Active())
		{
			// A capture that has just finished opens on its pictures, once.
			const bool select = m_capture_folder_first != 0 && m_images_tab_capture != m_capture_folder_first;
			if (select)
				m_images_tab_capture = m_capture_folder_first;
			if (ImGui::BeginTabItem(TrId("Images"), nullptr, select ? ImGuiTabItemFlags_SetSelected : 0))
			{
				const ImVec2 avail = ImGui::GetContentRegionAvail();
				DrawCaptureFiles(avail.x, avail.y);
				ImGui::EndTabItem();
			}
		}

		if (ImGui::BeginTabItem(TrId("Commands")))
		{
			if (!frame.HasDeepCapture())
			{
				ImGui::TextDisabled(Tr("No deep capture data for the frame on screen."));
			}
			else
			{
				ImGui::Text(Tr("Frame %llu carries state for %zu commands and %zu barrier sets."),
					static_cast<unsigned long long>(frame.index), frame.draw_states.size(),
					frame.barriers.size());
				if (scope.frame_index != 0 && scope.frame_index == frame.index)
					ImGui::TextDisabled(Tr("Only the 3D render of the main viewport was captured: commands #%u to #%u. The rest of the frame (the editor's interface) is in the timeline, not in the capture."),
						scope.first_event, scope.last_event);
				ImGui::Separator();

				// The list of recorded commands, and the state of whichever one is selected.
				if (ImGui::BeginChild("deep-events", ImVec2(260.0f, 0.0f), ImGuiChildFlags_Borders))
				{
					ImGuiListClipper clipper;
					clipper.Begin(static_cast<int>(frame.draw_states.size()));
					while (clipper.Step())
					{
						for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
						{
							const CommandState &state = frame.draw_states[static_cast<size_t>(i)];
							char label[128];
							std::snprintf(label, sizeof(label), "#%u  %u bindings##ds%d",
								state.record.event_index, state.record.binding_count, i);
							if (ImGui::Selectable(label, m_selected_event == state.record.event_index))
								m_selected_event = state.record.event_index;
						}
					}
				}
				ImGui::EndChild();

				ImGui::SameLine();

				if (ImGui::BeginChild("deep-state", ImVec2(0.0f, 0.0f)))
				{
					const CommandState *state = frame.DrawStateOfEvent(m_selected_event);
					if (state == nullptr)
					{
						ImGui::TextDisabled(Tr("Select a command on the left."));
					}
					else
					{
						ImGui::Text(Tr("Command #%u, pipeline %u"), state->record.event_index,
							state->record.pipeline_id);
						ImGui::TextDisabled("%s", TopologyName(state->record.topology));

						if ((state->record.flags & kDrawStateViewportValid) != 0)
							ImGui::Text(Tr("Viewport  %.0f, %.0f  %.0f x %.0f   depth %.2f to %.2f"),
								static_cast<double>(state->record.viewport[0]),
								static_cast<double>(state->record.viewport[1]),
								static_cast<double>(state->record.viewport[2]),
								static_cast<double>(state->record.viewport[3]),
								static_cast<double>(state->record.depth_range[0]),
								static_cast<double>(state->record.depth_range[1]));
						if ((state->record.flags & kDrawStateScissorValid) != 0)
							ImGui::Text(Tr("Scissor   %d, %d to %d, %d"), state->record.scissor[0],
								state->record.scissor[1], state->record.scissor[2], state->record.scissor[3]);

						if (state->TablesUnresolved())
							ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
								Tr("This command bound a descriptor table. Its contents are not readable "
								   "through the add-on API, so the list below is incomplete."));

						ImGui::Separator();
						DrawBindingTable(*state);
						ImGui::Separator();
						DrawPipelineStateTable(state->record.pipeline_id);

						if (const BarrierSet *barriers = frame.BarriersOfEvent(m_selected_event))
						{
							ImGui::Separator();
							ImGui::Text(Tr("Barriers (%zu)"), barriers->entries.size());
							for (const BarrierEntry &entry : barriers->entries)
								ImGui::TextDisabled(Tr("resource %u: 0x%X to 0x%X"), entry.resource_id,
									entry.old_state, entry.new_state);
						}
					}
				}
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();

		ImGui::End();
	}

	void Application::DrawBindingTable(const CommandState &state)
	{
		ImGui::Text(Tr("Bindings (%u)"), state.record.binding_count);
		if (state.bindings.empty())
		{
			ImGui::TextDisabled(Tr("Nothing was recorded for this command."));
			return;
		}

		if (!ImGui::BeginTable("bindings", 5,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
			return;

		ImGui::TableSetupColumn(TrId("Slot"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn(TrId("Kind"));
		ImGui::TableSetupColumn(TrId("Stage"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn(TrId("Resource"));
		ImGui::TableSetupColumn(TrId("Range"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
		ImGui::TableHeadersRow();

		const SessionModel *model = ActiveModel();
		for (const DrawBinding &binding : state.bindings)
		{
			const auto kind = static_cast<SlotKind>(binding.kind);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			if (kind == SlotKind::render_target)
				ImGui::Text(Tr("RT%u"), binding.slot);
			else if (kind == SlotKind::depth_stencil)
				ImGui::TextUnformatted(Tr("DS"));
			else if (kind == SlotKind::vertex_buffer)
				ImGui::Text(Tr("VB%u"), binding.slot);
			else if (kind == SlotKind::index_buffer)
				ImGui::TextUnformatted(Tr("IB"));
			else
				ImGui::Text(Tr("%c%u"), RegisterLetter(kind), binding.slot);

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(SlotKindName(kind));

			ImGui::TableNextColumn();
			const auto stage = static_cast<ShaderStage>(binding.stage);
			if (stage == ShaderStage::unknown)
				ImGui::TextDisabled("-");
			else
				ImGui::TextUnformatted(ShaderStageName(stage));

			ImGui::TableNextColumn();
			if (binding.resource_id == 0)
			{
				// A sampler has no resource, and an unknown id means the resource was created
				// before we were watching. Two different things, said differently.
				ImGui::TextDisabled(kind == SlotKind::sampler ? "(sampler)" : "(not tracked)");
			}
			else
			{
				std::string description;
				if (model != nullptr)
				{
					std::lock_guard<std::mutex> lock(model->Mutex());
					if (const ResourceInfo *resource = model->ResourceById(binding.resource_id))
						description = resource->Describe();
				}
				char label[192];
				std::snprintf(label, sizeof(label), "#%u %s##bind%u_%u", binding.resource_id,
					description.c_str(), binding.kind, binding.slot);
				if (ImGui::Selectable(label, m_selected_resource == binding.resource_id))
					m_selected_resource = binding.resource_id;
			}

			ImGui::TableNextColumn();
			if (binding.size != 0)
				ImGui::TextDisabled(Tr("+%u, %u bytes"), binding.offset, binding.size);
			else if (binding.offset != 0)
				ImGui::TextDisabled(Tr("+%u"), binding.offset);
			else
				ImGui::TextDisabled("-");
		}

		ImGui::EndTable();
	}

	void Application::DrawPipelineStateTable(uint32_t pipeline_id)
	{
		const SessionModel *model = ActiveModel();
		if (model == nullptr || pipeline_id == 0)
			return;

		PipelineStateRecord state = {};
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			if (const PipelineStateRecord *record = model->PipelineStateById(pipeline_id))
			{
				state = *record;
				found = true;
			}
		}

		ImGui::Text(Tr("Pipeline state"));
		if (!found)
		{
			ImGui::TextDisabled(Tr("The graphics API reported no fixed function state for this "
			                       "pipeline. That is normal for a compute pipeline, and it is also "
			                       "what happens when the pipeline was created before the capture."));
			return;
		}

		const auto known = [&](uint32_t bit) { return (state.known_fields & (1u << bit)) != 0; };

		if (known(2))
		{
			ImGui::Text(Tr("Depth     %s, write %s, %s"), state.depth_enable ? "on" : "off",
				state.depth_write ? "on" : "off", CompareOpName(state.depth_func));
			ImGui::Text(Tr("Stencil   %s"), state.stencil_enable ? "on" : "off");
		}
		if (known(1))
			ImGui::Text(Tr("Raster    cull %s, %s winding"), CullModeName(state.cull_mode),
				state.front_counter_clockwise ? "counter clockwise" : "clockwise");
		if (known(0))
		{
			ImGui::Text(Tr("Blend     %s, write mask 0x%X"),
				state.blend_enable_mask != 0 ? "on" : "off", state.render_target_write_mask);
			if (state.alpha_to_coverage != 0)
				ImGui::TextDisabled(Tr("alpha to coverage"));
		}
		if (known(4))
		{
			std::string formats;
			for (uint32_t i = 0; i < kMaxTrackedRenderTargets; ++i)
			{
				if (state.render_target_formats[i] == 0)
					continue;
				if (!formats.empty())
					formats += ", ";
				formats += FormatName(state.render_target_formats[i]);
			}
			if (!formats.empty())
				ImGui::Text(Tr("Targets   %s"), formats.c_str());
		}
		if (known(5) && state.depth_stencil_format != 0)
			ImGui::Text(Tr("Depth fmt %s"), FormatName(state.depth_stencil_format));
		if (known(7) && state.sample_count > 1)
			ImGui::Text(Tr("Samples   %u"), state.sample_count);
	}
}
