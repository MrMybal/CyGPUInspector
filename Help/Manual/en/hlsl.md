# Edit, compile, apply and restore HLSL

## Prepare an editable document
Select a shader with captured bytecode and open its HLSL tab. Choose an available reconstruction or open an HLSL file. Open in a window gives the editor more room. Check the shader ID, source provenance, entry point and target profile before compiling. Save your intended source separately when you need to keep it.

The editor supports UTF-8, tabs, line endings, selection, Find/Replace, wrapping and Undo/Redo. Changing theme or density must not erase the edit history. For documents above 32 MiB, common lexical preparation is disabled explicitly while the full text remains editable.

## Compile first
Use Compile / F7 to compile without replacing the live shader. Read the compiler messages and compatibility differences. Selecting a diagnostic navigates to its source line/column when available. Correct resource bindings, stage/profile and entry-point errors before continuing. Save as uses Ctrl+S in the focused editor; outside it Ctrl+S saves a frame.

## Apply deliberately
Compile & Inject / F5 in the focused editor is a separate action, available with a live session. It compiles and applies the result through the add-on’s replacement pipeline. The application checks signature compatibility, including inputs/outputs and resource bindings; a refused replacement states why. On D3D12, valid signed DXIL may require the matching dxil.dll.

Observe the actual game and compare a repeatable scene. A successful compile or IPC acknowledgement alone is not visual validation. Replacement is unavailable on a read-only offline capture. Restore returns the original shader; Enable reverses a disable. Magenta highlight is a diagnostic replacement, not a permanent source edit.

## Keep and share work
Use [Game profile](help:profiles) for per-game persistent state and [Mod export](help:mods) for an explicit portable package. Review the selected entries: diagnostic highlights are not selected for export by default. Undo in the editor changes text history; it does not automatically undo a replacement already applied to the game. Use Restore for that action.

See [the workflow diagram](figure:1) and [replacement reference](help:ref-ShaderReplacement).
