# CyGPUInspectorDecompiler

*Also available in [French](fr/ShaderDecompiler.md).*

**State: implemented and tested** (`Tests/CyGPUInspectorShaderTests`, 142 checks on real compiled
shaders). Disassembly, reflection, compilation, and **four decompilation backends** covering DXBC
(Shader Model 4/5) and DXIL (Shader Model 6). Detailed research and licences:
[Research/ShaderTooling.md](Research/ShaderTooling.md).

## What works today

| Function | DXBC (SM 4/5) | DXIL (SM 6) |
|---|---|---|
| Disassembly | `D3DDisassemble` | `IDxcCompiler3::Disassemble` |
| Reflection (bindings, signatures, thread group) | `D3DReflect` | `IDxcUtils::CreateReflection` |
| Compilation | `D3DCompile` | `IDxcCompiler3::Compile` |

Both DLLs are loaded **at run time**: `d3dcompiler_47.dll` is always present on Windows,
`dxcompiler.dll` is looked for next to the executable, then in the Windows SDK, then on the PATH.
Its absence does not stop the application from starting: it is reported in the `Tools` tab of the
code panel, with the exact path of whatever was found.

All of this runs in the standalone, on a background thread, never in the game. The result is cached
by signature, in memory and on disk, so a shader already seen in another run costs nothing.

## Pipeline

```
Byte code (DXBC / DXIL)
      ↓  disassembly               d3dcompiler (DXBC) / dxcompiler (DXIL)
  Disassembly
      ↓  decompilation             several backends, side by side
  Pseudo-HLSL (one file per backend)
      ↓  validation                recompile with DXC / FXC + compare the disassembly
  Verdict per backend
      ↓  AI clean-up (optional)
  Reconstructed HLSL, labelled
```

The AI is **never** the first decompiler: it works on pseudo-HLSL a real backend has already
produced and validated.

## Multi-backend architecture

A backend describes itself with `{ name, version, licence, input formats, shader models }` and
registers with the decompiler. Results are **stored separately**, never overwritten:

```
shaders/<signature>/decompiled/<backend>-<version>.hlsl
shaders/<signature>/decompiled/<backend>-<version>.json   verdict, duration, messages
```

That is what makes it possible to compare outputs, to pick the best reconstruction by hand, and to
let an AI select or combine the pieces it trusts most.

| Backend | Input | Licence | State |
|---|---|---|---|
| **`hlsldecompiler`** (3Dmigoto) | DXBC SM 4/5 | GPL-3.0, vendored | **shipped** |
| **`cygi-dxbc` 0.1** | DXBC SM 4/5 | AGPL-3.0 (ours) | **shipped** |
| **`dxbc-spirv`** (+ SPIRV-Cross) | DXBC SM 4/5 | MIT + Apache-2.0, vendored | **shipped** |
| **`dxil-spirv`** (+ SPIRV-Cross) | DXIL SM 6.x | MIT + Apache-2.0, vendored | **shipped** |
| `vkd3d-shader` | DXBC | LGPL-2.1 | **not buildable here**, see below |

### `hlsldecompiler` — 3Dmigoto, vendored

The best open source DXBC decompiler. It rebuilds **expressions**, recovers the names of resources,
of constant buffers **and of their members** from the reflection tables, and reconstructs the
control flow. On the tonemap shader in the tests:

```hlsl
cbuffer Tonemap : register(b0)
{
  float Exposure : packoffset(c0);
  float WhitePoint : packoffset(c0.y);
  float2 InvResolution : packoffset(c0.z);
}
SamplerState LinearClamp_s : register(s0);
Texture2D<float4> SceneColor : register(t0);
...
  r0.xy = InvResolution.xy + v1.xy;
  r0.xyz = SceneColor.Sample(LinearClamp_s, r0.xy).xyz;
  r0.xyz = Exposure * r0.xyz;
```

Validation verdict: `compiles, equivalent disassembly, opcode similarity 100 %`.

The sources are vendored **without any modification** in `ThirdParty/hlsldecompiler/`, so that a
`diff` against upstream stays empty and the GPL compliance is checkable. Everything that had to be
added lives beside them in `shim/` and is marked as not being 3Dmigoto's: four symbols only
(`LogInfo`, `LogDebug`, `LogTime`, `VER_FILE_VERSION_STR`). Compiler options are relaxed for those
files alone rather than fixing them. See `ThirdParty/hlsldecompiler/ORIGIN.md` and
[THIRD-PARTY.md](../THIRD-PARTY.md).

One trap that cost a failure on the first attempt: this decompiler parses the **text** of the
assembly, and the instruction numbers and byte offsets our viewer shows make it fail with "No
opcode". `Disassemble()` therefore has two styles, `annotated` for reading and `plain` for
decompilers.

### `cygi-dxbc` — why keep it next to 3Dmigoto

It translates the DXBC assembly **instruction by instruction** into HLSL and rebuilds the
declarations from reflection. Its output is less readable than 3Dmigoto's, and that is exactly why
it is kept: it follows the disassembly line for line, so it serves as the reference for checking
what the other backend rearranged. Two reconstructions that disagree on a shader are information,
not a problem — that is the whole point of having several. Validation on the same shader:
`compiles, equivalent disassembly, opcode similarity 100 %`.

What it deliberately does **not** do, and other backends do better:

* it does not rebuild expressions: one instruction stays one line;
* it recovers no names, no structures, no algorithms;
* it keeps the shape of the control flow instead of restructuring it;
* comparisons write `1.0 / 0.0` where the hardware writes a bit mask — consistently, through `movc`
  and through conditions, and the result's notes say so.

Instructions it cannot translate are left **as comments** and counted, not silently dropped.

CyGPUInspector is published under the **AGPL v3** and 3Dmigoto under the **GPL v3**; section 13 of
both licences allows linking the two into one program, so the decompiler is integrated directly,
with its copyrights, notices and sources preserved, and its files keep the GPL v3.

### `dxbc-spirv` — the third opinion on DXBC

The brief asked for **`vkd3d-shader`** here. It is not buildable with this toolchain, and the next
section says exactly why. This backend replaces it by covering the same ground: a DXBC decompiler
from an **entirely different lineage** than the other two, so that disagreements between
reconstructions mean something.

**dxbc-spirv** (MIT, Philip Rebohle) is the new DXBC front end of DXVK. It is not a text
translator: it parses the byte code, lowers it into an **SSA intermediate representation**, runs
real optimisation passes over it, then emits SPIR-V. SPIRV-Cross does the rest. On the tonemap
shader in the tests, as `ps_5_0`:

```hlsl
cbuffer Tonemap : register(b0)
{
    float2 Tonemap_1_m0 : packoffset(c0);
    float2 Tonemap_1_m1 : packoffset(c0.z);
};
SamplerState LinearClamp : register(s0);
Texture2D<float4> SceneColor : register(t0);
...
    float4 _48 = SceneColor.Sample(LinearClamp, float2(TEXCOORD.x + Tonemap_1_m1.x, ...));
```

Verdict: `compiles, equivalent disassembly, opcode similarity 100 %`.

It costs **no new dependency**: dxil-spirv already depends on it, so the sources were there. Only
the SPIR-V emitter was missing from the build, added in a target of ours rather than by editing
dxil-spirv's vendored `CMakeLists.txt`, which has to stay identical to upstream.

One real obstacle had to be cleared, and it is guarded: dxbc-spirv **always** declares the
`PhysicalStorageBuffer64` addressing model and the Vulkan memory model, because it targets Vulkan.
SPIRV-Cross refuses to emit HLSL from anything but the `Logical` model. The backend therefore
rewrites those declarations — **but only after checking that the module really uses no physical
pointer**: no `OpTypePointer` in `PhysicalStorageBuffer`, no integer ↔ pointer conversion. If the
shader uses one, the backend **gives up and says so**. Rewriting a module that genuinely needs
physical addressing would produce a silently wrong reconstruction, which is worse than none.

### `vkd3d-shader` — why it is not here

This is not a preference, it is a finding, checked against
[wine/vkd3d](https://gitlab.winehq.org/wine/vkd3d) at commit `f714cb80`.

`libvkd3d-shader` is C under autotools, written for GCC and MinGW. Seven headers it depends on do
not exist in the tree: they are **generated**, by four different generators, none of which is
available here and none of which is a Windows tool:

| Missing | Generated by |
|---|---|
| `config.h` | autoconf |
| `spirv_grammar.h` | the repository's `make_spirv` script |
| `vkd3d_version.h` | the Makefile |
| `vkd3d_d3d12shader.h`, `vkd3d_d3d11shader.h`, `vkd3d_d3d10shader.h`, `vkd3d_d3d10_1shader.h` | **widl**, Wine's IDL compiler |
| `hlsl.tab.c`, `hlsl.yy.c`, `preproc.tab.c`, `preproc.yy.c` | **flex** and **bison** |

On top of that, `vkd3d-common` includes `<pthread.h>` and `<unistd.h>`, which do not exist under
MSVC. Integrating it would not be a matter of a shim as it was for 3Dmigoto: its build system would
have to be rewritten and generated headers substituted, which is **forking the project** — and that
would destroy exactly the property that makes our licence compliance checkable, namely that a
`diff` against upstream stays empty.

The day one of those locks opens (pre-generated sources published upstream, or a Unix toolchain
accepted as a build prerequisite), the backend plugs in like the others: the registry is built for
it. Until then, `dxbc-spirv` does the job under a more permissive licence and adds nothing to the
tree.

### `dxil-spirv` — the Shader Model 6 chain

There is no direct DXIL decompiler worth having. What exists, and what every Direct3D 12
translation layer on Linux uses, is **dxil-spirv** (MIT, Hans-Kristian Arntzen): it reads the LLVM
bitcode a SM 6 shader is made of and produces SPIR-V. **SPIRV-Cross** (Apache-2.0 / MIT, Khronos)
then goes SPIR-V → HLSL. Two hops, two projects proven on real games, and nothing of ours in
between.

The detour has a price, and the user has to be told: the intermediate SPIR-V is a **Vulkan** module
and carries neither resource names nor Direct3D registers. Both are put back afterwards:

* **the registers** — dxil-spirv, left to its defaults, maps a D3D binding onto a Vulkan one by
  identity: `descriptor set` = `register space`, `binding` = register index. Reading the SPIR-V
  decorations back therefore restores the original registers exactly;
* **the names** — dxil-spirv discards them, it has no use for them. The ones shown come from the
  **shader's own reflection tables**, read by DXC, matched by (space, register). Nothing is
  invented: a resource the reflection does not name keeps its number;
* **vertex input semantics** — same principle, from the input signature.

On the tonemap shader in the tests, as `ps_6_0`:

```hlsl
cbuffer Tonemap : register(b0, space0)
{
    float4 Tonemap_1_m0[1] : packoffset(c0);
};
Texture2D<float4> SceneColor : register(t0, space0);
SamplerState LinearClamp : register(s0, space0);
...
    float4 _44 = SceneColor.Sample(LinearClamp, float2(Tonemap_1_m0[0u].z + TEXCOORD.x, ...));
```

Verdict: `compiles, equivalent disassembly, opcode similarity 98 %` (100 % on the compute shader in
the tests).

What the detour destroys, and what the result's `notes` field states:

* the **members** of a constant buffer disappear, collapsed into the array of `float4` that a Vulkan
  uniform block is — `Exposure` and `WhitePoint` become `.x` and `.y`. That is the most visible loss
  against 3Dmigoto on SM 5;
* interpolant semantics become `TEXCOORD<n>`, because SPIR-V does not carry them;
* the control flow is the one the *structurizer* rebuilt, not the one that was written;
* temporaries are numbered.

The entry point is emitted as `main` rather than under its real name, because that is what
validation by recompilation assumes; the real name is reported in the notes rather than lost.

Sources are vendored **unmodified** in `ThirdParty/dxil-spirv/` and `ThirdParty/SPIRV-Cross/`, the
latter pinned to the commit dxil-spirv itself uses, so the pair upstream tests together is the pair
consumed here. Only the converter is built: no command line tools, and the builtin bitcode reader
rather than a full LLVM — the difference between 13 MB of dependency and a gigabyte. See the
`ORIGIN.md` files and [THIRD-PARTY.md](../THIRD-PARTY.md).

## Validation

Every reconstruction is recompiled and compared with the original, which turns "it looks right" into
a verdict:

| Verdict | Meaning |
|---|---|
| `not validated` | validation did not run |
| `does not compile` | the reconstruction is not valid HLSL for its profile |
| `compiles` | it compiles, but the disassembly differs |
| `compiles, equivalent disassembly` | same opcode multiset, similarity ≥ 90 % |

Only the last one means "faithful", and the interface never says more than the verdict allows.

## What is never claimed

The original source **does not exist** in a compiled shader. Variable names, function names and
comments are destroyed at compilation. Everything produced here is a reconstruction, labelled with
the backend and the version that made it, and kept distinct from an AI reconstruction (§31).
