---
id: unreal
title: Capture from Unreal Engine
description: Install the Unreal plugin and capture an editor or development viewport.
section: User guide
order: 14
parent: installation
revision: d455c6459e50
---

# Capture from Unreal Engine {#capture-from-unreal-engine}

## Install the matching plugin {#install-the-matching-plugin}
Use the release’s Unreal/UE5.3 or Unreal/UE5.8 package for the matching engine. Copy CyGPUInspector into the project’s Plugins folder. A Blueprint-only project needs the prebuilt plugin for its engine; a C++ project may build compatible source. The source integration targets Unreal 5.3+, but a binary built for another engine version is not interchangeable.

Keep the plugin’s ThirdParty ReShade/add-on binaries and license folders. In Project Settings > Plugins > CyGPUInspector, check ReShade/add-on paths and the standalone executable path. Restart the editor: the loader must run before the graphics device is created. Inspect LogCyGPUInspectorLoader if it did not load.

## Capture {#capture}
Use the toolbar action, console command or Blueprint interface documented in the [full Unreal reference](doc:ref-unrealplugin). Keep the target viewport visible and drawing. The integration marks the selected viewport’s render so deep capture can isolate its commands and choose its final image; the standalone’s runtime timeline may still describe the broader frame.

An inactive editor viewport can redraw only after a change. The plugin requests a redraw and can time out after waiting for an appropriate frame. Inspect the capture’s scope and resource names rather than assuming it includes every editor window.

## Build configurations {#build-configurations}
The loader is intended for Editor, Development and DebugGame. Shipping disables the active capture/loading path; Blueprint nodes can remain but do not enable inspection there. On other platforms they are inactive. The packaged plugin carries runtime dependencies only in the configurations that support them.

Project Settings > Load In Game controls normal game loading. -NoCyGPUInspector disables it for a run; -CyGPUInspector forces the intended supported path. Logs/configuration belong under the project’s Saved/CyGPUInspector area, not the shared engine installation.

Do not mix an old plugin binary with a newly labelled descriptor. Rebuild for the actual engine or use the matching supplied package.


## Illustrations {#illustrations}

### Where the data goes {#figure-0}

ReShade loads the observer. The standalone owns analysis, files and permission decisions.

```diagram
{"type":"flow","title":"Where the data goes","nodes":[{"id":"game","label":"Game + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Shared memory / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Standalone analysis","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profiles / mods","x":600,"y":160,"width":280},{"id":"ai","label":"AI provider / MCP agent","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```

### Choose the right capture {#figure-1}

Live observation, a saved single frame and deep buffer export serve different purposes; none replays a frame.

```diagram
{"type":"flow","title":"Choose the right capture","nodes":[{"id":"live","label":"Live history","x":0,"y":0,"width":280},{"id":"saved","label":"Saved single frame","x":300,"y":0,"width":280},{"id":"deep","label":"Deep command state","x":600,"y":0,"width":280},{"id":"bounded","label":"Bounded observations","x":0,"y":160,"width":280},{"id":"offline","label":"Offline, no live pixels","x":300,"y":160,"width":280},{"id":"export","label":"Separate PNG / DDS export","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```
