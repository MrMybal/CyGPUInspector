# GPU sharing for previews

*Also available in [French](fr/GPUSharing.md).*

**State: implemented and tested end to end** (`Tests/CyGPUInspectorIpcTests`), with the limits
listed in §6.

## Goal

Show a texture from the game inside the standalone **without a CPU readback** (§10 of the brief).

```
Game resource ──(copy on the game's queue)──▶ Shared texture (add-on)
                                                    │ handle
                                                    ▼
                  CyGPUInspectorApp: OpenSharedResource → SRV → ImGui::Image
```

What is deliberately **not** done for the live path: `GPU → CPU readback → IPC → standalone → GPU`.

## What is implemented

* `PreviewBridge` on the add-on side: one request from the standalone, one shared texture created
  on the game's device, one copy per frame on its own queue, the handle sent once.
* `SharedTexture` on the standalone side: open the handle (legacy or NT), create an SRV, display it
  directly with `ImGui::Image`.
* `Preview` panel: fit to window or zoom, mip and slice selection, stop. Double-clicking a resource
  in the list starts its preview.
* Explicit diagnosis: `PreviewStatus` says *why* a preview is unavailable (resource destroyed,
  format with no D3D11 equivalent, multisampled, sharing unsupported, handle duplication refused)
  instead of showing a black rectangle.

The end-to-end test creates a shared texture in one process, opens it in another, builds the SRV
**and reads the pixels back** to prove it really is the same GPU memory.

## Creating the shared texture

`device::create_resource(desc, nullptr, state, &resource, &shared_handle)` with:

* `resource_flags::shared` on D3D11 → a *legacy* DXGI handle, openable directly by another process
  on the same adapter;
* `resource_flags::shared | resource_flags::shared_nt_handle` on D3D12 → an NT handle, which has to
  be **duplicated** into the standalone's process with `DuplicateHandle`. The PID that needs is
  received in `HelloRequest`.

**Both bits on D3D12.** `shared` is what makes a resource shared at all; `shared_nt_handle` only
says which kind of handle. They are separate bits (0x2 and 0x800), and ReShade's D3D12 back end
tests `shared`. The first version passed the NT bit alone: ReShade created an ordinary, unshared
texture, returned no handle, and no D3D12 game ever produced a preview. A texture created without a
handle is now reported as such (`sharing_unsupported`) rather than as a failed duplication.

**Render target usage.** The texture is also created as a render target, although nothing is ever
rendered into it: a shared texture has to be bindable as render target *and* shader resource, or
D3D11 refuses to create it (legacy handles) or to open it (`E_INVALIDARG` on
`OpenSharedResource1` for an NT handle from D3D12). Formats that cannot be render targets — the
24/8 depth family — get a second try without it, with a warning in the log.

The texture is created with **one mip and one layer**, because a legacy D3D11 shared handle
requires it. That is also exactly what a preview needs: the requested mip and slice are copied into
mip 0 of the shared texture.

### The final image

`resource_id = kPreviewFinalImage` asks for whatever the swap chain presents: the game's own image
of the frame, before ReShade's effects. The add-on resolves it at present time with
`swapchain::get_current_back_buffer()`, because the buffer being presented changes every frame. It
is what the standalone shows by itself when it connects, beside both timeline views, and it costs
one GPU copy of the back buffer per frame.

### States on D3D12

The copy has to take the source out of the state it is in and put it back. A back buffer at present
time is in `present`, and that is the state used for the final image and for any resource flagged
as a back buffer. For any other resource, the state is the one its last recorded barrier left it
in: the barrier callback compares every barrier against the previewed resource with a single atomic
load, so it costs nothing when no preview runs. Only when no barrier has been seen is
`shader_resource` assumed.

### Freezing, and the image of a capture

`freeze_preview` stops refreshing the shared image without releasing it. The standalone sends it
when **Follow** is unchecked, so the image stays the one of the moment the timeline was paused.

When a deep capture finishes, the add-on copies the image of the last captured frame in that same
present and then holds it. The standalone recognises the finished capture by its first frame
number, marks the image as the captured frame's, saves it as `final.png` in the capture's own folder
next to the executable (`Images/<game>_<date>_<time>_frame<N>/`) and shows it in the Deep capture panel. **Back
to live** lets it go. A new preview request always starts live, so a freeze left over from a
capture can never keep a new viewer on an old image without saying so.

### Who closes a handle

ReShade hands the add-on the NT handle of a new shared texture and keeps no copy of it. An open NT
handle keeps the texture's memory alive whatever happens to the resource, so the add-on closes its
own as soon as it has duplicated it into the standalone, and the standalone closes its copy when it
closes the texture — including when opening it failed. Before this, every recreated preview
texture leaked its memory in the game until the game exited: a few megabytes each time, and it
would have been the whole of a capture's buffers each time.

### The buffers of a deep capture

The same helpers — create a shared copy target, hand its handle over — serve the deep capture's
buffers: one shared texture per texture the captured frame wrote to, copied once at the end of that
frame instead of every frame, read back once by the standalone and then released. See
[CaptureModes.md](CaptureModes.md#what-it-saves-to-disk).

### Saving

**Save as PNG** writes what is on screen — channels and range applied — at the texture's native
resolution, through the Windows Imaging Component. The read back happens once, in the standalone's
process, when asked: never in the game's, and never in the live path.

### The adapter

A shared texture opens only on the adapter that created it. The standalone creates its device on
the high performance adapter (`EnumAdapterByGpuPreference`), which is the one games use, instead of
"the default" one, which on a machine with an integrated GPU as well is the wrong one. When the
game's adapter and the standalone's still differ, the Preview panel says so with both names.

### Choosing the format

A D3D copy is only legal inside one *typeless* family, and a depth texture can be neither shared
nor viewed as a shader resource. `FormatShareableCopyTarget` (in Core) solves all three constraints
at once by sharing depth formats through their typeless member:

| Source | Shared texture | View on the standalone side |
|---|---|---|
| `d32_float` | `r32_typeless` | `r32_float` |
| `d24_unorm_s8_uint` | `r24_g8_typeless` | `r24_unorm_x8_uint` |
| `d16_unorm` | `r16_typeless` | `r16_unorm` |
| `d32_float_s8_uint` | `r32_g8_typeless` | `r32_float_x8_uint` |
| everything else | the source format | the same, made concrete if it was typeless |

`FormatShaderResourceView` does the reverse on the standalone side, deriving the concrete name from
the typeless one (`…_typeless` → `…_unorm` / `…_float` / …) rather than from a table somebody has to
keep up to date.

## Conversion — done on the standalone side

`PreviewRenderer` applies the display conversion in the standalone's process, which already owns a
D3D11 device and the view of the shared texture. The add-on therefore carries **no shader at all**:

* channel selection: RGB, R, G, B, A (as greyscale);
* alpha shown over a chequerboard, so transparent is distinguishable from black;
* raw depth and **linearised** depth, with near/far and reverse-Z;
* rescaling a range of values, which is indispensable for an HDR or depth buffer.

The shader for that pass is compiled at start-up **by our own `CompileHlsl`**: the compilation
chain the tool offers the user is therefore exercised on every launch.

A depth preview starts in linearised mode automatically, because a raw depth buffer displays as
uniform white and says nothing.

`PreviewChannels` stays in the protocol for the day a conversion has to happen on the game side
(to cut the bandwidth of an 8K texture, for instance).

## Synchronisation

* If `device_caps::shared_fence`: `create_fence(..., &shared_handle)`, the add-on signals value N
  after the copy, the standalone waits for N before reading. That is the clean path.
* Otherwise: two textures in alternation plus a frame counter in shared memory. The tearing that
  can remain on a preview is documented, not hidden behind a CPU copy.

## Protocol

`PreviewRequest { request_id, resource_id, mip_level, array_slice, channels, max_width, max_height }`
on the control pipe, `PreviewReadyRecord { request_id, resource_id, shared_handle, fence_handle,
fence_value, width, height, format, is_nt_handle }` in the ring. Both structures are already in
`Protocol.hpp`.

## Current limits (§6)

* **No synchronisation.** The standalone samples while the game copies: tearing is possible on a
  frame. The shared fence described above is not wired up yet. It is visible, bounded, and
  documented rather than hidden behind a CPU copy.
* **MSAA unsupported**: it would need a `resolve_texture_region`, refused for now with the
  `multisampled` status.
* **`max_width` / `max_height` ignored**: the texture is shared at its native resolution and scaled
  when displayed.
* Mips and slices are requested from the game (a new copy), not sampled locally.
* **D3D12 states from recorded barriers**: exact for the final image and back buffers; for other
  resources, the state of the last barrier *recorded*, which on a game recording on several threads
  is not always the last one *executed*.
* One preview at a time per session.

## When a CPU readback is legitimate

Only for one-off, explicit operations: saving a resource, exporting, an offline frame capture,
inspecting the contents of a structured buffer. Never in the live path.
