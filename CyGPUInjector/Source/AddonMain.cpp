// CyGPUInjector — ReShade add-on entry points.
//
// The other half of CyGPUInspector, and deliberately the small half. The inspector is how you
// find out what a game draws and how you change it; this is how the change is handed to someone
// else, or to yourself next week, without any of the machinery that found it.
//
// It loads mod packages exported by CyGPUInspector and applies them: a shader is replaced by the
// one you compiled, another one is skipped. No IPC, no capture, no analysis, no standalone — read
// the packages once at start-up, answer ReShade's callbacks, and stay out of the way.
//
// The name says "injector" but nothing here injects anything: ReShade loads this add-on the way
// it loads any other, and the modifications go in through the official add-on API. CyGPUInspector
// does not implement graphics injection and never will (brief section 2), and it defeats no
// protection of any kind (section 3). A game that refuses ReShade is a game this does not work on.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <imgui.h>      // must come before reshade.hpp: enables the ImGui function table
#include <reshade.hpp>

#include "Apply.hpp"
#include "Log.hpp"
#include "ModLibrary.hpp"
#include "Overlay.hpp"

#include "Config.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

extern "C" __declspec(dllexport) const char *NAME = "CyGPUInjector";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
	"Applies shader modifications exported from CyGPUInspector: replaces shaders with your own "
	"and skips the draws you disabled. Put the *.cygimod folders in CyGPUInjector/ next to the "
	"game, or set ModPath in the [CYGPUINJECTOR] section of ReShade.ini.";

namespace
{
	// Where the packages live. Next to the game by default, because that is where someone who
	// downloaded a mod will drop it without reading anything.
	std::string ResolveModRoot()
	{
		const std::string configured = cygi::config::GetString("ModPath", "");
		if (!configured.empty())
			return configured;

		wchar_t executable[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
			return "CyGPUInjector";

		std::error_code code;
		const std::filesystem::path folder =
			std::filesystem::path(executable).parent_path() / "CyGPUInjector";
		return folder.string();
	}
}

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	if (!reshade::register_addon(addon_module, reshade_module))
		return false;

	cygi::log::SetVerbose(cygi::config::GetBool("Verbose", false));

	const uint32_t loaded = cygi::Library().LoadFrom(ResolveModRoot());

	// The events are registered after the packages are read, because what is registered depends
	// on what they ask for: a package that only replaces shaders costs nothing per frame.
	cygi::RegisterModEvents();
	cygi::RegisterOverlay();

	cygi::log::Info("Add-on initialized (CyGPUInjector %s, ReShade API %u, %u package(s) loaded)",
		cygi::kVersionString, static_cast<unsigned int>(RESHADE_API_VERSION), loaded);
	return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
	cygi::UnregisterOverlay();
	cygi::UnregisterModEvents();

	cygi::log::Info("Add-on unloaded");
	reshade::unregister_addon(addon_module, reshade_module);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}
