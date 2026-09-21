# SPIRV-Cross — vendored sources

## What this is

The SPIR-V → HLSL emitter used by CyGPUInspector as the second half of the `dxil-spirv`
decompilation backend. dxil-spirv turns a Shader Model 6 shader into SPIR-V; this turns that
SPIR-V back into HLSL.

## Where it comes from

- Project: [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- Commit: `be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a` (2026-09-07)
- Licence: **Apache-2.0 OR MIT**, at the user's option, plus the Khronos free use licence for the
  SPIR-V headers it carries. See [LICENSE](LICENSE) and [LICENSES/](LICENSES).

The commit is not the newest one: it is the commit `dxil-spirv` pins for its own
`third_party/SPIRV-Cross` submodule, so the two halves of the chain are the pair that upstream
tests together.

## Licence compliance

Apache-2.0 and MIT are both compatible with the GPL v3 under which CyGPUInspector as a whole is
distributed. Their obligations are met: the licence texts ship with the sources, and the copyright
and SPDX headers inside every file are untouched.

## What was taken, and what was left behind

The source tree as it is upstream, minus what only serves upstream's own testing:

| Not copied | Why |
|---|---|
| `reference/`, `shaders*/` | the reference outputs and test shaders of the upstream test suite, several thousand files |
| `tests-other/`, `samples/` | test harnesses and examples |
| `.git/`, `.github/`, `.gitignore` | version control metadata |

**No copied file is modified.**

## How it is built

`ThirdParty/CMakeLists.txt` builds the GLSL and HLSL backends only, as static libraries, and links
`spirv-cross-hlsl`. The MSL, C++ and JSON reflection targets, the C API, the utility module, the
CLI and the install rules are all switched off.

## Updating

Move to whatever commit the matching `dxil-spirv` pins, replace this directory, and update the
hash here, in `ThirdParty/dxil-spirv/ORIGIN.md`, and in the hand written part of the backend
version string in `CyGPUInspectorDecompiler/Source/DxilSpirvDecompiler.cpp`.
