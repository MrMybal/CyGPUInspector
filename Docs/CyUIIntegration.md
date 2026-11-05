# CyUI integration

The standalone vendors **CyUI 1.2.0** at revision
`ff023e646c6a3038b3dfbfd53f6d2b4a2f5955e3` from
[MrMybal/CyUI](https://github.com/MrMybal/CyUI). `ThirdParty/CyUI/ORIGIN.txt`
records the subset. Upstream sources are unchanged; no sibling checkout,
network download or installed CyUI runtime is required by a build.

## Selected presentation modules

| Target | Application use |
| --- | --- |
| `CyUI::Core` | Themes, density, DPI, fonts, widgets, notifications, locale and searchable panel menus |
| `CyUI::CodeEditor` | Editable HLSL, read-only AI reconstructions and disassembly, wrapping, find/replace, markers and Undo/Redo |
| `CyUI::Code` (dependency) | Shared HLSL lexer, token roles and cached syntax; no application lexer copy |
| `CyUI::Timeline` | Read-only frame/pass/command spans and continuous GPU tracks, ruler, zoom, pan and selection |
| `CyUI::Plots` | CPU/GPU frame history, frame budgets, selected frame and visible interval |
| `CyUI::Images` | Live preview, final image and saved capture buffers; fit, zoom, pan and pixel coordinates |
| `CyUI::Inspector` | Filterable read-only captured pipeline properties with exact integer values |
| `CyUI::Diagnostics` | Navigable compiler output and bounded, searchable MCP activity with refusal severity |
| `CyUI::DocumentViews` | Cached Markdown conversation transcript with stable application message IDs; the manual uses CyDoc |
| `CyUI::Canvas` (dependency) | Shared coordinate transforms and clipping for plots, timelines and images |

`CYUI_MODULES` is configured **before** `add_subdirectory`. Explicit targets are
linked instead of the compatibility umbrella. The existing
`CyGPUInspectorImGui` target is supplied through `CYUI_IMGUI_TARGET`: one ImGui
instance, still the application's docking build (1.93 WIP, satisfying >=1.92).
The old independent `ThirdParty/ImGuiColorTextEdit` copy is removed. CyUI owns
the single editor backend and its documented serialization/syntax patches.

Production builds disable CyUI Demo, Audio, Gallery and WebCEF. Gallery may be
enabled for developer inspection with `-DCYGI_BUILD_UI_GALLERY=ON`; it only sees
the modules selected above. No AudioViews, Curves, Nodes, Diff, Assets, Viewport,
Commands, Web, ImGuizmo or browser runtime is needed by the current views.

## Responsibilities and compatibility

The application retains capture, IPC/MCP permissions, provider calls, GPU resource
lifetime, image channel/depth conversion, shader compilation/injection, derived
frame graphs, timing interpretation, storage and document load/save behavior.
The shared views never execute provider requests or mutate measured timings.
Unknown pipeline fields remain unknown. Waiting intervals and estimated pass
positions remain explicitly annotated; host-derived names retain confidence and
provenance. Continuous passes retain overlapping lanes, frame selection and
command inspection. The position slider replaces the former bespoke scrollbar.

The timeline uses the common wheel zoom, middle-drag pan, Shift-wheel horizontal
pan and Ctrl-wheel lane scroll. A host input adapter also retains the former
left/right drag panning shortcuts, without making measured timings editable.
Double-click fits the frame or resumes following
the newest frames. The CPU/GPU plot retains the full values in its tooltip when
the percentile display scale clips a loading spike. Images use wheel zoom,
middle-drag pan and double-click fit. The final-image thumbnail also keeps its
double-click shortcut to Preview.

The application adapter preserves original UTF-8, CRLF, tabs and final-newline
state, its document dirty baseline and compiler marks. Theme/DPI changes do not
reload a document or reset Undo. Compiler diagnostics retain line/column data and
select the corresponding source line. Disassembly keeps line filtering, wrap,
selection and copying the complete original output. Derived code is prepared on
document/filter revisions, not reconstructed every frame.

Existing `Lang/*.json` files remain the application catalog format, including
runtime-added languages and translation-template support. The UI registers them
with CyUI and synchronizes the selected locale. Common controls use CyUI's
English/French catalogs; application windows retain their existing English
hidden IDs. User content, code, paths and provider responses are not translated.
Conversation IDs survive append/eviction and are not sent in provider prompts.
The common syntax engine has a 32 MiB snapshot limit. Larger HLSL documents remain
fully editable with highlighting disabled and an explicit notice; they are never
truncated. Disassembly uses the read-only backend without lexical preparation.

Dock layouts, additive appearance preferences, provider configuration, captures,
profiles and databases retain their existing formats. No settings migration runs.
Conversation links are copied for review; provider text cannot launch a browser.

## Limits and integrations

CyUI's Markdown renderer is a native subset, not a complete CommonMark/HTML
engine. It adds formatted conversation display without a browser dependency.
The resource/event tables, capture comparison and reconstructed frame-graph
list remain domain views: sorting, correlations, confidence, renaming and capture
actions are application-specific. They are not replaced by a generic asset tree,
editable node graph or text diff. The Unreal plugin continues to use its existing
Slate/editor and runtime capture integration; it does not acquire a second UI
backend or an Editor dependency in runtime modules.

## Build and validation

The product targets **Windows x64**, as before. Use Visual Studio 2022, Desktop
C++ tools including C++20, the Windows SDK, CMake and Ninja (`build.cmd` locates
the Visual Studio tools). Core stays C++17; the CodeEditor dependency propagates
C++20 to its consumers. Linux/macOS product builds are not provided by this
Win32/D3D application; CyUI itself supports other hosts.

```powershell
build.cmd Release
python -m unittest discover -s Tests -p test_package_release.py -v
python Tests/check_interface.py --output build/ui-check
```

Run the native Core, Assistant, Shader, IPC and Interface test executables from
an isolated runtime directory, with FakeSession, MCP and the capture add-on beside
them. `CyGPUInspectorInterfaceTests` exercises real ImGui frames without a game:
editor bytes, typing, keyboard Undo/Redo, readonly state, themes/density/DPI,
timeline selection/navigation, image coordinates, property filtering/precision,
transcript cache eviction and MCP log filtering. The render script exercises the
real D3D11 application across themes, densities, UI scale and migrated panels,
using synthetic frame data and private settings. These checks do not validate a
real game's capture path or call a paid AI provider.

`build.cmd` now uses `build/Native-Release` (or the corresponding configuration)
for its native cache, keeping `build/Release` archives intact. It selects UTF-8
console output and a stable compiler language for reliable Ninja header dependencies;
a French-only MSVC installation is supported as well. The former locale-dependent
cache is deliberately not reused. For manual CMake runs, configure from a UTF-8
developer console (`chcp 65001`) too.

For a clean configuration use a fresh CMake binary directory rather than deleting
user settings or an earlier package. Verify its target list contains only the
selected module closure and the host's ImGui target. Rebuild with no sibling CyUI
checkout paths in the cache or link command.

`LICENCES/` and the compatibility notice folder in distribution archives preserve
CyUI's MIT text, its dependency inventory, TextEditor snapshot, and the full
Lexilla, Scintilla and SciTE licenses. Packaging compares notices byte for byte
against their vendored sources; the product remains AGPL-3.0-or-later. See
[THIRD-PARTY.md](../THIRD-PARTY.md) and [Licensing.md](Licensing.md).

The [CyUI MIT license](https://github.com/MrMybal/CyUI/blob/ff023e646c6a3038b3dfbfd53f6d2b4a2f5955e3/LICENSE)
is also available upstream to users with repository access. The repository is
currently private; the full license is included locally and in the package at
`LICENCES/CyUI/LICENSE.txt`.
