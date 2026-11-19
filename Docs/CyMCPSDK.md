# CyMCPSDK integration

CyGPUInspector builds CyMCPSDK **0.7.0**, source revision
`791d4d7d7680213b282ec8529d248a10f8494d77`, from the pinned snapshot in
`ThirdParty/CyMCPSDK`. The snapshot came from the local SDK checkout, with no source changes.
Targets: Core, Platform, Http, MCP, AI. Local is unnecessary: existing local model services use
HTTP; no engine, model downloader or GPU runtime is bundled. ChatUI requires CyUI's newer
`TranscriptSource`/`messageComposer` API, absent from this application's CyUI 1.2.0. The host
therefore retains its panel and uses Conversation, including streamed text and cancellation.
CyToolsCore is absent; there are no CyToolsCore jobs or databases to migrate.
The Unreal capture plugin continues to use the existing capture add-on ABI, not an AI SDK.

## Operations and permissions

`Integration/Operations.cpp` builds the same 34-operation registry for the application,
conversation tool path and companion. Descriptions and input properties come from the existing
`McpTools.hpp` inventory; required IDs and HLSL are validated before dispatch. Outputs use
`{ok,result}` or `{ok:false,error}`; each result retains the existing domain structure.
The UI thread executes all application handlers. Existing shader job annotations, profiles,
captures and settings remain compatible. Expensive analysis can still return a pending result.

All existing shader, resource, frame, timeline, capture and comparison operations are exposed.
UI layout, connection selection, provider credentials, permission grants and preference editing
are intentionally excluded from remote tools. They belong to the host/user, not the agent.
The assistant retains Automatic/Ask/Full budgets and explicit approval callbacks. CLI and MCP
both use the application's external Read Only/Debug Control/Shader Modification level.
An MCP/CLI caller cannot raise that level through an operation.

## CLI and instance selection

The existing executable remains a stdio MCP server with no arguments. New commands:

```powershell
.\CyGPUInspectorMCP.exe --help
.\CyGPUInspectorMCP.exe list
.\CyGPUInspectorMCP.exe describe inspect_shader
.\CyGPUInspectorMCP.exe invoke get_session '{}'
.\CyGPUInspectorMCP.exe invoke list_shaders '{"limit":3}'
.\CyGPUInspectorMCP.exe invoke-file inspect_shader args.json
```

`args.json` contains, for example, `{"shader_id":2}`. `list` and `describe` work without the GUI.
`invoke` requires the running standalone with a connected producer or an open capture. It calls
the fixed local named pipe, not a separately loaded or empty session. Only one companion may
hold this legacy application pipe at a time. Choose the game using existing `--connect=<pid>`;
when several producers exist, `--connect` refuses to guess. There is no standalone headless
capture loader in this integration. Existing startup flags remain available unchanged.

Stdout is JSON for discovery/invocation, stderr carries diagnostics. Exit 0 = success,
1 = business refusal/failure, 2 = usage, schema, transport or connection failure. No automatic
retries of mutations. The application defaults to Read Only. For deliberate scripted control,
launch the host with `--mcp-permission=debug-control` or `--mcp-permission=shader-modification`;
`--mcp-permission=read-only` explicitly selects the default. This grant lasts for that process
and can be changed in its MCP panel. A proxy argument cannot grant host permissions.

## MCP transport and results

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe","args":["mcp"]}}}
```

Full discovery is the default. SDK legacy initialization supports `2025-11-25` and
`2025-06-18`; modern discovery is `2026-07-28`. Clients using only `2024-11-05` must update.
No HTTP listener or remote network authentication is added. The existing Windows pipe security
and host permissions remain authoritative; this is not a cross-user network service.

Use `mcp-compact` instead of `mcp` for `cy_tools_search`, `cy_tools_describe`, `cy_tools_call`,
`cy_resources_read` and `cy_resources_grep`. Follow the returned manifest/chunk URIs until
`nextUri` is absent; a preview is not the whole result. Results are private to the SDK context,
retained in memory for five minutes, up to 128 results / 64 MiB, in 32 KiB chunks. Expiration
is an error, never permission to replay an operation. Complete full discovery remains available
in a separate `mcp` connection. No arbitrary filesystem/ripgrep root is exposed.

Each host setting is independent: `CYGI_MCP_CACHE_TTL_MS` (default 30000),
`CYGI_MCP_RESULT_TTL_MS` (300000), `CYGI_MCP_RESULT_BYTES` (67108864) and
`CYGI_MCP_CHUNK_BYTES` (32768). Invalid/out-of-range values fail before serving.
Discovery has the SDK's 30-second private TTL. Live tool calls are always executed; mutations
are never cached. No subscriptions are needed for the fixed tool catalogue and immutable result
snapshots. No HTTP/SSE notification support is claimed. Disk spooling is off because this
integration transfers shader/text summaries rather than capture buffers or GPU images.
Set `CYGI_MCP_METRICS_FILE` to an explicitly chosen private JSONL path to enable SDK timings,
byte counts and status metadata. It is off by default and excludes arguments, result contents,
conversation text and credentials. Keep it outside the source tree.

## Conversations

Codex uses native app-server dynamic tools; compatible HTTP APIs use streamed function calls.
Claude Code uses the SDK provider with the existing bounded JSON tool loop: its transport does
not expose the same dynamic-tool interface. Shell tools are disabled for Codex, sandbox remains
read-only, native approvals are denied, and external user MCP configuration is not delegated.
Claude's built-in tools and setting sources are disabled for this host path. Existing login
is still resolved by each CLI. Keys remain in memory; HTTPS is required except on loopback.

Connection settings independently control on-demand discovery and API context bytes (0 means
unlimited). API ContextWindow keeps full checkpoint history and complete tool exchanges while
projecting a smaller model context; it does not summarize. Native agents keep their own context
management. The transcript is no longer evicted after 32 messages. Optional **Remember
conversations on this computer** stores private SDK checkpoints under the current user’s Local AppData in
`CyGPUInspector/AssistantHistory/`, keyed by provider/model/endpoint hash, without credentials. It is off by
default. New conversation resets the active checkpoint. Native restoration requests the exact
session and fails rather than silently replacing it; canceled/failed turns are not replayed.
Shader jobs remain independent one-shot conversations, and their durable annotations stay in
the application's existing database. Attachments and external MCP forwarding are not added:
shader input already comes from the selected frame, and unrelated application tools are not
needed by this inspector. Existing provider/model selection stays explicit.

## Build and delivery

Use the application's normal CMake build with Visual Studio 2022 x64. SDK targets require
C++20; the capture core/add-ons retain their separate build boundary. No prebuilt SDK is used.
`Tools/package_release.py` copies and validates the complete SDK/dependency licenses and
provenance in the actual archive. Private histories, configuration and logs are never copied.
The normal CyDoc generation includes the EN/FR command guide and preserves offline use.
