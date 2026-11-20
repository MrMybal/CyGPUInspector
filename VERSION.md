# CyGPUInspector 0.5.1

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.5.1)

**Stage: Alpha — experimental.** Available for testing and feedback; functionality and compatibility
may be incomplete. This designation describes the application, not the maturity of its dependencies.

## Published build

- Version **0.5.1**, published **20 November 2026**.
- **Windows x64**, portable ZIP: [download](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-0.5.1-win64.zip).
- [SHA-256 file](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-0.5.1-win64.zip.sha256).

## Changes in 0.5.1

- CyMCPSDK **0.7.2**, revision `e562c731a146d599e3404718ea8414bb5b96b66d`, includes the
  local access profiles introduced in `34d4bca` and is compiled from pinned local source.
- Local HTTP MCP is enabled by default at `http://127.0.0.1:41823/mcp`. The MCP panel can disable
  the endpoint, copy its actual address and select an access profile.
- **Free** is the default: all **111 registered operations** are allowed without a token or
  additional confirmation. Argument schemas and business prerequisites remain enforced.
- **Local controlled**, **Token only** and **Protected** remain available. Controlled profiles
  apply the host permission level; token profiles require the in-memory session token, which can
  be copied or rotated in the panel. Protected requires both authentication and host permission.
- Configurations without an explicit profile migrate to Free; explicit profiles and unrelated
  user preferences are retained. Explicit legacy permission settings retain controlled access.
- Existing stdio/CLI commands and built-in chat modes remain available. HTTP calls execute on
  the UI thread through the shared operation registry. The chat cancellation test now waits for
  its local provider fixture to receive the request before cancellation.
- Updated English/French manual with **49 topics per language**, search, images, diagrams and
  API references. [Download the full offline HTML reader](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-Documentation-0.5.1.zip).

CyUI **1.2.0** (`ff023e6`) and CyDoc **0.1.0** (`6ddf9a5`) are unchanged.
The native application, companion and add-ons are rebuilt. Existing Unreal wrapper binaries
are reused; this release does not claim a new Unreal Engine module build.
GitHub Pages remains on its previous deployment; deployments require an explicit manual workflow.
The bundled and downloadable HTML manuals are current. Existing published release assets remain unchanged.

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
