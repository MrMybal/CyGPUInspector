# Shader tracking

*Also available in [French](fr/ShaderTracking.md).*

**State: implemented (milestone 1).**

## Capturing the byte code

At `init_pipeline`, ReShade hands over the pipeline's subobjects. For every shader subobject,
`shader_desc::code` / `code_size` give the compiled byte code. **That pointer is only valid during
the callback**: `ShaderTracker::RegisterShader` copies the bytes immediately.

Stages tracked: vertex, hull, domain, geometry, pixel, compute, amplification, mesh, raygen,
any hit, closest hit, miss, intersection, callable — as far as the API actually exposes them.

## Signature

Two digests are computed:

| Digest | How | Used for |
|---|---|---|
| `signature` | SHA-256 of the whole container, **with the checksum field zeroed** | identifying the shader byte for byte; the key of the database and of replacements |
| `semantic_hash` | SHA-256 of the **code part alone** (`SHEX` / `SHDR` for DXBC, `DXIL` for SM6) | matching variants that differ only in debug or reflection parts |

The DXBC container checksum is derived from the rest and written differently by different
compilers: including it would make the signature unstable and buy nothing. A test checks this.

The container also gives the real stage and the shader model, read from the version token at the
head of the code part: bits 16+ are the program type, bits 4–7 the major, bits 0–3 the minor. The
ReShade subobject type is only used as a fallback (GLSL and SPIR-V carry no program type).

## Pipelines

`init_pipeline` also gives the handle of the finished pipeline. The `handle → PipelineRecord` table
lets `bind_pipeline` resolve in O(1) which shaders a draw is about to run — that is the only cost
per bind, and there is no cost per draw.

In D3D11 a ReShade "pipeline" is a single state object (an `ID3D11PixelShader`), so one pipeline
carries one shader. In D3D12 an `ID3D12PipelineState` carries VS+PS+…: the mapping is 1..N.

`PipelineRecord`s are **never deleted**, even on `destroy_pipeline`: only the entry in the handle
table goes. Events from previous frames still refer to those identifiers.

## Shader → draw call correlation

On the standalone side, at the end of every frame, each draw or dispatch event is attributed to
every shader of its pipeline. Per shader that gives: draws this frame, dispatches this frame, the
running total, and the list of event indices (`EventsUsingShader`) — which is what lets you select
a shader and see its 381 draw calls (§19 of the brief).

## Still missing

* Library and ray tracing shaders beyond recording their byte code.
* Per-signature persistence in the database is implemented for blobs and notes; the SQLite index
  is still to come — see [Database.md](Database.md).

Binding reflection (SRV / UAV / CBV per shader) is implemented and lives in
[ShaderDecompiler.md](ShaderDecompiler.md), on the standalone side, through `D3DReflect` (DXBC) and
`IDxcUtils::CreateReflection` (DXIL).
