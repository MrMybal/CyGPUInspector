// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"
#include "Templates/Function.h"

class FViewport;

struct FCyGPUInspectorCaptureOptions
{
	int32 FrameCount = 1;
	bool bSaveBuffers = true;
	bool bRecordDescriptors = true;
	bool bRecordBarriers = true;
	bool bTimestampPerCommand = true;

	// What Project Settings > Plugins > CyGPUInspector says.
	static CYGPUINSPECTOR_API FCyGPUInspectorCaptureOptions FromSettings();
};

// The link between this process and CyGPUInspector: the add-on, which records a capture, and the
// standalone, which receives it and saves it. Everything runs on the game thread.
class CYGPUINSPECTOR_API FCyGPUInspectorModule : public IModuleInterface
{
public:
	// Null when the module is not loaded.
	static FCyGPUInspectorModule* Get();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// The add-on is in the process and attached to the graphics device.
	bool IsAvailable() const;
	// The add-on module is loaded, as of the last tick: cheap enough for every view family.
	bool IsAddonPresent() const { return bAddonPresent; }

	// The viewport whose 3D render a capture is about. By default the game viewport; the editor
	// module says which one in the editor: Play In Editor while it runs, else the level viewport
	// last worked in. Game thread.
	FViewport* GetMainViewport() const;
	void SetMainViewportResolver(TFunction<FViewport*()> Resolver) { MainViewportResolver = MoveTemp(Resolver); }
	bool IsStandaloneConnected() const;
	// Waiting for the standalone, or for the add-on to finish recording.
	bool IsBusy() const;
	// One line on where things are, for a tooltip or the log.
	FText GetStatusText() const;

	// Starts the standalone first when none is connected and the settings allow it, and asks for
	// the capture once it is there. The result arrives through OnCaptureEvent.
	bool RequestCapture(const FCyGPUInspectorCaptureOptions& Options, FText& OutMessage);
	bool LaunchStandalone(FText& OutMessage);

	// Fired when a capture is armed, when it has finished and when it failed.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCaptureEvent, bool /*bSucceeded*/, const FText& /*Message*/);
	FOnCaptureEvent OnCaptureEvent;

private:
	bool Tick(float DeltaTime);
	bool ResolveAddon() const;
	bool SubmitCapture(const FCyGPUInspectorCaptureOptions& Options, FText& OutMessage);
	FString FindStandalone() const;

	void RegisterViewExtension();

	FTSTicker::FDelegateHandle TickHandle;
	TArray<class IConsoleObject*> ConsoleObjects;
	TFunction<FViewport*()> MainViewportResolver;
	TSharedPtr<class FCyGPUInspectorViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;
	bool bAddonPresent = false;

	// The add-on's exported functions, looked up again whenever the module is not the one they
	// were found in: ReShade unloads and reloads its add-ons as devices come and go.
	mutable void* AddonModule = nullptr;
	mutable void* RequestCaptureFunction = nullptr;
	mutable void* GetStatusFunction = nullptr;

	TOptional<FCyGPUInspectorCaptureOptions> PendingCapture;
	double PendingSince = 0.0;
	bool bWatchingCapture = false;
	uint64 CaptureFirstFrameBefore = 0;
	double WatchingSince = 0.0;
};
