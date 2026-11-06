---
id: shaders
title: Find, inspect and decompile shaders
description: Search shaders, inspect evidence and compare HLSL reconstructions.
section: User guide
order: 6
revision: e4c53d62df87
---

# Find, inspect and decompile shaders {#find-inspect-and-decompile-shaders}

## Find a candidate {#find-a-candidate}
Open Shaders. Sort by stage, draw count or available GPU time; use the current-frame filter when you only want shaders used in that frame. The query field supports structured searches such as stage:cs, writes:rgba16f, reads:depth and ms>0.2. Combine criteria to follow an effect instead of guessing from IDs.

## Read the evidence {#read-the-evidence}
Select a shader. Details shows stage, container/model, bytecode size, signatures and usage. Shader code offers disassembly, declared bindings, HLSL reconstructions, AI Analysis and tool availability. Session IDs are temporary. Signatures identify shader content across sessions; semantic signatures ignore container metadata for mod matching.

DXBC usually has readable SM5-style assembly. DXIL uses DXC’s LLVM representation. Reflection describes what the shader declares; to know what was bound for a specific draw, inspect a deep capture. A shader without captured bytecode cannot be decompiled just because its ID is known.

## Decompile {#decompile}
Request analysis/decompilation, then wait for the worker result; the UI remains usable. Available backends include 3Dmigoto hlsldecompiler, cygi-dxbc and the dxil-spirv/SPIRV-Cross path where supported. Select each backend to inspect its reconstruction and validation verdict. A decompiler may reject a shader or produce code that does not compile.

Recompilation validation is useful evidence, but a compiling reconstruction is not proof of identical rendering. Review resource layouts and the compatibility differences before replacement. Never present reconstructed HLSL as the game’s original source. Use [the editor workflow](doc:hlsl) to test and apply an intentional change.

DXC is needed for Shader Model 6. Inspect Tools for the compiler search result; keep dxcompiler.dll and the matching dxil.dll together where the package provides them. See [the full decompiler reference](doc:ref-shaderdecompiler).


## Illustrations {#illustrations}

### Reconstructed HLSL {#figure-0}

A decompiler reconstruction shown in the light theme; this is not the game’s original source.

```image
{"id":"figure-0","src":"assets/media/hlsl-en.png","alt":"Reconstructed HLSL","controls":"minimal"}
```

### Shader editing workflow {#figure-1}

Keep the original bytecode; review and compile a reconstruction before explicitly applying it.

```diagram
{"type":"flow","title":"Shader editing workflow","nodes":[{"id":"inspect","label":"1. Inspect bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruct / edit","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compile and review","x":600,"y":0,"width":280},{"id":"apply","label":"4. Apply to live session","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observe or restore","x":300,"y":160,"width":280},{"id":"save","label":"6. Save profile / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
