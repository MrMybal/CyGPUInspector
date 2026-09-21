# Third party components

CyGPUInspector is distributed under the **GNU Affero General Public License v3 or later**
(see [LICENSE](LICENSE)). The components below are vendored in `ThirdParty/`, each with its own
licence text, and each of them can be combined with the AGPL v3 under which the whole is
distributed: the permissive ones (MIT, BSD, Apache-2.0) directly, and the 3Dmigoto decompiler, which
is under the GNU GPL v3, through section 13 of both licences, which expressly allows a GPL v3 work
and an AGPL v3 work to be linked into a single one.

| Component | Version | Licence | Used by | Vendored in |
|---|---|---|---|---|
| **ReShade SDK** (headers only) | 6.8.x, API 20 | BSD-3-Clause | the add-on | `ThirdParty/reshade/` |
| **Dear ImGui** (docking) | 1.93 WIP | MIT | the standalone | `ThirdParty/imgui/` |
| **Dear ImGui** (headers only) | 1.92.5 | MIT | the add-on overlay, matching ReShade's own version | `ThirdParty/reshade_imgui/` |
| **3Dmigoto HLSL decompiler** | commit `8f329bd` | **GPL-3.0** | the `hlsldecompiler` decompilation backend | `ThirdParty/hlsldecompiler/` |
| **dxil-spirv** | 2.73.0, commit `bed30ff` | MIT | the `dxil-spirv` backend, first half: DXIL → SPIR-V | `ThirdParty/dxil-spirv/` |
| **SPIRV-Cross** | commit `be71ee8c` | Apache-2.0 OR MIT | the `dxil-spirv` backend, second half: SPIR-V → HLSL | `ThirdParty/SPIRV-Cross/` |

`dxil-spirv` carries four components of its own, vendored inside its tree and each under its own
notice: the Khronos **SPIR-V headers** (MIT / Khronos free use), the SPIR-V builder from
**glslang** (BSD-3-Clause), the LLVM bitcode reader from **RenderDoc** (MIT, Baldur Karlsson) and
**dxbc-spirv** (MIT, Philip Rebohle). All are GPL-compatible.

## ReShade itself, for the Unreal plugin

The Unreal plugin loads **ReShade64.dll**, the official, unmodified "with full add-on support"
build of **ReShade 6.8.0** (BSD-3-Clause, Patrick Mours), into the editor and into Development and
DebugGame builds. It is **not in this repository**: release zips of the plugin carry it, and
`Tools/fetch_reshade.py` downloads it from reshade.me for anyone building from source, checks it
against the SHA-256 it records, and reads it out of the installer's archive without running the
installer.

Wherever the DLL goes, two notices go with it, kept in the repository next to where it is put
(`CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/`):

* `LICENSE-ReShade.txt` — ReShade's own licence, verbatim;
* `NOTICES-ReShade.txt` — the full licences of the libraries compiled into the DLL: Dear ImGui
  (MIT), MinHook with the Hacker Disassembler Engine (BSD-2-Clause), stb (MIT or public domain),
  glad (MIT), utfcpp (BSL-1.0), SPIRV-Headers (MIT-style, Khronos), Vulkan Memory Allocator (MIT),
  DirectX-Headers (MIT), OpenXR SDK (Apache-2.0), simple-lossless-encoder (BSD-3-Clause), the
  D3D11On12 / D3D9On12 headers (MIT), the D3D12On7 headers (Microsoft Software License Terms),
  OpenVR (BSD-3-Clause), fpng and stb_image_dds (public domain).

CyGPUInspector is not affiliated with, nor endorsed by, ReShade or its author.

**dxbc-spirv** is used directly as well, as the `dxbc-spirv` decompilation backend, not only as a
dependency of dxil-spirv. It stands in for `vkd3d-shader`, which cannot be built with this
toolchain; `Docs/ShaderDecompiler.md` lists exactly which generated headers it would need.

## 3Dmigoto

The DXBC → HLSL decompiler of [bo3b/3Dmigoto](https://github.com/bo3b/3Dmigoto), including the
`BinaryDecompiler` layer it depends on, which itself derives from **HLSLcc** by James Jones.

It is **GPL-3.0**. It can be linked directly into CyGPUInspector, which is AGPL-3.0, rather than
isolated in a separate process, because section 13 of the GPL v3 and section 13 of the AGPL v3 each
allow combining a work under one with a work under the other: 3Dmigoto's files keep the GPL v3, the
rest keeps the AGPL v3. The obligations are met in full:

- the licence text ships with the sources, at `ThirdParty/hlsldecompiler/LICENSE.GPL.txt`;
- the copyright and licence headers inside the vendored files are untouched;
- the vendored files are **unmodified**, so the correspondence with upstream can be verified with a
  plain `diff` against the commit named above. Everything CyGPUInspector had to add lives beside
  them, in `ThirdParty/hlsldecompiler/shim/`, and is clearly marked as not being 3Dmigoto's;
- the complete source of CyGPUInspector is distributed, under the AGPL v3.

`ThirdParty/hlsldecompiler/ORIGIN.md` records exactly which files were taken, from which commit,
what the shim replaces and why, and how to update to a newer version.

Only the decompiler is used. None of 3Dmigoto's DirectX wrapper, hooking or injection code is
present: CyGPUInspector captures through the official ReShade add-on API.

## dxil-spirv and SPIRV-Cross

The Shader Model 6 chain. Both are permissively licensed, both are vendored unmodified, and the
obligation each imposes — carry the copyright and permission notices — is met by shipping the
licence files and leaving every file header untouched.

Only the converter is built: no CLI tools, no shared libraries, and, in the case of dxil-spirv, the
builtin LLVM bitcode reader rather than a full LLVM, which is why this dependency is 13 MB of
sources instead of a gigabyte. Upstream's reference outputs and test shaders are not copied.

The versions are a matched pair: SPIRV-Cross is pinned to the commit dxil-spirv itself uses, so
CyGPUInspector consumes the combination upstream tests. `ThirdParty/dxil-spirv/ORIGIN.md` and
`ThirdParty/SPIRV-Cross/ORIGIN.md` record the commits, what was left behind and why, and how to
move to a newer version.

## Tools loaded at run time

These are not vendored in the sources: they are loaded from the system or the Windows SDK when
present, and their absence is reported in the application rather than being fatal.

`dxcompiler.dll` and `dxil.dll` **are** copied next to the application in a binary distribution,
because without them Shader Model 6 cannot be disassembled, reflected or compiled at all. They come
from the Windows SDK, carry the University of Illinois / NCSA open source licence, and Microsoft
permits their redistribution with an application.

| Tool | Licence | Used for |
|---|---|---|
| `d3dcompiler_47.dll` | Microsoft, part of Windows | DXBC disassembly, reflection and compilation |
| `dxcompiler.dll` / `dxil.dll` | University of Illinois / NCSA | DXIL disassembly, reflection and compilation |
