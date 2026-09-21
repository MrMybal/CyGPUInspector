// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
//
// Puts ReShade and the CyGPUInspectorRS add-on into this process before the renderer starts.
//
// ReShade only sees a graphics device it was there to see created. In a game it is installed next
// to the executable as dxgi.dll and Windows loads it; the editor is one executable shared by every
// project, so instead this module loads it at PostConfigInit, from a path the user chose — the
// same moment and the same way Unreal's own RenderDoc plugin loads renderdoc.dll. From then on
// everything graphics related is ReShade's own doing: this module hooks, patches and intercepts
// nothing. It then puts the add-on in ReShade's add-on folder, and ReShade loads it like any other
// add-on it finds there.
//
// Nothing happens in a commandlet, a server, with -nullrhi, or when the settings say not to. The
// module is not built for Shipping at all (see the .uplugin).

#include "ICyGPUInspectorLoader.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <Psapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCyGPUInspectorLoader, Log, All);

namespace CyGPUInspectorLoader
{
	// The settings live in UCyGPUInspectorSettings, in the CyGPUInspector module. That module is
	// not loaded yet and UObjects do not exist yet at PostConfigInit, so they are read here
	// straight from the configuration, which is all that is available this early.
	const TCHAR* const SettingsSection = TEXT("/Script/CyGPUInspector.CyGPUInspectorSettings");
	const TCHAR* const AddonFileName = TEXT("CyGPUInspectorRS.addon64");

	bool ReadBool(const TCHAR* Key, bool bDefault)
	{
		bool bValue = bDefault;
		if (GConfig != nullptr)
		{
			GConfig->GetBool(SettingsSection, Key, bValue, GEngineIni);
		}
		return bValue;
	}

	// An FFilePath is stored as (FilePath="..."); a plain string is accepted too.
	FString ReadPath(const TCHAR* Key)
	{
		FString Raw;
		if (GConfig == nullptr || !GConfig->GetString(SettingsSection, Key, Raw, GEngineIni))
		{
			return FString();
		}
		FString Path = Raw;
		const int32 Start = Raw.Find(TEXT("FilePath=\""));
		if (Start != INDEX_NONE)
		{
			const int32 From = Start + 10;
			const int32 End = Raw.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			Path = End != INDEX_NONE ? Raw.Mid(From, End - From) : FString();
		}
		Path.TrimStartAndEndInline();
		if (!Path.IsEmpty() && FPaths::IsRelative(Path))
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		}
		return Path;
	}

	// The plugin's own Binaries/ThirdParty folder, where ReShade and the add-on can simply be put.
	FString PluginThirdPartyDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CyGPUInspector"));
		if (!Plugin.IsValid())
		{
			return FString();
		}
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("CyGPUInspector"), TEXT("Win64")));
	}

	// Command line first, then the settings, then the plugin's folder.
	FString ResolvePath(const TCHAR* CommandLineKey, const TCHAR* SettingKey, const TCHAR* DefaultName)
	{
		FString Path;
		if (FParse::Value(FCommandLine::Get(), CommandLineKey, Path) && !Path.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(Path);
		}
		Path = ReadPath(SettingKey);
		if (!Path.IsEmpty())
		{
			return Path;
		}
		const FString Folder = PluginThirdPartyDir();
		if (!Folder.IsEmpty())
		{
			const FString Candidate = FPaths::Combine(Folder, DefaultName);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}
}

class FCyGPUInspectorLoaderModule : public ICyGPUInspectorLoader
{
public:
	virtual void StartupModule() override
	{
#if PLATFORM_WINDOWS
		FString Reason;
		if (!ShouldLoad(Reason))
		{
			StateText = Reason;
			UE_LOG(LogCyGPUInspectorLoader, Log, TEXT("%s"), *StateText);
			return;
		}
		if (LoadReShade())
		{
			LoadAddon();
		}
		UE_LOG(LogCyGPUInspectorLoader, Log, TEXT("%s"), *StateText);
#else
		StateText = TEXT("CyGPUInspector only runs on Windows.");
#endif
	}

	// ReShade and the add-on stay until the process exits, as they would in a game: the device
	// they are attached to outlives every module, and unloading them under it would be worse.
	virtual void ShutdownModule() override {}

	virtual bool IsReShadeLoaded() const override { return ReShadeModule != nullptr; }
	virtual bool IsAddonLoaded() const override
	{
#if PLATFORM_WINDOWS
		return GetModuleHandleW(CyGPUInspectorLoader::AddonFileName) != nullptr;
#else
		return false;
#endif
	}
	virtual FString GetStateText() const override { return StateText; }
	virtual FString GetReShadePath() const override { return ReShadePath; }
	virtual FString GetAddonPath() const override { return AddonPath; }

private:
#if PLATFORM_WINDOWS
	bool ShouldLoad(FString& OutReason) const
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		if (FParse::Param(CommandLine, TEXT("NoCyGPUInspector")))
		{
			OutReason = TEXT("CyGPUInspector: disabled on the command line (-NoCyGPUInspector).");
			return false;
		}
		// Nothing to capture without a renderer, and nothing to load into a tool that only cooks.
		FString Commandlet;
		if (IsRunningCommandlet() || FParse::Value(CommandLine, TEXT("-run="), Commandlet))
		{
			OutReason = TEXT("CyGPUInspector: not loaded in a commandlet.");
			return false;
		}
		if (FParse::Param(CommandLine, TEXT("nullrhi")) || FParse::Param(CommandLine, TEXT("server")))
		{
			OutReason = TEXT("CyGPUInspector: not loaded without a renderer (-nullrhi, -server).");
			return false;
		}
		if (FParse::Param(CommandLine, TEXT("CyGPUInspector")))
		{
			return true;
		}

#if WITH_EDITOR
		const bool bEditor = !FParse::Param(CommandLine, TEXT("game"));
#else
		const bool bEditor = false;
#endif
		const bool bEnabled = CyGPUInspectorLoader::ReadBool(bEditor ? TEXT("bLoadInEditor") : TEXT("bLoadInGame"), true);
		if (!bEnabled)
		{
			OutReason = bEditor
				? TEXT("CyGPUInspector: not loaded in the editor (Project Settings > Plugins > CyGPUInspector).")
				: TEXT("CyGPUInspector: not loaded in the game (Project Settings > Plugins > CyGPUInspector).");
		}
		return bEnabled;
	}

	static HMODULE FindLoadedReShade()
	{
		HMODULE Modules[1024];
		DWORD Needed = 0;
		if (!EnumProcessModules(GetCurrentProcess(), Modules, sizeof(Modules), &Needed))
		{
			return nullptr;
		}
		const DWORD Count = FMath::Min<DWORD>(Needed / sizeof(HMODULE), UE_ARRAY_COUNT(Modules));
		for (DWORD Index = 0; Index < Count; ++Index)
		{
			if (GetProcAddress(Modules[Index], "ReShadeRegisterAddon") != nullptr)
			{
				return Modules[Index];
			}
		}
		return nullptr;
	}

	static bool ExportsReShade(const FString& Path)
	{
		// Mapped without running it, only to look at its exports.
		HMODULE Module = LoadLibraryExW(*Path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
		if (Module == nullptr)
		{
			return false;
		}
		const bool bReShade = GetProcAddress(Module, "ReShadeRegisterAddon") != nullptr;
		FreeLibrary(Module);
		return bReShade;
	}

	bool LoadReShade()
	{
		// Already there: installed next to the executable and loaded by Windows, or loaded by
		// another plugin. One ReShade per process; a second one would fight the first.
		ReShadeModule = FindLoadedReShade();
		if (ReShadeModule != nullptr)
		{
			ReShadePath = ModulePath(ReShadeModule);
			return true;
		}

		// Installed next to the executable but not loaded yet, because nothing has asked for the
		// graphics API yet: load that one now, so it is in place before the device is created.
		const FString ExecutableDir = FPaths::GetPath(FString(FPlatformProcess::ExecutablePath()));
		for (const TCHAR* Proxy : { TEXT("dxgi.dll"), TEXT("d3d12.dll"), TEXT("d3d11.dll") })
		{
			const FString Candidate = FPaths::Combine(ExecutableDir, Proxy);
			if (FPaths::FileExists(Candidate) && ExportsReShade(Candidate))
			{
				ReShadeModule = LoadLibraryW(*Candidate);
				if (ReShadeModule != nullptr)
				{
					ReShadePath = Candidate;
					return true;
				}
			}
		}

		ReShadePath = CyGPUInspectorLoader::ResolvePath(TEXT("CyGPUInspectorReShade="), TEXT("ReShadePath"), TEXT("ReShade64.dll"));
		if (ReShadePath.IsEmpty() || !FPaths::FileExists(ReShadePath))
		{
			StateText = FString::Printf(TEXT("CyGPUInspector: ReShade not found%s. Put ReShade64.dll (the build with full add-on support) in %s, or set its path in Project Settings > Plugins > CyGPUInspector."),
				ReShadePath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" at %s"), *ReShadePath), *CyGPUInspectorLoader::PluginThirdPartyDir());
			return false;
		}

		// ReShade keeps its configuration and its log in its base path, by default the folder of
		// the executable: the engine's own Binaries folder, shared by every project. This project's
		// Saved folder is where they belong. A value the user set themselves is left alone.
		FString BasePath = FPlatformMisc::GetEnvironmentVariable(TEXT("RESHADE_BASE_PATH_OVERRIDE"));
		if (BasePath.IsEmpty())
		{
			BasePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CyGPUInspector"), TEXT("ReShade")));
			FPlatformMisc::SetEnvironmentVar(TEXT("RESHADE_BASE_PATH_OVERRIDE"), *BasePath);
		}
		// Loaded under its own name rather than as dxgi.dll, ReShade only starts where it finds its
		// configuration: that keeps a globally installed ReShade out of every other program. An
		// empty one is all it needs; it fills in the rest itself.
		IFileManager::Get().MakeDirectory(*BasePath, true);
		const FString IniPath = FPaths::Combine(BasePath, TEXT("ReShade.ini"));
		if (!FPaths::FileExists(IniPath))
		{
			FFileHelper::SaveStringToFile(TEXT("[GENERAL]\r\n"), *IniPath);
		}

		ReShadeModule = LoadLibraryW(*ReShadePath);
		if (ReShadeModule == nullptr || GetProcAddress(ReShadeModule, "ReShadeRegisterAddon") == nullptr)
		{
			const uint32 Error = static_cast<uint32>(GetLastError());
			StateText = Error == ERROR_DLL_INIT_FAILED
				? FString::Printf(TEXT("CyGPUInspector: ReShade (%s) declined to start; its log in %s says why."), *ReShadePath, *BasePath)
				: FString::Printf(TEXT("CyGPUInspector: %s could not be loaded as ReShade (Windows error %u). It has to be a ReShade build with add-on support."), *ReShadePath, Error);
			ReShadeModule = nullptr;
			return false;
		}
		ReShadeBasePath = BasePath;
		return true;
	}

	void LoadAddon()
	{
		// Already loaded: ReShade found it in its own add-on folder.
		if (HMODULE Loaded = GetModuleHandleW(CyGPUInspectorLoader::AddonFileName))
		{
			AddonPath = ModulePath(Loaded);
			StateText = FString::Printf(TEXT("CyGPUInspector: ReShade %s, add-on %s."), *ReShadePath, *AddonPath);
			return;
		}

		AddonPath = CyGPUInspectorLoader::ResolvePath(TEXT("CyGPUInspectorAddon="), TEXT("AddonPath"), CyGPUInspectorLoader::AddonFileName);
		if (AddonPath.IsEmpty() || !FPaths::FileExists(AddonPath))
		{
			StateText = FString::Printf(TEXT("CyGPUInspector: ReShade loaded from %s, but the add-on was not found. Put CyGPUInspectorRS.addon64 in %s, or set its path in Project Settings > Plugins > CyGPUInspector."),
				*ReShadePath, *CyGPUInspectorLoader::PluginThirdPartyDir());
			return;
		}

		// ReShade loads the add-ons of its base path whenever it starts its first device, and
		// unloads them all when its last device goes. Unreal creates a device on every adapter
		// before it keeps one, so ReShade goes through that cycle once at start-up, and an add-on
		// registered from outside does not survive it. So the add-on goes where ReShade looks, and
		// ReShade loads it itself, as often as it needs to.
		if (ReShadeBasePath.IsEmpty())
		{
			StateText = FString::Printf(TEXT("CyGPUInspector: ReShade %s was installed by hand next to the executable: put CyGPUInspectorRS.addon64 in its add-on folder (next to it, by default)."),
				*ReShadePath);
			return;
		}
		const FString Installed = FPaths::Combine(ReShadeBasePath, CyGPUInspectorLoader::AddonFileName);
		if (!FPaths::IsSamePath(Installed, AddonPath))
		{
			IFileManager& Files = IFileManager::Get();
			const bool bSame = Files.FileSize(*Installed) == Files.FileSize(*AddonPath) &&
				Files.GetTimeStamp(*Installed) == Files.GetTimeStamp(*AddonPath);
			if (!bSame && Files.Copy(*Installed, *AddonPath, true, true) != COPY_OK)
			{
				// Most likely held by another editor running the same project: its copy is used.
				UE_LOG(LogCyGPUInspectorLoader, Warning, TEXT("CyGPUInspector: could not refresh %s from %s; the copy already there is used."),
					*Installed, *AddonPath);
			}
		}
		StateText = FString::Printf(TEXT("CyGPUInspector: ReShade %s; add-on %s, which ReShade loads from %s when the renderer starts."),
			*ReShadePath, *AddonPath, *Installed);
	}

	static FString ModulePath(HMODULE Module)
	{
		TCHAR Buffer[MAX_PATH] = {};
		GetModuleFileNameW(Module, Buffer, MAX_PATH);
		return FString(Buffer);
	}

	HMODULE ReShadeModule = nullptr;
	// Where ReShade keeps its configuration and looks for add-ons, when this module loaded it.
	FString ReShadeBasePath;
#endif

	FString StateText = TEXT("CyGPUInspector: not started.");
	FString ReShadePath;
	FString AddonPath;
};

IMPLEMENT_MODULE(FCyGPUInspectorLoaderModule, CyGPUInspectorLoader)
