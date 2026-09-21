# dxil-spirv — vendored sources

## What this is

The DXIL → SPIR-V converter used by CyGPUInspector as the first half of the `dxil-spirv`
decompilation backend. It reads the LLVM bitcode a Shader Model 6 shader is made of and emits a
SPIR-V module; SPIRV-Cross, vendored beside it, turns that back into HLSL.

It is the converter behind vkd3d-proton, so it is exercised daily on entire shipped games rather
than on a test suite.

## Where it comes from

- Project: [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv)
- Commit: `bed30ff2115d845fb6bf8752bcbe1c681efb0e01` (2026-09-15), version 2.73.0
- Licence: **MIT**, see [LICENSE.MIT](LICENSE.MIT)

## Licence compliance

MIT imposes one obligation, and it is met: the copyright notice and the permission notice ship
with the sources, both in `LICENSE.MIT` and in the header of every file. MIT is compatible with
the GPL v3 under which CyGPUInspector as a whole is distributed.

The same holds for the components vendored inside this tree, each under its own licence:

| Path | Component | Licence |
|---|---|---|
| `third_party/spirv-headers/` | Khronos SPIR-V headers | MIT / Khronos free use |
| `third_party/glslang-spirv/` | the SPIR-V builder from glslang (LunarG, Google) | BSD-3-Clause |
| `third_party/bc-decoder/` | the LLVM bitcode reader from RenderDoc (Baldur Karlsson) | MIT |
| `subprojects/dxbc-spirv/` | [doitsujin/dxbc-spirv](https://github.com/doitsujin/dxbc-spirv) (Philip Rebohle) | MIT |

## What was taken, and what was left behind

The source tree as it is upstream, minus what only serves upstream's own testing:

| Not copied | Why |
|---|---|
| `reference/`, `shaders/`, `reference-dxbc/` | the reference outputs and test shaders of the upstream test suite — several thousand files, some with paths too long for Windows to check out |
| `external/llvm/` | only needed with `DXIL_SPIRV_NATIVE_LLVM`, which builds a full LLVM. The builtin bitcode reader is used instead, and that is the difference between 10 MB of dependency and a gigabyte |
| `third_party/SPIRV-Tools/`, `third_party/SPIRV-Cross/` | submodules the upstream CLI needs. The CLI is not built. SPIRV-Cross is vendored separately, in `ThirdParty/SPIRV-Cross`, pinned to the commit this one expects |
| `.git/`, `.github/`, `.gitmodules` | version control metadata |

One directory was *added* rather than removed: `subprojects/dxbc-spirv/submodules/spirv_headers/`
holds the Khronos SPIR-V headers, which is what `git submodule update` would have put there. They
are the same headers already vendored at `third_party/spirv-headers`, copied because the sources
of dxbc-spirv include them by that relative path.

**No copied file is modified.** A `diff` against the commit named above is empty for every file
present here, which is what makes the licence correspondence checkable.

## How it is built

`ThirdParty/CMakeLists.txt` adds this directory with `DXIL_SPIRV_CLI=OFF` and
`DXIL_SPIRV_NATIVE_LLVM=OFF`, and links only `dxil-spirv-c-static`, the static build of the C API
declared in `dxil_spirv_c.h`. The CLI tools, the shared library and the install rules are never
reached.

CyGPUInspector also uses **dxbc-spirv** directly, as its own DXBC decompilation backend. The two
files that emit SPIR-V from its intermediate representation (`spirv/spirv_builder.cpp` and
`spirv/spirv_mapping.cpp`) are not part of the `dxbc-spirv` target dxil-spirv declares, because
dxil-spirv feeds the IR into its own emitter instead. They are compiled by a target of **ours**,
`dxbc-spirv-emitter`, declared in `ThirdParty/CMakeLists.txt` — deliberately, rather than by
editing the vendored `third_party/CMakeLists.txt`, which has to stay diff-clean against upstream.

Warnings are turned off for this tree (`/W0`), because the alternative is patching code we
promise is identical to upstream.

## Updating

Re-run the sparse checkout described above at a newer commit, replace this directory, and update
the commit hash and version here and in `CyGPUInspectorDecompiler/Source/DxilSpirvDecompiler.cpp`
(the version there is read at run time from `dxil_spv_get_version`, so only the SPIRV-Cross hash
in the version string is hand written). Keep SPIRV-Cross on whatever commit the new dxil-spirv
pins for `third_party/SPIRV-Cross`: upstream tests the pair together.

If a new release adds a dependency, the build will say so immediately. Resist patching the
vendored files — extend `ThirdParty/CMakeLists.txt` instead, so the `diff` with upstream stays
empty.
