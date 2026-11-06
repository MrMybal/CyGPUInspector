---
id: hlsl
title: Edit, compile, apply and restore HLSL
description: Edit and compile HLSL, apply a live replacement, then restore the original.
section: User guide
order: 7
parent: shaders
revision: 418e10758453
---

# Edit, compile, apply and restore HLSL {#edit-compile-apply-and-restore-hlsl}

## Prepare an editable document {#prepare-an-editable-document}
Select a shader with captured bytecode and open its HLSL tab. Choose an available reconstruction or open an HLSL file. Open in a window gives the editor more room. Check the shader ID, source provenance, entry point and target profile before compiling. Save your intended source separately when you need to keep it.

The editor supports UTF-8, tabs, line endings, selection, Find/Replace, wrapping and Undo/Redo. Changing theme or density must not erase the edit history. For documents above 32 MiB, common lexical preparation is disabled explicitly while the full text remains editable.

## Compile first {#compile-first}
Use Compile / F7 to compile without replacing the live shader. Read the compiler messages and compatibility differences. Selecting a diagnostic navigates to its source line/column when available. Correct resource bindings, stage/profile and entry-point errors before continuing. Save as uses Ctrl+S in the focused editor; outside it Ctrl+S saves a frame.

## Apply deliberately {#apply-deliberately}
Compile & Inject / F5 in the focused editor is a separate action, available with a live session. It compiles and applies the result through the add-on’s replacement pipeline. The application checks signature compatibility, including inputs/outputs and resource bindings; a refused replacement states why. On D3D12, valid signed DXIL may require the matching dxil.dll.

Observe the actual game and compare a repeatable scene. A successful compile or IPC acknowledgement alone is not visual validation. Replacement is unavailable on a read-only offline capture. Restore returns the original shader; Enable reverses a disable. Magenta highlight is a diagnostic replacement, not a permanent source edit.

## Keep and share work {#keep-and-share-work}
Use [Game profile](doc:profiles) for per-game persistent state and [Mod export](doc:mods) for an explicit portable package. Review the selected entries: diagnostic highlights are not selected for export by default. Undo in the editor changes text history; it does not automatically undo a replacement already applied to the game. Use Restore for that action.

See [the workflow diagram](doc:hlsl#figure-1) and [replacement reference](doc:ref-shaderreplacement).


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
