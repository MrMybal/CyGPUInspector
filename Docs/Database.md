# CyGPUInspectorDatabase

*Also available in [French](fr/Database.md).*

**State: blob store, tags and annotations implemented and tested** (`ShaderStore`, `ShaderNotes`).
**SQLite and per-game profiles: designed, still to come.**

## Principle

SQLite for metadata and queries, disk for the large blobs. Nothing expensive is computed twice:
everything is cached **by shader signature**, so it is recognised from one run of a game to the
next, and even from one game to another.

```
Database/
├── cygpuinspector.db            shaders, tags, relations, captures, analyses, verdicts
└── shaders/<aa>/<signature>/
    ├── metadata.json            stage, API, shader model, sizes, first and last seen
    ├── original.dxbc | .dxil
    ├── disassembly.txt
    ├── decompiled/<backend>-<version>.hlsl   one file per backend, never overwritten
    ├── decompiled/<backend>-<version>.json   validation verdict
    ├── notes.json               name, tags and annotations, each with its origin
    ├── analysis.md              AI analyses, dated and attributed
    └── replacements/<name>.hlsl + .cso

Games/<Executable>/
    ├── profile.json             known shaders, names given by the user
    ├── tags.json
    ├── state.json               disabled / highlighted / active replacements
    └── captures/
```

The first level `<aa>` is the first two hexadecimal characters of the signature: it keeps any one
directory from holding thousands of entries.

## What is cached

Disassembly, decompilation (per backend), AI clean-up, AI classification, user tags, compiled
replacements.

## Versioning

Every derived artefact carries `{ tool, tool version, date, AI model, prompt version }` (§58). That
is what makes it possible to know a result is stale and regenerate it, rather than recomputing
everything on every open or keeping the output of an obsolete tool forever.

## Matching a shader already seen

1. Same `signature` → the same shader byte for byte, the whole cache applies.
2. Otherwise same `semantic_hash` → the same code with different debug parts: the cache applies,
   and says so.
3. Otherwise: a new shader.

## Access from the interface

Always asynchronous: background indexing, lazy blob loading, virtualised lists. Thousands of
shaders and tens of thousands of draw calls are the normal case (§61), not the edge case.
