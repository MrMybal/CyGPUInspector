# Known limits

*Also available in [French](fr/Limitations.md).*

This file is a living one. It exists so that the limits are **stated**, in the tool as much as in
the documentation, rather than taken for bugs.

## Scope

* CyGPUInspector only works where **ReShade works normally**. No bypass of an anti-cheat, of a
  protection, of an injection block or of any security mechanism is developed, and none will be. An
  application that refuses ReShade is **unsupported**, full stop.
* ReShade has to be an **add-on enabled** build. Builds without add-ons load no `.addon64` at all.
* 64-bit binaries only.

## What the ReShade API does not give

* **No pass names.** `PIXBeginEvent`, `ID3DUserDefinedAnnotation` and
  `vkCmdBeginDebugUtilsLabelEXT` are not exposed. Frame graph pass names will therefore always be
  either derived, or inferred by an AI, or given by the user — never read from the game.
* **No access to the original HLSL source.** It does not exist in the binary. Every piece of HLSL
  the tool shows is a reconstruction, and is labelled as one.
* No built-in disassembler or reflection: that work is done on the standalone side with
  `d3dcompiler` and `dxcompiler`.
* `destroy_pipeline` is not called on D3D9.

## Limits of the current implementation (v0.1.0)

* Only **render target 0** is attached to each draw event. The full set of targets travels in the
  binding events, but limited to the **first four slots**.
* **SRV reads are only tracked during a deep capture**: they come through the descriptor events,
  the most expensive in the API. In runtime mode, "read" means copy source or depth target.
* `RuntimeControl` speeds up the "is this pipeline disabled?" decision with a flat table of 131072
  entries. A game creating more pipelines than that would see the last ones miss the fast path.
* The event ring is 64 MiB. If the standalone does not consume fast enough, events are **dropped**
  (never queued at the game's expense) and the loss is displayed.
* A frame is capped at 250 000 events (`MaxEventsPerFrame`).

## Frame graph limits (milestone 4, implemented)

* A **dispatch does not say what it writes** without descriptor tracking: compute passes are
  grouped by pipeline and their dependencies are incomplete. That is the main limit in runtime
  mode; a deep capture fills it in.
* Pass names are **derivations**, with a confidence displayed. A pass that is not recognised is
  called `Unknown Pass #N` rather than being given an invented name.
* A draw's depth target counts as read **and** written, because the API does not say whether depth
  writing is enabled. A graph can therefore show a depth dependency slightly wider than reality.
* The graph is rebuilt four times a second on the last frame received; it does not yet compare
  frames with each other.

## Shader chain limits (milestone 5, implemented)

* `dxcompiler.dll` is not a system DLL: without it, **Shader Model 6** shaders can be neither
  disassembled nor reflected. The `Tools` tab says where it was looked for.
* DXIL disassembly is **LLVM** assembly, not the readable SM 5 form. That is the format DXC
  produces, not a choice of ours.
* Reflection gives what a shader **declares**, not what is actually bound to it at a given draw:
  that correspondence needs descriptor tracking, so it needs a deep capture.

## Runtime control limits (milestone 7, implemented)

* A pipeline with a subobject that cannot be copied is **not replaceable**. The reason is shown
  rather than the failure being silent.
* A replaced draw costs two extra `bind_pipeline` calls: it is a diagnostic mode.
* On D3D12 a DXIL replacement shader has to be signed (see the `dxil.dll` trap).

## GPU profiling limits (milestone 8, implemented)

* ReShade calls the add-on **before** a command, never after: the time shown for a draw is the
  interval to the next recorded command, not the exact duration of the draw. The interface says so.
* Results come from frame N-3 and carry their own frame number; they are matched against the frame
  on screen by event index, which is an approximation.
* A cap of 16384 timestamps per frame; beyond it, marks are counted as dropped.
* On D3D12 with several command lists, recording order is not execution order.

## Capture limits

* **Neither capture replays the frame.** A debugger like RenderDoc serialises the command stream
  and re-executes it on its own device; that is where pixel history, stepping through a draw and
  post-vertex-shader geometry come from. CyGPUInspector observes the real frame through the
  official ReShade add-on API and never injects a device (§2 of the brief), so those four things
  are out of reach. See [CaptureModes.md](CaptureModes.md).
* **No pass name comes from the game**: the add-on API exposes neither `PIXBeginEvent` nor
  `ID3DUserDefinedAnnotation`. Every name is derived and shown with its confidence.
* The contents of a Direct3D 12 **descriptor table** are not readable: it is bound by handle and
  the API does not hand out the heap. The command is marked incomplete rather than appearing to
  bind nothing.
* The deep capture has a **ceiling** of 60 000 commands per frame and 256 descriptors per command.
  Beyond it, what is lost is counted and published, not hidden.
* Bindings set **before** a deep capture is armed are unknown to it. It starts at a frame boundary,
  which is enough for a game that rebinds its state per frame, but not in the absolute for the
  Direct3D 11 immediate context.
* The runtime capture only has **bar lengths** at level `Pass Timing` or above. Otherwise the
  passes are listed in order, with dimmed bars sized by command count — and the panel says so.

## Mod package limits (CyGPUInjector)

* A **replacement only takes effect when the pipeline is created**, so usually at load time.
  Ticking a package mid-session from the overlay changes nothing until that pipeline is recreated;
  draw suppressions, on the other hand, are immediate. Hot swapping is in the inspector, not in the
  injector, which is deliberately small.
* A package **stops applying** when the game really changes the shader. That is intended: matching
  is on the hash of the code, so a stale package finds nothing rather than touching the wrong
  shader.
* **Two packages on the same shader**: the first alphabetically wins, the other is ignored with a
  warning in `ReShade.log`.
* CyGPUInjector **injects nothing**, despite its name: it is an add-on ReShade loads, and the
  modifications go in through the official add-on API. A game that refuses ReShade stays out of
  reach.

## Offline capture limits (milestone 11, implemented)

* A capture keeps **one frame**, not a sequence, and **no pixels**: GPU previews are gone when it
  is reopened. Everything else works.
* Only the shaders **used in the frame** are archived, with their byte code. The others are not: a
  capture is a unit of analysis, not a dump of everything the game ever created.
* The format carries a version number and refuses a capture written by an incompatible version,
  rather than reading it crooked.
* Comparison matches shaders **by signature**, never by identifier: an identifier is local to one
  session.

## AI and MCP limits (milestones 9 and 10)

* The standalone **calls no AI API**: the implemented path is MCP, where an external agent does the
  reasoning. Assisted jobs with an API key are still to do.
* The MCP server accepts **one proxy at a time**.
* A tool that needs long work starts it and answers "ask again in a moment" rather than blocking
  the interface.
* The permission level is decided in the standalone and **is not persisted**: it goes back to
  `Read Only` on every launch, deliberately.

## Structural limits of decompilation

* Reconstructed HLSL **may not recompile**. That is expected; the validation state is part of every
  backend's metadata.
* `cygi-dxbc` is register by register: it recovers neither names nor expressions.
* `dxbc-spirv` is a compiler, not a translator: it reorders and folds instructions. Its output is
  readable but it is not the shape that was written. It **gives up** on a shader that uses buffer
  addresses (physical pointers), because HLSL cannot express them.
* **`vkd3d-shader` is not integrated and cannot be with this toolchain**: seven of its headers are
  generated by autoconf, widl, flex, bison and a home-grown script, and its common code includes
  `<pthread.h>` and `<unistd.h>`. Detail in [ShaderDecompiler.md](ShaderDecompiler.md).
  `dxbc-spirv` fills the place it was meant for.
* `hlsldecompiler` (3Dmigoto) recovers the names of resources, of constant buffers and of their
  members, and rebuilds expressions, but **not** local variable names, structures or algorithms:
  the compiler destroyed them.
* `dxil-spirv` goes through Vulkan SPIR-V, which carries neither names nor Direct3D registers. Both
  are put back from the shader's reflection tables, but **the members of a constant buffer are
  lost**: they are collapsed into an array of `float4`. Interpolant semantics become `TEXCOORD<n>`,
  and the control flow is the one the *structurizer* rebuilt. That is the price of the only
  existing path to Shader Model 6.
* The entry point of a `dxil-spirv` reconstruction is emitted as `main`, not under its real name,
  so that validation by recompilation works. The real name is given in the notes.
* The vendored 3Dmigoto code is from 2014 and is compiled with relaxed options (`/permissive`,
  `/Zc:strictStrings-`, `/W0`) to stay identical to upstream. That is a deliberate choice: fixing
  it would make the GPL compliance uncheckable by a plain `diff`.
* **DXIL loses more information than DXBC** (LLVM inlining, SROA, vectorisation destroyed): an
  SM 6.x reconstruction will systematically be less readable than an SM 5.x one. That is not a
  defect of the tool.
* An AI output is a **hypothesis**, with a confidence, never presented as a fact. The three levels
  `Original source` / `Decompiler reconstructed` / `AI reconstructed` are always kept distinct.

## GPU sharing limits (milestone 3, implemented)

* **No synchronisation** for now: the standalone samples the texture while the game fills it, so
  tearing is possible on a frame. The shared fence is designed but not wired up. It is a deliberate
  trade-off: the alternative would be a CPU copy, which the brief forbids in the live path.
* **Multisampled** resources are not previewed (it would need a resolve): the `multisampled` status
  is returned.
* `max_width` / `max_height` are ignored: the texture is shared at native resolution.
* On **D3D12**, the barriers assume the source is in `shader_resource` at present time. That is the
  case for a render target or a texture at the end of a frame, but it remains an assumption. D3D11
  is unaffected.
* One preview at a time per connected session.
* The preview reads a resource's handle and then queries its description. If the game destroys that
  resource from another thread exactly in between, the add-on uses a stale handle. The window is a
  handful of instructions wide and has not been observed, but it exists: closing it would require
  holding a reference on the resource, which the add-on API does not expose.

## Localization limits

* A translation is only as complete as its catalogue. What is missing falls back to English, which
  is visible but not broken; **View → Language** shows how many strings have no translation.
* Log lines, error messages from the add-ons, and MCP tool names are deliberately not translated —
  see [Translating.md](Translating.md).
