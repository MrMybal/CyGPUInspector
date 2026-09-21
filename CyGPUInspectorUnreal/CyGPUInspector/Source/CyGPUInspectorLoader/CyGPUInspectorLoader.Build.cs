// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

using System.IO;
using UnrealBuildTool;

// Loads ReShade and the CyGPUInspectorRS add-on at PostConfigInit, before the renderer creates its
// device. Nothing is linked against either: both are loaded at run time from paths the user chose.
public class CyGPUInspectorLoader : ModuleRules
{
	public CyGPUInspectorLoader(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects" });
		// ICyGPUInspectorLoader lives in the CyGPUInspector module, which exists in every build; this
		// one does not exist in Shipping, so nothing may depend on it.
		PrivateIncludePathModuleNames.Add("CyGPUInspector");

		// When ReShade and the add-on are put in the plugin itself, a packaged Development or
		// DebugGame build takes them along. They are the user's own copies: nothing is shipped with
		// the plugin, and the module is not built for Shipping at all (see the .uplugin).
		string ThirdParty = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "CyGPUInspector", "Win64");
		foreach (string Name in new string[] { "ReShade64.dll", "CyGPUInspectorRS.addon64" })
		{
			if (File.Exists(Path.Combine(ThirdParty, Name)))
			{
				RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/CyGPUInspector/Win64/" + Name);
			}
		}
	}
}
