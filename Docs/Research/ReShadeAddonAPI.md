# Research — the ReShade add-on API

*Also available in [French](../fr/Research/ReShadeAddonAPI.md).*

Source of truth: the ReShade SDK headers vendored in `ThirdParty/reshade/include/`
(`RESHADE_API_VERSION 20`, ReShade 6.8.x, BSD-3-Clause). Everything below was read in those
headers, not assumed.

## 1. What the API guarantees

| CyGPUInspector need | ReShade mechanism | Verdict |
|---|---|---|
| Load code into the game | `.addon64` loaded by ReShade, `AddonInit` / `AddonUninit` exports, `reshade::register_addon` | **Available** — no hooking of our own |
| Detect the graphics API | `device::get_api()` → `d3d9/d3d10/d3d11/d3d12/opengl/vulkan` | Available |
| Get the compiled shader byte code | `create_pipeline` / `init_pipeline` → `pipeline_subobject[]` with `shader_desc{ code, code_size, entry_point }` | **Available** (see §3) |
| Associate a pipeline handle with shaders | `init_pipeline(device, layout, subobject_count, subobjects, pipeline)` | Available |
| Track draws / dispatches | `draw`, `draw_indexed`, `dispatch`, `dispatch_mesh`, `dispatch_rays`, `draw_or_dispatch_indirect` | Available |
| Cancel a draw / dispatch | The callback returns `bool`: `true` = command dropped | **Available** |
| Replace a shader | `create_pipeline`: modify the `subobjects` and return `true` | **Available** (see §4) |
| Track render targets | `bind_render_targets_and_depth_stencil`, `begin_render_pass` / `end_render_pass` | Available |
| Track resource bindings | `push_descriptors`, `bind_descriptor_tables`, `update_descriptor_tables`, `copy_descriptor_tables`, `push_constants` | Available, but expensive (see §5) |
| Track input buffers | `bind_index_buffer`, `bind_vertex_buffers`, `bind_stream_output_buffers` | Available |
| Track copies / resolves / clears | `copy_resource`, `copy_buffer_region`, `copy_texture_region`, `copy_buffer_to_texture`, `copy_texture_to_buffer`, `resolve_texture_region`, `clear_*`, `generate_mipmaps` | Available |
| Track barriers | `barrier(cmd, count, resources, old_states, new_states)` | Available (D3D12 / Vulkan) |
| Frame boundaries | `present`, `finish_present`, `reshade_present` | Available |
| GPU timings | `device::create_query_heap(query_type::timestamp, …)`, `command_list::begin_query` / `end_query`, `device::get_query_heap_results`, `command_queue::get_timestamp_frequency()` | **Available** |
| Cross-process shared textures | `device::create_resource(desc, init, state, out, void **shared_handle)` with `resource_flags::shared` / `shared_nt_handle` | **Available** |
| Shared fences | `device::create_fence(value, fence_flags, out, void **shared_handle)` | Available |
| Native D3D handles | `device::get_native()`, and `resource::handle` / `resource_view::handle` hold the native pointer | Available |
| Overlay | `reshade::register_overlay(title, callback)` + the ImGui function table | Available |

## 2. Full event list (API 20)

`init/create/destroy` for: device, command_list, command_queue, swapchain, effect_runtime, sampler,
resource, resource_view, pipeline, pipeline_layout, query_heap.

Commands: `barrier`, `begin_render_pass`, `end_render_pass`,
`bind_render_targets_and_depth_stencil`, `bind_pipeline`, `bind_pipeline_states`, `bind_viewports`,
`bind_scissor_rects`, `push_constants`, `push_descriptors`, `bind_descriptor_tables`,
`bind_index_buffer`, `bind_vertex_buffers`, `bind_stream_output_buffers`, `draw`, `draw_indexed`,
`dispatch`, `dispatch_mesh`, `dispatch_rays`, `draw_or_dispatch_indirect`, `copy_*`,
`resolve_texture_region`, `clear_*`, `generate_mipmaps`, `begin_query`, `end_query`,
`copy_query_heap_results`, `*_acceleration_structure`, `reset_command_list`, `close_command_list`,
`execute_command_list`, `execute_secondary_command_list`, `map_*`, `unmap_*`,
`update_buffer_region`, `update_texture_region`, `present`, `finish_present`,
`set_fullscreen_state`, plus the `reshade_*` events (effects, overlay, screenshot).

Every event the brief asks for (§5) exists, **except** an explicit separate "Sampler Bind" notion:
on D3D12 and Vulkan samplers come through `push_descriptors` / `bind_descriptor_tables` with
`descriptor_type::sampler`, and on D3D11 they arrive through `push_descriptors` too. There is no
"Begin Frame" either: the frame boundary is `present` (end of frame N, start of frame N+1).

## 3. Access to shader byte code — the critical points

* `shader_desc::code` is a pointer **valid only during the callback**. The `code_size` bytes have
  to be copied immediately.
* On D3D11 a ReShade "pipeline" is **one** state object (an `ID3D11PixelShader`, or an
  `ID3D11BlendState`…). A pipeline there typically has one shader subobject.
* On D3D12, `CreateGraphicsPipelineState` produces **one** pipeline with several shader subobjects
  (VS+PS+…). The shader to pipeline mapping is therefore 1..N.
* On OpenGL, `code` is **GLSL text** (`glShaderSource`), not a binary.
* On Vulkan, `code` is SPIR-V.
* `init_pipeline` gives the final pipeline handle plus the subobjects: it is the event that lets the
  `pipeline handle -> [shader signatures]` table be built, which `bind_pipeline` then uses.
* `destroy_pipeline` is not called on D3D9.

## 4. Shader replacement

Replacement happens in `create_pipeline`, by modifying `subobjects[i].data->code` / `code_size`
before ReShade passes the creation on to the API. Consequences:

* A shader can only be replaced **at creation time**. To replace a shader that already exists you
  either wait for a recreation, or create a replacement pipeline with `device::create_pipeline` and
  swap it in at `bind_pipeline` (return `true` + rebind).
* The second strategy is what makes a hot replacement possible without restarting the game, and it
  is what CyGPUInspectorRS uses (see [ShaderReplacement.md](../ShaderReplacement.md)).
* The first strategy is enough when the modifications are known up front, which is the case for
  CyGPUInjector (see [ModPackages.md](../ModPackages.md)) — and it is far simpler.

## 5. Cost of the events

ReShade only calls the callbacks for events that were actually registered: not registering
`push_descriptors` costs zero. Ranked by cost:

1. `push_descriptors` / `bind_descriptor_tables` / `update_descriptor_tables` — the most frequent,
   several thousand calls per frame. **Only do work during a deep capture.**
2. `draw` / `draw_indexed` / `dispatch` — a few thousand per frame. On in normal tracking, but with
   O(1) handling and no allocation.
3. `bind_pipeline`, `bind_render_targets_and_depth_stencil` — a few hundred to a few thousand.
4. `init_*` / `destroy_*` / `create_pipeline` — rare (resource creation).

## 6. Verified traps (SDK, plus the same author's CyGameCapture project)

* On D3D11 the immediate context is **both** a `command_queue` and a `command_list`: ReShade
  returns the same object, so the private data are shared.
* `reshade_begin_effects` only fires if effects are loaded, which makes it unusable as a reliable
  frame boundary.
* `reshade_overlay` is called every frame as soon as an overlay is registered, even a closed one:
  track the open state with `reshade_open_overlay`.
* `ImTextureID` has to be defined as `ImU64`: in the ReShade overlay, an ImGui texture is a
  `resource_view` handle.
* `destroy_pipeline` and `destroy_resource` can be called from inside other API calls: be careful
  with locks.
* `create_resource` with `resource_flags::shared` returns the *legacy* DXGI handle on D3D11
  (`IDXGIResource::GetSharedHandle`); on D3D12 only `shared_nt_handle` is supported.

## 7. What ReShade does not give

* **No pass names and no debug markers**: `PIXBeginEvent`, `ID3DUserDefinedAnnotation` and
  `vkCmdBeginDebugUtilsLabelEXT` are not exposed by the add-on API. The frame graph therefore has
  to be **derived** from resource dependencies, not read from the game.
* No direct access to the contents of a constant buffer: it means intercepting `push_constants`,
  `update_buffer_region` or `map_buffer_region`, or copying the buffer to a staging buffer.
* No shader disassembler and no reflection: that is on us (DXC / D3DReflect).
* No access to the game's original HLSL source — it does not exist in the binary.
