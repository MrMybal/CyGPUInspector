# Architecture — index

*Also available in [French](fr/Architecture.md).*

The full architecture document is [CyGPUInspector_Architecture.md](CyGPUInspector_Architecture.md).
This file is only a table of contents for `Docs/`.

English is the base language of this project. Every document here has a French counterpart under
[`fr/`](fr/); when the two disagree, the English is the one that is right. See
[Translating.md](Translating.md).

## Research (done before implementing)

| Document | Contents |
|---|---|
| [Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md) | what the ReShade add-on API actually guarantees, checked against the SDK |
| [Research/ShaderTooling.md](Research/ShaderTooling.md) | disassembly, decompilation, licences, honest limits |

## Subsystems

| Document | State |
|---|---|
| [ReShadeAddon.md](ReShadeAddon.md) | implemented (milestone 1) — the capture add-on |
| [StandaloneApp.md](StandaloneApp.md) | implemented (milestone 2) |
| [IPC.md](IPC.md) | implemented |
| [ShaderTracking.md](ShaderTracking.md) | implemented |
| [ResourceTracking.md](ResourceTracking.md) | implemented |
| [GPUSharing.md](GPUSharing.md) | implemented (milestone 3) |
| [FrameGraph.md](FrameGraph.md) | implemented (milestone 4) |
| [CaptureModes.md](CaptureModes.md) | implemented: the two captures, runtime and deep, and what neither of them does |
| [ShaderDecompiler.md](ShaderDecompiler.md) | implemented: disassembly, reflection, compilation, four decompilation backends |
| [ShaderReplacement.md](ShaderReplacement.md) | implemented (milestone 7) |
| [ModPackages.md](ModPackages.md) | implemented: exporting a modification and replaying it with CyGPUInjector |
| [GPUProfiling.md](GPUProfiling.md) | implemented (milestone 8) |
| [Database.md](Database.md) | blobs, tags and annotations implemented; SQLite still to come |
| [MCP.md](MCP.md) | implemented (milestone 10) |
| [AI.md](AI.md) | foundations implemented (milestone 9) |
| [Translating.md](Translating.md) | implemented: the interface and these documents, in other languages |
| [Limitations.md](Limitations.md) | living — read before reporting a bug |
