# CyGPUInspector 0.5.0

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.5.0)

**Stage: Alpha — experimental.** Available for testing and feedback; functionality and compatibility
may be incomplete. This designation describes the application, not the maturity of its dependencies.

## Published build

- Version **0.5.0**, published **19 November 2026**.
- **Windows x64**, portable ZIP: [download](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-0.5.0-win64.zip).
- [SHA-256 file](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-0.5.0-win64.zip.sha256).

## Changes in 0.5.0

- CyMCPSDK **0.7.0**, revision `791d4d7`, provides native Codex sessions, streamed HTTP conversations,
  bounded Claude integration, cancellation and optional private conversation checkpoints.
- **111 shared operations** (34 GPU tools and 77 application operations) cover connection, captures,
  filters, layouts, previews, annotations, HLSL editing, mods and assistant controls from chat, CLI and MCP.
- Managed exports and backups preserve recoverable notes, editor drafts, layouts and game profiles.
- Updated English/French manual: **49 topics per language**, images, diagrams, search and public API references.
  F1 and contextual help open the complete local reader without a server or Internet connection.
- [Complete offline HTML manual](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-Documentation-0.5.0.zip)
  with its scripts, styles and resources is also available separately.

CyUI **1.2.0** (`ff023e6`) and CyDoc **0.1.0** (`6ddf9a5`) are unchanged component versions.
The application, proxy and native capture add-on are rebuilt. Existing Unreal wrapper binaries
are reused; this release does not claim a new Unreal Engine module build.
GitHub Pages deployment is manual-only; the hosted manual remains on its previous deployment
until explicitly deployed. The bundled manual and downloadable HTML export are current.
Previously published 0.4.1 assets remain unchanged.

## Known limitations

- Windows x64 with supported D3D11/12 workflows; Vulkan and OpenGL are not supported.
- No frame replay, pixel history, shader stepping or recovery of original HLSL source.
- Ordinary saved frame captures contain metadata, not live GPU pixels; deep buffer exports are
  a separate operation and can fail for individual resources.
- Reconstructed passes, shader code and AI interpretations have limits; timings depend on
  available events, query capacity and capture level.
- ReShade must be accepted by the game. There is no bypass for anti-cheat or other protections.

See [troubleshooting and limitations](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/en/troubleshooting)
and the [full limits reference](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/en/ref-limitations).

## Bugs and feedback

Use the
[issue tracker](https://github.com/MrMybal/CyGPUInspector/issues) for current reports and to submit
reproducible problems. The limitations above are not confirmed issue tickets.

## Updating

Extract the entire package and preserve existing preferences, layout, shader database, game
profiles, captures and managed Automation backups. Optional assistant history remains in Local AppData. Keep the manual and DLLs with the executable, and retain all license notices.
See the [installation guide](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/en/installation).
