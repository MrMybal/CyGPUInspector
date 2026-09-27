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

What was disabled, highlighted or replaced is saved in the game's profile with **Save this
session's changes**, as a mod package keyed by semantic hash, and applied again with **Reapply now**
or, if asked, when the game connects (§38); tags and names are kept per signature in the shader's
notes. See [Database.md](Database.md). Exporting it for someone else is what
[ModPackages.md](ModPackages.md) is about.

## Interface

HLSL tab: an editor over the reconstruction of the chosen backend, then `[ Compile ]`,
`[ Compile & Inject ]`, `[ Reset to backend output ]`, `[ Restore original ]`.
Shader tab: `Disable` / `Enable`, `Highlight`, `Restore`.
Draws that went through a replacement come back marked `replaced` in the event list.

## Checked before it is sent

Direct3D 12 checks, when a pipeline is created, that its shaders fit together and fit its root
signature, and refuses the pipeline with nothing an add-on can read: the add-on can only report
"the graphics API refused the replacement pipeline". So the standalone compares the compiled
replacement with the original first, the way CyRSAssistant checks a replacement before activating it,
and says what does not fit instead of sending it:

| Refused before sending | Why the game would refuse it |
|---|---|
| an input the original does not have | nothing before this stage provides it |
| an input in another register, with more components or another type | the previous stage's output does not match it |
| a texture, sampler, buffer or UAV register the original does not declare | the root signature has no place for it |
| for a vertex, hull, domain or geometry shader, an output of the original no longer written | the next stage reads it |
| another stage, or DXBC instead of DXIL | not the same kind of shader |

Differences the game accepts are shown as notes: a pixel shader writing fewer or other targets, a
constant buffer declared larger than the original's. The check runs on Compile & Inject, on the
`replace_shader` MCP tool and for the AI assistant, and in the HLSL editor when it compiles for the
shader's own profile, where the differences appear in the list under the compiler's messages.

## Limits

* The time for a replaced draw includes binding the replacement pipeline and putting the original
  back: two extra `bind_pipeline` calls per affected draw. It is a diagnostic mode.
* A pipeline using a subobject that cannot be copied is not replaceable (the reason is shown).
* On D3D12 the replacement shader's profile has to be signed: see the `dxil.dll` trap in
  [ShaderDecompiler.md](ShaderDecompiler.md).
