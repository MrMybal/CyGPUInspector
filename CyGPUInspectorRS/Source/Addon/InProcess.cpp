// CyGPUInspectorRS — the in-process interface.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "InProcess.hpp"

#include "Log.hpp"

#include <CyGPUInspectorCore/InProcessApi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace cygi::inprocess
{
	namespace
	{
		std::mutex g_mutex;
		uint32_t g_devices = 0;
		bool g_has_request = false;
		CaptureFrameRequest g_request = {};
		StatusSnapshot g_status = {};
		ViewportScope g_scope = {};
	}

	ViewportScope GetViewportScope()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_scope;
	}

	void DeviceCreated()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		++g_devices;
	}

	void DeviceDestroyed()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_devices != 0)
			--g_devices;
	}

	bool TakeCaptureRequest(CaptureFrameRequest &out)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_has_request)
			return false;
		out = g_request;
		g_has_request = false;
		return true;
	}

	void PublishStatus(const StatusSnapshot &status)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_status = status;
	}

	namespace
	{
		int32_t RequestCapture(const CygiCaptureOptions *options)
		{
			// The size is checked against the fields this add-on knows, so a caller built against
			// an older header still works, and a newer one is read no further than we understand.
			if (options == nullptr || options->size < offsetof(CygiCaptureOptions, include_buffers))
				return 0;
			CygiCaptureOptions copy = {};
			std::memcpy(&copy, options, std::min<size_t>(options->size, sizeof(copy)));

			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_devices == 0)
				return 0;
			g_request = {};
			g_request.frame_count = std::clamp<uint32_t>(copy.frame_count, 1u, 8u);
			g_request.mode = CaptureMode::deep;
			g_request.include_bindings = copy.include_bindings != 0 ? 1u : 0u;
			g_request.include_barriers = copy.include_barriers != 0 ? 1u : 0u;
			g_request.per_draw_timing = copy.per_draw_timing != 0 ? 1u : 0u;
			g_request.include_buffers = copy.include_buffers != 0 ? 1u : 0u;
			g_has_request = true;
			return 1;
		}

		int32_t SetViewportScope(const CygiViewportScope *scope)
		{
			if (scope == nullptr || scope->size < sizeof(CygiViewportScope))
				return 0;
			std::lock_guard<std::mutex> lock(g_mutex);
			g_scope.begin_marker = scope->begin_marker;
			g_scope.end_marker = scope->end_marker;
			g_scope.final_image = scope->final_image;
			return 1;
		}

		int32_t GetStatus(CygiStatus *status)
		{
			if (status == nullptr || status->size < sizeof(uint32_t))
				return 0;

			CygiStatus out = {};
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				out.api_version = CYGI_INPROCESS_API_VERSION;
				out.device_count = g_devices;
				out.graphics_api = static_cast<uint32_t>(g_status.api);
				out.tracking_level = static_cast<uint32_t>(g_status.level);
				out.standalone_connected = g_status.standalone_connected ? 1u : 0u;
				out.standalone_process_id = g_status.standalone_process_id;
				out.capture_stage = static_cast<uint32_t>(g_status.capture.stage);
				out.capture_first_frame = g_status.capture.first_frame;
				out.capture_frames_done = g_status.capture.frames_done;
				out.capture_frames_requested = g_status.capture.frames_requested;
				out.frame_index = g_status.frame_index;
				out.pending_request = g_has_request ? 1u : 0u;
				out.viewport_scope = g_scope.HasMarkers() ? 1u : 0u;
			}
			const uint32_t size = std::min<uint32_t>(status->size, sizeof(out));
			out.size = size;
			std::memcpy(status, &out, size);
			return 1;
		}
	}
}

extern "C" __declspec(dllexport) int32_t CyGPUInspectorRS_RequestCapture(const CygiCaptureOptions *options)
{
	return cygi::inprocess::RequestCapture(options);
}

extern "C" __declspec(dllexport) int32_t CyGPUInspectorRS_GetStatus(CygiStatus *status)
{
	return cygi::inprocess::GetStatus(status);
}

extern "C" __declspec(dllexport) int32_t CyGPUInspectorRS_SetViewportScope(const CygiViewportScope *scope)
{
	return cygi::inprocess::SetViewportScope(scope);
}

extern "C" __declspec(dllexport) uint32_t CyGPUInspectorRS_GetApiVersion()
{
	return CYGI_INPROCESS_API_VERSION;
}
