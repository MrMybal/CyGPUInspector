# Capture from Unreal Engine

## Install the matching plugin
Use the release’s Unreal/UE5.3 or Unreal/UE5.8 package for the matching engine. Copy CyGPUInspector into the project’s Plugins folder. A Blueprint-only project needs the prebuilt plugin for its engine; a C++ project may build compatible source. The source integration targets Unreal 5.3+, but a binary built for another engine version is not interchangeable.

Keep the plugin’s ThirdParty ReShade/add-on binaries and license folders. In Project Settings > Plugins > CyGPUInspector, check ReShade/add-on paths and the standalone executable path. Restart the editor: the loader must run before the graphics device is created. Inspect LogCyGPUInspectorLoader if it did not load.

## Capture
Use the toolbar action, console command or Blueprint interface documented in the [full Unreal reference](help:ref-UnrealPlugin). Keep the target viewport visible and drawing. The integration marks the selected viewport’s render so deep capture can isolate its commands and choose its final image; the standalone’s runtime timeline may still describe the broader frame.

An inactive editor viewport can redraw only after a change. The plugin requests a redraw and can time out after waiting for an appropriate frame. Inspect the capture’s scope and resource names rather than assuming it includes every editor window.

## Build configurations
The loader is intended for Editor, Development and DebugGame. Shipping disables the active capture/loading path; Blueprint nodes can remain but do not enable inspection there. On other platforms they are inactive. The packaged plugin carries runtime dependencies only in the configurations that support them.

Project Settings > Load In Game controls normal game loading. -NoCyGPUInspector disables it for a run; -CyGPUInspector forces the intended supported path. Logs/configuration belong under the project’s Saved/CyGPUInspector area, not the shared engine installation.

Do not mix an old plugin binary with a newly labelled descriptor. Rebuild for the actual engine or use the matching supplied package.
