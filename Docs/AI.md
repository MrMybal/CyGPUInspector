# AI

*Also available in [French](fr/AI.md).*

**State: the foundations are implemented** (milestone 9): source labelling, tags, persistent
annotations and the analysis snapshot. **AI jobs wired to an API are still to do.**

## Where it sits in the tool

The standalone **embeds no model**. There are only two paths:

1. **An external agent over MCP** — the main path, **implemented**: the AI walks the data itself
   with the tools described in [MCP.md](MCP.md).
2. **Assisted jobs** — HLSL clean-up, classification, explanation, started from the interface with
   an API key the user supplies. **Not implemented yet**: the storage, the labels and the prompt
   versioning exist, the HTTP call does not.

## What the AI does, and from what

Always from concrete data the tool extracted: disassembly, pseudo-HLSL produced by a real
decompiler, bindings, the dependency graph, timings.

* rename variables and functions (`r0`, `r1` → `SceneColor`, `Exposure`, `Luminance`);
* comment, identify known algorithms, rebuild structures;
* guess the probable role of a resource;
* simplify generated code, explain mathematical operations;
* propose a pass name and a role (`Likely Role: Tonemapping — Confidence 92 %`).

## What is implemented

**Source labelling.** An annotation always carries its origin — `user`, `derived` or `AI` — its
kind (`note`, `classification`, `cleanup`, `explanation`), its author (the model, for an AI
output), its prompt version, its date and its confidence. The three origins are shown in three
different colours. It is structurally impossible to store a sentence from a model without saying it
came from a model.

**Tags** (§39): Depth, GBuffer, Lighting, Shadow, GI, Reflection, SSR, AO, Fog, Volumetric,
Post Process, Bloom, Tonemap, Upscale, UI, Particles, Video, Unknown. Plus a free name given by the
user. All of it is persisted **by signature** in `notes.json`, so it survives restarting the game,
updating it, and even applies from one game to another.

**Versioning** (§58): every annotation keeps the model and the prompt version, which is what makes
it possible to know a result is stale and regenerate it.

## Non-negotiable rules

* Any AI output is labelled `AI reconstructed` and kept distinct from `Decompiler reconstructed`,
  which is itself kept distinct from `Original source` — which is never available (§31).
* A classification is **a hypothesis with a confidence**, never a fact (§40). Where a check is
  possible (disable the shader and look at the image), the tool offers it.
* The AI never replaces a decompiler: it works after one.
* The model and the prompt version are stored with the result, so it can be regenerated (§58).

## Analysis Snapshot

**Implemented.** `AI analysis snapshot` writes a self-contained directory: a full capture
(`capture.json`, `events.bin`, `shaders/`) plus an `analysis/` folder holding

* `snapshot.md`: the derived passes in prose **with their origin and their confidence**, the table
  of the frame's shaders with their GPU times, and the warning that none of it is original source;
* the disassemblies and HLSL reconstructions already computed, one file per backend, each prefixed
  by its validation verdict.

That is exactly what would be given to a model — so it is what the user can re-read, correct or
archive **before** handing it over, rather than an opaque bundle.
