// CyGPUInspectorRS — per device state and event registration.
//
// One DeviceContext lives per graphics device, attached as ReShade private data. It owns the
// trackers, the IPC server and the runtime control, and it is the only place where per frame
// work happens (inside the `present` event).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Addon/InProcess.hpp"
#include "Control/RuntimeControl.hpp"
#include "Ipc/IpcServer.hpp"
#include "Preview/CaptureBuffers.hpp"
#include "Preview/PreviewBridge.hpp"
#include "Profiling/GpuTimer.hpp"
#include "Tracking/CommandListState.hpp"
#include "Tracking/DeepCapture.hpp"
#include "Tracking/FrameRecorder.hpp"
#include "Tracking/ResourceTracker.hpp"
#include "Tracking/ShaderTracker.hpp"
#include "Tracking/StateShadow.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct __declspec(uuid("f1117580-299c-4e1a-9325-7c212168093e")) DeviceContext
	{
		explicit DeviceContext(reshade::api::device *device);
		~DeviceContext();

		reshade::api::device *device = nullptr;
		GraphicsApi api = GraphicsApi::unknown;
		uint32_t device_index = 0;
		uint32_t capability_flags = 0;
		uint64_t timestamp_frequency = 0;
		// Whether the frequency above came from a graphics queue, so a later compute or copy queue
		// does not replace a good value with its own.
		bool timestamp_frequency_from_graphics = false;

		ShaderTracker shaders;
		ResourceTracker resources;
		FrameRecorder frames;
		RuntimeControl control;
		PreviewBridge preview;
		GpuTimer timer;
		IpcServer ipc;
		DeepCapture capture;
		CaptureBuffers capture_buffers;

		std::atomic<TrackingLevel> level{ TrackingLevel::tracking };
		std::atomic<uint32_t> skipped_draws{ 0 };
		std::atomic<uint32_t> timed_marks{ 0 };

		// Command lists that recorded events but were never executed explicitly (the D3D11
		// immediate context), merged at present time.
		std::mutex immediate_mutex;
		std::vector<CommandListState *> immediate_states;

		void RegisterImmediateState(CommandListState *state);
		void UnregisterImmediateState(CommandListState *state);

		// Programs with several windows present several swap chains: the Unreal editor presents
		// its main window, and every tooltip, menu and notification, each with its own. Only the
		// main one — the largest, until it stops presenting — ends a frame and gives the final
		// image; the others are let through untouched. Counting them as frames split one frame into
		// slivers, and made the final image change size at every present.
		bool IsPrimaryPresent(reshade::api::swapchain *swapchain);
		std::mutex present_mutex;
		uint64_t primary_swapchain = 0;
		uint64_t primary_area = 0;
		uint32_t presents_without_primary = 0;

		// Called from the present event of the primary swap chain.
		void EndFrame(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain);
		void MergeCommandList(CommandListState &state);
		void HandleControlMessages();
		// Arms a deep capture, from the standalone or from inside the process. Refuses, and says
		// why, when tracking is idle: there would be no frame to capture.
		bool ArmDeepCapture(const CaptureFrameRequest &request, const char *&refusal);

		uint64_t last_present_qpc = 0;
		double addon_cpu_ms = 0.0;

		// Sent once per pipeline, and only while a deep capture is running, so a capture that
		// starts mid game still gets the state of the pipelines it actually sees used.
		void PublishPipelineStates();

		// The part of the frame a capture is about, as the in-process interface last said, and
		// where it is in this frame's events: between the clears of the two marker textures.
		inprocess::ViewportScope viewport_scope;
		bool FindViewportScope(const std::vector<FrameEvent> &events, uint32_t &first_event, uint32_t &last_event) const;

		// The names the game gave the resources a captured frame used, read from the native
		// objects (see Tracking/ResourceNames.hpp) and sent when they change. Render thread only.
		void PublishResourceNames(const std::vector<FrameEvent> &events, const FrameRecorder::CapturedState &captured);
		std::unordered_map<uint32_t, std::string> published_names;
		std::mutex pipeline_state_mutex;
		std::vector<PipelineStateRecord> pipeline_states;
		std::vector<PipelineStateRecord> pending_pipeline_states;
	};

	// Retrieves the context of the device that owns this object, or nullptr.
	DeviceContext *ContextOf(reshade::api::device *device);
	DeviceContext *ContextOf(reshade::api::command_list *command_list);
	DeviceContext *ContextOf(reshade::api::command_queue *queue);

	void RegisterDeviceEvents();
	void UnregisterDeviceEvents();
}
