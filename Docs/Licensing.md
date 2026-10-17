# Licensing and distribution

CyGPUInspector **0.3.0** and its Unreal plugin remain under **GNU AGPL v3 or later**.
The full product license is in [LICENSE](../LICENSE) and
[LICENCES/CyGPUInspector-AGPL-3.0.txt](../LICENCES/CyGPUInspector-AGPL-3.0.txt).
Third-party license texts and copyright notices are preserved unchanged.
See [THIRD-PARTY.md](../THIRD-PARTY.md) for the component inventory.

## SDK scope

The assistant, provider transports and MCP server are native C++ implementations
owned by CyGPUInspector. Neither **CyToolsCore** nor **CyUEMCPSDK** is embedded,
installed or required. There is no Python companion or assistant runtime bundle.
## What to ship

Every release contains a root **LICENCES/** folder with complete product and
third-party texts and **SOURCES.txt** with provenance and source links. The legacy
**Licenses/** directory remains in binary releases for compatibility with existing
distribution instructions. It is not the canonical license index.

Each separately copyable Unreal plugin contains its own **LICENCES/** with the
product license and notices for ReShade, its SDK and the add-on's ImGui headers.
ReShade's full license and dependency notices also remain next to its DLL.
`FilterPlugin.ini` includes these files in `BuildPlugin` output; the existing
runtime modules stage notices as loose **NonUFS** files into packaged games.

When DXC is included, copy its original `LICENCE-MIT.txt`, `LICENSE-LLVM.txt`,
`LICENSE-MS.txt` and `ORIGIN.txt` into **LICENCES/DirectXShaderCompiler/** too.
Preserve the selected DXC version and binaries; the interface update does not
select newer compiler dependencies.

`Tools/package_release.py` compares delivered notices byte for byte against their
source texts, checks the staging tree and reads back the final ZIP, including CRC
verification. Missing or changed required notices fail packaging. For notices
embedded in source headers, the complete comment is extracted without comment
markers. Keep the source notice and generated text synchronized.

## Update and rollback

Build the application normally and rebuild the plugin with `RunUAT BuildPlugin`
for each engine version being delivered. Then package into a separate output
directory to preserve an earlier release:

```powershell
python Tools/package_release.py --plugin-builds C:/CyGPUInspectorBuild --dxc <DXC-release-folder> --output-root build/Release
```

Inspect the actual ZIP, not only the source folders. Run the test suites from its
`TestWithoutAGame/` directory and verify the native MCP handshake and tool discovery
from the delivered executable. Do not call an engine version validated without
executing its build. Live captures and paid provider calls require their own tests.
The packager refuses to overwrite an existing staging folder or release archive;
choose a new output directory for each rebuild.

Extract updates into a new folder. Retain user-owned `CyGPUInspectorApp.ini`,
`CyGPUInspectorApp.settings.json`, databases, game profiles and captures; none are
part of the release ZIP. Preserve Unreal project settings and external binary path
overrides. Reuse the existing settings when starting the new copy. To roll back,
restore the previous application/plugin files and retain those user-owned files.
Appearance preferences are additive; existing provider configuration and tool IDs
are retained, without configuration conversion or a new assistant setup.
