# Frame graph and dependency graph

*Also available in [French](fr/FrameGraph.md).*

**State: implemented and tested** (`Tests/CyGPUInspectorIpcTests`).

No game supplies a pass name: the ReShade add-on API exposes neither `PIXBeginEvent` nor
`ID3DUserDefinedAnnotation` (see [Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md) §7).
The graph is therefore **reconstructed** from the dependencies, on the standalone side, from the
events already received.

## Step 1 — clustering into passes

Consecutive events form one pass for as long as their **grouping key** does not change:

| Command kind | Key |
|---|---|
| draw | render target 0 + depth target |
| dispatch | the resource it writes when that is known, and **a barrier ends the pass** |
| copy / resolve / clear | destination + source |

Binding events (`bind_render_targets`, `begin_render_pass`) do not form a pass: they update the
current render target set. That is indispensable, because a draw event only carries slot 0 —
without the binding event a GBuffer would be undetectable.

The dispatch key deliberately **does not** include the pipeline. Grouping by pipeline reads well
and is wrong on a real engine: a post-processing chain is twenty different compute pipelines one
after another, and it came out of a real D3D12 game as twenty passes of one command each, which is
a list of nothing. What actually separates two compute passes is the **barrier** between them —
an engine saying that the second reads what the first wrote — so that is what ends one here. When
a dispatch does say what it writes, that still splits passes as before.

Each pass keeps: its event range, the targets it wrote, the resources it read, the shaders it used,
the number of draws, and the aggregated timing.

## Step 2 — resource dependency graph

Built per resource, from the `primary_resource` and `secondary_resource` fields of every event. One
point that is not obvious: for a **draw**, the secondary resource is the depth target, and a depth
prepass **writes** it. The API does not say whether depth writing is enabled, so it counts as both
a read and a write — which is what makes the prepass appear as the producer of the depth buffer the
lighting pass will consume. For a copy, the secondary resource is the source, so a read only.

That gives:

```
Created by · Written by · Read by · Copied from · Copied to · Resolved from · Resolved to · Destroyed
```

Every edge carries the index of the event that produced it, so it is always possible to go back
from the graph to the exact command. Pass → pass edges come from "pass B reads a resource pass A
wrote".

## Step 3 — heuristic classification

Applied on invariants, with a confidence score:

| Invariant observed | Name proposed |
|---|---|
| depth only, no colour RT, early in the frame | Depth Prepass |
| ≥ 3 simultaneous RTs with normal / albedo / roughness formats | GBuffer |
| compute reading depth + normals, writing a full resolution UAV | Lighting / AO |
| a chain of HDR RTs at successively halved resolution | Bloom downsample |
| a single pass reading an HDR RT and writing an LDR RT just before the UI | Tonemap |
| small alpha-blended draws writing the back buffer at the end of the frame | UI |

A depth-only pass is told from a shadow map by the **size** of the depth buffer it writes: the
camera's depth buffer is the size of what is presented, a shadow map is some other size. That
replaced "is it early in the frame", which called every shadow cascade of a real game a depth
prepass. When nothing is presented yet, and only then, position in the frame is used again, at a
much lower confidence.

Everything else becomes `Unknown Pass #N`. The origin of the name (`user`, `heuristic`, `ai`) and
its confidence are **always shown**, and the colour of the name in the interface follows the
confidence: an inference is never presented as a fact (§25, §40). The user can rename a pass; that
name survives rebuilds and switches the origin to `user`.

## Current limits

* A **dispatch does not say what it writes**: without descriptor tracking, compute passes are
  grouped by the barriers between them and their dependencies are partial. A deep capture does
  track descriptors, so it fills this in — see [CaptureModes.md](CaptureModes.md).
* A pass that nothing recognises is named `Unknown Pass #N (WxH)`. The size is a fact, not a
  guess, and it is what tells one unnamed pass from another in a list — a post-processing ladder
  reads as 1708x960, then 854x480, and so on. The origin stays `unknown` and the confidence zero.
* Only **four render targets** per binding are carried (slots 0 to 3).
* Classification looks at one frame only: it does not yet take advantage of a pass being stable
  from one frame to the next.

## Presentation

A navigable column view (Depth → GBuffer → Lighting → SSR → Volumetrics → Bloom → Tonemap → UI →
Present), with cross selection: clicking a pass filters the events, the shaders and the resources
involved. The same passes are drawn as time-scaled bars in the
[frame timeline](CaptureModes.md).
