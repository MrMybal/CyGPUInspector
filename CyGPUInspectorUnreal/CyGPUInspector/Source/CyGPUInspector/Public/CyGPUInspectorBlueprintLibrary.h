// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CyGPUInspectorBlueprintLibrary.generated.h"

// For a capture at a precise moment of the game — when a bug shows up, when an effect starts.
// Safe to leave in a project: in Shipping and on other platforms the nodes do nothing and say so.
UCLASS()
class CYGPUINSPECTOR_API UCyGPUInspectorBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Deep capture of the next frames. The other options come from Project Settings > Plugins >
	// CyGPUInspector. Returns false when CyGPUInspector is not running in this process.
	UFUNCTION(BlueprintCallable, Category = "CyGPUInspector", meta = (AdvancedDisplay = "bSaveBuffers"))
	static bool CaptureFrames(int32 FrameCount = 1, bool bSaveBuffers = true);

	// ReShade and the add-on are in this process and attached to the renderer.
	UFUNCTION(BlueprintPure, Category = "CyGPUInspector")
	static bool IsCyGPUInspectorAvailable();

	// Where CyGPUInspector is, in one line.
	UFUNCTION(BlueprintPure, Category = "CyGPUInspector")
	static FString GetCyGPUInspectorStatus();

	// Starts the CyGPUInspector standalone, connected to this process.
	UFUNCTION(BlueprintCallable, Category = "CyGPUInspector")
	static bool OpenCyGPUInspector();
};
