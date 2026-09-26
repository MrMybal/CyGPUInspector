# ImGuiColorTextEdit — origin

Syntax highlighting text editor widget for Dear ImGui, by Johan A. Goossens. MIT licence
(`LICENSE`, unchanged).

* Repository: https://github.com/goossens/ImGuiColorTextEdit
* Commit: `99fcb0d7b3bbac831a3125cc712b34e723b77397` (2026-09-20)
* Files taken, unmodified:

| File | Size | SHA-256 |
|---|---|---|
| `TextEditor.cpp` | 488 718 | `050623559602993e3a6e559cf187a86be67de949b1184543f3413877da8db0fd` |
| `TextEditor.h` | 85 638 | `44a3b8efe41aef3b614a9f07807c86dc0e85805e695a36aa1d1f1f9a3137f840` |
| `LICENSE` | 1 101 | `214dc907ae8db1e78574aca034360192a5bf6d7385bd62b85bd0d404e2d86b68` |

Left behind: the example application, the documentation, the `TextDiff` widget and its `dtl.h`,
and the `extras/` folder (LSP bridge, autocomplete).

CyGPUInspectorApp uses it for the HLSL editor and for the coloured views of disassembly and
reconstructed HLSL, with its built-in HLSL language definition.

To move to a newer version: take the same three files at a new commit, update this table, rebuild,
and check the HLSL editor (typing, undo, find, error markers after a failed compilation).
