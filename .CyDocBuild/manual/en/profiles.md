---
id: profiles
title: Game profiles, tags and persistent state
description: Recognise known shaders and preserve names, tags and intentional changes.
section: User guide
order: 10
parent: shaders
revision: 35981a9ded18
---

# Game profiles, tags and persistent state {#game-profiles-tags-and-persistent-state}

## Recognise previous work {#recognise-previous-work}
The Game profile panel groups what the application knows about the connected game: executable/API/adapter, known and newly seen shaders, frame statistics and persistent decisions. Profiles are associated with the game, while shader analysis and annotations are keyed by signature in the shader database.

Give a shader a useful name, tag or note when the evidence supports it. An annotation retains its origin (user, derived or AI), date and, where applicable, model/prompt version and confidence. Reusing a signature must not turn an AI suggestion into a measured fact.

## Preserve changes across runs {#preserve-changes-across-runs}
The saved state records shader replacements/disables by semantic signature rather than transient shader ID. Inspect the saved-state controls before reapplying work in a later session. Restore/Enable reverses the corresponding live change. A missing shader can simply belong to a scene that has not loaded yet. A changed shader signature should not receive an old replacement blindly.

## Keep data portable {#keep-data-portable}
The application stores readable JSON and shader blobs under its data directories. Back up Database and Games, alongside sources/mod packages you want to keep. Do not delete these folders as a first response to a connection or preview issue. The UI settings JSON and docking INI are separate from analysis data.

An offline capture can be examined without connecting a game; it does not make a live game profile writable or execute stored state. For portable distribution, explicitly export a [mod package](doc:mods) and review its entries.

See [the storage layout and profile reference](doc:ref-database) for individual files, signatures and annotation provenance.


## Illustrations {#illustrations}

### Shader editing workflow {#figure-0}

Keep the original bytecode; review and compile a reconstruction before explicitly applying it.

```diagram
{"type":"flow","title":"Shader editing workflow","nodes":[{"id":"inspect","label":"1. Inspect bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruct / edit","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compile and review","x":600,"y":0,"width":280},{"id":"apply","label":"4. Apply to live session","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observe or restore","x":300,"y":160,"width":280},{"id":"save","label":"6. Save profile / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
