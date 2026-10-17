# 3Dmigoto HLSL decompiler — vendored sources

## What this is

The DXBC → HLSL decompiler of **3Dmigoto**, used by CyGPUInspector as the `hlsldecompiler`
backend. It is the best open source reconstruction of Shader Model 4 and 5 shaders available.

## Where it comes from

- Project: [bo3b/3Dmigoto](https://github.com/bo3b/3Dmigoto)
- Commit: `8f329bd94fecc9bbcb9211ffd42a95dd7fe6b43e` (2026-08-19)
- Licence: **GNU General Public License v3**, see [LICENSE.GPL.txt](LICENSE.GPL.txt)

3Dmigoto was started by Chiri and is maintained by bo3b and DarkStarSword. The decompiler's
binary parsing layer (`BinaryDecompiler/`) itself derives from **HLSLcc** by James Jones.

## Licence compliance

CyGPUInspector is published under the **GNU GPL v3 or later**, so this code is linked directly
rather than isolated in a separate process. The obligations are met as follows:

- the licence text ships with the sources, in `LICENSE.GPL.txt`;
- the copyright and licence headers inside every vendored file are untouched;
- the vendored files are **unmodified**, so the correspondence with upstream is verifiable by a
  plain `diff` against the commit named above;
- the whole of CyGPUInspector is distributed under the GPL v3, sources included.

## Which files were taken

| Path here | Path upstream |
|---|---|
| `DecompileHLSL.cpp`, `DecompileHLSL.h` | `HLSLDecompiler/` |
| `BinaryDecompiler/decode.cpp`, `decodeDX9.cpp`, `reflect.cpp` | `BinaryDecompiler/` |
| `BinaryDecompiler/include/`, `BinaryDecompiler/internal_includes/` | idem |
| `LICENSE.GPL.txt` | repository root |

About 12 700 lines in total. Nothing else from 3Dmigoto is used: not its DirectX wrapper, not its
hooking, not its injection machinery. CyGPUInspector does its own capture through the official
ReShade add-on API.

## What was added, and why

Everything in `shim/` is **ours**, not 3Dmigoto's. It exists so that the vendored files can stay
identical to upstream while still building outside 3Dmigoto:

| File | Replaces | Why |
|---|---|---|
| `shim/log.h`, `shim/Shim.cpp` | 3Dmigoto's `log.h` | `LogInfo` / `LogDebug` write into 3Dmigoto's own log file inside a game process. Here the decompiler runs in the standalone, so the messages are collected in memory and attached to the decompilation result, where the user will actually see them. |
| `shim/version.h` | 3Dmigoto's `version.h` | Supplies `VER_FILE_VERSION_STR`, which the decompiler stamps into the HLSL it produces. The stamp is kept because it is honest about the origin of the reconstruction. |

## Updating

Replace the files listed above from a newer commit, update the commit hash here and in
`shim/version.h`, and rebuild. If upstream adds a dependency, the build will say so immediately;
resist the urge to patch the vendored files — extend the shim instead, so the `diff` with upstream
stays empty.
