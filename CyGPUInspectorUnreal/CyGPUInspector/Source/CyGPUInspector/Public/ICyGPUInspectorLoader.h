// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

// What the loader did at start-up, for the rest of the plugin to show. The loader module only
// exists in Win64 builds that are not Shipping: Get() returns null everywhere else.
class ICyGPUInspectorLoader : public IModuleInterface
{
public:
	static ICyGPUInspectorLoader* Get()
	{
		return FModuleManager::GetModulePtr<ICyGPUInspectorLoader>(TEXT("CyGPUInspectorLoader"));
	}

	virtual bool IsReShadeLoaded() const = 0;
	virtual bool IsAddonLoaded() const = 0;
	// One sentence: what was loaded from where, or why nothing was.
	virtual FString GetStateText() const = 0;
	virtual FString GetReShadePath() const = 0;
	virtual FString GetAddonPath() const = 0;
};
