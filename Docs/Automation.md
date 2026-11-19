# Application automation

CyGPUInspector exposes **111 semantic operations** through the same application registry:
the original 34 GPU inspection/control commands and 77 application/workspace commands.
All handlers use the real application model and execute on its UI thread. No synthetic UI clicks,
script evaluator, terminal access or arbitrary preference JSON patching is exposed.

## Coverage

| Area | Supported workflows |
| --- | --- |
| Connections | Discover exact process/device, connect, disconnect, tracking, full synchronization |
| Captures | Save/deep capture, open/close, compare, analysis snapshots, captured-buffer viewer |
| Navigation | Shader/resource/event/pass selection, filters, workspace/panel focus |
| Layout | Discover real panels, dock tabs/splits, reset and restore with backups |
| Appearance | Installed themes/languages, density and scale, persisted preferences |
| Preview | Channels, depth/range, mip/slice, fit/zoom, freeze/release, PNG export |
| Timeline | Retained frame summaries, frame/pass selection, range and follow controls |
| Metadata | Shader names/tags/AI notes; edit/delete/restore annotations; pass naming |
| HLSL | Revision-checked edits, native undo/redo, compiler diagnostics, checked injection, import/export |
| Mods and profiles | Inspect export selections, include/exclude, new package export, recoverable clear, save/reapply |
| Assistant | Non-secret provider preferences, connect/disconnect, submit/cancel chat, job lifecycle, transcript export/reset |
| Application | Offline help, About, diagnostics, acknowledged exit with draft/conversation backups |

## Rights and recovery

External MCP and CLI use the same host permission. Read Only permits metadata inspection;
Debug Control permits navigation/settings and normal workspace operations. Shader Modification
is required for HLSL, conversation contents, provider configuration and high-impact workflows.
The built-in assistant retains Automatic / Ask / Full policies and its tool/operation budgets.
Sensitive code-bearing reads use the Ask approval path. Permission grants, login, API keys and
provider secret fields are never exposed as remote operations. An agent cannot promote itself. Changing the provider or endpoint through automation clears the in-memory key; enter any required key yourself in Connection.

Use `get_application_state` to discover installed themes/languages and current selection,
`get_panel_layout` for exact panel names, and `list_sessions` to choose an exact process/device.
Operations still obey real prerequisites: live-only commands refuse an offline capture, a busy
assistant refuses a second request, and provider changes require disconnection first.
`request_shader_job` is intended outside an active chat turn; the assistant cannot recursively
run another generation while handling its current tools.

Every editor replacement uses `expected_sha256` from `get_editor_state`. Changed text is refused
before mutation; old drafts are saved and edits participate in native undo. Annotation edits/removal
check `expected_text`; backup restoration verifies the shader signature. Layout edits and reset save
recoverable copies. New conversation exports a transcript before resetting; a still-stopping turn
returns an error without dropping history. Exit refuses recorded changes until they are exported or
cleared with a backup, then saves the draft and transcript before closing.

Automation-created files live in `Automation/` beside the application's captures. They are user
data and are never packaged. New names are generated without overwriting an existing export.
`list_automation_files` lists those files. HLSL import accepts an existing `.hlsl` filename from that
directory, layout restore accepts `.layout`, and note restore accepts `.notes`; traversal and links
outside that directory are refused. A user may place an HLSL document there to make it available
explicitly. There is no unrestricted filesystem reader or remote file chooser.

## Examples

With the application open and the appropriate host permission:

```powershell
.\CyGPUInspectorMCP.exe invoke get_application_state '{}'
.\CyGPUInspectorMCP.exe invoke list_sessions '{}'
.\CyGPUInspectorMCP.exe describe set_editor_text
.\CyGPUInspectorMCP.exe invoke-file set_filters filters.json
```

`filters.json` can contain `{"shaders":"stage:ps writes:hdr","used_this_frame":true}`.
For longer JSON, `invoke-file` avoids shell-specific quote escaping. MCP discovery is paginated;
follow `nextCursor`. Compact mode searches, describes and invokes these same commands. It does
not bypass permissions. Existing operation names, startup flags and saved data remain supported.

## Added operation inventory

The input schema is available through `describe NAME` or MCP tool discovery. All commands return
the existing `{ok,result}` / `{ok:false,error}` envelope; business failures set MCP `isError`.

| Command | External level | Mutates | Purpose |
| --- | --- | --- | --- |
| `get_application_state` | Read Only | no | Read connection, capture, selection, workspaces and available automation domains without an active game. |
| `list_sessions` | Read Only | no | Refresh and list live producers. Use process_id and device_index to select an exact producer. |
| `connect_session` | Debug Control | yes | Connect to an explicitly selected live producer. Existing editor work is retained. |
| `disconnect_session` | Debug Control | yes | Disconnect the live producer without deleting captures, editor work or profiles. |
| `open_capture` | Debug Control | yes | Open a capture name returned by list_captures. Preserves the editor document. |
| `close_capture` | Debug Control | yes | Close the offline capture and return to the retained live connection. |
| `set_tracking_level` | Debug Control | yes | Set the live tracking level; full_draw_timing can add GPU overhead. |
| `request_full_sync` | Debug Control | yes | Request a fresh complete live producer synchronization without changing tracking settings. |
| `set_follow_frame` | Debug Control | yes | Freeze or resume application frame following. |
| `refresh_frame_graph` | Debug Control | yes | Rebuild the derived frame graph and timeline from the active model. |
| `select_shader` | Debug Control | yes | Select an existing shader in the real application and focus its relevant panel. |
| `select_resource` | Debug Control | yes | Select an existing resource in the real application and focus its relevant panel. |
| `select_event` | Debug Control | yes | Select an existing event in the real application and focus its relevant panel. |
| `select_pass` | Debug Control | yes | Select an existing pass in the real application and focus its relevant panel. |
| `set_filters` | Debug Control | yes | Update shader/resource/event filters in the UI. Unspecified filters remain unchanged. |
| `set_workspace` | Debug Control | yes | Focus the requested application workspace without replacing its docking layout. |
| `focus_panel` | Debug Control | yes | Focus an existing named application panel. See get_panel_layout for stable panel names. |
| `get_panel_layout` | Read Only | no | Read stable panel names and current geometry/docking state. |
| `dock_panel` | Debug Control | yes | Dock a panel beside or as a tab of another panel. Writes a recoverable layout backup first. |
| `reset_layout` | Debug Control | yes | Restore the default docking layout after saving a recoverable backup. |
| `restore_layout` | Debug Control | yes | Restore a layout backup from the automation export directory; backs up current layout first. |
| `set_appearance` | Debug Control | yes | Set persisted theme, density or scale. Unspecified fields remain unchanged. |
| `set_language` | Debug Control | yes | Select and persist one of the installed interface language codes. |
| `open_help` | Debug Control | yes | Open a stable documentation topic in the offline reader. |
| `get_preview_settings` | Read Only | no | Read preview settings, selected resource and mip/slice. |
| `set_preview_settings` | Debug Control | yes | Set display channel, depth interpretation, range, mip, slice and image zoom. |
| `start_preview` | Debug Control | yes | Request the final live image (resource_id=0) or a known resource. |
| `stop_preview` | Debug Control | yes | Stop the shared preview without affecting capture data. |
| `export_preview` | Debug Control | yes | Save the currently rendered preview to a new PNG in the automation export directory. |
| `get_shader_notes` | Read Only | no | Read persistent name, tags and annotations for an existing shader. |
| `set_shader_name` | Debug Control | yes | Persist a human-readable shader name for its signature. |
| `set_shader_tag` | Debug Control | yes | Add or remove a recognized persistent shader tag. |
| `add_shader_annotation` | Debug Control | yes | Append a note marked as AI-generated; never impersonates a user annotation. |
| `rename_pass` | Debug Control | yes | Rename a derived pass and remember it by its resource/command identity. |
| `get_editor_state` | Shader Modification | no | Read the current HLSL document and revision hash. Requires Shader Modification externally. |
| `set_editor_text` | Shader Modification | yes | Replace the editor document using its expected SHA-256. Saves previous text before replacement. |
| `editor_undo` | Shader Modification | yes | Undo the last editor edit using the real editor history. |
| `editor_redo` | Shader Modification | yes | Redo the last undone editor edit. |
| `compile_editor` | Shader Modification | yes | Compile current editor HLSL and return real compiler diagnostics; does not inject. |
| `inject_editor` | Shader Modification | yes | Compile and inject current HLSL using the real compatibility checks. Requires a live shader. |
| `export_editor` | Shader Modification | yes | Write the current editor document to a new HLSL file in the automation export directory. |
| `list_automation_files` | Debug Control | no | List application-owned automation exports and draft/layout backups. |
| `import_editor` | Shader Modification | yes | Load an HLSL export by file name, with optimistic revision check and automatic draft backup. |
| `get_mod_package` | Read Only | no | Read recorded changes and export selections. |
| `set_mod_included` | Debug Control | yes | Include or exclude a recorded shader change from a mod package. |
| `export_mod_package` | Debug Control | yes | Export selected real changes into a new .cygimod directory, without overwriting an existing package. |
| `save_game_profile` | Debug Control | yes | Persist disabled/replaced shader state in the current game profile. |
| `reapply_game_profile` | Shader Modification | yes | Reapply the existing game profile modifications to the live game. |
| `set_timing_focus` | Debug Control | yes | Measure a selected shader, draw or resource using the existing focus controls. |
| `set_timeline_view` | Debug Control | yes | Set frame/continuous timeline view, follow state or visible range. |
| `get_assistant_status` | Read Only | no | Read provider type/model, connection, permissions and job state; excludes credentials and transcript. |
| `request_shader_job` | Shader Modification | yes | Start an existing cleanup/classification/explanation job for a selected shader. Refuses while chat is active. |
| `cancel_shader_job` | Shader Modification | yes | Cancel the current shader analysis job; saved annotations remain intact. |
| `open_about` | Debug Control | yes | Show application version and legal notices. |
| `configure_assistant` | Shader Modification | yes | Configure non-secret provider preferences while disconnected. Never changes keys or permission modes. |
| `connect_assistant` | Shader Modification | yes | Connect the explicitly configured provider; never initiates login or sends a generation. |
| `disconnect_assistant` | Shader Modification | yes | Disconnect the provider and cancel active assistant work. |
| `cancel_assistant_request` | Shader Modification | yes | Cancel active chat generation without undoing completed actions. |
| `export_conversation` | Shader Modification | yes | Export the complete visible conversation to a new private JSON file. Contains user data. |
| `new_conversation` | Shader Modification | yes | Start a new conversation only while idle, after exporting a recoverable transcript backup. |
| `set_editor_view` | Shader Modification | yes | Change real editor display options, scroll position or show Find. |
| `set_preview_frozen` | Debug Control | yes | Freeze or release the live shared preview image. |
| `edit_shader_annotation` | Shader Modification | yes | Edit an annotation after checking its expected text; preserves a private backup and marks AI origin. |
| `remove_shader_annotation` | Shader Modification | yes | Remove an annotation after checking its expected text, retaining a private recoverable backup. |
| `send_assistant_message` | Shader Modification | yes | Submit an explicit user message to the configured built-in assistant, asynchronously. Refuses when disconnected or busy. |
| `get_assistant_conversation` | Shader Modification | no | Read complete visible messages and partial streamed response; requires Shader Modification externally. |
| `restore_shader_notes` | Shader Modification | yes | Restore a notes backup for the exact same shader signature, saving current notes first. |
| `export_analysis_snapshot` | Debug Control | yes | Save the current full analysis snapshot using the application snapshot exporter. |
| `get_timeline_history` | Read Only | no | Read retained timing frame summaries; these are not full replayable captures. |
| `select_history_frame` | Debug Control | yes | Select a retained timing frame and optional pass in the continuous view. |
| `show_capture_buffer` | Debug Control | yes | Show a saved PNG buffer from the last deep capture by index. |
| `set_profile_filter` | Debug Control | yes | Set the game profile list filter. |
| `get_diagnostics` | Read Only | no | Read application diagnostics without credentials or conversation text. |
| `clear_mod_recording` | Shader Modification | yes | Forget the export selection after saving every recorded change to a new recovery package; does not undo game changes. |
| `request_exit` | Shader Modification | yes | Close the application only after backing up editor/conversation and refusing unexported recorded changes. |
| `set_profile_reapply` | Shader Modification | yes | Persist the automatic reapply preference. |
| `clear_saved_profile_state` | Shader Modification | yes | Forget saved modifications after exporting a recovery package. |
