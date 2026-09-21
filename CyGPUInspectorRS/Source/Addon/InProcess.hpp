// CyGPUInspectorRS — the in-process interface, add-on side.
//
// The exported C functions (see CyGPUInspectorCore/InProcessApi.h) run on whatever thread the
// caller is on — the Unreal game thread, typically — while everything the add-on does happens in
// the present event on the render thread. So the two only meet here: a request is queued, and
// taken by the next present; the status is written by each present, and read by the caller.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>

namespace cygi::inprocess
{
	void DeviceCreated();
	void DeviceDestroyed();

	// Called at present: the request queued by the caller since the last one, if any.
	bool TakeCaptureRequest(CaptureFrameRequest &out);

	struct StatusSnapshot
	{
		GraphicsApi api = GraphicsApi::unknown;
		TrackingLevel level = TrackingLevel::idle;
		bool standalone_connected = false;
		uint32_t standalone_process_id = 0;
		CaptureStateRecord capture = {};
		uint64_t frame_index = 0;
	};
	// Called at the end of every present.
	void PublishStatus(const StatusSnapshot &status);

	// The part of the frame a capture is about, as the host set it; all zero when none is.
	struct ViewportScope
	{
		uint64_t begin_marker = 0;
		uint64_t end_marker = 0;
		uint64_t final_image = 0;

		bool HasMarkers() const { return begin_marker != 0 && end_marker != 0; }
	};
	ViewportScope GetViewportScope();
}
