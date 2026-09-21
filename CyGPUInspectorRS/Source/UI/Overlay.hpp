// CyGPUInspectorRS — minimal ReShade overlay.
//
// Deliberately small: the overlay only reports the state of the agent and offers the few actions
// that make sense without a mouse-driven analysis UI. All the real work happens in
// CyGPUInspectorApp.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

namespace cygi
{
	void RegisterOverlays();
	void UnregisterOverlays();
}
