# Mod packages — export a modification, replay it without the tool

*Also available in [French](fr/ModPackages.md).*

**State: implemented and tested** (`Tests/CyGPUInspectorShaderTests`, 19 checks on the full round
trip with real compiled shaders, including the refusal of corrupted byte code).

Everything CyGPUInspector does to a running game dies with the game. The add-on holds the
replacements in memory, and the identifiers they use mean nothing on the next launch. A **mod
package** is that work written down, and **CyGPUInjector** is the small add-on that replays it —
with no inspector, no standalone and no IPC.

```
CyGPUInspector              →   MyMod.cygimod   →   CyGPUInjector.addon64
(find it and change it)         (a folder)          (apply it, here or on someone else's machine)
```

## 1. The key: the semantic hash

An entry does not name "shader number 47". It names the **SHA-256 of a shader's code chunks**,
excluding the debug and reflection parts of the container. That is what makes a package:

* survive restarting the game, where every number has changed;
* survive a recompile that only changes the container's metadata;
* **stop applying** when the game really does change that shader.

That last point is a feature, not a defect. A stale package finds nothing and does nothing, rather
than applying your change to a shader that is no longer the right one.

## 2. What a package contains

A **folder**, not an opaque blob — the same reason a capture is one: you have to be able to look
inside, diff two versions, fix a note by hand.

```
MyMod.cygimod/
  mod.json                  manifest: metadata and entries
  shaders/<hash>.cso        replacement byte code, named after the hash it replaces
  shaders/<hash>.hlsl       the HLSL it was compiled from — provenance, not execution
```

Two actions, and only two:

| Action | Effect |
|---|---|
| `replace` | the package's byte code is substituted when the game creates the pipeline |
| `disable` | every draw or dispatch using that shader is dropped |

The HLSL source is kept so that in a year's time you can see **what was meant**, not only what was
compiled. It is never executed.

## 3. Exporting

The **Mod export** panel in the standalone. Everything you do to the game arrives there by itself:

* a shader replaced from the HLSL editor → a `replace` entry, ticked;
* a shader disabled from Details → a `disable` entry, ticked;
* a magenta **highlight** → a `replace` entry, **unticked**. It is a way of looking at a shader, not
  something anyone wants to receive in a mod; it is recorded so nothing is lost, but it cannot ship
  by inattention.
* `Restore` or `Enable` on a shader → the entry is **forgotten**. Exporting a change the user took
  back would be exporting a mistake.

A shader modified twice keeps only the last decision: that is what the user sees in the game, so
that is what an export has to mean.

## 4. Applying — CyGPUInjector

Copy the `*.cygimod` folder into `CyGPUInjector/` next to the game executable, and put
`CyGPUInjector.addon64` where ReShade loads its add-ons. Another path can be set with `ModPath` in
the `[CYGPUINJECTOR]` section of `ReShade.ini`.

The **CyGPUInjector** tab in the ReShade overlay shows what loaded, how many shaders were actually
replaced, how many draws were skipped, and lets a package be switched off.

### How it applies, exactly

* **Replacement** — `create_pipeline` hands an add-on the description the game is about to create a
  pipeline from, and lets it change it. The package's byte code is written in before the graphics
  API ever sees the original. This is the simple path, and it is available here precisely because
  the modifications are **known up front**; the inspector has to do something far more involved,
  since it only learns which shader it wants to replace long after the pipeline exists (see
  [ShaderReplacement.md](ShaderReplacement.md)).
* **Suppression** — a draw callback returns `true`, which makes ReShade drop the command.

### What it costs

Almost nothing, and that shows in which events get registered: a package that only replaces shaders
**registers no per-draw callback at all**. The only work is one hash per shader as the game creates
its pipelines, which happens a few thousand times over a whole session, not per frame. The
per-draw callbacks are only wired up when a package actually disables something.

## 5. Limits, stated plainly

* **A replacement takes effect when the game creates the pipeline**, which usually means at load
  time. Ticking a package in the overlay mid-session changes nothing until that pipeline is created
  again; the overlay says so. Draw suppressions, on the other hand, take effect immediately. Hot
  swapping exists in the inspector, not here: the point of CyGPUInjector is to be small.
* **Two packages touching the same shader**: the first one alphabetically wins, and the other is
  ignored with a warning in `ReShade.log`. Letting the second overwrite the first silently would
  make the result depend on something nobody can see.
* **Byte code is checked when it is loaded**, not when it is applied: a truncated or hand-edited
  `.cso` fails the whole package rather than reaching a graphics driver.
* The game name in the manifest is **advisory**. Matching is by hash, so a package works in any game
  that happens to use the same shader. The overlay shows the game it was made on, which is what
  explains a package that loads and never matches anything.

## 6. What CyGPUInjector is not

The name says "injector", but **nothing here injects anything**. It is a ReShade add-on like any
other: ReShade loads it, and the modifications go in through the official add-on API.
CyGPUInspector implements no graphics injection system and will not (§2 of the brief), and it
defeats no protection of any kind (§3). A game that refuses ReShade is simply a game this does not
work on.

## 7. Where this lives in the code

| Piece | File |
|---|---|
| The format, read and written by the same code on both sides | `CyGPUInspectorCore/{Include/CyGPUInspectorCore,Source}/ModPackage.*` |
| What the user changed, within the session | `CyGPUInspectorApp/Source/Mod/ModRecorder.*` |
| The export panel | `CyGPUInspectorApp/Source/App/ModExportPanel.cpp` |
| Loading packages, resolving them into one table | `CyGPUInjector/Source/ModLibrary.*` |
| Applying through the add-on API | `CyGPUInjector/Source/Apply.cpp` |
| Overlay tab | `CyGPUInjector/Source/Overlay.cpp` |
