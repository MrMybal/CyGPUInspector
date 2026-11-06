---
id: ref-reshadeaddon
title: ReShade capture add-on
description: Technical detail: Install the portable package, try the demo and connect a supported game.
section: Technical reference
order: 30
parent: installation
revision: 22525d0fa606
---

# CyGPUInspectorRS — the agent inside the game {#cygpuinspectorrs-the-agent-inside-the-game}



## Role {#role}

`Hook · Track · Capture · Control · Transfer` — nothing else. No database, no decompiler, no
analysis, no analysis interface. All of that lives in [CyGPUInspectorApp](doc:ref-standaloneapp).

The add-on **implements no injection mechanism**. It uses the official ReShade add-on API
(`reshade::register_addon`, `reshade::register_event`) and nothing else. Where ReShade cannot
load, CyGPUInspector is simply not supported.

## State per object {#state-per-object}

| Object | Private data | Contents |
|---|---|---|
| `device` | `DeviceContext` | trackers, IPC, runtime control, tracking level, deep capture |
| `command_list` | `CommandListState` | current bindings + local event buffer, **no lock** |

A command list is only ever recorded by one thread at a time, so its buffer needs no protection.
It is merged into the `FrameRecorder`:

* on `execute_command_list` / `execute_secondary_command_list` (D3D12, deferred contexts);
* on `present` for the D3D11 immediate context, which never executes a command list. ReShade
  exposes that context as both a `command_queue` and a `command_list`: `init_command_queue` takes
  `get_immediate_command_list()` and registers it in the list to merge.

## Registered events {#registered-events}

`init/destroy_device`, `init/destroy_command_list`, `init/destroy_command_queue`, `init_swapchain`,
`init/destroy_pipeline`, `create_pipeline`, `init/destroy_resource`, `init/destroy_resource_view`,
`bind_pipeline`, `bind_render_targets_and_depth_stencil`, `begin/end_render_pass`, `draw`,
`draw_indexed`, `dispatch`, `dispatch_mesh`, `draw_or_dispatch_indirect`, `copy_resource`,
`copy_texture_region`, `resolve_texture_region`, `clear_render_target_view`,
`clear_depth_stencil_view`, `clear_unordered_access_view_float/uint`, `generate_mipmaps`,
`reset_command_list`, `execute_command_list`, `execute_secondary_command_list`, `present`.

The expensive ones are registered too, but only do anything during a deep capture:
`push_descriptors`, `bind_descriptor_tables`, `bind_vertex_buffers`, `bind_index_buffer`,
`bind_viewports`, `bind_scissor_rects`, `bind_pipeline_states`, `barrier`. Each begins with a
relaxed atomic load that says no capture is running, and returns. See
[CaptureModes.md](doc:ref-capturemodes) for why they cannot simply be registered and unregistered on
demand.

## Per frame work (`present`) {#per-frame-work-present}

1. Merge the immediate context's buffers.
2. Handle the commands received from the standalone (on this thread, never on the pipe thread).
3. Publish new shaders (byte code included, once), pipelines and resources.
4. Publish the frame: `frame_begin`, `frame_events` in chunks, `frame_end`.
5. Publish the deep capture payload when one is running, then disarm it if its frames are done.
6. Read back the GPU timestamps that finished three frames ago.
7. One preview copy into a shared texture, on the game's own queue.
8. Every 60 frames: `stats` (the cost of the tool itself).
9. Signal the reader, heartbeat in the directory, increment the frame index.

The time spent in this function is measured and returned in `FrameEndRecord::addon_cpu_ms`: the
cost of the tool is visible in the interface, not hidden.

## Tracking level {#tracking-level}

`Idle`, `Tracking` (default), `PassTiming`, `Capture`, `FullDrawTiming` — changeable live from the
overlay or from the standalone. The level controls what is published and how often timestamps are
taken.

## Overlay {#overlay}

Deliberately minimal (§13–§14 of the brief): connection state, tracking level, counters, IPC cost,
two buttons (`Resend everything`, `Restore all shaders`). The real work happens in the standalone,
in its own process.

## Configuration {#configuration}

`[CYGPUINSPECTOR]` section of `ReShade.ini`:

| Key | Default | Effect |
|---|---|---|
| `Verbose` | `0` | detailed logging in the ReShade log |
| `MaxEventsPerFrame` | `250000` | memory guard for a pathological frame |

## Installing {#installing}

Copy `CyGPUInspectorRS.addon64` next to the game executable (or into ReShade's `AddonPath`
folder). ReShade has to be an **add-on enabled** build.
