# Runtime shader control

*Also available in [French](fr/ShaderReplacement.md).*

**State: implemented and tested** (milestone 7).

## Disable — implemented

The add-on keeps a flat `pipeline_id → disabled` table (131072 entries, atomic bytes, no lock). The
`draw`, `draw_indexed`, `dispatch` and `draw_or_dispatch_indirect` callbacks return `true` — which
drops the command — when the bound pipeline is marked.

* Disabling a shader marks every pipeline that uses it, including ones created later
  (`OnPipelineRegistered` inherits the state).
* The event is still recorded, with the `kEventSkipped` flag: the standalone therefore sees what
  was dropped, and there is no silent hole in the frame.
* When nothing is disabled, a single `atomic<bool>` short-circuits the whole mechanism: the cost on
  the hot path is nil in normal use.

It is the fastest test for confirming what a shader puts on screen (§54, §84).

## Replace — implemented

```
Original → HLSL editing (standalone) → FXC/DXC → byte code → control pipe
        → replacement pipeline rebuilt (add-on) → swapped at draw time
```

Three real obstacles, and what was done about them:

**1. `create_pipeline` only allows substituting a shader at creation time.** Far too late for a
shader created ten minutes ago. The description of **every** pipeline is therefore captured at
`init_pipeline` (`PipelineBlueprint`), which makes it possible to rebuild a variant on demand. The
byte code is not duplicated in it — it already lives once per signature in the `ShaderTracker` —
without which a real game would cost hundreds of megabytes.

**2. `bind_pipeline` is a `void` event**: it cannot be cancelled to substitute something else. The
swap therefore happens **at draw time**, which does return a `bool`: the add-on binds the
replacement pipeline, issues the draw itself, puts the original back, and drops the game's command.

**3. A command issued by the add-on goes back through ReShade's hooks**, and therefore through our
own callbacks. A `thread_local` re-entrancy guard cuts the recursion.

A pipeline with a subobject that cannot be copied is marked **not replaceable, with the reason**,
rather than failing silently.

`Restore` withdraws the published handle *before* destroying the pipeline: a draw must never see a
handle that has already been freed.

## Highlight — implemented, as a replacement

Highlight is not a separate mechanism: the standalone **generates** a pixel shader that writes
magenta to every target the original declares — the output signature comes from reflection, which
is what guarantees it fits the pipeline — compiles it, and sends it as a replacement. The add-on
therefore still carries no compiler.

## Persistent state

Per signature, in the game's profile: `disabled`, `highlighted`, tag, the name given by the user,
the replacement shader. Reapplied on the next launch (§38). Exporting it for someone else is what
[ModPackages.md](ModPackages.md) is about.

## Interface

HLSL tab: an editor over the reconstruction of the chosen backend, then `[ Compile ]`,
`[ Compile & Inject ]`, `[ Reset to backend output ]`, `[ Restore original ]`.
Shader tab: `Disable` / `Enable`, `Highlight`, `Restore`.
Draws that went through a replacement come back marked `replaced` in the event list.

## Limits

* The time for a replaced draw includes binding the replacement pipeline and putting the original
  back: two extra `bind_pipeline` calls per affected draw. It is a diagnostic mode.
* A pipeline using a subobject that cannot be copied is not replaceable (the reason is shown).
* On D3D12 the replacement shader's profile has to be signed: see the `dxil.dll` trap in
  [ShaderDecompiler.md](ShaderDecompiler.md).
