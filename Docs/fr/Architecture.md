# Architecture — index

*Disponible aussi en [anglais](../Architecture.md), qui fait foi.*

Le document d'architecture complet est [CyGPUInspector_Architecture.md](CyGPUInspector_Architecture.md).
Ce fichier n'est qu'une table des matières de `Docs/`.

## Recherche (faite avant l'implémentation)

| Document | Contenu |
|---|---|
| [Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md) | ce que l'API add-on ReShade garantit réellement, vérifié dans le SDK |
| [Research/ShaderTooling.md](Research/ShaderTooling.md) | désassemblage, décompilation, licences, limites honnêtes |

## Sous-systèmes

| Document | État |
|---|---|
| [ReShadeAddon.md](ReShadeAddon.md) | implémenté (milestone 1) — l'add-on de capture |
| [StandaloneApp.md](StandaloneApp.md) | implémenté (milestone 2) |
| [IPC.md](IPC.md) | implémenté |
| [ShaderTracking.md](ShaderTracking.md) | implémenté |
| [ResourceTracking.md](ResourceTracking.md) | implémenté |
| [GPUSharing.md](GPUSharing.md) | implémenté (milestone 3) |
| [FrameGraph.md](FrameGraph.md) | implémenté (milestone 4) |
| [CaptureModes.md](CaptureModes.md) | implémenté : les deux captures, runtime et profonde, et ce que ni l'une ni l'autre ne fait |
| [ShaderDecompiler.md](ShaderDecompiler.md) | implémenté : désassemblage, réflexion, compilation, quatre backends de décompilation |
| [ShaderReplacement.md](ShaderReplacement.md) | implémenté (milestone 7) |
| [ModPackages.md](ModPackages.md) | implémenté : exporter une modification et la rejouer avec CyGPUInjector |
| [GPUProfiling.md](GPUProfiling.md) | implémenté (milestone 8) |
| [Database.md](Database.md) | blobs, tags et annotations implémentés ; SQLite à venir |
| [MCP.md](MCP.md) | implémenté (milestone 10) |
| [AI.md](AI.md) | fondations implémentées (milestone 9) |
| [Translating.md](Translating.md) | implémenté : l'interface et cette documentation, dans d'autres langues |
| [Limitations.md](Limitations.md) | vivant — à lire avant de signaler un bug |
