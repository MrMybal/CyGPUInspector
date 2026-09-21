// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#include "CyGPUInspector.h"

#include "CyGPUInspectorSettings.h"
#include "CyGPUInspectorViewExtension.h"
#include "ICyGPUInspectorLoader.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if CYGPUINSPECTOR_ENABLED
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "CyGPUInspectorInProcessApi.h"
#endif

#define LOCTEXT_NAMESPACE "CyGPUInspector"

DEFINE_LOG_CATEGORY_STATIC(LogCyGPUInspector, Log, All);

namespace CyGPUInspector
{
	// Renamed to a getter in 5.8; the member is all earlier versions have.
	auto& OnPostEngineInit()
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		return FCoreDelegates::OnPostEngineInit;
#else
		return FCoreDelegates::GetOnPostEngineInit();
#endif
	}

	// The standalone gets this long to start and connect before a capture waiting for it is given up.
	constexpr double StandaloneTimeoutSeconds = 30.0;
	// A capture of eight frames at a few frames per second in a heavy editor scene still fits.
	constexpr double CaptureTimeoutSeconds = 60.0;

	const TCHAR* StageName(uint32 Stage)
	{
		switch (Stage)
		{
		case 1: return TEXT("armed");
		case 2: return TEXT("capturing");
		case 3: return TEXT("finished");
		case 4: return TEXT("failed");
		default: return TEXT("idle");
		}
	}

	const TCHAR* LevelName(uint32 Level)
	{
		switch (Level)
		{
		case 0: return TEXT("idle");
		case 1: return TEXT("tracking");
		case 2: return TEXT("pass timing");
		case 3: return TEXT("full draw timing");
		default: return TEXT("?");
		}
	}
}

FCyGPUInspectorCaptureOptions FCyGPUInspectorCaptureOptions::FromSettings()
{
	const UCyGPUInspectorSettings* Settings = GetDefault<UCyGPUInspectorSettings>();
	FCyGPUInspectorCaptureOptions Options;
	Options.FrameCount = FMath::Clamp(Settings->CaptureFrames, 1, 8);
	Options.bSaveBuffers = Settings->bSaveBuffers;
	Options.bRecordDescriptors = Settings->bRecordDescriptors;
	Options.bRecordBarriers = Settings->bRecordBarriers;
	Options.bTimestampPerCommand = Settings->bTimestampPerCommand;
	return Options;
}

FCyGPUInspectorModule* FCyGPUInspectorModule::Get()
{
	return FModuleManager::GetModulePtr<FCyGPUInspectorModule>(TEXT("CyGPUInspector"));
}

void FCyGPUInspectorModule::StartupModule()
{
#if CYGPUINSPECTOR_ENABLED
	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("CyGPUInspector.Capture"),
		TEXT("Deep capture of the next frames with CyGPUInspector. Optional argument: the number of frames (1 to 8). ")
		TEXT("The other options come from Project Settings > Plugins > CyGPUInspector."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Arguments)
		{
			FCyGPUInspectorCaptureOptions Options = FCyGPUInspectorCaptureOptions::FromSettings();
			if (Arguments.Num() > 0)
			{
				Options.FrameCount = FMath::Clamp(FCString::Atoi(*Arguments[0]), 1, 8);
			}
			FText Message;
			RequestCapture(Options, Message);
			UE_LOG(LogCyGPUInspector, Display, TEXT("%s"), *Message.ToString());
		}),
		ECVF_Default));
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("CyGPUInspector.Status"),
		TEXT("Where CyGPUInspector is: ReShade, the add-on, the standalone and the last capture."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UE_LOG(LogCyGPUInspector, Display, TEXT("%s"), *GetStatusText().ToString());
		}),
		ECVF_Default));
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("CyGPUInspector.OpenStandalone"),
		TEXT("Starts CyGPUInspectorApp, connected to this process."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			FText Message;
			LaunchStandalone(Message);
			UE_LOG(LogCyGPUInspector, Display, TEXT("%s"), *Message.ToString());
		}),
		ECVF_Default));

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FCyGPUInspectorModule::Tick), 0.1f);

	// The markers of the main viewport's render need the engine; this module may load before it.
	if (GEngine != nullptr)
	{
		RegisterViewExtension();
	}
	else
	{
		PostEngineInitHandle = CyGPUInspector::OnPostEngineInit().AddRaw(this, &FCyGPUInspectorModule::RegisterViewExtension);
	}

	if (const ICyGPUInspectorLoader* Loader = ICyGPUInspectorLoader::Get())
	{
		UE_LOG(LogCyGPUInspector, Log, TEXT("%s"), *Loader->GetStateText());
	}
#endif
}

void FCyGPUInspectorModule::RegisterViewExtension()
{
#if CYGPUINSPECTOR_ENABLED
	if (!ViewExtension.IsValid() && GEngine != nullptr)
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FCyGPUInspectorViewExtension>();
	}
#endif
}

FViewport* FCyGPUInspectorModule::GetMainViewport() const
{
	if (MainViewportResolver)
	{
		return MainViewportResolver();
	}
	return GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->Viewport : nullptr;
}

void FCyGPUInspectorModule::ShutdownModule()
{
	CyGPUInspector::OnPostEngineInit().Remove(PostEngineInitHandle);
	ViewExtension.Reset();
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();
}

bool FCyGPUInspectorModule::ResolveAddon() const
{
#if CYGPUINSPECTOR_ENABLED
	// Found by name, whoever loaded it; and checked every time, because ReShade unloads its
	// add-ons when its last device goes and loads them again with the next one.
	HMODULE Addon = GetModuleHandleW(TEXT("CyGPUInspectorRS.addon64"));
	if (Addon == nullptr)
	{
		AddonModule = nullptr;
		RequestCaptureFunction = nullptr;
		GetStatusFunction = nullptr;
		return false;
	}
	if (Addon == AddonModule && RequestCaptureFunction != nullptr && GetStatusFunction != nullptr)
	{
		return true;
	}
	AddonModule = Addon;
	RequestCaptureFunction = reinterpret_cast<void*>(GetProcAddress(Addon, CYGI_EXPORT_REQUEST_CAPTURE));
	GetStatusFunction = reinterpret_cast<void*>(GetProcAddress(Addon, CYGI_EXPORT_GET_STATUS));
	if (RequestCaptureFunction == nullptr || GetStatusFunction == nullptr)
	{
		// An add-on from before the in-process interface: it captures, but only when asked by the
		// standalone.
		RequestCaptureFunction = nullptr;
		GetStatusFunction = nullptr;
		return false;
	}
	return true;
#else
	return false;
#endif
}

#if CYGPUINSPECTOR_ENABLED
namespace CyGPUInspector
{
	bool ReadStatus(void* Function, CygiStatus& Status)
	{
		FMemory::Memzero(Status);
		Status.size = sizeof(Status);
		return Function != nullptr && reinterpret_cast<PFN_CyGPUInspectorRS_GetStatus>(Function)(&Status) == 1;
	}
}
#endif

bool FCyGPUInspectorModule::IsAvailable() const
{
#if CYGPUINSPECTOR_ENABLED
	CygiStatus Status;
	return ResolveAddon() && CyGPUInspector::ReadStatus(GetStatusFunction, Status) && Status.device_count != 0;
#else
	return false;
#endif
}

bool FCyGPUInspectorModule::IsStandaloneConnected() const
{
#if CYGPUINSPECTOR_ENABLED
	CygiStatus Status;
	return ResolveAddon() && CyGPUInspector::ReadStatus(GetStatusFunction, Status) && Status.standalone_connected != 0;
#else
	return false;
#endif
}

bool FCyGPUInspectorModule::IsBusy() const
{
	return PendingCapture.IsSet() || bWatchingCapture;
}

FText FCyGPUInspectorModule::GetStatusText() const
{
#if CYGPUINSPECTOR_ENABLED
	if (!ResolveAddon())
	{
		const ICyGPUInspectorLoader* Loader = ICyGPUInspectorLoader::Get();
		return FText::FromString(Loader != nullptr ? Loader->GetStateText()
			: FString(TEXT("CyGPUInspector: the loader did not run, so ReShade and the add-on are not in this process.")));
	}
	CygiStatus Status;
	if (!CyGPUInspector::ReadStatus(GetStatusFunction, Status) || Status.device_count == 0)
	{
		return LOCTEXT("NoDevice", "CyGPUInspector: the add-on is loaded, but no graphics device was created after it. Has ReShade turned add-ons off? See its log in Saved/CyGPUInspector/ReShade.");
	}
	const FString Api = Status.graphics_api == 0xc000 ? TEXT("Direct3D 12") : Status.graphics_api == 0xb000 ? TEXT("Direct3D 11") : TEXT("another API");
	const FString Standalone = Status.standalone_connected != 0
		? FString::Printf(TEXT("the standalone is connected (PID %u)"), Status.standalone_process_id)
		: FString(TEXT("no standalone connected"));
	FString Line = FString::Printf(TEXT("CyGPUInspector: ready on %s, tracking level %s, %s, frame %llu."),
		*Api, CyGPUInspector::LevelName(Status.tracking_level), *Standalone, Status.frame_index);
	if (Status.capture_stage != 0)
	{
		Line += FString::Printf(TEXT(" Last capture: %s, first frame %llu."), CyGPUInspector::StageName(Status.capture_stage),
			Status.capture_first_frame);
	}
	return FText::FromString(Line);
#else
	return LOCTEXT("NotInThisBuild", "CyGPUInspector is not available in this build: it only runs on Windows, and never in Shipping.");
#endif
}

FString FCyGPUInspectorModule::FindStandalone() const
{
	FString Path = GetDefault<UCyGPUInspectorSettings>()->StandalonePath.FilePath;
	if (!Path.IsEmpty())
	{
		return FPaths::IsRelative(Path) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path) : Path;
	}
	// A CyGPUInspector package has Addons\ and App\ side by side: next to the add-on in use.
	if (const ICyGPUInspectorLoader* Loader = ICyGPUInspectorLoader::Get())
	{
		const FString AddonDir = FPaths::GetPath(Loader->GetAddonPath());
		if (!AddonDir.IsEmpty())
		{
			for (const FString& Candidate : {
				FPaths::Combine(AddonDir, TEXT(".."), TEXT("App"), TEXT("CyGPUInspectorApp.exe")),
				FPaths::Combine(AddonDir, TEXT("CyGPUInspectorApp.exe")) })
			{
				if (FPaths::FileExists(Candidate))
				{
					return FPaths::ConvertRelativePathToFull(Candidate);
				}
			}
		}
	}
	return FString();
}

bool FCyGPUInspectorModule::LaunchStandalone(FText& OutMessage)
{
#if CYGPUINSPECTOR_ENABLED
	const FString Path = FindStandalone();
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		OutMessage = LOCTEXT("NoStandalone", "CyGPUInspectorApp.exe was not found: set its path in Project Settings > Plugins > CyGPUInspector.");
		return false;
	}
	const FString Arguments = FString::Printf(TEXT("--connect=%u"), FPlatformProcess::GetCurrentProcessId());
	FProcHandle Process = FPlatformProcess::CreateProc(*Path, *Arguments, true, false, false, nullptr, 0,
		*FPaths::GetPath(Path), nullptr);
	if (!Process.IsValid())
	{
		OutMessage = FText::Format(LOCTEXT("LaunchFailed", "{0} could not be started."), FText::FromString(Path));
		return false;
	}
	FPlatformProcess::CloseProc(Process);
	OutMessage = FText::Format(LOCTEXT("Launched", "CyGPUInspector started ({0}), connecting to this process."), FText::FromString(Path));
	return true;
#else
	OutMessage = GetStatusText();
	return false;
#endif
}

bool FCyGPUInspectorModule::RequestCapture(const FCyGPUInspectorCaptureOptions& Options, FText& OutMessage)
{
#if CYGPUINSPECTOR_ENABLED
	if (IsBusy())
	{
		OutMessage = LOCTEXT("Busy", "A capture is already on its way.");
		return false;
	}
	if (!IsAvailable())
	{
		OutMessage = GetStatusText();
		OnCaptureEvent.Broadcast(false, OutMessage);
		return false;
	}

	// The standalone is what saves a capture: without it the frames would be recorded for nobody.
	if (!IsStandaloneConnected())
	{
		if (!GetDefault<UCyGPUInspectorSettings>()->bLaunchStandalone)
		{
			UE_LOG(LogCyGPUInspector, Warning, TEXT("No standalone is connected: the capture runs, but nothing will save it."));
			return SubmitCapture(Options, OutMessage);
		}
		FText LaunchMessage;
		if (!LaunchStandalone(LaunchMessage))
		{
			OutMessage = LaunchMessage;
			OnCaptureEvent.Broadcast(false, OutMessage);
			return false;
		}
		PendingCapture = Options;
		PendingSince = FPlatformTime::Seconds();
		OutMessage = LOCTEXT("WaitingForStandalone", "Starting CyGPUInspector: the capture begins as soon as it is connected.");
		return true;
	}
	return SubmitCapture(Options, OutMessage);
#else
	OutMessage = GetStatusText();
	return false;
#endif
}

bool FCyGPUInspectorModule::SubmitCapture(const FCyGPUInspectorCaptureOptions& Options, FText& OutMessage)
{
#if CYGPUINSPECTOR_ENABLED
	CygiStatus Before;
	CyGPUInspector::ReadStatus(GetStatusFunction, Before);

	CygiCaptureOptions Request = {};
	Request.size = sizeof(Request);
	Request.frame_count = static_cast<uint32_t>(FMath::Clamp(Options.FrameCount, 1, 8));
	Request.include_bindings = Options.bRecordDescriptors ? 1u : 0u;
	Request.include_barriers = Options.bRecordBarriers ? 1u : 0u;
	Request.per_draw_timing = Options.bTimestampPerCommand ? 1u : 0u;
	Request.include_buffers = Options.bSaveBuffers ? 1u : 0u;
	if (reinterpret_cast<PFN_CyGPUInspectorRS_RequestCapture>(RequestCaptureFunction)(&Request) != 1)
	{
		OutMessage = LOCTEXT("Refused", "The add-on refused the capture: no graphics device is attached to it.");
		OnCaptureEvent.Broadcast(false, OutMessage);
		return false;
	}

	// A viewport that only redraws when something changes would otherwise keep the capture
	// waiting: ask it for a frame.
	if (FViewport* Viewport = GetMainViewport())
	{
		Viewport->Invalidate();
	}

	bWatchingCapture = true;
	CaptureFirstFrameBefore = Before.capture_first_frame;
	WatchingSince = FPlatformTime::Seconds();
	OutMessage = FText::Format(LOCTEXT("Armed", "Capturing {0} frame(s) with CyGPUInspector."), FText::AsNumber(Request.frame_count));
	OnCaptureEvent.Broadcast(true, OutMessage);
	return true;
#else
	OutMessage = GetStatusText();
	return false;
#endif
}

bool FCyGPUInspectorModule::Tick(float DeltaTime)
{
#if CYGPUINSPECTOR_ENABLED
	bAddonPresent = GetModuleHandleW(TEXT("CyGPUInspectorRS.addon64")) != nullptr;
	if (!PendingCapture.IsSet() && !bWatchingCapture)
	{
		return true;
	}
	CygiStatus Status;
	if (!ResolveAddon() || !CyGPUInspector::ReadStatus(GetStatusFunction, Status))
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();

	if (PendingCapture.IsSet())
	{
		if (Status.standalone_connected != 0)
		{
			const FCyGPUInspectorCaptureOptions Options = PendingCapture.GetValue();
			PendingCapture.Reset();
			FText Message;
			SubmitCapture(Options, Message);
			UE_LOG(LogCyGPUInspector, Display, TEXT("%s"), *Message.ToString());
		}
		else if (Now - PendingSince > CyGPUInspector::StandaloneTimeoutSeconds)
		{
			PendingCapture.Reset();
			const FText Message = LOCTEXT("StandaloneTimeout", "CyGPUInspector did not connect within 30 seconds: no capture was made.");
			UE_LOG(LogCyGPUInspector, Warning, TEXT("%s"), *Message.ToString());
			OnCaptureEvent.Broadcast(false, Message);
		}
		return true;
	}

	// A capture of ours has finished when the add-on reports a finished one it had not before.
	if (Status.capture_first_frame != CaptureFirstFrameBefore && Status.capture_stage == 3)
	{
		bWatchingCapture = false;
		const FText Message = FText::Format(
			LOCTEXT("Finished", "Captured {0} frame(s) from frame {1}. CyGPUInspector saves them in its Images folder."),
			FText::AsNumber(Status.capture_frames_done), FText::AsNumber(Status.capture_first_frame));
		UE_LOG(LogCyGPUInspector, Display, TEXT("%s"), *Message.ToString());
		OnCaptureEvent.Broadcast(true, Message);
	}
	else if (Status.capture_stage == 4)
	{
		bWatchingCapture = false;
		const FText Message = Status.viewport_scope != 0
			? LOCTEXT("FailedScope", "The capture found no 3D render of the main viewport to record: is it visible, and drawing?")
			: LOCTEXT("Failed", "The capture failed: see the add-on's log in the CyGPUInspector standalone.");
		UE_LOG(LogCyGPUInspector, Warning, TEXT("%s"), *Message.ToString());
		OnCaptureEvent.Broadcast(false, Message);
	}
	else if (Now - WatchingSince > CyGPUInspector::CaptureTimeoutSeconds)
	{
		bWatchingCapture = false;
		const FText Message = LOCTEXT("CaptureTimeout", "The capture has not finished after a minute: is the viewport still rendering?");
		UE_LOG(LogCyGPUInspector, Warning, TEXT("%s"), *Message.ToString());
		OnCaptureEvent.Broadcast(false, Message);
	}
#endif
	return true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCyGPUInspectorModule, CyGPUInspector)
