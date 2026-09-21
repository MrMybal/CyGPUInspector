# CyGPUInspectorMCP

*Also available in [French](fr/MCP.md).*

**State: implemented and tested** (milestone 10).

## Shape

`CyGPUInspectorMCP.exe` is a **stdio proxy**: an external agent launches it, it speaks JSON-RPC
(MCP) on stdin/stdout and forwards to the standalone over the named pipe
`\.\pipe\CyGPUInspector\app`.

That split is deliberate. The proxy **holds no data and decides nothing**: it runs in whatever
process an agent started it from, so it cannot be trusted to police itself. The standalone owns the
session and the permission level.

Requests are handled **on the standalone's interface thread**, not on the pipe thread: they read
the model, the frame graph and the analysis cache, and answering from another thread would mean
locking all of that from outside. One frame of latency is irrelevant for a tool call.

The transport is JSON-RPC, **one object per line**. Supported methods: `initialize`, `tools/list`,
`tools/call`, `ping`, plus notifications, which are ignored. `initialize` returns `instructions`
telling the agent where to start and warning it that pass names are derived and that the HLSL is a
reconstruction.

## Permission levels

Three levels, `Read Only` by default, changeable **only in the standalone's interface**:

| Level | Tools it adds |
|---|---|
| `Read Only` | `get_current_frame`, `list_shaders`, `inspect_shader`, `get_shader_disassembly`, `get_shader_decompiled_hlsl`, `decompile_shader`, `get_shader_draw_calls`, `get_shader_resources`, `get_shader_timing`, `list_resources`, `inspect_resource`, `get_resource_readers`, `get_resource_writers`, `get_frame_graph`, `get_passes`, `get_frame_timeline`, `get_capture_state`, `get_command_state`, `inspect_draw`, `inspect_dispatch`, `search_shaders`, `search_resources` |
| `Debug Control` | `disable_shader`, `enable_shader`, `highlight_shader`, `capture_frame`, `start_deep_capture` |
| `Shader Modification` | `compile_shader`, `replace_shader`, `restore_shader` |

Every call above `Read Only` is logged and visible in the standalone.

## Why these tools

The goal is not to hand an AI a large document to guess from, but to let it **walk the data
itself**. A typical workflow for "find the Bloom pass":

```
get_frame_graph()
 → spot the half resolution resources
 → find shaders reading the HDR SceneColor
 → follow the downsample chain
 → identify the blur and composite passes
 → answer with pass, shaders, resources, timings and reasoning
```

Then, with `Debug Control`, `disable_shader(...)` confirms or refutes the conclusion by looking at
the game.

## Answers

Every tool returns structured, **sourced** data: event, shader and resource identifiers that lead
back to the exact command. An answer never contains a classification without its confidence and
without the facts it rests on.

Some answers carry an explicit `caveat` field, for example:

* `get_frame_graph`: "no game names its passes, these names are derived";
* `get_shader_decompiled_hlsl`: "these are reconstructions, not the original source";
* `get_shader_timing`: "the time for a command is the interval to the next one".

## Limits

* A tool that needs long work (disassembly, decompilation) **starts** it and answers "ask again in
  a moment" rather than blocking the interface.
* The pipe accepts one proxy at a time.
* `get_shader_timing` needs a tracking level that profiles; it says so instead of returning zero.
* `get_command_state` needs a deep capture to have run; it says which tool to call first.
