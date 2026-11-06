---
id: architecture
title: Components, files and data flow
description: Understand the components and locate settings, captures and saved work.
section: Reference
order: 17
revision: 1965fbd02211
---

# Components, files and data flow {#components-files-and-data-flow}

## Responsibilities {#responsibilities}
ReShade loads CyGPUInspectorRS inside the supported process. That observer sends inventories, commands, bounded captures and control acknowledgements through shared memory and named pipes. The standalone owns analysis, display, persistence, provider workers and permission checks. CyGPUInspectorMCP is a local proxy; CyGPUInjector is a separate mod player. The Unreal plugin handles early loading and viewport integration.

The [data-flow diagram](doc:architecture) separates the game and analysis processes. Shared GPU images are another path: the preview opens compatible shared textures instead of sending all pixels through the event ring. Deep buffer exports involve a separate copy/readback/write workflow.

## Files to recognise {#files-to-recognise}
| Location | Purpose |
| --- | --- |
| App/Documentation | Offline reader content, languages, figures and technical references |
| App/Lang | Application interface catalogues |
| CyGPUInspectorApp.settings.json | Appearance, language and provider preferences; no persisted API key |
| CyGPUInspectorApp.ini | Docking layout |
| Database | Shader blobs, cached analyses, tags and annotations |
| Games | Per-game profile and persistent state |
| Captures | Saved frames and deep capture output |
| Images | Individually saved preview PNGs |
| .cygimod directories | Explicit mod manifests, bytecode and source provenance |

Keep full directories when backing up. Do not treat generated analysis as original game source. Inspect provenance and validation metadata when reusing cached output after changing a backend/model.

## Developer references {#developer-references}
Technical details are grouped under the related task topics. They explain capture data, profiling, shader analysis, storage and supported integration points in both languages.

See [system architecture](doc:ref-cygpuinspector-architecture), [IPC](doc:ref-ipc) and [public integration API](doc:api-overview).


## Illustrations {#illustrations}

### Where the data goes {#figure-0}

ReShade loads the observer. The standalone owns analysis, files and permission decisions.

```diagram
{"type":"flow","title":"Where the data goes","nodes":[{"id":"game","label":"Game + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Shared memory / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Standalone analysis","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profiles / mods","x":600,"y":160,"width":280},{"id":"ai","label":"AI provider / MCP agent","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```

### Two independent permission controls {#figure-1}

The built-in assistant’s modes and the external MCP tool levels are separate settings. The standalone enforces both paths.

```diagram
{"type":"flow","title":"Two independent permission controls","nodes":[{"id":"assistant","label":"Assistant: Auto / Ask / Full","x":0,"y":0,"width":280},{"id":"host","label":"Standalone permission checks","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP: Read / Debug / Modify","x":640,"y":0,"width":280},{"id":"provider","label":"CLI / API provider","x":0,"y":160,"width":280},{"id":"actions","label":"Permitted game / file actions","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```
