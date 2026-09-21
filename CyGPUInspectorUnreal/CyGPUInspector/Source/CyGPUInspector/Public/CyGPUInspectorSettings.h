// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"

#include "CyGPUInspectorSettings.generated.h"

// Project Settings > Plugins > CyGPUInspector. Saved per user (Saved/Config), not in the project's
// Default*.ini: the paths are those of one machine. The loading options are read at start-up,
// before this class even exists, so they apply from the next launch.
UCLASS(config = Engine, meta = (DisplayName = "CyGPUInspector"))
class CYGPUINSPECTOR_API UCyGPUInspectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCyGPUInspectorSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("CyGPUInspector", "SettingsSection", "CyGPUInspector"); }
	virtual FText GetSectionDescription() const override;
#endif

	// Load ReShade and the add-on when the editor starts (restart the editor to apply).
	UPROPERTY(config, EditAnywhere, Category = "Loading", meta = (ConfigRestartRequired = true))
	bool bLoadInEditor = true;

	// Load them in a game started with -game or packaged as Development or DebugGame. Never in
	// Shipping: the plugin does not even build that part there.
	UPROPERTY(config, EditAnywhere, Category = "Loading", meta = (ConfigRestartRequired = true))
	bool bLoadInGame = true;

	// ReShade with full add-on support, 6.8 or later. Empty: Binaries/ThirdParty/CyGPUInspector/Win64/ReShade64.dll
	// in the plugin. The standard ReShade build turns add-ons off in programs that use the network,
	// which the editor does.
	UPROPERTY(config, EditAnywhere, Category = "Loading", meta = (ConfigRestartRequired = true, FilePathFilter = "dll"))
	FFilePath ReShadePath;

	// CyGPUInspectorRS.addon64. Empty: the plugin's Binaries/ThirdParty/CyGPUInspector/Win64 folder.
	UPROPERTY(config, EditAnywhere, Category = "Loading", meta = (ConfigRestartRequired = true, FilePathFilter = "addon64"))
	FFilePath AddonPath;

	// CyGPUInspectorApp.exe, which saves the captures. Empty: looked for next to the add-on, in
	// the ..\App folder of a CyGPUInspector package.
	UPROPERTY(config, EditAnywhere, Category = "Standalone", meta = (FilePathFilter = "exe"))
	FFilePath StandalonePath;

	// Start the standalone, connected to this process, when a capture is asked for and none is
	// connected. Without it, a capture still runs, but nothing saves it.
	UPROPERTY(config, EditAnywhere, Category = "Standalone")
	bool bLaunchStandalone = true;

	UPROPERTY(config, EditAnywhere, Category = "Capture", meta = (ClampMin = 1, ClampMax = 8))
	int32 CaptureFrames = 1;

	// Every texture the last captured frame wrote to, saved as PNG and DDS by the standalone.
	UPROPERTY(config, EditAnywhere, Category = "Capture")
	bool bSaveBuffers = true;

	UPROPERTY(config, EditAnywhere, Category = "Capture")
	bool bRecordDescriptors = true;

	UPROPERTY(config, EditAnywhere, Category = "Capture")
	bool bRecordBarriers = true;

	UPROPERTY(config, EditAnywhere, Category = "Capture")
	bool bTimestampPerCommand = true;
};
