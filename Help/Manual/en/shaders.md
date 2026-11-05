# Find, inspect and decompile shaders

## Find a candidate
Open Shaders. Sort by stage, draw count or available GPU time; use the current-frame filter when you only want shaders used in that frame. The query field supports structured searches such as stage:cs, writes:rgba16f, reads:depth and ms>0.2. Combine criteria to follow an effect instead of guessing from IDs.

## Read the evidence
Select a shader. Details shows stage, container/model, bytecode size, signatures and usage. Shader code offers disassembly, declared bindings, HLSL reconstructions, AI Analysis and tool availability. Session IDs are temporary. Signatures identify shader content across sessions; semantic signatures ignore container metadata for mod matching.

DXBC usually has readable SM5-style assembly. DXIL uses DXC’s LLVM representation. Reflection describes what the shader declares; to know what was bound for a specific draw, inspect a deep capture. A shader without captured bytecode cannot be decompiled just because its ID is known.

## Decompile
Request analysis/decompilation, then wait for the worker result; the UI remains usable. Available backends include 3Dmigoto hlsldecompiler, cygi-dxbc and the dxil-spirv/SPIRV-Cross path where supported. Select each backend to inspect its reconstruction and validation verdict. A decompiler may reject a shader or produce code that does not compile.

Recompilation validation is useful evidence, but a compiling reconstruction is not proof of identical rendering. Review resource layouts and the compatibility differences before replacement. Never present reconstructed HLSL as the game’s original source. Use [the editor workflow](help:hlsl) to test and apply an intentional change.

DXC is needed for Shader Model 6. Inspect Tools for the compiler search result; keep dxcompiler.dll and the matching dxil.dll together where the package provides them. See [the full decompiler reference](help:ref-ShaderDecompiler).
