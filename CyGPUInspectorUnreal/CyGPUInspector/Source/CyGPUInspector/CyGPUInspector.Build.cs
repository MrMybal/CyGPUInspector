// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

using System.IO;
using UnrealBuildTool;

// Captures on request: the console commands, the Blueprint nodes, the settings, and the link to
// the add-on and the standalone. Built for every target so that a project calling its Blueprint
// nodes still packages for Shipping or another platform; there, everything it does compiles out
// and the nodes answer that CyGPUInspector is not available.
public class CyGPUInspector : ModuleRules
{
	public CyGPUInspector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings" });
		// RenderCore and RHI for the markers of the main viewport's 3D render.
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "RenderCore", "RHI" });

		// The C interface of the add-on, copied from CyGPUInspectorCore/InProcessApi.h.
		PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "CyGPUInspector"));

		bool bEnabled = Target.Platform == UnrealTargetPlatform.Win64 &&
			Target.Configuration != UnrealTargetConfiguration.Shipping;
		PublicDefinitions.Add("CYGPUINSPECTOR_ENABLED=" + (bEnabled ? "1" : "0"));

		// Notices are loose files in packaged games, including when this plugin is copied alone.
		string Licences = Path.Combine(PluginDirectory, "LICENCES");
		if (Directory.Exists(Licences))
		{
			foreach (string FileName in Directory.GetFiles(Licences, "*", SearchOption.AllDirectories))
			{
				string Relative = FileName.Substring(Licences.Length + 1).Replace('\\', '/');
				RuntimeDependencies.Add("$(PluginDir)/LICENCES/" + Relative, StagedFileType.NonUFS);
			}
		}
	}
}
