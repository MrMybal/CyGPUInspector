// CyGPUInspector — adapters from GPU/assistant data to shared CyUI presentation.
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include "TimelineDrawing.hpp"
#include <CyGPUInspectorCore/Localization.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	namespace
	{
		// Stable within and across frames; the different object families have separate namespaces.
		cyui::ItemId ClipId(uint64_t frame, uint32_t family, uint32_t object)
		{
			uint64_t value = 14695981039346656037ull;
			for (const uint64_t part : {frame, uint64_t(family), uint64_t(object)})
				for (unsigned shift = 0; shift < 64; shift += 8)
					value = (value ^ ((part >> shift) & 255)) * 1099511628211ull;
			return value ? value : 1;
		}

		std::string TimeLabel(double value, bool timed = true)
		{
			char label[64];
			std::snprintf(label, sizeof(label), timed ? "%.3f ms" : "%.0f", value);
			return label;
		}

		std::function<std::string(double)> RulerLabels(cyui::Range range, float room, bool timed, bool seconds)
		{
			// Formatting and time units belong to the host. Leave unlabeled grid ticks between
			// labels when a long absolute GPU-clock value needs more room than one tick.
			const double width = std::max(double(cyui::dp(1)), double(room) * .7);
			const double step = range.span() * cyui::dp(90) / width / (seconds ? 1000. : 1.);
			const int decimals = timed ? std::clamp(int(std::ceil(-std::log10(std::max(1e-12, step)))), 0, 9) : 0;
			return [=, last = -std::numeric_limits<double>::infinity(), previous_width = 0.f](double value) mutable {
				if (value < range.min) return std::string();
				char label[96];
				if (timed) std::snprintf(label, sizeof(label), "%.*f %s", decimals, value / (seconds ? 1000. : 1.), seconds ? "s" : "ms");
				else std::snprintf(label, sizeof(label), "%.0f", value);
				const double x = (value - range.min) / range.span() * width;
				if (x - last < previous_width + cyui::dp(8)) return std::string();
				last = x; previous_width = ImGui::CalcTextSize(label).x;
				return std::string(label);
			};
		}

		struct ClipData
		{
			uint64_t frame = 0;
			int pass = -1;
			uint32_t event = UINT32_MAX;
			bool estimated = false, wait = false;
			std::string tooltip;
		};

		// Only domain annotations are drawn by the host. CyUI owns hit testing, zoom, pan,
		// ruler, selection and clip presentation. No edit proposals mutate measured GPU data.
		void AnnotateClip(const cyui::TimelineClip &clip, const cyui::Rect &rect, const ClipData &data)
		{
			if (data.wait || data.estimated)
			{
				auto *draw = ImGui::GetWindowDrawList();
				if (data.wait)
					timeline_drawing::DrawHatchedBar(draw, rect.min.x, rect.max.x, rect.min.y,
						rect.max.y, rect.min.x, rect.max.x, clip.label.c_str());
				else
					draw->AddLine(rect.min, {rect.max.x, rect.min.y}, cyui::palette().warning, cyui::dp(2));
			}
			if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
				ImGui::IsMouseHoveringRect(rect.min, rect.max) && !data.tooltip.empty())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 42);
				ImGui::TextUnformatted(data.tooltip.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
	}

	cyui::ImageResult Application::DrawImageSurface(const char *id, ImageSurface &surface,
		const cyui::ImageData &image, ImVec2 room)
	{
		const auto origin = ImGui::GetCursorScreenPos();
		const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
			ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + room.x, origin.y + room.y));
		if (hovered && (ImGui::GetIO().MouseWheel || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
		{
			surface.fit = false;
			surface.view.fitPending = false;
		}
		if (surface.fit) surface.view.fitPending = true;
		const auto result = cyui::imageViewport(id, surface.view, image, room);
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) surface.fit = true;
		return result;
	}

	void Application::DrawCompilerDiagnostics(float height)
	{
		std::vector<cyui::Diagnostic> rows;
		for (size_t i = 0; i < m_hlsl_messages.size(); ++i)
		{
			const auto &message = m_hlsl_messages[i];
			rows.push_back({i + 1, message.error ? cyui::LogLevel::error : cyui::LogLevel::warning,
				m_hlsl_file.empty() ? "HLSL" : m_hlsl_file, message.text, int(message.line), int(message.column)});
		}
		const auto selected = cyui::diagnosticList("output", rows, {0, height});
		if (selected && selected <= m_hlsl_messages.size())
			m_hlsl_editor.GoToLine(m_hlsl_messages[size_t(selected - 1)].line);
	}

	void Application::DrawMcpLog()
	{
		const auto &log = m_mcp.Log();
		if (m_mcp_displayed_log != log)
		{
			// The bridge owns its bounded log. This mirror only prepares the shared view.
			m_mcp_log_view.clear();
			for (const auto &line : log)
				m_mcp_log_view.append(line.find("REFUSED") != std::string::npos ? cyui::LogLevel::error :
					line.find("  ! ") != std::string::npos ? cyui::LogLevel::warning : cyui::LogLevel::info, line);
			m_mcp_displayed_log = log;
		}
		cyui::logView("mcplog", m_mcp_log_state, m_mcp_log_view);
	}

	void Application::DrawFrameStrip(uint64_t measured, uint64_t first_visible, uint64_t last_visible, bool pick)
	{
		const auto *model = ActiveModel();
		if (!model) return;
		std::vector<FrameHistoryEntry> history;
		{
			std::lock_guard<std::mutex> lock(model->Mutex());
			history = model->History();
		}
		if (history.size() < 2) return;
		const size_t shown = std::min(history.size(), size_t(std::max(2.f, ImGui::GetContentRegionAvail().x / cyui::dp(3))));
		const size_t first = history.size() - shown;
		std::vector<double> values;
		std::vector<cyui::Point> cpu, gpu, budget60, budget30, selection, focus;
		double peak = 0;
		for (size_t i = first; i < history.size(); ++i)
		{
			const auto &frame = history[i];
			values.push_back(frame.cpu_frame_ms);
			peak = std::max(peak, double(frame.cpu_frame_ms));
			cpu.push_back({double(frame.index), frame.cpu_frame_ms});
			gpu.push_back({double(frame.index), frame.gpu_ms > 0 ? frame.gpu_ms : std::numeric_limits<double>::quiet_NaN()});
		}
		std::nth_element(values.begin(), values.begin() + ptrdiff_t(values.size() * 19 / 20), values.end());
		const double scale = std::max(1., values[values.size() * 19 / 20] * 1.3);
		const double low = double(history[first].index) - .5, high = double(history.back().index) + .5;
		budget60 = {{low, 1000. / 60}, {high, 1000. / 60}};
		budget30 = {{low, 1000. / 30}, {high, 1000. / 30}};
		if (first_visible && last_visible >= first_visible)
			selection = {{double(first_visible), scale * .92}, {double(last_visible), scale * .92}};
		if (measured) focus = {{double(measured), 0}, {double(measured), scale}};
		m_frame_plot.x.range = {low, high};
		m_frame_plot.y.range = {0, scale};
		std::vector<cyui::PlotSeries> series = {
			{1, "CPU", cpu, cyui::PlotStyle::bars, 0}, {2, "GPU", gpu, cyui::PlotStyle::line, 2},
			{3, "60 fps", budget60, cyui::PlotStyle::line, 1}, {4, "30 fps", budget30, cyui::PlotStyle::line, 3},
			{5, Tr("Visible frames"), selection, cyui::PlotStyle::line, 4},
			{6, Tr("Frame"), focus, cyui::PlotStyle::line, 5}};
		cyui::PlotOptions options;
		options.navigation = false;
		options.axes = options.legend = false;
		const auto result = cyui::plot("frame-strip", m_frame_plot, series, cyui::dp(0, 64), options);
		const bool scrubbing = ImGui::IsItemActive();
		cyui::textDim(Tr("CPU %.2f ms, peak %.2f ms"), double(history.back().cpu_frame_ms), peak);
		if (result.hovered)
		{
			const auto at = std::clamp(result.mouse.x, double(history[first].index), double(history.back().index));
			const auto it = std::min_element(history.begin() + ptrdiff_t(first), history.end(),
				[&](const auto &a, const auto &b) { return std::abs(double(a.index) - at) < std::abs(double(b.index) - at); });
			if (pick && scrubbing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (m_track_selected_frame != it->index)
				{
					m_track_selected_frame = it->index;
					m_track_selected_pass = -1;
					m_track_center_request = it->index;
				}
				m_track_follow = false;
			}
			ImGui::BeginTooltip();
			ImGui::Text(Tr("Frame %llu"), static_cast<unsigned long long>(it->index));
			ImGui::Text(Tr("CPU %.2f ms"), double(it->cpu_frame_ms));
			if (it->gpu_ms > 0) ImGui::Text(Tr("GPU %.2f ms, measured passes only"), it->gpu_ms);
			else ImGui::TextUnformatted(Tr("GPU not measured for this frame"));
			ImGui::Text(Tr("%u draws, %u dispatches, %u commands"), it->draw_count, it->dispatch_count, it->event_count);
			ImGui::EndTooltip();
		}
	}

	void Application::DrawTimingView()
	{
		const auto &passes = m_graph.Passes();
		if (passes.empty()) return;
		const auto &spans = m_timeline.Spans();
		const bool timed = m_timeline.Timed();
		// The live single-frame view keeps logical pass/event identities while new timings arrive.
		const uint64_t frame = 0;
		const double length = std::max(m_timeline.Length(), .000001);
		const size_t measured = size_t(std::count_if(spans.begin(), spans.end(), [](const auto &s) { return s.measured; }));
		const bool commands = timed && m_timeline.Points().size() > measured;
		if (timed && !m_timeline.MeasuredPositions())
			cyui::textDim("%s", Tr("Positions laid end to end: this add-on predates measured start times."));
		std::vector<cyui::TimelineLane> lanes = {{1, Tr("Frame"), 30}, {2, Tr("Passes"), 30}};
		if (commands) lanes.push_back({3, Tr("Commands"), 30});
		std::vector<cyui::TimelineClip> clips;
		std::unordered_map<cyui::ItemId, ClipData> data;
		clips.push_back({ClipId(frame, 1, 0), 1, 0, length, TimeLabel(length, timed), 0, true});
		for (size_t i = 0; i < passes.size() && i < spans.size(); ++i)
		{
			const auto &pass = passes[i]; const auto &span = spans[i];
			const auto id = ClipId(frame, 2, pass.id);
			clips.push_back({id, 2, span.start, std::max(.000001, span.Length()),
				pass.name + "  " + TimeLabel(span.Length(), timed), int(pass.kind), true});
			char tip[1024];
			std::snprintf(tip, sizeof(tip), "%s\n%s, %.0f%%\n%s: %s, %s: %s (%.1f%%)\n%u draws, %u dispatches, events %u-%u%s",
				pass.name.c_str(), PassNameOriginName(pass.origin), double(pass.confidence) * 100,
				Tr("Start"), TimeLabel(span.start, timed).c_str(), Tr("Duration"), TimeLabel(span.Length(), timed).c_str(),
				span.Length() / length * 100, pass.draw_count, pass.dispatch_count, pass.first_event, pass.last_event,
				timed && !span.measured ? Tr("\nNo measurement of its own: placed between the two around it") : "");
			data[id] = {frame, int(i), pass.first_event, timed && !span.measured, false, tip};
		}
		if (commands)
			for (const auto &point : m_timeline.Points())
			{
				const auto id = ClipId(frame, 3, point.event);
				clips.push_back({id, 3, point.start, std::max(.000001, point.length),
					std::to_string(point.event) + " " + EventKindName(point.kind), int(point.kind), true});
				data[id] = {frame, -1, point.event, false, false,
					std::string(EventKindName(point.kind)) + "\n" + TimeLabel(point.start) + " / " + TimeLabel(point.length) +
					"\n" + Tr("until the next measured command, which is what a before-only interceptor can measure")};
			}
		if (m_view_fit || m_view_span <= 0)
			m_frame_timeline_view.view = {0, length * 1.02};
		else m_frame_timeline_view.view = {m_view_start, m_view_start + m_view_span};
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			m_frame_timeline_view.selected.clear();
			if (m_selected_pass) m_frame_timeline_view.selected.push_back(ClipId(frame, 2, m_selected_pass));
			if (commands) m_frame_timeline_view.selected.push_back(ClipId(frame, 3, m_selected_event));
		}
		cyui::ItemId hovered_clip = 0;
		cyui::TimelineOptions options;
		options.readOnly = true; options.revision = m_timeline.FrameIndex();
		options.formatTime = RulerLabels(m_frame_timeline_view.view, ImGui::GetContentRegionAvail().x, timed, false);
		options.drawClip = [&](const auto &clip, const auto &rect) {
			const auto it = data.find(clip.id);
			if (it != data.end())
			{
				AnnotateClip(clip, rect, it->second);
				if (ImGui::IsMouseHoveringRect(rect.min, rect.max)) hovered_clip = clip.id;
			}
		};
		const auto before = m_frame_timeline_view.view;
		cyui::timeline("timing-view", m_frame_timeline_view, lanes, clips, {},
			ImVec2(0, ImGui::GetFrameHeight() + cyui::dp(30.f * float(lanes.size()) + 2)), options);
		const bool hovered = ImGui::IsItemHovered();
		m_frame_navigation.Update(m_frame_timeline_view);
		if (before.min != m_frame_timeline_view.view.min || before.max != m_frame_timeline_view.view.max)
			m_view_fit = false;
		m_view_start = m_frame_timeline_view.view.min;
		m_view_span = m_frame_timeline_view.view.span();
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_view_fit = true;
		if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_frame_navigation.dragged && hovered_clip)
		{
			const auto it = data.find(hovered_clip);
			if (it != data.end())
			{
				if (it->second.pass >= 0) m_selected_pass = passes[size_t(it->second.pass)].id;
				else if (const auto *owner = m_graph.PassOfEvent(it->second.event)) m_selected_pass = owner->id;
				SelectEvent(it->second.event);
			}
		}
	}

	void Application::DrawContinuousView()
	{
		const auto &frames = m_track.Frames();
		if (frames.empty())
		{
			cyui::textDim("%s", Tr("No measured frame yet. The continuous view is built from GPU timings: raise the tracking level to Pass Timing."));
			return;
		}
		if (!m_track.MeasuredAxis())
			cyui::textDim("%s", Tr("Frames laid one CPU frame apart: this add-on does not send where frames sit on the GPU clock, so the waits between them are not measured."));
		if (m_track_view_span <= 0)
		{
			const size_t back = std::min<size_t>(frames.size() - 1, 4);
			const double period = back ? (frames.back().begin - frames[frames.size() - 1 - back].begin) / double(back) : frames.back().Busy();
			m_track_view_span = std::max(period * 4, .05);
		}
		if (m_track_center_request)
		{
			const auto it = std::min_element(frames.begin(), frames.end(), [&](const auto &a, const auto &b) {
				return std::abs(double(a.index) - double(m_track_center_request)) < std::abs(double(b.index) - double(m_track_center_request));
			});
			m_track_view_start = it->begin - (m_track_view_span - it->Busy()) * .5;
			m_track_selected_frame = it->index; m_track_center_request = 0;
		}
		if (m_track_follow) m_track_view_start = m_track.End() - m_track_view_span * .97;
		m_continuous_timeline_view.view = {m_track_view_start, m_track_view_start + m_track_view_span};
		const auto before = m_continuous_timeline_view.view;
		std::vector<cyui::TimelineLane> lanes = {{1, Tr("Frames"), 30}, {2, Tr("GPU waiting"), 30}};
		for (int i = 0; i < 4; ++i) lanes.push_back({uint64_t(i + 3), std::string(Tr("Passes")) + " " + std::to_string(i + 1), 30});
		lanes.push_back({7, Tr("Commands"), 26});
		std::vector<cyui::TimelineClip> clips;
		std::unordered_map<cyui::ItemId, ClipData> data;
		double lane_end[4] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
		const TrackFrame *previous = nullptr;
		int pass_lanes = 1; bool has_commands = false;
		for (const auto &frame : frames)
		{
			if (previous && frame.begin > previous->end && frame.begin >= before.min && previous->end <= before.max)
			{
				const auto id = ClipId(frame.index, 4, 0);
				const auto missing = frame.index > previous->index + 1 ? frame.index - previous->index - 1 : 0;
				const auto label = std::string(Tr("GPU waiting")) + " " + TimeLabel(frame.begin - previous->end);
				clips.push_back({id, 2, previous->end, frame.begin - previous->end, label, 0, true});
				data[id] = {frame.index, -1, UINT32_MAX, false, true, label + "\n" +
					std::to_string(missing) + " " + Tr("frames not measured") + "\n" +
					Tr("No measured GPU work: the GPU was waiting for the next frame, or running commands that come before its first timestamp.")};
			}
			previous = &frame;
			if (frame.end < before.min || frame.begin > before.max) continue;
			const auto id = ClipId(frame.index, 1, 0);
			const auto name = std::string(Tr("Frame")) + " " + std::to_string(frame.index);
			clips.push_back({id, 1, frame.begin, std::max(.000001, frame.Busy()), name, 0, true});
			char tip[512];
			std::snprintf(tip, sizeof(tip), "%s\nGPU %.3f ms, CPU %.2f ms\n%u draws, %u dispatches, %u commands%s", name.c_str(),
				frame.Busy(), double(frame.cpu_ms), frame.draw_count, frame.dispatch_count, frame.event_count,
				frame.own_structure ? "" : Tr("\nPass structure reconstructed from a later frame"));
			data[id] = {frame.index, -1, UINT32_MAX, false, false, tip};
			for (size_t i = 0; i < frame.passes.size(); ++i)
			{
				const auto &pass = frame.passes[i];
				if (pass.end < before.min || pass.start > before.max) continue;
				int lane = 0;
				while (lane < 3 && pass.start < lane_end[lane]) ++lane;
				lane_end[lane] = pass.end;
				pass_lanes = std::max(pass_lanes, lane + 1);
				const auto key = ClipId(frame.index, 2, uint32_t(i));
				clips.push_back({key, uint64_t(lane + 3), pass.start, std::max(.000001, pass.Length()), pass.name, int(pass.kind), true});
				char details[1024];
				std::snprintf(details, sizeof(details), "%s\n%s, %.0f%%\n%s: %s / %s (%.1f%%)\n%u draws, %u dispatches, events %u-%u%s%s",
					pass.name.c_str(), PassNameOriginName(pass.origin), double(pass.confidence) * 100, name.c_str(),
					TimeLabel(pass.start - frame.begin).c_str(), TimeLabel(pass.Length()).c_str(),
					frame.Busy() > 0 ? pass.Length() / frame.Busy() * 100 : 0., pass.draw_count, pass.dispatch_count,
					pass.first_event, pass.last_event, pass.measured ? "" : Tr("\nNo measurement of its own: placed between the two around it"),
					frame.own_structure ? "" : Tr("\nPass structure reconstructed from a later frame"));
				data[key] = {frame.index, int(i), pass.first_event, !pass.measured, false, details};
			}
			if (frame.commands.size() > frame.passes.size())
				for (const auto &command : frame.commands)
				{
					if (command.start + command.length < before.min || command.start > before.max) continue;
					has_commands = true;
					const auto key = ClipId(frame.index, 3, command.event);
					const auto label = std::to_string(command.event) + " " + EventKindName(command.kind);
					clips.push_back({key, 7, command.start, std::max(.000001, command.length), label, int(command.kind), true});
					data[key] = {frame.index, -1, command.event, false, false,
						name + "\n" + label + "\n" + TimeLabel(command.start - frame.begin) + " / " + TimeLabel(command.length)};
				}
		}
		lanes.erase(lanes.begin() + 2 + pass_lanes, lanes.begin() + 6);
		if (!has_commands) lanes.pop_back();
		m_continuous_timeline_view.selected.erase(std::remove_if(m_continuous_timeline_view.selected.begin(),
			m_continuous_timeline_view.selected.end(), [&](auto id) { return !data.count(id); }), m_continuous_timeline_view.selected.end());
		cyui::ItemId hovered_clip = 0;
		cyui::TimelineOptions options;
		options.readOnly = true; options.revision = frames.back().index;
		options.formatTime = RulerLabels(m_continuous_timeline_view.view, ImGui::GetContentRegionAvail().x, true, true);
		options.drawClip = [&](const auto &clip, const auto &rect) {
			const auto it = data.find(clip.id);
			if (it != data.end())
			{
				AnnotateClip(clip, rect, it->second);
				if (ImGui::IsMouseHoveringRect(rect.min, rect.max)) hovered_clip = clip.id;
			}
		};
		cyui::timeline("continuous-view", m_continuous_timeline_view, lanes, clips, {}, ImVec2(0, ImGui::GetFrameHeight() + cyui::dp(30.f * float(lanes.size()) + 2)), options);
		const bool hovered = ImGui::IsItemHovered();
		m_continuous_navigation.Update(m_continuous_timeline_view);
		if (before.min != m_continuous_timeline_view.view.min || before.max != m_continuous_timeline_view.view.max)
			m_track_follow = false;
		m_track_view_start = m_continuous_timeline_view.view.min;
		m_track_view_span = m_continuous_timeline_view.view.span();
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_track_follow = true; m_track_view_span = 0;
		}
		if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_continuous_navigation.dragged && hovered_clip)
		{
			const auto it = data.find(hovered_clip);
			if (it != data.end() && !it->second.wait)
			{
				m_track_selected_frame = it->second.frame;
				m_track_selected_pass = it->second.pass;
				m_track_follow = false;
			}
		}
		// The application chooses the time origin and retained range; the common slider
		// replaces the duplicated scrollbar, and remains accessible from the keyboard.
		const double low = m_track.Start() - m_track_view_span * .5;
		const double high = std::max(low, m_track.End() - m_track_view_span * .1);
		double start = std::clamp(m_track_view_start, low, high);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderScalar("##continuous-position", ImGuiDataType_Double, &start, &low, &high, "%.3f ms"))
		{
			m_track_view_start = start; m_track_follow = false;
		}
		cyui::tooltip(Tr("Drag to move through the frames kept; click beside the bar to jump there."));
	}
}
