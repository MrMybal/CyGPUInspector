// CyGPUInspectorRS — minimal ReShade overlay.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <imgui.h>
#include <reshade.hpp>

#include "Overlay.hpp"

#include "Addon/DeviceContext.hpp"
#include "Addon/Log.hpp"

#include <CyGPUInspectorCore/Localization.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#include <cstdio>

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
		const char *const kLevelNames[] = { "Idle", "Tracking", "Pass Timing", "Capture", "Full Draw Timing" };

		void DrawOverlay(reshade::api::effect_runtime *runtime)
		{
			DeviceContext *context = ContextOf(runtime->get_device());
			if (context == nullptr)
			{
				ImGui::TextUnformatted(Tr("No device context (the add-on did not attach to this device)."));
				return;
			}

			ImGui::Text(Tr("CyGPUInspectorRS %s"), kVersionString);
			ImGui::Separator();

			ImGui::Text(Tr("API:            %s"), GraphicsApiName(context->api));
			ImGui::Text(Tr("Standalone:     %s"), context->ipc.IsClientConnected() ? "connected" : "not connected");

			const TrackingLevel level = context->level.load(std::memory_order_relaxed);
			int level_index = static_cast<int>(level);
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::Combo(TrId("Tracking"), &level_index, kLevelNames, IM_ARRAYSIZE(kLevelNames)))
			{
				const TrackingLevel new_level = static_cast<TrackingLevel>(level_index);
				context->level.store(new_level, std::memory_order_relaxed);
				context->ipc.PublishSessionInfo(new_level);
			}

			ImGui::Separator();
			ImGui::Text(Tr("Tracked shaders:    %u"), context->shaders.ShaderCount());
			ImGui::Text(Tr("Tracked pipelines:  %u"), context->shaders.PipelineCount());
			ImGui::Text(Tr("Tracked resources:  %u alive / %u seen"), context->resources.AliveCount(),
				context->resources.ResourceCount());
			ImGui::Text(Tr("Frame:              %llu"), static_cast<unsigned long long>(context->frames.FrameIndex()));
			ImGui::Text(Tr("Disabled shaders:   %u"), context->control.DisabledShaderCount());
			ImGui::Text(Tr("Replaced shaders:   %u"), context->control.ReplacedShaderCount());
			ImGui::Text(Tr("Skipped draws:      %u"), context->skipped_draws.load(std::memory_order_relaxed));

			ImGui::Separator();
			const double written_mb = static_cast<double>(context->ipc.BytesWritten()) / (1024.0 * 1024.0);
			const double dropped_mb = static_cast<double>(context->ipc.DroppedBytes()) / (1024.0 * 1024.0);
			ImGui::Text(Tr("IPC written:        %.1f MiB (%llu records)"), written_mb,
				static_cast<unsigned long long>(context->ipc.RecordsWritten()));
			if (context->ipc.DroppedBytes() != 0)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), Tr("IPC dropped:        %.1f MiB (standalone too slow)"), dropped_mb);
			ImGui::Text(Tr("Add-on CPU / frame: %.3f ms"), context->addon_cpu_ms);

			ImGui::Separator();
			if (ImGui::Button(TrId("Resend everything")))
			{
				context->shaders.QueueEverything();
				context->resources.QueueEverything();
			}
			ImGui::SameLine();
			if (ImGui::Button(TrId("Restore all shaders")))
			{
				context->control.RestoreAll();
				context->control.ClearAllReplacements(context->device);
			}

			ImGui::Spacing();
			ImGui::TextDisabled(Tr("The analysis UI is CyGPUInspectorApp, in its own process."));
		}
	}

	void RegisterOverlays()
	{
		reshade::register_overlay("CyGPUInspector", DrawOverlay);
	}

	void UnregisterOverlays()
	{
		reshade::unregister_overlay("CyGPUInspector", DrawOverlay);
	}
}
