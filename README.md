<p align="center"><img src="Assets/cygpuinspector-logo-256.png" alt="CyGPUInspector logo" width="160"></p>

# CyGPUInspector

*Also available in [French](README.fr.md).*

A real-time and offline GPU inspector: frame debugger, shader and resource browser, profiler,
decompiler and AI-assisted analysis, with **ReShade as the capture and control agent** inside the
game process.

It sits between ReShade, RenderDoc, ShaderToggler and a shader decompiler. The part that is loaded
into the game stays small; all the analysis lives in the standalone application.

Licence: **GNU Affero General Public License v3 or later** (AGPL-3.0-or-later). See [LICENSE](LICENSE).

## Components

| Component | Role | State |
|---|---|---|
| `CyGPUInspectorCore` | IPC protocol, shared ring buffer, SHA-256, DXBC/DXIL containers, formats, translation | ✅ |
| `CyGPUInspectorRS` | ReShade add-on (`.addon64`): tracking, capture, control, timings | ✅ |
| `CyGPUInjector` | ReShade add-on that applies an exported mod, without the rest of the tool | ✅ |
| `CyGPUInspectorApp` | standalone Win32 + D3D11 + Dear ImGui: all of the analysis | ✅ milestones 2–11 |
| `CyGPUInspectorDecompiler` | disassembly, reflection, compilation, decompilation (4 backends) | ✅ milestones 5–6 |
| `CyGPUInspectorDatabase` | blobs, tags and annotations by signature; SQLite still to come | ✅ partial |
| `CyGPUInspectorMCP` | local MCP server (read only by default) | ✅ milestone 10 |
| `CyGPUInspectorUnreal` | Unreal Engine plugin (5.3+): captures from the editor and from Development / DebugGame builds | ✅ first version |

## What works today

```
Game (D3D11 / D3D12)
 → ReShade 6.8 (add-on enabled build, API 20)
 → CyGPUInspectorRS.addon64
     · shaders tracked, byte code captured, persistent SHA-256 signature
     · pipelines, resources, views, render targets
     · draws, dispatches, copies, clears, resolves, render passes
     · disable / enable a shader (the draws that use it are dropped)
     · live shader replacement: the pipeline is rebuilt and swapped at draw time
     · GPU timestamps per pass or per draw, read three frames later without ever waiting
     · deep capture on demand: every descriptor of every command, the fixed function state of
       every pipeline, the barriers — a handful of frames, then it disarms itself
     · GPU preview: a resource copied into a shared texture, with no CPU readback
     · the buffers of a deep capture: every texture the captured frame wrote to, copied on
       the GPU for the standalone to save
 → shared memory ring buffer + control named pipe
 → CyGPUInspectorApp
     · list of connected applications (several games in parallel)
     · shaders, resources, frame events, details, correlations
     · preview of a render target or a depth buffer: RGB/R/G/B/A channel, linearised depth,
       reverse-Z, adjustable range, chosen mip and slice
     · reconstructed frame graph: derived passes, dependencies between resources, renaming
     · frame timeline: the passes as bars scaled by GPU time, plus the history of the last few
       hundred frames — this is the runtime capture, and it is meant to be left running
     · deep capture panel: bindings by register, pipeline state, barriers
     · every deep capture saved to its own folder: the final image, and each buffer of the
       frame as a PNG to look at and a DDS with the data, listed in capture.json
     · DXBC and DXIL disassembly, binding reflection, persistent cache by signature
     · decompilation to HLSL by four backends, validated by recompilation
     · HLSL editing, compilation and injection into the game, magenta highlight
     · GPU time per command, per pass and per shader
     · persistent tags, names and annotations by signature, each labelled with its origin
     · offline captures: save, reopen without the game, compare two frames
     · mod export: what you replaced or disabled, written into a .cygimod folder
     · AI analysis snapshot: everything a model needs to read, in a readable directory
     · local MCP server, read only by default, every call logged
     · measured cost of the tool itself (add-on CPU, IPC throughput, losses)
```

**Sharing a modification** — what you change in a game with the inspector exports to a `.cygimod`
folder that **CyGPUInjector.addon64** replays on anyone's machine, with no standalone and no IPC:
one shader replaced by yours, another one dropped. Matching is done on the hash of the shader's
code, so it survives a restart and stops applying cleanly when the game changes the shader. See
[Docs/ModPackages.md](Docs/ModPackages.md).

**The two captures** — one runtime, continuous and cheap, and one deep, one-shot and complete —
are described in [Docs/CaptureModes.md](Docs/CaptureModes.md), including what neither of them can
do: replay the frame. That is what separates CyGPUInspector from a RenderDoc, and it follows
directly from going through the official ReShade add-on API rather than a homegrown injection.

**In Unreal** — the `CyGPUInspectorUnreal/CyGPUInspector` plugin captures the editor, and
Development or DebugGame builds of the game, from a toolbar button, a console command or a
Blueprint node. It loads ReShade (the build with full add-on support) and the add-on before the
renderer starts, the way Unreal's RenderDoc plugin loads RenderDoc, starts the standalone when
needed, and the captured buffers carry the names Unreal gives its render targets. See
[Docs/UnrealPlugin.md](Docs/UnrealPlugin.md).

Not there yet: AI jobs wired to an API (MCP covers the main path), SQLite for metadata, per-game
profiles, Vulkan and OpenGL.

## Connecting an AI agent

Point an MCP client at `bin/Release/CyGPUInspectorMCP.exe`. It speaks MCP over stdio and forwards
everything to the standalone. The permission level is set **in the standalone**, never in the
proxy: Read Only by default, then Debug Control, then Shader Modification. Every call is logged in
the MCP panel. See [Docs/MCP.md](Docs/MCP.md).

## Language

English is the base language: the code, the comments, the documentation and every string in the
interface are written in English first. A translation is a catalogue in `Lang/` that maps those
English strings onto another language, and anything it does not cover falls back to English — so a
half-finished translation never produces a blank label.

French is provided (`Lang/fr.json`). Adding a language is copying `Lang/template.json`, filling it
in and dropping it in `Lang/`; no rebuild is needed. See [Docs/Translating.md](Docs/Translating.md).

## Building

Visual Studio 2022 with the Desktop C++ workload, and the Windows SDK. Everything else is vendored
in `ThirdParty/`.

```
build.cmd
```

This configures CMake with Ninja and builds everything into `bin/Release/`. The options
`CYGI_BUILD_ADDON`, `CYGI_BUILD_INJECTOR`, `CYGI_BUILD_APP`, `CYGI_BUILD_MCP` and
`CYGI_BUILD_TESTS` each turn one part off.

## Installing

| File | Where |
|---|---|
| `CyGPUInspectorRS.addon64` | next to the game executable, with an add-on enabled build of ReShade |
| `CyGPUInjector.addon64` | same, to apply an exported mod |
| `CyGPUInspectorApp.exe` | anywhere; it finds running sessions by itself |
| `Lang/` | next to `CyGPUInspectorApp.exe` (the build copies it there) |

## Tests

```
bin/Release/CyGPUInspectorCoreTests.exe
bin/Release/CyGPUInspectorShaderTests.exe
bin/Release/CyGPUInspectorIpcTests.exe
```

Today: 4106 + 142 + 141 checks, none failing. The tests compile real shaders, run a fake session
in a separate process, and exercise the whole chain rather than merely linking it.

## Honesty about what this is not

* **Nothing here is an injection system.** CyGPUInspector uses the official ReShade add-on API and
  implements no D3D hooking, no DLL proxying and no swapchain interception. A game that refuses
  ReShade is simply out of scope.
* **Nothing here defeats a protection**, an anti-cheat, or a block on injection, and nothing hides
  the presence of ReShade or of this tool.
* **No reconstructed shader is the original source.** The original does not exist in a compiled
  shader. Everything the decompilers produce is labelled as a reconstruction, with the backend and
  the version that made it.
* **An AI result is a hypothesis**, presented with a confidence, never as a fact.

The limits that follow from all this are collected, without softening, in
[Docs/Limitations.md](Docs/Limitations.md).

## Third party

| Library | Licence | Used for |
|---|---|---|
| ReShade SDK (headers) | BSD-3-Clause | add-on API |
| Dear ImGui | MIT | standalone interface and overlays |
| 3Dmigoto (HLSL decompiler) | GPL-3.0 | the `hlsldecompiler` decompilation backend |
| dxil-spirv | MIT | the `dxil-spirv` backend, DXIL → SPIR-V |
| dxbc-spirv | MIT | the `dxbc-spirv` backend, DXBC → SPIR-V |
| SPIRV-Cross | Apache-2.0 / MIT | both SPIR-V backends, SPIR-V → HLSL |

Full detail, licence obligations and exact provenance: [THIRD-PARTY.md](THIRD-PARTY.md) and the
`ORIGIN.md` of each component under `ThirdParty/`.
