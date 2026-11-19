---
id: automation
title: Control the application with AI or scripts
description: Navigate and operate the real application through shared semantic commands.
section: User guide
order: 14
parent: mcp
revision: f7286812af82
---

# Control the application with AI or scripts {#application-automation}

The shared catalogue contains 111 operations: 34 GPU commands and 77 application commands.
The built-in assistant, CLI and MCP reach the same real handlers on the interface thread.
Actions obey the current connection, capture state, permissions and approval mode.

## Discover and navigate {#discover}

Start with `get_application_state`: it returns selection, connection/capture state, installed
themes/languages and the automation export directory. `list_sessions` returns process/device
identities. `connect_session` chooses one explicitly; `disconnect_session` keeps editor work.
`open_capture` uses a name from `list_captures`; `close_capture` returns to the live connection.
Tracking, synchronization, capture, comparison and snapshot commands remain available.

Use `select_shader`, `select_resource`, `select_event` and `select_pass` for real selection.
`set_filters`, `set_workspace` and `focus_panel` control navigation. `get_panel_layout` lists
stable panel names; `dock_panel` arranges tabs/splits. Layout reset and restoration retain backups.
`set_appearance` and `set_language` persist the same preferences used by the menus.

## Inspect and edit {#edit}

Preview settings cover channels, depth, ranges, mip/slice, zoom and fitting. Preview commands
start, stop, freeze and export the actual image; a PNG export can fail until an image is ready.
Timing history contains retained summaries, not replayable full captures. Select an available
history frame/pass and change the timeline view without inventing missing frame data.

Shader notes support names, tags and AI-labelled annotations. Annotation edits/deletion require
the expected current text. Restore a `.notes` backup only for its original shader signature.
Pass renaming keeps the derived pass identity. Mod commands inspect recorded changes, select
entries, export a new package and clear the list with a recovery package. Clearing this list
does not undo changes in the game. Profiles can save and reapply their existing state.

Read `get_editor_state` before `set_editor_text`: pass its `sha256` as `expected_sha256`.
Changed work is refused before mutation. Replacements save previous text and support native
undo/redo. Compile the real editor document and inspect diagnostics before checked injection.
HLSL import/export and editor display options are available; compilation is not proof of an
identical rendered result. An offline capture cannot receive a live shader replacement.

## Assistant and permissions {#assistant-controls}

Non-secret provider preferences, connection, message submission/cancellation, shader jobs,
conversation export/reset and status are exposed. Provider changes require disconnection;
busy states refuse conflicting work. Shader jobs cannot recursively start inside an active
chat turn. New conversation keeps a transcript backup and refuses while a previous turn stops.

Read Only permits normal inspection. Debug Control adds workspace operations. Shader
Modification is required for HLSL, conversation contents and provider configuration. The built-in
assistant still uses Automatic / Ask / Full, with real approvals and operation budgets.
Agents cannot grant their own permissions, retrieve API key fields or perform account login.
No command evaluates scripts, launches a terminal or reads arbitrary files.

## Recover work and use the CLI {#recovery}

Backups and exports use new private files under `Automation/` beside captures; they are user
data and are excluded from application packages. `list_automation_files` lists them. Imports
accept a single filename with the matching `.hlsl`, `.layout` or `.notes` suffix, never a path
outside that directory. You may explicitly place an HLSL file there for import.

Exit refuses recorded changes until they are exported or cleared with a backup, then preserves
the draft and transcript. Error results are authoritative; never replay a mutation to recover
an expired result reference.

```powershell
.\CyGPUInspectorMCP.exe invoke get_application_state '{}'
.\CyGPUInspectorMCP.exe describe set_editor_text
.\CyGPUInspectorMCP.exe invoke-file set_filters filters.json
```

`filters.json` can contain `{"shaders":"stage:ps","used_this_frame":true}`. Discovery and schema
inspection work without a GUI; execution addresses the open application. Follow all MCP
`nextCursor` pages, or search/describe/invoke in compact mode. See [connection and permissions](doc:mcp).
