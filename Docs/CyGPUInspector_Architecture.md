# CyGPUInspector — Architecture

*Also available in [French](fr/CyGPUInspector_Architecture.md).*

Version 0.1.0 — written **before** the heavy implementation, as asked for (§71 of the brief). It is
the reference: any implementation that diverges from it has to be corrected here first.

Project licence: **GPLv3**. That choice allows GPL components (the 3Dmigoto decompiler) to be
integrated directly, provided copyrights, notices and sources are preserved.

---

## 1. Overview

```
GAME PROCESS                                       STANDALONE PROCESS
─────────────────────────────────────              ─────────────────────────────────────
Game (D3D11 / D3D12)
   ↓  API calls
ReShade 6.8 (add-on build, API 20)
   ↓  reshade::api events
CyGPUInspectorRS.addon64                           CyGPUInspectorApp.exe
 ├── ShaderTracker    byte code → signature         ├── Session / connection manager
 ├── ResourceTracker  resources + access            ├── Frame model (events, shaders, res.)
 ├── FrameRecorder    event stream                  ├── Frame graph builder
 ├── GpuTimer         timestamp query heaps         ├── Resource preview (D3D11 + ImGui)
 ├── DeepCapture      the expensive one shot        ├── Shader disassembly + decompilation
 ├── RuntimeControl   disable / highlight / replace ├── Search engine
 ├── PreviewBridge    copy → shared texture         ├── Database (SQLite + blobs)
 └── Overlay          small ReShade panel           └── Control server (local pipe)
        │                                                        ▲
        │  ① metadata: shared memory ring buffer                 │  ③ JSON-RPC over stdio
        │  ② control : named pipe (request/response)             │
        │  ④ pixels  : shared GPU textures (handles)             │
        └────────────────────────────────────────────────────────┘
                                                    CyGPUInspectorMCP.exe (MCP proxy)
```

Four separate channels, for four needs that do not share constraints:

| # | Channel | Transport | Direction | Constraint |
|---|---|---|---|---|
| ① | Frame events | SPSC ring buffer in shared memory | RS → App | never blocks the game |
| ② | Commands / requests | message-mode named pipe | App → RS (+ reply) | reliable, ordered, rare |
| ③ | MCP | JSON-RPC over stdio, relayed by a named pipe | AI agent → App | off the critical path |
| ④ | Pixels | shared D3D textures + fence | RS → App | zero CPU readback |

---

## 2. Components

| Component | Nature | Contents |
|---|---|---|
| `CyGPUInspectorCore` | static C++17 library, no dependencies | IPC protocol, shared structures, SHA-256, ring buffer, session directory, format helpers, mod packages, translation |
| `CyGPUInspectorRS` | `.addon64` loaded by ReShade | tracking, capture, runtime control, minimal overlay |
| `CyGPUInjector` | `.addon64` loaded by ReShade | applies an exported mod: replaces shaders, drops draws. No IPC, no analysis |
| `CyGPUInspectorApp` | Win32 + D3D11 + Dear ImGui executable | all of the analysis, visualisation and editing |
| `CyGPUInspectorDecompiler` | static library | disassembly, decompilation backends, validation |
| `CyGPUInspectorDatabase` | static library | SQLite + blob store, game profiles, cache |
| `CyGPUInspectorMCP` | small stdio executable | MCP server, proxy to the App |
| `ThirdParty` | — | reshade (BSD-3), imgui (MIT), sqlite (public domain), 3Dmigoto decompiler (GPL-3), dxil-spirv and dxbc-spirv (MIT), SPIRV-Cross (Apache-2.0) |

The structuring rule: **the add-on only observes, transmits and carries out orders.** It contains
no database, no decompiler, no analysis and no complex interface.

---

## 3. CyGPUInspectorRS — the agent inside the game

### 3.1 Per device state

Attached with `device->set_private_data<DeviceContext>()`:

```
DeviceContext
├── api, adapter, capabilities (shared_resource, shared_fence, timestamp queries)
├── ShaderTracker      signature → ShaderRecord; pipeline handle → [signatures]
├── ResourceTracker    resource handle → ResourceRecord; view handle → resource handle
├── FrameRecorder      the current frame's events + counters
├── GpuTimer           timestamp query heaps, results read N frames later
├── DeepCapture        arming, budgeting and disarming the one-shot capture
├── RuntimeControl     the "disabled" and "highlighted" sets, active replacements
├── PreviewBridge      preview requests → shared textures
└── IpcServer          ring buffer + named pipe
```

Per command list (`set_private_data<CommandListState>`): the current bindings (RT, DSV, pipeline
per stage, index and vertex buffers, descriptors during a capture) and a local event buffer. **No
lock**: a command list is only recorded by one thread at a time. The buffer is merged into the
`FrameRecorder` on `execute_command_list` (D3D12, deferred contexts) or on `present` (the D3D11
immediate context).

### 3.2 Activity levels

The cost of the tool is driven by a global level, changeable live from the App:

| Level | Events recorded | Target cost |
|---|---|---|
| `Idle` | `init/destroy_*` only | near zero |
| `Tracking` (default) | + `create/init_pipeline`, `bind_pipeline`, RT/DSV, draw, dispatch, clear, copy | < 2 % CPU |
| `PassTiming` | + timestamps per detected pass | < 5 % |
| `Capture` (one frame) | + descriptors, viewports, states, buffers, constants | a spike accepted over one frame |
| `FullDrawTiming` | + a timestamp around every draw | diagnostic mode, turned on explicitly |

The binding callbacks are registered permanently, because the API does not allow adding and
removing handlers on the fly without racing the game's threads; what makes them free is that each
one begins with a relaxed atomic load and returns. See [CaptureModes.md](CaptureModes.md).

### 3.3 Shader signature

At `create_pipeline` / `init_pipeline`, for each shader subobject:

```
signature      = SHA-256( the whole byte code, with the DXBC container checksum zeroed )
semantic_hash  = SHA-256( the code parts only: SHEX/SHDR for DXBC, DXIL for SM6 )
```

* `signature` identifies it byte for byte: it is the key of the database and of replacements.
* `semantic_hash` groups variants that differ only in debug parts or metadata: it is the key for
  "I have already seen this shader in another build", and the key a mod package matches on.

The byte code is copied **inside the callback** (the pointer is not valid afterwards) and then sent
once to the App, which persists it. Later frames carry only the signature.

### 3.4 The flow of a frame

1. `bind_pipeline` → the command list remembers the signature per stage (O(1) lookup).
2. `bind_render_targets_and_depth_stencil` / `begin_render_pass` → the current RT/DSV set.
3. `draw*` / `dispatch*` → a compact `DrawEvent` (indices into the tables, no strings) is pushed
   into the local buffer; if `RuntimeControl` marks the shader as disabled, the callback returns
   `true` and the command is dropped.
4. `copy_*` / `clear_*` / `resolve_*` / `barrier` → resource read/write events.
5. `present` → merge the buffers, close the frame, send the event block into the ring, read the
   timestamp results of frame N-3, handle the commands received.

Events are fixed-size POD structures (see `Protocol.hpp`). A typical frame of 5 000 draws produces
about 400 KB: the 64 MB ring absorbs more than 100 frames of lag.

### 3.5 The non-blocking rule

If the ring is full, the add-on **drops** the frame's events and increments a `dropped_frames`
counter carried in the next frame's header. It never blocks, never reallocates on the hot path, and
never waits for the App. Data that cannot be replayed (the byte code of a new shader) is put in a
resend queue and retried on the next frame.

---

## 4. IPC — details

### 4.1 Session directory

A named file mapping `Local\CyGPUInspector.Sessions.v1` (created by whoever gets there first) holds
a header plus 32 slots:

```
SessionSlot { pid, api, process_name[64], ring_name[64], pipe_name[64],
              protocol_version, start_time, heartbeat_qpc, flags }
```

The add-on claims a slot with `InterlockedCompareExchange` on `pid`, and updates `heartbeat_qpc` on
every present. The App lists the slots, checks the freshness of the heartbeat **and** that the
process still exists (`OpenProcess`), and shows the list of connected applications (§64). A slot
whose process has died is recycled. Several simultaneous games are therefore supported natively
(§65).

### 4.2 Event ring buffer

`Local\CyGPUInspectorRS.<pid>.events` — SPSC, lock free:

```
RingHeader { magic, version, capacity, write_pos (atomic), read_pos (atomic),
             dropped_bytes, sequence }
Record     { size, type, sequence } + payload, 16-byte aligned
```

One producer (the present thread), one consumer (the App's IPC thread). An auto-reset Win32 `Event`
signals that data has arrived, so the App does not poll.

### 4.3 Control channel

The named pipe `\\.\pipe\CyGPUInspector\<pid>` in message mode, one client at a time. Requests are
serialised as POD structures (no JSON inside the game):

`Hello(app_pid, protocol) → HelloAck(caps)`, `SetLevel`, `CaptureFrame`,
`RequestPreview(resource_id, mip, slice, channel_mode)`, `DisableShader`, `EnableShader`,
`HighlightShader`, `ReplaceShader(signature, byte code)`, `RestoreShader`, `GetResourceBlob`,
`SetTimingMode`, `Ping`.

`Hello` carries the App's PID: indispensable for `DuplicateHandle` on NT handles (§5).

### 4.4 Robustness (§66)

* Each channel is independent: if the App dies the pipe closes, the add-on falls back to `Tracking`
  and keeps running. There is no infinite `WaitForSingleObject` on the game side.
* Every write into the ring is bounded; every read on the App side validates `size` before
  dereferencing anything (the game may have been killed mid-write).
* A `__try/__except` around event handling in the add-on turns an exception into tracking being
  switched off, never into the game crashing.
* On reconnection the App asks for a `FullSync`: the shader and resource tables are retransmitted.

---

## 5. GPU sharing for previews (§10, §12)

```
Game resource ──(copy_texture_region on the game's queue)──▶ Shared texture (RS)
                                                                   │ handle
                                                                   ▼
                                CyGPUInspectorApp: OpenSharedResource → SRV → ImGui::Image
```

* The shared texture is created by the add-on with `resource_flags::shared` (D3D11) or
  `shared_nt_handle` (D3D12), with one mip and one layer, in a format **copy compatible with the
  source**: depth formats go through their typeless family, which is both shareable and viewable as
  a shader resource. See [GPUSharing.md](GPUSharing.md) for the exact table.
* The display conversion (channel, linearised depth, reverse-Z, rescaling) happens on the
  standalone side, which already has a D3D11 device and the view: the add-on carries no shader.
* Synchronisation: `device::create_fence(..., shared_handle)` when `device_caps::shared_fence` is
  available — the add-on signals value N after the copy, the App waits for N before reading.
  Without a shared fence, two textures in alternation plus a frame counter in shared memory: the
  tearing that can remain is visually acceptable for a preview and is documented as such.
* NT handles: `DuplicateHandle` from the game's process into the App's (the PID arrives with
  `Hello`). Legacy D3D11 handles: directly openable, no duplication.
* **No CPU readback in the live path.** Readback (`copy_texture_to_buffer` + map) is used only for:
  saving a resource, exporting, an offline frame capture, analysing a structured buffer. Those are
  one-off, explicit operations.

---

## 6. Data model (App side)

```
Session        (a connected process, or an opened capture)
 ├── Shader     signature, stage, api, shader_model, size, first_seen, last_seen, tags
 ├── Pipeline   handle, [signatures], layout, RT/DSV formats, states
 ├── Resource   id, full desc, creation/destruction events, R/W counters
 ├── Frame      index, duration, [Event]
 │    ├── DrawEvent      pipeline, shaders, RT/DSV, counts, timing, bindings (during a capture)
 │    ├── DispatchEvent  groups, CS, SRV/UAV/CBV, timing
 │    └── ResourceEvent  copy / clear / resolve / barrier / map
 ├── Pass       a cluster of events (derived), name, aggregated timing
 └── Graphs     the pass graph and the resource dependency graph
```

Every list in the interface is virtualised (`ImGuiListClipper`) and indexed in the background:
50 000 draw calls and 3 000 shaders are the normal case, not the edge case (§61).

---

## 7. Derived frame graph (§6, §7)

No game supplies pass names (see [Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md) §7).
The graph is therefore **reconstructed** in three steps:

1. **Clustering** — consecutive events form a pass when they share the same render target and depth
   target set and the same class of pipeline. An RT change, a clear, a dispatch or a barrier closes
   the pass.
2. **Dependencies** — per resource: producers (draws, dispatches, copies and clears that write) and
   consumers (reads through an SRV, copy sources). Pass → pass edges come from "pass B reads a
   resource pass A wrote".
3. **Heuristic classification** — applied on invariants, with a confidence score:
   * depth only, no colour RT, early in the frame → *Depth Prepass*
   * ≥ 3 simultaneous RTs, normal/albedo/roughness formats → *GBuffer*
   * compute reading depth + normals, writing a full resolution UAV → *Lighting* / *AO*
   * a chain of HDR RTs at successively halved resolution → *Bloom downsample*
   * a single pass reading an HDR RT and writing an LDR RT just before the UI → *Tonemap*
   * small alpha-blended draws writing the back buffer at the end of the frame → *UI*
   Everything else: `Unknown Pass #N`. Names can be imposed by the user or proposed by an AI, and
   the origin of the name (`user` / `heuristic` / `ai`) is always shown (§25, §40).

The resource dependency graph answers exactly the questions in §7 (created by, written by, read by,
copied from/to, resolved from/to, destroyed) because every edge carries the index of the event that
produced it.

---

## 8. GPU profiling (§22–§25)

* A `query_heap(timestamp)` allocated per frame in a ring (3 frames in flight), read back late so
  the GPU is never waited on.
* `command_queue::get_timestamp_frequency()` turns ticks into milliseconds.
* Levels: pass / selected shader / selected draw / aggregated per shader / full draw.
* In `FullDrawTiming` there is a mark on every draw: it is a diagnostic mode that can halve the
  frame rate, announced as such in the interface (§23, §67).
* Aggregated per shader signature: calls, total, average, maximum (§24).
* The cost of the tool itself is measured and shown (§68): CPU time inside the callbacks, bytes per
  second in the ring, VRAM used by shared textures.

---

## 9. Persistent database (§17, §57, §62, §63)

```
Database/
├── cygpuinspector.db            SQLite: shaders, tags, relations, captures, analyses
├── shaders/<aa>/<signature>/    blobs (byte code, disassembly, decompilations, analyses)
│   ├── metadata.json
│   ├── original.dxbc | original.dxil
│   ├── disassembly.txt
│   ├── decompiled/<backend>-<version>.hlsl     ← one file per backend, never overwritten
│   ├── analysis.md
│   ├── tags.json
│   └── replacements/<name>.hlsl + .cso
└── Games/<Executable>/          per-game profile: known shaders, tags, captures, replacements
```

SQLite for metadata and queries, disk for the large blobs. Every expensive operation (disassembly,
decompilation, AI clean-up, classification) is cached by signature, with
`{ tool, version, date, AI model, prompt version }` so it can be regenerated later (§58). A shader
already known from another run is recognised by `signature`, otherwise matched by `semantic_hash`.

---

## 10. Runtime control (§33–§38)

| Action | Mechanism |
|---|---|
| `Disable` (shader) | the add-on returns `true` on draws and dispatches whose bound pipeline contains the signature |
| `Disable draw/dispatch` | the same mechanism, filtered on one event identifier |
| `Highlight` | the pixel shader is replaced by a generated magenta shader, or the rest is dimmed |
| `Replace` | compilation on the App side (DXC/FXC) → byte code sent over the pipe → a replacement pipeline is created → swapped at `bind_pipeline` |
| `Restore` | the replacement pipeline is destroyed and the original comes back |

The state (`disabled`, `highlighted`, tag, name, replacement) is persisted **per signature** in the
game's profile and reapplied on the next launch (§38). Exporting it for someone else is what
[ModPackages.md](ModPackages.md) covers.

---

## 11. Standalone — the interface (§59–§61)

Dear ImGui (docking) on Win32 + D3D11. The patterns kept:

* Docking layout: frame graph on the left, preview in the centre, properties on the right, tabs
  (Disassembly | HLSL | AI Analysis | Resources | Draw Calls) at the bottom.
* Virtualised lists, indices built on a background thread, asynchronous database access, lazy blob
  loading.
* The App's own D3D11 rendering also displays the game's shared textures: one device, no CPU copy.
* Multi-monitor: several windows through ImGui viewports.
* Every string goes through the translation layer, and translated labels keep the English as their
  `###` identity so one layout is shared across languages — see [Translating.md](Translating.md).

The justification (§60): the App has to open shared D3D textures and display lists of tens of
thousands of rows; ImGui + D3D11 gives both with no interop layer, in C++, with nothing to install
(Qt is not on the machine, and Avalonia or WinUI would force a C++/C# bridge across the native
core). Accepted cost: widgets we have to write ourselves (code editor, graph).

---

## 12. MCP (§48–§53)

`CyGPUInspectorMCP.exe` is a **stdio proxy**: an external agent launches it, it speaks JSON-RPC MCP
on stdin/stdout and relays to the App over the named pipe `\\.\pipe\CyGPUInspector\app`. The App
stays the source of truth, including for permissions.

Three levels, `Read Only` by default, changeable only in the App's interface. The full list is in
[MCP.md](MCP.md). Every call above `Read Only` is logged and visible in the App.

---

## 13. AI (§30, §31, §40, §56)

The App embeds no model. Two paths:

1. **An external agent over MCP** — the main path: the AI walks the data itself.
2. **Assisted jobs** — HLSL clean-up, classification, explanation, started from the interface with
   an API key the user supplies, run in the background, results cached.

Non-negotiable rules:

* Any AI output is labelled `AI reconstructed` and kept distinct from `Decompiler reconstructed`,
  both of which are kept distinct from `Original source` (never available) — §31.
* An AI classification always comes with a confidence and is presented as a hypothesis, never as a
  fact (§40).
* The AI works **on concrete data** (disassembly, bindings, graph, timings) the tool extracted; the
  analysis snapshot (§56) is exactly the bundle it is given.

---

## 14. Offline captures (§41–§45)

```
Captures/<Game>_2026-09-16_23-45/
├── capture.json        header, counters, tool versions
├── events.bin          the frame's event stream (the same format as the IPC)
├── drawstates.bin      per command state, when a deep capture was running
├── barriers.bin        resource transitions, same condition
├── shaders/            the signatures present (references the database, does not duplicate blobs)
└── analysis/           user notes and AI analyses
```

The most useful design point: **loading does not deserialise into the model**. It rebuilds the same
IPC records the add-on would have sent and passes them to `SessionModel::ApplyRecord`. An opened
capture therefore goes through exactly the same code as a live session, and there is no second
deserialiser to keep in sync.

A capture opened without the game gives access to everything except the runtime actions, which are
disabled (§43): the frame graph is rebuilt, shaders disassemble and decompile, search and MCP work.
Only GPU previews disappear, because a capture keeps no pixels. Two captures can be compared (§44,
§45): draws, dispatches, events, shaders (by signature, never by identifier, which is local to one
session), resources and GPU time.

---

## 15. Compatibility and accepted limits (§3)

* The tool works **only** where ReShade works normally. No bypass of an anti-cheat, of a protection
  or of an injection block is developed. An application that refuses ReShade is simply
  **unsupported**.
* D3D11 and D3D12 first. Vulkan and OpenGL are provided for by the architecture (the ReShade API is
  already abstract) but have no decompilation backend to start with.
* The known limits of every subsystem are collected in [Limitations.md](Limitations.md) and shown in
  the interface rather than hidden.

---

## 16. Milestones

| # | Contents | State |
|---|---|---|
| 1 | RS loads, detects the API, tracks shaders/resources/draws, sends the metadata | done |
| 2 | Standalone App: connections, shader / resource / draw lists | done |
| 3 | Shared GPU previews (RT, depth) | done |
| 4 | Frame graph + resource graph | done |
| 5 | DXBC/DXIL disassembly with a cache | done (+ reflection, + compilation) |
| 6 | Multi-backend decompilation + validation | done (4 backends shipped) |
| 7 | Runtime control (disable / highlight / replace / restore) | done |
| 8 | GPU profiling (pass → shader → draw → full) | done |
| 9 | AI (labelling, tags, annotations, snapshot) | done; API jobs still to do |
| 10 | MCP server (read only, then debug, then modification) | done |
| 11 | Offline captures, opening, comparison | done |
| 12 | The two captures: runtime timeline and deep capture | done |
| 13 | Mod export and CyGPUInjector | done |
| 14 | English as the base language, with translations of the interface and the documentation | done |
