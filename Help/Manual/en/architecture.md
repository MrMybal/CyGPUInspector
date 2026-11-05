# Components, files and data flow

## Responsibilities
ReShade loads CyGPUInspectorRS inside the supported process. That observer sends inventories, commands, bounded captures and control acknowledgements through shared memory and named pipes. The standalone owns analysis, display, persistence, provider workers and permission checks. CyGPUInspectorMCP is a local proxy; CyGPUInjector is a separate mod player. The Unreal plugin handles early loading and viewport integration.

The [data-flow diagram](figure:0) separates the game and analysis processes. Shared GPU images are another path: the preview opens compatible shared textures instead of sending all pixels through the event ring. Deep buffer exports involve a separate copy/readback/write workflow.

## Files to recognise
| Location | Purpose |
| --- | --- |
| App/Help | Offline reader content, languages, figures and technical references |
| App/Lang | Application interface catalogues |
| CyGPUInspectorApp.settings.json | Appearance, language and provider preferences; no persisted API key |
| CyGPUInspectorApp.ini | Docking layout |
| Database | Shader blobs, cached analyses, tags and annotations |
| Games | Per-game profile and persistent state |
| Captures | Saved frames and deep capture output |
| Images | Individually saved preview PNGs |
| .cygimod directories | Explicit mod manifests, bytecode and source provenance |

Keep full directories when backing up. Do not treat generated analysis as original game source. Inspect provenance and validation metadata when reusing cached output after changing a backend/model.

## Developer references
The Technical reference index contains the bundled documents on architecture, IPC, capture modes, GPU sharing/profiling, shaders, resource tracking, database, AI/MCP, Unreal, limitations and translation. English is the base; French equivalents are provided. Source integration and dependency details are separated from the task-oriented guide.

See [architecture](help:ref-Architecture), [IPC](help:ref-IPC) and [CyUI integration](help:ref-CyUIIntegration).
