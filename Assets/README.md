# Assets

The CyGPUInspector logo, by Cyberalien, part of the project and under the same licence (GNU AGPL v3
or later).

| File | What it is | Used by |
|---|---|---|
| `cygpuinspector-logo.png` | the original, 1254 × 1254, transparent background | the source of the two below |
| `cygpuinspector-logo-256.png` | 256 × 256, for the interface | the menu bar and the About window of `CyGPUInspectorApp` (embedded as a resource), the READMEs |
| `cygpuinspector.ico` | 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixels | the icon of `CyGPUInspectorApp.exe` and `CyGPUInspectorMCP.exe`, and of the application's window and taskbar button |

Both derived files are downscaled from the original with a high quality filter. After changing the
original, regenerate them the same way and rebuild: the resource scripts
(`CyGPUInspectorApp/Source/CyGPUInspectorApp.rc`, `CyGPUInspectorMCP/Source/CyGPUInspectorMCP.rc`)
embed them at link time, so nothing here is read at run time.
