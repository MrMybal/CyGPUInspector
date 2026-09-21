# Research — shader disassembly, decompilation and compilation

*Also available in [French](../fr/Research/ShaderTooling.md).*

## 1. Formats encountered

| API | Container | Shader Model | Contents |
|---|---|---|---|
| D3D11 | DXBC | SM 4.0 – 5.1 | "TokenizedProgramFormat" byte code, readable assembly, full reflection |
| D3D12 | DXBC container with a **DXIL** part | SM 6.0 – 6.9 | LLVM 3.7 bitcode, encapsulated and signed |
| Vulkan | SPIR-V | — | a SPIR-V module (later) |
| OpenGL | GLSL text | — | already source (later) |

The first two are the priority target.

## 2. Disassembly — available with no external dependency

* **DXBC**: `D3DDisassemble()` from `d3dcompiler_47.dll`, present on every Windows 10/11 machine
  and in the Windows SDK. Gives assembly like `ps_5_0 / dcl_… / mad r0.xyzw, …`.
* **DXBC reflection**: `D3DReflect()` → `ID3D11ShaderReflection`: resource bindings, input and
  output signatures, constant buffer layout, instruction count. That is where the "resources read
  and written" metadata comes from, without decompiling anything.
* **DXIL**: `IDxcCompiler3::Disassemble()` from `dxcompiler.dll`. The Windows SDK installed on this
  machine supplies `dxc.exe`, `dxcompiler.dll` and `dxil.dll` in
  `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\`.
* **DXIL reflection**: `D3DReflect` does **not** work on DXIL. It has to be
  `IDxcUtils::CreateReflection()` / `IDxcContainerReflection` (same DLL).

Conclusion: disassembly (milestone 5) needs no questionable third-party dependency, and can be done
**on the standalone side**, outside the game.

## 3. Decompilation — the actual state of the art

| Backend | Input → output | Licence | Quality | Status in the project |
|---|---|---|---|---|
| **3Dmigoto HLSLDecompiler** | DXBC SM4/SM5 → HLSL | **GPL-3.0** (`LICENSE.GPL.txt`) | The best available for SM5: control flow rebuilt, resource names from reflection | Main DXBC backend — integrable directly, CyGPUInspector being AGPLv3, which section 13 of both licences allows to combine with it. ✔ shipped |
| **dxbc-spirv + SPIRV-Cross** | DXBC → SPIR-V → HLSL | MIT + Apache-2.0 | A real compiler: SSA, optimisation passes, readable output, but reordered | Second opinion on DXBC. ✔ shipped |
| **dxil-spirv + SPIRV-Cross** | DXIL → SPIR-V → HLSL/GLSL | MIT (`LICENSE.MIT`) + Apache-2.0 | Compilable and semantically correct, but very machine-like: many `_123` temporaries, control flow shaped by the structurizer | Main DXIL backend. ✔ shipped |
| **vkd3d-shader** | DXBC → SPIR-V/HLSL | LGPL-2.1 (AGPLv3 compatible) | Another DXBC option, less readable | **Not buildable with this toolchain** — see [ShaderDecompiler.md](../ShaderDecompiler.md) |
| **DXC (`dxcompiler`)** | HLSL → DXBC/DXIL, disassembly, reflection | NCSA / University of Illinois | — | Always used: disassembly, validation, recompilation |

An AI reconstruction (milestone 9) comes **after** one of these backends, never instead of one.

### Multi-backend policy

Every backend produces an artefact **stored separately** in the database:
`decompiled/<backend>-<version>.hlsl`. No result ever overwrites another. The interface and the AI
can compare outputs, pick the best reconstruction, or combine fragments of them. A backend is
described by `{ name, version, licence, supported input formats, shader models }` and registers
with `CyGPUInspectorDecompiler`.

### Honest limits to state in the interface

* The original HLSL source **does not exist** in the binary: everything produced is a
  reconstruction. Variable names, function names and comments are lost at compilation.
* DXIL loses even more information than DXBC (LLVM inlining, SROA, vectorisation destroyed): a DXIL
  reconstruction will systematically be less readable. The user has to be told, rather than left to
  think the tool failed.
* A decompiler can produce HLSL **that does not recompile**. That is expected, not a blocking bug:
  the validation state is part of the metadata.

## 4. Validating a reconstruction

```
Reconstructed HLSL → DXC/FXC (same shader model) → byte code
                                                       ↓
                        disassembly compared with the original disassembly
```

Three verdict levels, stored per shader and per backend:

1. `compile_failed` — the reconstructed HLSL does not compile.
2. `compiles` — it compiles for the right shader model, with compatible I/O signatures and
   bindings.
3. `equivalent_disasm` — the recompiled disassembly is close to the original (normalised comparison
   on opcodes). The only level that justifies calling a reconstruction faithful.

## 5. Compiling replacement shaders — the D3D12 trap

D3D12 **refuses** an unsigned DXIL shader unless developer mode is on. The signature is only
produced if **`dxil.dll` is present next to `dxcompiler.dll`** at compilation time. That is the
classic cause of "my replacement shader is rejected". CyGPUInspectorApp checks for `dxil.dll` and
shows the result in its diagnostics panel. On D3D11 (FXC/DXBC) the problem does not exist.

## 6. What is vendored

`ThirdParty/` holds, with full licences and origin notices preserved:

* `reshade/` — SDK headers (BSD-3-Clause) ✔
* `imgui/` — Dear ImGui docking (MIT) ✔
* `reshade_imgui/` — ImGui headers **at ReShade's version** for the add-on overlays ✔
* `hlsldecompiler/` — the 3Dmigoto decompiler (GPL-3) ✔ integrated
* `dxil-spirv/` (which carries `dxbc-spirv`) and `SPIRV-Cross/` — the SPIR-V chains
  (MIT / Apache-2.0) ✔ integrated
* `sqlite/` — the SQLite amalgamation (public domain) — still to come

DXC is not vendored: `dxcompiler.dll` and `dxil.dll` come from the Windows SDK, with the option of
pointing at a newer version in the preferences.

## Sources

- [bo3b/3Dmigoto — LICENSE.GPL.txt](https://github.com/bo3b/3Dmigoto/blob/master/LICENSE.GPL.txt)
- [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv)
- [doitsujin/dxbc-spirv](https://github.com/doitsujin/dxbc-spirv)
- [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler)
- [wine/vkd3d](https://gitlab.winehq.org/wine/vkd3d)
- [crosire/reshade](https://github.com/crosire/reshade)
- [crossous/DXIL2HLSL](https://github.com/crossous/DXIL2HLSL) — a precedent for a DXIL → SPIR-V → HLSL chain
