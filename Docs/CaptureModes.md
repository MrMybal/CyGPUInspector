# The two captures

*Also available in [French](fr/CaptureModes.md).*

**State: implemented and tested** (`Tests/CyGPUInspectorIpcTests`, 141 checks, including the whole
path of a deep capture: arming, per command recording, disarming itself, and a round trip through
disk).

CyGPUInspector has **two** captures. They are not two settings of one slider, they are two tools.
Asking the runtime one for "more detail" would eventually bring the game to its knees; asking the
deep one to run continuously would do it immediately.

| | **Runtime** | **Deep** |
|---|---|---|
| Duration | continuous, left running while playing | a few frames, then it stops by itself |
| Cost in the game | a few percent | high, on purpose |
| What it gives | the command stream, pass boundaries, GPU time per pass, frame history | all of that, plus every descriptor of every command, the fixed function state of every pipeline, the barriers, and a timestamp **per command** |
| Panel | `Frame timeline` | `Deep capture` |
| MCP tools | `get_frame_timeline` | `start_deep_capture`, `get_capture_state`, `get_command_state` |

---

## 1. The runtime capture — `Frame timeline`

This is the panel to leave open. It is laid out the way a profiler's timing view is:

* **the frame strip** — one bar per frame over the last few hundred, newest on the right, its
  height the CPU frame time and its colour the budget it fits in (60 fps, 30 fps, neither). A blue
  tick marks the GPU time of the same frame, so whether a frame was CPU or GPU bound reads straight
  off the gap. The frame whose timings are shown below is outlined. A spike stays visible for
  seconds instead of being a number that has already changed.
* **the timing view** — one frame on a time axis, with a ruler in milliseconds and three rows: the
  whole frame, its passes, and the commands measured inside them. Each bar sits **where it ran on
  the GPU**, not end to end: the add-on sends where every timestamp starts, not only how long it
  lasts. Wheel to zoom around the mouse, drag to pan, double-click or `Fit` to see the whole frame
  again. Clicking a bar selects the pass or the command everywhere else in the tool.
* **the pass list and the selected pass**, under the view.

**Colour is what the pass does** (graphics, compute, transfer), **length is time**. A pass with no
timestamp of its own is placed between the measurements around it and drawn **dimmed**; its length
is shown with a `~` in the list, because it is an estimate. The commands row appears only when it
says more than the passes row — at `Pass Timing` there is one measurement per pass, and the two
rows would be the same bars twice.

The view, the list, the selected pass and the MCP tool `get_frame_timeline` all read one layout
(`Analysis/FrameTimeline`), so a pass has one length wherever it is shown.

### Two views: one frame, or continuous

The panel has two views of the same capture, chosen at the top (`One frame` / `Continuous`), each
with its own layout:

* **One frame** is what is described above: the latest frame taken apart.
* **Continuous** is the layout of a profiler's timing view. Under the frame strip, every measured
  frame of the last few seconds sits one after another on the **GPU clock**: a row of frames — each
  bar the time the GPU was busy with that frame, and between two frames a hatched bar for the time
  it **waited** — then the passes of each frame under it, then its measured commands. Clicking a
  frame in the strip brings it into view and outlines it; the frames the view is showing are
  highlighted in the strip. Under the view, the frame picked is listed pass by pass, with where each
  pass starts in the frame and how long it lasts: a spike is read after the fact, not only while
  it is on screen. Wheel, drag and double-click work as in the other view; double-click or `Fit`
  goes back to following the newest frame.

What makes it continuous is that the add-on sends, for every measured frame, where it begins and
ends on the GPU clock (`FrameGpuSpanRecord`). The wait between two frames is the gap between the
closing timestamp of one and the first measured command of the next: time in which the GPU either
waited for work or ran commands that come before the first timestamp. The tooltip says both,
because the capture cannot tell them apart. When frames are missing — dropped because the
interface fell behind, or never measured — the gap says how many rather than calling it a wait.

Every frame is cut into passes with **its own** commands. Timings arrive three or four frames after
the frame they measure, so the application keeps the last eight frames whole
(`SessionModel::FrameByIndex`); a frame whose commands were already gone is cut with a later
frame's, and says so. What is kept per frame is compact — names, positions, lengths — so the last
600 frames stay inspectable long after their commands are gone (`Analysis/FrameTrack`).

An add-on that predates `FrameGpuSpanRecord` still works: frames are then laid one CPU frame apart,
and the view says the waits are not measured.

Bars only have a length at tracking level `Pass Timing` or above. Without timings the same view is
laid out by command count, the ruler says `commands` rather than `ms`, and the panel says why.

### Where the names come from

Strictly speaking, from nowhere: **no graphics API exposes its debug markers to a ReShade add-on**
(no `PIXBeginEvent`, no `ID3DUserDefinedAnnotation` — see
[Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md)). Names are **derived** from what each
command reads and writes, and every name is shown with its origin and its confidence. A name the
user gave is marked as such. An inference is never allowed to look like a fact.

That is the fundamental difference from Nsight or PIX, which read the markers the engine placed.
Where they show `ShadowMapPass` because the game wrote it, CyGPUInspector shows
`Shadow map (heuristic, 72 %)` because it worked it out.

---

## 2. The deep capture — `Deep capture`

One shot. You arm it (a button, the `File` menu, or `start_deep_capture` over MCP), the add-on
switches to the expensive path for *n* frames, then **disarms itself** — including when something
goes wrong. A capture that forgot to disarm would leave the game on the expensive path forever;
that is the one thing in this part that was not negotiable.

### What it records, per command

* **every bound descriptor**: constant buffers (with offset and size), textures, UAVs, samplers, by
  register and by stage — `b0`, `t3`, `u1`, `s0`, the way anyone reading HLSL thinks of them;
* the **vertex buffers** and the **index buffer**, with its format;
* the **viewport** and the **scissor**;
* the whole **render target** set and the depth stencil;
* the primitive **topology**;
* the **GPU time of that exact command**, not of its pass.

### What it records, per pipeline

The fixed function state read at creation: depth test and write, comparison function, stencil, cull
mode, winding, blending and write mask, target formats, sample count. Each field comes with a flag
saying whether the API **actually** reported it, so the interface can tell "depth test off" from
"the API said nothing".

### And the barriers

Every resource transition becomes an event on the timeline, between the commands it separates. It
is interesting precisely because the frame graph *derives* dependencies from reads and writes:
when the two disagree, there is something to look at.

### What it saves to disk

Every deep capture gets a folder of its own next to the standalone's executable, written without a
click:

```
Images/<game>_<date>_<time>_frame<N>/
    final.png                       what the game presented for the captured frame
    01_depth_2560x1440_d32_float_res812.png / .dds
    02_rt_2560x1440_r16g16b16a16_float_res815.png / .dds
    ...
    capture.json                    every buffer, what it was to the frame, and why some were not saved
```

**Save the buffers** (on by default) has the add-on copy, at the end of the last captured frame,
every texture that frame wrote to: render targets (all eight slots when descriptors are recorded,
the first four otherwise), depth buffers, UAV textures written by compute, copy and resolve
destinations. Each is copied on the game's own queue into a shared texture of its own — the same
mechanism as the preview, see [GPUSharing.md](GPUSharing.md) — and the standalone reads each one
back once and writes two files:

* a **DDS** with the data itself, in the game's format (a depth buffer through its typeless
  family: `d32_float` is saved as `r32_float`), for tools that need the real values — HDR colour,
  depth, normals, motion vectors;
* a **PNG** to look at: colour as it is, widened past 0–1 when the texture really holds values out
  there (HDR, signed vectors, the extreme half percent ignored); depth stretched between its nearest
  and farthest values, the cleared ones left out, or a raw depth buffer is one flat grey. The range
  used is written in `capture.json`.

The files are named in the order the frame first used the textures, which is the order a person
reads a frame in. The **Images** tab of the Deep capture panel, which opens by itself when a capture
finishes, lists them beside the captured image; a click shows one, read back from its PNG, so what
is on screen is exactly what was written. **Open folder** opens the folder. The recorded commands
are in the **Commands** tab next to it.

Three things to know:

* **each buffer is as it was at the *end* of the frame.** A texture several passes render into in
  turn shows the last of them, and a texture reused for something else later in the frame shows
  that. Seeing the state between two passes would take a copy in the middle of the game's command
  lists; that is not done;
* **on Direct3D 12 a texture is only copied from a known state.** Copying means taking it out of the
  state it is in, and on D3D12 that state is only known from the barriers the game recorded, which
  is why saving buffers switches barrier recording on. A texture no barrier was seen for is listed
  as *not copied* rather than guessed: a wrong guess is undefined behaviour, and on a compressed
  depth buffer it shows;
* **the copies cost the game GPU memory until they are saved**: at most 64 buffers and 1.5 GiB. The
  standalone reads them one per interface frame, a few frames after the capture so the GPU has
  certainly made them, and then tells the add-on to let go. A standalone that never asks gets them
  released after a minute. Only mip 0 of layer 0 is copied, and multisampled textures are listed but
  not copied.

### Guard rails

* a **per frame ceiling** (60 000 commands by default): one pathological frame cannot blow up the
  game's memory;
* a **per command ceiling** (256 descriptors); beyond it, the count of dropped descriptors is
  published rather than passed over in silence;
* the captured frame is **kept apart** from the current one. Without that, the frame you just asked
  the game for would be replaced a sixtieth of a second later.

---

## 3. What neither of them does: replay

This has to be said plainly, because it is the question anyone who knows RenderDoc will ask.

A frame debugger like **RenderDoc serialises the command stream and re-executes it on its own
device**. That is what buys it pixel history, stepping through a draw, changing a parameter and
seeing the result immediately, the mesh viewer after the vertex shader. All of it follows from
replay, not from observation.

CyGPUInspector **observes the real frame through the official ReShade add-on API and never injects
a device of its own**. That is a decision in the specification (§2), not a hole to be patched: the
project reimplements no D3D hooking, no DLL proxying, no swapchain interception. The consequence is
mechanical: it reports what happened, it does not replay it.

Concretely, these are **out of reach** and will stay so for as long as that constraint holds:

* pixel history ("which draws touched this pixel, and with what value");
* stepping with re-execution of a single command;
* viewing the geometry after the vertex shader;
* changing a state and re-rendering the same frame.

What **remains possible**, and is delivered: seeing the full state of every command, replacing a
shader live and watching the next frame, disabling a shader to see what disappears, previewing any
resource through a shared texture with no CPU readback.

One more limit, this one purely technical: a Direct3D 12 **descriptor table** (or a Vulkan
descriptor set) is bound by *handle*, and the add-on API does not hand out the contents of the
heap. Those descriptors are not readable. The command is then marked **incomplete** in the
interface, rather than showing an empty list that would read as "nothing was bound".

---

## 4. What it costs the game

The runtime capture is meant to be left on. The deep capture is not, and the way that is guaranteed
is worth explaining, because it is the delicate point of the whole add-on.

The callbacks that follow bindings (`push_descriptors`, `bind_vertex_buffers`, `bind_viewports`,
`barrier`…) are **registered once and for all** with ReShade: the API does not let an add-on add
and remove handlers on the fly without racing the game's threads. What makes them free outside a
capture is their first line: a relaxed atomic load that says no capture is running, and an
immediate return. The price in runtime mode is one predictable branch per binding call, and nothing
else.

The cost of the tool itself is measured and published every frame (`addon_cpu_ms`) and shown in the
panel: it does not have to be taken on trust.

---

## 5. Where this lives in the code

| Piece | File |
|---|---|
| Protocol vocabulary (`CaptureMode`, `DrawStateRecord`, `BarrierEntry`, `PipelineStateRecord`) | `CyGPUInspectorCore/Include/CyGPUInspectorCore/Protocol.hpp` |
| Deep capture state machine, arming and disarming | `CyGPUInspectorRS/Source/Tracking/DeepCapture.{hpp,cpp}` |
| Shadowing the Direct3D state during a capture | `CyGPUInspectorRS/Source/Tracking/StateShadow.{hpp,cpp}` |
| ReShade callbacks and publishing | `CyGPUInspectorRS/Source/Addon/DeviceContext.cpp` |
| Standalone model, frame history, the captured frame | `CyGPUInspectorApp/Source/Session/SessionModel.{hpp,cpp}` |
| The two panels | `CyGPUInspectorApp/Source/App/CapturePanels.cpp` |
| The buffers, in the game: choosing, copying, releasing | `CyGPUInspectorRS/Source/Preview/CaptureBuffers.{hpp,cpp}` |
| The buffers, on disk: read back, DDS, PNG, `capture.json` | `CyGPUInspectorApp/Source/Render/CaptureBufferWriter.{hpp,cpp}` |
| The capture's folder, final image, file list | `CyGPUInspectorApp/Source/App/CaptureFiles.cpp` |
| Persistence (`drawstates.bin`, `barriers.bin`) | `CyGPUInspectorApp/Source/Session/CaptureArchive.cpp` |
