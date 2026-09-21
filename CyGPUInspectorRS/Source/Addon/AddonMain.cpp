// CyGPUInspectorRS — ReShade add-on entry points.
//
// ReShade (add-on enabled build, API 20) loads every *.addon64 found next to the game executable
// or in [ADDON] AddonPath, calls AddonInit, then dispatches the events registered below.
// NAME / DESCRIPTION are shown in the ReShade "Add-ons" tab.
//
// This add-on is the capture agent only: it tracks shaders, resources and frame events and hands
// them to CyGPUInspectorApp over IPC. Nothing is analysed here.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <imgui.h>      // must come before reshade.hpp: enables the ImGui function table
#include <reshade.hpp>

#include "Config.hpp"
#include "DeviceContext.hpp"
#include "Log.hpp"
#include "UI/Overlay.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

extern "C" __declspec(dllexport) const char *NAME = "CyGPUInspectorRS";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
	"Capture agent of CyGPUInspector: tracks shaders, pipelines, resources and frame events, and "
	"streams them to the CyGPUInspectorApp standalone for frame debugging, profiling and shader "
	"analysis.";

namespace
{
	bool g_initialized = false;
}

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	// A host that loads the add-on itself — the Unreal plugin, before the renderer starts — calls
	// AddonInit first; ReShade then calls it again for what it sees as an externally registered
	// add-on. Registering twice fails, and ReShade unloads an add-on whose AddonInit fails, so the
	// second call only confirms.
	if (g_initialized)
		return true;
	if (!reshade::register_addon(addon_module, reshade_module))
		return false;
	g_initialized = true;

	cygi::log::SetVerbose(cygi::config::GetBool("Verbose", false));

	cygi::RegisterDeviceEvents();
	cygi::RegisterOverlays();

	cygi::log::Info("Add-on initialized (CyGPUInspectorRS %s, ReShade API %u, protocol %u)",
		cygi::kVersionString, static_cast<unsigned int>(RESHADE_API_VERSION),
		static_cast<unsigned int>(cygi::kProtocolVersion));
	return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
	if (!g_initialized)
		return;
	g_initialized = false;
	cygi::UnregisterOverlays();
	cygi::UnregisterDeviceEvents();

	cygi::log::Info("Add-on unloaded");
	reshade::unregister_addon(addon_module, reshade_module);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}
