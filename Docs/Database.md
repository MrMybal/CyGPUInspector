# CyGPUInspectorDatabase

*Also available in [French](fr/Database.md).*

**State: implemented and tested**: the shader store by signature (`ShaderStore`), tags and
annotations (`ShaderNotes`), and a profile per game with its persistent state (`GameProfile`).

## Principle

Everything is on disk, in files a person can read: JSON for what describes things, the files
themselves for the large blobs. There is no database engine to install or to repair, and anything
can be looked at, diffed or edited by hand. Nothing expensive is computed twice: what is derived from
a shader is cached **by shader signature**, so it is recognised from one run of a game to the next,
and even from one game to another.

```
Database/
└── shaders/<aa>/<signature>/
    ├── metadata.json            stage, API, shader model, sizes, first and last seen
    ├── original.dxbc | .dxil
    ├── disassembly.txt
    ├── decompiled/<backend>-<version>.hlsl   one file per backend, never overwritten
    ├── decompiled/<backend>-<version>.json   validation verdict
    └── notes.json               name, tags and annotations (user, derived, AI), each with its origin

Games/<executable>/
    ├── profile.json             the game, its sessions, every shader seen in it
    └── state.cygimod/           the shaders left disabled or replaced (section 38)
```

The first level `<aa>` is the first two hexadecimal characters of the signature: it keeps any one
directory from holding thousands of entries. `Database/` and `Games/` sit next to the executable,
like the captures: the tool stays portable.

## What is cached

Disassembly, decompilation (per backend), AI clean-up, AI classification and explanation, user tags
and names.

## Versioning

Every derived artefact carries `{ tool, tool version, date, AI model, prompt version }` (§58). That
is what makes it possible to know a result is stale and regenerate it, rather than recomputing
everything on every open or keeping the output of an obsolete tool forever.

## Matching a shader already seen

1. Same `signature` → the same shader byte for byte, the whole cache applies.
2. Otherwise same `semantic_hash` → the same code with different debug parts: the cache applies,
   and says so.
3. Otherwise: a new shader.

## Game profiles (§63)

A profile opens by itself when a game connects, or when a capture of it is opened, in
`Games/<executable>/`. `profile.json` holds:

* the executable, the graphics API and the adapter, the first and last session, the number of
  sessions;
* every shader the game has sent, by signature: its semantic hash, stage, format and shader model,
  when it was first and last seen, and in how many sessions.

The **Game profile** panel shows it. For the session in progress it says how many shaders are new,
how many were seen in an earlier session, and how many have the same code as a known one; the
shader list marks the new ones. It also lists the captures of that game. The `get_game_profile` MCP
tool gives the same to an agent, and to the AI assistant: *what is new since the last session?*

The profile is saved every few seconds while it changes, and when the game disconnects or the
application closes. A capture opened from disk shows the profile of its game without changing it.

## Persistent state (§38)

What was changed in a game — shaders disabled, shaders replaced, a magenta highlight — is lost when
the game exits: the add-on holds it, and shader ids mean nothing in the next run. **Save this
session's changes** writes it to `Games/<executable>/state.cygimod/`, a mod package keyed by
semantic hash, the format CyGPUInjector reads. The changes of this session replace the saved ones
for the same shaders; saved changes for shaders this session did not load are kept.

**Reapply now**, or **Reapply when this game connects**, applies them again: each change is sent
when the game has loaded its shader, which can be minutes into a session. A saved replacement is
checked against the shader the game has now, the same check as a new replacement, and is not sent
when a patch has changed that shader. Reapplied changes are recorded like any other, so they can be
exported and saved again.

## Access from the interface

Always asynchronous or bounded: background disassembly and decompilation, lazy loading of notes,
virtualised lists. Thousands of shaders and tens of thousands of draw calls are the normal case
(§61), not the edge case.
