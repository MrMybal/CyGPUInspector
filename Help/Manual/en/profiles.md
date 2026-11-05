# Game profiles, tags and persistent state

## Recognise previous work
The Game profile panel groups what the application knows about the connected game: executable/API/adapter, known and newly seen shaders, frame statistics and persistent decisions. Profiles are associated with the game, while shader analysis and annotations are keyed by signature in the shader database.

Give a shader a useful name, tag or note when the evidence supports it. An annotation retains its origin (user, derived or AI), date and, where applicable, model/prompt version and confidence. Reusing a signature must not turn an AI suggestion into a measured fact.

## Preserve changes across runs
The saved state records shader replacements/disables by semantic signature rather than transient shader ID. Inspect the saved-state controls before reapplying work in a later session. Restore/Enable reverses the corresponding live change. A missing shader can simply belong to a scene that has not loaded yet. A changed shader signature should not receive an old replacement blindly.

## Keep data portable
The application stores readable JSON and shader blobs under its data directories. Back up Database and Games, alongside sources/mod packages you want to keep. Do not delete these folders as a first response to a connection or preview issue. The UI settings JSON and docking INI are separate from analysis data.

An offline capture can be examined without connecting a game; it does not make a live game profile writable or execute stored state. For portable distribution, explicitly export a [mod package](help:mods) and review its entries.

See [the storage layout and profile reference](help:ref-Database) for individual files, signatures and annotation provenance.
