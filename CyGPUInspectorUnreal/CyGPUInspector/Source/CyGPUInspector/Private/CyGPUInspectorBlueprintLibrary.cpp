// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#include "CyGPUInspectorBlueprintLibrary.h"

#include "CyGPUInspector.h"

bool UCyGPUInspectorBlueprintLibrary::CaptureFrames(int32 FrameCount, bool bSaveBuffers)
{
	FCyGPUInspectorModule* Module = FCyGPUInspectorModule::Get();
	if (Module == nullptr)
	{
		return false;
	}
	FCyGPUInspectorCaptureOptions Options = FCyGPUInspectorCaptureOptions::FromSettings();
	Options.FrameCount = FMath::Clamp(FrameCount, 1, 8);
	Options.bSaveBuffers = bSaveBuffers;
	FText Message;
	return Module->RequestCapture(Options, Message);
}

bool UCyGPUInspectorBlueprintLibrary::IsCyGPUInspectorAvailable()
{
	const FCyGPUInspectorModule* Module = FCyGPUInspectorModule::Get();
	return Module != nullptr && Module->IsAvailable();
}

FString UCyGPUInspectorBlueprintLibrary::GetCyGPUInspectorStatus()
{
	const FCyGPUInspectorModule* Module = FCyGPUInspectorModule::Get();
	return Module != nullptr ? Module->GetStatusText().ToString() : FString(TEXT("CyGPUInspector is not loaded."));
}

bool UCyGPUInspectorBlueprintLibrary::OpenCyGPUInspector()
{
	FCyGPUInspectorModule* Module = FCyGPUInspectorModule::Get();
	FText Message;
	return Module != nullptr && Module->LaunchStandalone(Message);
}
