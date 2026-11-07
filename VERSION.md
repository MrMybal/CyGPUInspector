# CyGPUInspector 0.4.1

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.4.1)

**Stage: Alpha — experimental.** Available for testing and feedback; functionality and compatibility
may be incomplete. This designation describes the application, not the maturity of its dependencies.

## Published build

- Version **0.4.1**, published **6 November 2026**.
- **Windows x64**, portable ZIP: [download](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.4.1/CyGPUInspector-0.4.1-win64.zip).
- [SHA-256 file](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.4.1/CyGPUInspector-0.4.1-win64.zip.sha256).

## Changes in 0.4.1

- Complete English/French manual with 48 topics, screenshots, diagrams and search.
- Updated CyDoc reader with header search, nested navigation, quick tooltips and a wider sidebar.
- Public C/C++ API references with signatures, parameters and examples.
- F1 and contextual help open the bundled `Documentation/Documentation.html` reader without a
  server or Internet connection.
- Portable packages include the full manual and a separate `DocAI.md` sidecar.

The reader is CyDoc **0.1.0**, revision `6ddf9a5`; the native interface uses CyUI **1.2.0**,
revision `ff023e6`. These are component versions, not additional application releases.
Existing Unreal plugin binaries are reused; this release does not claim a new Unreal module build.

The repository's README presentation and Alpha designation were updated after the 0.4.1 package
was published. They do not imply a replacement ZIP or additional functionality in that release.

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

The published 0.4.1 notes do not provide a bug-by-bug list. Use the
[issue tracker](https://github.com/MrMybal/CyGPUInspector/issues) for current reports and to submit
reproducible problems. The limitations above are not confirmed issue tickets.

## Updating

Extract the entire package and preserve existing preferences, layout, shader database, game
profiles and captures. Keep the manual and DLLs with the executable, and retain all license notices.
See the [installation guide](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/en/installation).
