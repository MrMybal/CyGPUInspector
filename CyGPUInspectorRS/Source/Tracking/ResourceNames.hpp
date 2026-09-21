// CyGPUInspectorRS — the names a game gives its resources.
//
// Graphics APIs let an application name its objects for debuggers: SetName on Direct3D 12,
// SetPrivateData(WKPDID_D3DDebugObjectName) on Direct3D 11. Engines do it in their development
// builds — Unreal names every render target it allocates (SceneDepthZ, GBufferA, ...) in the
// editor and in Development and DebugGame builds, not in Shipping.
//
// The name is read from the native object ReShade hands the add-on, when it is needed: nothing is
// intercepted, so a name set after creation (which is how engines do it) is seen, and a pooled
// texture renamed for its next use shows the name it has at that moment.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <reshade.hpp>

#include <string>

namespace cygi
{
	// UTF-8, empty when the resource has no name or the API has no such thing (Vulkan, OpenGL).
	std::string ReadNativeName(reshade::api::device *device, reshade::api::resource resource);
}
