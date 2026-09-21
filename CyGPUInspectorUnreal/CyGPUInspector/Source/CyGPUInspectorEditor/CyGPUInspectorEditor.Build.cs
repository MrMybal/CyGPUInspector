// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

using UnrealBuildTool;

// The editor side: a Capture button and a menu in the level editor toolbar, the Tools menu, and a
// notification when a capture has been made.
public class CyGPUInspectorEditor : ModuleRules
{
	public CyGPUInspectorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"Projects",
			"Settings",
			"LevelEditor",
			"CyGPUInspector",
		});
	}
}
