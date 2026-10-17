# CyGPUInspectorApp — the standalone

*Also available in [French](fr/StandaloneApp.md).*

**State: milestones 2 to 11 implemented.**

## Appearance and navigation

The standalone uses **CyUI 1.0.0**, vendored in `ThirdParty/CyUI`, with the existing
Dear ImGui docking build. The default is the blue **Navy** theme. **View > Appearance**
offers five themes, Compact/Comfortable density and 85–175% interface scaling. These
preferences are saved under `appearance` in the existing settings JSON, preserving
language, provider settings and other keys. DPI changes follow the monitor.

The top bar selects Inspect, Captures, Shaders or AI assistant by bringing the relevant
existing tabs forward; it preserves the saved docking layout. **View > Panels** reaches
every panel. Capture frame, deep capture and connection refresh are available from the
top bar. The bottom strip shows the session, current frame and GPU time. F5 refreshes
connections and Ctrl+S saves a frame when the HLSL editor or a text input is not focused.
The editor keeps its own shortcuts. Narrow tables scroll horizontally, and preview
display controls are grouped under **Display settings**.

For repeatable visual checks without saving a layout or appearance changes:

```powershell
bin/Release/CyGPUInspectorApp.exe --capture-ui=preview.bmp --ui-theme=navy --ui-size=1280x800
```

This renders the actual D3D11 interface for at least 60 frames and two seconds, writes
the back buffer as BMP and exits. Add `--ui-scale=1.25`, `--ui-comfortable` or existing
session/shader arguments. A nonzero exit indicates a capture or ImGui layout error;
layout diagnostics, when present, are written beside the image. No screenshot of the
desktop or account/provider request is made.

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
│   ├── AssistantPanel    the AI assistant panel and the AI Analysis tab
│   ├── HlslEditorPanel   the coloured HLSL tab and the HLSL editor window
│   └── ModExportPanel    mod export, preferences
├── Assistant/
│   ├── AssistantProvider   Codex CLI, Claude Code CLI, OpenAI-compatible and local APIs
│   ├── AssistantAgent      the bounded tool loop and the shader jobs
│   └── Assistant           connection, conversation, approvals, worker threads
├── Editor/
│   ├── HlslEditor          ImGuiColorTextEdit with HLSL colouring and error marks
│   └── CompilerMessages    FXC and DXC messages as lines of the source
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
| Shader code | Disassembly / Bindings / HLSL / AI Analysis / Tools tabs. The HLSL tab shows each decompiler's reconstruction coloured as HLSL, editable, with Compile, Compile & Inject and Open in a window |
| AI assistant | provider connection, chat with the assistant that reads the frame with CyGPUInspector's tools, permission mode. See [AI.md](AI.md) |
| HLSL editor | a window of its own (View > HLSL editor): the full editor, see below |
| Mod export | what was replaced or disabled, and the package it exports to |
| Captures | saving, reopening and comparing offline captures: the comparison lists the shaders that ran in only one frame or ran differently, and the resources present in only one (§45). `compare_captures` gives it to an agent and the AI assistant, `live` standing for the frame of the connected game |
| Game profile | what is remembered about the game: sessions, shaders new or already known, the saved state of disabled and replaced shaders, the game's captures. See [Database.md](Database.md) |
| MCP | permission level and the log of every call |
| Details | Shader / Resource / Event / Log tabs, with the runtime actions, the dependency graph, a shader's GPU time (calls, total, average, max) and the buttons that time one shader, one draw or the draws into one resource on their own. See [GPUProfiling.md](GPUProfiling.md) |
| Status | IPC throughput, bytes pending, losses, add-on cost, counters on the game side |

Selecting a shader highlights its draw calls in the frame; selecting an event selects its shader
and its render target. That is the navigation loop asked for in §84. It runs the other way as
well: picking a pass, in the bars or in the list, scrolls Frame events to that pass's first
command rather than leaving the list where it was, and the resources and shaders listed beside
the bars are themselves the selection.

## Searching shaders

The search field of the Shaders panel takes plain text (a piece of the signature or of the stage) or
words that each narrow the list, all of which must match (§46):

| Word | Keeps the shaders that |
|---|---|
| `stage:cs`, `stage:ps,vs` | are of that stage (`vs ps cs gs hs ds ms as`, or the full names) |
| `writes:rgba16f`, `writes:depth`, `writes:hdr`, `writes:#12` | draw into a target of that format, a depth target, any float format, or that resource |
| `reads:depth`, `reads:#7` | have that resource bound for reading — needs a deep capture of the frame |
| `ms>0.2`, `calls>1000` | cost more than 0.2 ms of GPU time, run more than 1000 draws and dispatches in the frame |
| `format:dxil`, `sm:6` | have that byte code format, that shader model |
| `tag:bloom`, `name:tonemap` | carry that tag or name in their notes |
| `used`, `disabled`, `replaced`, `new` | ran in the frame, are disabled, are replaced, are new in this game's profile |

"Find the shaders writing RGBA16F" is `writes:rgba16f`; "compute shaders writing this resource" is
`stage:cs writes:#12`. Colour targets count for the pixel shader of a draw, a depth target for its
whole pipeline, and UAVs and shader resources come from the bindings of a deep capture. A word that
is not understood is shown, never silently ignored. The same text is the `query` of the
`find_shaders` MCP tool, so an agent and the AI assistant search the same way.

## HLSL editor

The HLSL of the selected shader is one document, shown either in the HLSL tab or in a window of its
own (**Open in a window**, or View > HLSL editor). That window is a separate Windows window with the
system's title bar: it can go to another screen and be maximised. It has

* syntax colouring, line numbers, bracket matching, undo and redo, find and replace (Ctrl+F),
  word wrap, whitespace and a minimap (ImGuiColorTextEdit, MIT);
* **Load a reconstruction**: any decompiler's output for this shader, or any AI clean-up of it;
* an entry point and a profile to **test the compilation** for (F7), prefilled with the shader's
  own profile; FXC for Shader Model 5, DXC for Shader Model 6;
* the compiler's errors and warnings **marked on their lines**, with the message as a tooltip, and
  listed under the editor: clicking one moves to its line;
* **Compile & Inject** (F5) into the running game, with the shader's own profile, and **Restore
  original**;
* opening and saving `.hlsl` files (Ctrl+O, Ctrl+S).

The document belongs to one shader. Selecting another shader loads its reconstruction, unless the
document holds edits: those are kept, and the HLSL tab offers to load the new shader instead.

A first run opens on a built layout rather than a pile of windows in a corner: what you are
connected to and what to pick from on the left, the frame in the middle, its commands underneath,
and whatever is selected on the right. It is built once, only when the ini has no layout of its
own, so it never overwrites an arrangement someone has made.

## Command line

Options for a scripted run, so it does not depend on someone clicking through the interface.
Everything they do is also reachable from the interface.

| Option | Effect |
|---|---|
| `--connect[=<pid>]` | attaches to a running session at start-up. Without a pid it takes the only session there is, and refuses rather than guessing when several are running. |
| `--level=<name>` | sets the tracking level on that session: `idle`, `tracking`, `pass-timing`, `capture`, `full-draw-timing`. Applied after `--connect`, because a level means nothing until there is a session to set it on. |
| `--search=<query>` | fills the search of the Shaders panel, in the language above; quote it when it has several words. |
| `--select-shader=<id>` | selects that shader as soon as the session has sent it. With `--decompile`, also runs the decompilers on it; with `--time`, times it on its own (Selected Shader, see [GPUProfiling.md](GPUProfiling.md)). |
| `--show=<view>[,<view>]` | brings views forward: `hlsl`, `ai-analysis` (tabs of Shader code), `hlsl-editor` (the editor window), `assistant` (the AI assistant panel). Used for the screenshots of this documentation. |

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
