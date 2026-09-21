# CyGPUInspectorApp — the standalone

*Also available in [French](fr/StandaloneApp.md).*

**State: milestones 2 to 11 implemented.**

## Technology

Win32 + Direct3D 11 + Dear ImGui (docking branch, MIT). The choice is reasoned, not a default:

* the standalone has to **open shared D3D textures** coming from the game and display them with no
  CPU copy; a D3D11 device in the process makes that direct, with no interop layer;
* it has to show **tens of thousands of rows** without slowing down: `ImGuiListClipper` only builds
  the visible ones;
* the core of the project is native C++ (IPC, D3D, decompilation): Qt 6 is not on the machine and
  brings its own licensing, and Avalonia or WinUI 3 would force a C++/C# bridge across the whole
  core.

Accepted cost: rich widgets (code editor, graph) have to be written here.

## Structure

```
CyGPUInspectorApp/Source
├── Main.cpp            Win32 window, D3D11 device, ImGui loop (docking + viewports)
├── App/
│   ├── Application       panels, selection, actions
│   ├── CapturePanels     the frame timeline and the deep capture panel
│   └── ModExportPanel    mod export, language preference
├── Analysis/
│   ├── FrameGraph              clustering, dependencies, pass classification
│   └── ShaderAnalysisService   disassembly and reflection on a background thread, with a cache
├── Mod/ModRecorder     what was changed in the session, ready to export
├── Mcp/McpBridge       the MCP server side and its permission levels
├── Render/
│   ├── SharedTexture           opens the game's shared texture, creates the view
│   └── PreviewRenderer         display pass: channel, linearised depth, range
└── Session/
    ├── SessionModel     in-memory model of one session (shaders, pipelines, resources, frames)
    ├── SessionClient    ring reader thread + control pipe client
    └── CaptureArchive   saving and reopening a frame without the game
```

`SessionModel` and `SessionClient` depend on neither ImGui nor D3D: that is what lets
`Tests/CyGPUInspectorIpcTests` test exactly the code the application runs.

## Panels

| Panel | Contents |
|---|---|
| Connections | detected applications, connecting, tracking level, capabilities, resynchronisation |
| Frame timeline | the history of recent frames, the passes as bars scaled by GPU time, the same passes as a sortable list, and everything about the selected one |
| Deep capture | arming the one-shot capture, bindings by register, pipeline state, barriers; the captured image and the frame's buffers, saved to the capture's folder under `Images/` and listed there |
| Frame graph | derived passes, confidence, resources written and read, shaders, renaming |
| Shaders | id, stage, format, draws/frame, GPU ms; text filter, "this frame" filter. Shader model, size and signature are columns of the same table, hidden by default and brought back from its right-click menu |
| Resources | id, kind, dimensions, format, mips, writes/frame, usage; "render targets only" filter |
| Frame events | the whole frame, virtualised; the selected shader's events are highlighted, and three filters reduce the list to the draws, to one shader's commands or to one pass's |
| Preview | the shared GPU image, RGB/R/G/B/A channel, linearised depth, range, mip, slice |
| Shader code | Disassembly / Bindings / HLSL / AI Analysis / Tools tabs |
| Mod export | what was replaced or disabled, and the package it exports to |
| Captures | saving, reopening and comparing offline captures |
| MCP | permission level and the log of every call |
| Details | Shader / Resource / Event / Log tabs, with the runtime actions and the dependency graph |
| Status | IPC throughput, bytes pending, losses, add-on cost, counters on the game side |

Selecting a shader highlights its draw calls in the frame; selecting an event selects its shader
and its render target. That is the navigation loop asked for in §84. It runs the other way as
well: picking a pass, in the bars or in the list, scrolls Frame events to that pass's first
command rather than leaving the list where it was, and the resources and shaders listed beside
the bars are themselves the selection.

A first run opens on a built layout rather than a pile of windows in a corner: what you are
connected to and what to pick from on the left, the frame in the middle, its commands underneath,
and whatever is selected on the right. It is built once, only when the ini has no layout of its
own, so it never overwrites an arrangement someone has made.

## Command line

Two options, both there so a scripted run does not depend on someone clicking through a combo box.
Everything they do is also reachable from the interface.

| Option | Effect |
|---|---|
| `--connect[=<pid>]` | attaches to a running session at start-up. Without a pid it takes the only session there is, and refuses rather than guessing when several are running. |
| `--level=<name>` | sets the tracking level on that session: `idle`, `tracking`, `pass-timing`, `capture`, `full-draw-timing`. Applied after `--connect`, because a level means nothing until there is a session to set it on. |

## Threads and locking

One mutex per session (`SessionModel::Mutex()`). The reader thread takes it per record, the
interface per panel. An important rule: **no pipe command is sent while the lock is held**, because
a command waits for the game's render thread to answer and would block the reader thread for a
whole frame.

## Layout

The layout the user builds is saved in `CyGPUInspectorApp.ini` next to the executable. ImGui
viewports allow a panel to be pulled out of the main window, for multi-monitor setups. Because
translated labels keep the English as their `###` identity, that layout is shared across languages
rather than being rebuilt per language.

**View > Reset the layout** puts every panel back where a first run has it. It is also the way back
for a panel pulled out onto a monitor that is no longer there, which otherwise stays out of sight.

## Background work

Nothing expensive runs on the interface thread:

* the **reader thread** drains the ring and feeds the model;
* the **analysis thread** disassembles and reflects shaders, with a memory and disk cache;
* the **frame graph** is rebuilt four times a second, not every frame, and the `Follow` checkbox
  freezes one frame's graph while it is being inspected.
