// CyGPUInspectorApp — Selected Shader, Selected Draw, Selected Resource (section 22), and the per
// shader timing of section 24.
//
// Pass Timing costs almost nothing because it marks only where the render targets change; Full
// Draw Timing marks every command and can halve the frame rate. A timing focus sits between the
// two: on top of Pass Timing, the add-on marks just before and just after each command the focus
// names — the draws of one shader, one draw, or the draws into one resource — so those, and only
// those, get a duration of their own.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include <cyui/CyUI.h>
#include "LayoutHelpers.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	namespace
	{
		// The pipelines using a shader, as the add-on needs them. The model lock is the caller's.
		uint32_t PipelinesOf(const SessionModel &model, uint32_t shader_id, uint32_t *out)
		{
			uint32_t count = 0;
			for (const PipelineInfo &pipeline : model.Pipelines())
			{
				if (count == kMaxTimingFocusPipelines)
					break;
				if (std::find(pipeline.shader_ids.begin(), pipeline.shader_ids.end(), shader_id) != pipeline.shader_ids.end())
					out[count++] = pipeline.id;
			}
			return count;
		}
	}

	void Application::SetTimingFocus(TimingFocusKind kind, uint32_t id)
	{
		if (!IsLive())
			return;

		TimingFocusRequest request = {};
		request.kind = kind;
		TrackingLevel level = TrackingLevel::tracking;
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());
			level = model.Session().level;
			switch (kind)
			{
			case TimingFocusKind::shader:
				request.pipeline_count = PipelinesOf(model, id, request.pipelines);
				break;
			case TimingFocusKind::draw:
			{
				const std::vector<FrameEvent> &events = model.LastFrame().events;
				const auto found = std::find_if(events.begin(), events.end(),
					[id](const FrameEvent &event) { return event.index == id; });
				if (found == events.end() || !IsDrawOrDispatch(found->kind))
				{
					m_timing_focus_message = Tr("That command is not a draw or a dispatch of the current frame.");
					return;
				}
				request.draw = *found;
				break;
			}
			case TimingFocusKind::resource:
				request.resource_id = id;
				break;
			case TimingFocusKind::none:
			default:
				break;
			}
		}

		// The focus adds to Pass Timing: below it nothing is timed, and Full Draw Timing already
		// times everything.
		if (kind != TimingFocusKind::none && level != TrackingLevel::pass_timing && level != TrackingLevel::full_draw_timing)
		{
			std::string message;
			SetTrackingLevel(TrackingLevel::pass_timing, message);
		}

		if (!m_client->SetTimingFocus(request))
		{
			m_timing_focus_message = Tr("The add-on did not accept the timing focus: it may be older than this application.");
			return;
		}
		m_timing_focus = request;
		m_timing_focus_id = kind == TimingFocusKind::none ? 0 : id;
		m_timing_focus_message.clear();
		m_since_focus_refresh = 0.0;
	}

	void Application::RefreshTimingFocus(double delta_seconds)
	{
		if (m_client == nullptr)
		{
			m_timing_focus = {};
			m_timing_focus_id = 0;
			return;
		}
		if (m_timing_focus.kind != TimingFocusKind::shader || !IsLive())
			return;
		// A game creates pipelines as it goes: the ones using the shader are sent again when
		// their list changes.
		m_since_focus_refresh += delta_seconds;
		if (m_since_focus_refresh < 2.0)
			return;
		m_since_focus_refresh = 0.0;

		TimingFocusRequest request = m_timing_focus;
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());
			request.pipeline_count = PipelinesOf(model, m_timing_focus_id, request.pipelines);
		}
		if (request.pipeline_count != m_timing_focus.pipeline_count ||
		    !std::equal(request.pipelines, request.pipelines + request.pipeline_count, m_timing_focus.pipelines))
		{
			if (m_client->SetTimingFocus(request))
				m_timing_focus = request;
		}
	}

	bool Application::DrawTimingFocusButton(TimingFocusKind kind, uint32_t id)
	{
		const bool active = m_timing_focus.kind == kind && m_timing_focus_id == id;
		const char *label = kind == TimingFocusKind::shader ? (active ? TrId("Stop timing this shader") : TrId("Time this shader precisely"))
			: kind == TimingFocusKind::draw ? (active ? TrId("Stop timing this draw") : TrId("Time this draw precisely"))
			: (active ? TrId("Stop timing these draws") : TrId("Time the draws into this resource"));
		ImGui::BeginDisabled(!IsLive());
		const bool clicked = ImGui::SmallButton(label);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", Tr("On top of Pass Timing, two timestamps around each of these commands only, so "
			                           "they get a GPU time of their own without the cost of Full Draw Timing. The "
			                           "tracking level goes to Pass Timing if it was lower."));
		if (clicked)
			SetTimingFocus(active ? TimingFocusKind::none : kind, id);
		return active;
	}

	void Application::DrawShaderTiming(uint32_t shader_id)
	{
		cyui::sectionTitle(Tr("GPU time"));

		// Section 24: calls, total, average and maximum, from the commands of the frame held that
		// have a measurement of their own.
		uint32_t calls = 0;
		uint32_t measured = 0;
		uint64_t total = 0;
		uint64_t maximum = 0;
		bool timings_valid = false;
		uint64_t timings_frame = 0;
		TrackingLevel level = TrackingLevel::tracking;
		double milliseconds_per_tick = 0.0;
		if (SessionModel *active = ActiveModel())
		{
			std::lock_guard<std::mutex> lock(active->Mutex());
			const FrameTimings &timings = active->Timings();
			timings_valid = timings.IsValid();
			timings_frame = timings.frame_index;
			if (active->HasSession())
				level = active->Session().level;
			milliseconds_per_tick = timings_valid ? timings.Milliseconds(1) : 0.0;
			for (const FrameEvent &event : active->LastFrame().events)
			{
				if (!IsDrawOrDispatch(event.kind) || event.pipeline_id == 0)
					continue;
				const PipelineInfo *pipeline = active->PipelineById(event.pipeline_id);
				if (pipeline == nullptr ||
				    std::find(pipeline->shader_ids.begin(), pipeline->shader_ids.end(), shader_id) == pipeline->shader_ids.end())
					continue;
				++calls;
				const auto found = timings.by_event.find(event.index);
				if (found == timings.by_event.end())
					continue;
				++measured;
				total += found->second;
				maximum = std::max<uint64_t>(maximum, found->second);
			}
		}

		const bool focused = m_timing_focus.kind == TimingFocusKind::shader && m_timing_focus_id == shader_id;
		const bool precise = focused || level == TrackingLevel::full_draw_timing;
		if (!timings_valid)
		{
			TextDisabledWrapped(Tr("No GPU timing yet: set the tracking level to Pass Timing, or time this shader "
			                       "precisely."));
		}
		else if (precise && measured != 0)
		{
			ImGui::Text(Tr("Calls: %u (%u measured)"), calls, measured);
			ImGui::Text(Tr("Total GPU: %.3f ms"), total * milliseconds_per_tick);
			ImGui::Text(Tr("Average: %.4f ms"), measured != 0 ? total * milliseconds_per_tick / measured : 0.0);
			ImGui::Text(Tr("Max: %.4f ms"), maximum * milliseconds_per_tick);
			char line[128];
			std::snprintf(line, sizeof(line), Tr("measured on frame %llu"), static_cast<unsigned long long>(timings_frame));
			TextDisabledWrapped(line);
		}
		else
		{
			char line[256];
			std::snprintf(line, sizeof(line), Tr("%u call(s) this frame. Pass Timing gives each pass one duration, "
				"attributed to the shaders of its first command: time this shader precisely for its own."), calls);
			TextDisabledWrapped(line);
		}

		DrawTimingFocusButton(TimingFocusKind::shader, shader_id);
		if (!m_timing_focus_message.empty())
			TextDisabledWrapped(m_timing_focus_message.c_str());
	}
}
