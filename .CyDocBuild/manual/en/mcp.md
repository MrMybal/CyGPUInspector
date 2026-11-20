---
id: mcp
title: Connect an external agent with MCP
description: Connect an external agent and choose the tool access allowed by the standalone.
section: User guide
order: 13
parent: ai
revision: 98491ad2ba6b
---

# Connect an external agent with MCP {#connect-an-external-agent-with-mcp}

## Configure the client {#configure-the-client}

Run the standalone and open its **MCP** panel. Local HTTP MCP is enabled by default at
`http://127.0.0.1:41823/mcp` (the panel shows and copies the actual address). With the default
**Free** profile, paste that address into an HTTP MCP client: no token or confirmation is needed
to call any of the 111 registered operations. Arguments and operation prerequisites are still validated.
Only loopback is served. Disable the local HTTP endpoint with its checkbox when desired.

Stdio clients can still launch `App/CyGPUInspectorMCP.exe` using its installed path:

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

The example path must be replaced by your installation. The companion forwards calls to the
same running application; it does not capture a game itself. Connect a live producer or open
a saved frame for operations that require capture data.

## Select the external tool level {#select-the-external-tool-level}

The MCP panel offers four profiles:

| Profile | HTTP token | Operations |
| --- | --- | --- |
| Free (default) | None | All registered operations, without confirmation |
| Local controlled | None | Host permission level |
| Token only | Required | All registered operations, without confirmation |
| Protected | Required | Host permission level |

Controlled profiles offer Read Only, Debug Control and Shader Modification. The client cannot
raise its own level. Copy the session token from the panel for token profiles; it is generated
in memory, rotates when the application restarts and is never stored in preferences. New token
revokes the previous token. HTTP tokens apply to HTTP; the existing stdio/CLI pipe retains its
local-process transport and obeys controlled permission levels. The built-in chat keeps its
separate Auto/Ask/Full modes.

Preferences without an explicit selection migrate to Free. Existing profile choices, disabled
HTTP, port and unrelated settings are retained. Legacy explicitly saved permission levels migrate
to Local controlled. Invalid explicit profiles remain preserved and disable HTTP until corrected.

## Typical workflow {#typical-workflow}
Ask for the frame graph, follow passes/resources to candidate shaders, then inspect bindings and timings. Decompilation can return pending: wait and ask again instead of assuming an empty result. Command state needs a completed deep capture; timing needs a suitable tracking level. Read caveat, source and confidence fields in tool results.

Only one proxy can use the pipe at a time. If connection fails, verify the application is open and another client is not holding it. External conversations are stored by that client, not by the built-in assistant. See [the complete tool inventory and transport reference](doc:ref-mcp).


## Terminal commands and SDK transport {#terminal-sdk}
The [application automation guide](doc:automation) covers the 111 shared operations, including
connections, layout, settings, previews, notes, HLSL, mods and assistant controls.

CyMCPSDK 0.7.2 now supplies the native registry and stdio transport. No argument still starts
MCP. Use `mcp-compact` for on-demand discovery and private chunked results; keep `mcp` for full
discovery. Legacy initialization supports 2025-11-25 and 2025-06-18, not 2024-11-05.

```powershell
.\CyGPUInspectorMCP.exe list
.\CyGPUInspectorMCP.exe describe inspect_shader
.\CyGPUInspectorMCP.exe invoke get_session '{}'
.\CyGPUInspectorMCP.exe invoke list_shaders '{"limit":3}'
.\CyGPUInspectorMCP.exe invoke-file inspect_shader args.json
```

The arguments file contains an object such as `{"shader_id":2}`. Discovery works without a GUI;
invocation addresses the running standalone. JSON goes to stdout, diagnostics to stderr.
Exit codes: 0 success, 1 business refusal, 2 invalid input or connection failure. CLI and MCP
apply the same access profile. For a configuration without an explicit profile choice, scripts may select controlled access with
`--mcp-permission=read-only`, `--mcp-permission=debug-control` or `--mcp-permission=shader-modification`. An explicitly selected profile remains authoritative. Existing `--connect=<pid>`, tracking and view options are preserved.

In compact mode, read every returned chunk URI before treating a result as complete. Result
retention is five minutes in memory (64 MiB / 128 results). An expired reference is an error;
never repeat a mutation to recover its output. No filesystem search root is exposed.

## Illustrations {#illustrations}

### Two independent permission controls {#figure-0}

The built-in assistant’s modes and external MCP access profiles are separate settings; controlled profiles additionally use the host permission level. The standalone enforces both paths.

```diagram
{"type":"flow","title":"Two independent permission controls","nodes":[{"id":"assistant","label":"Assistant: Auto / Ask / Full","x":0,"y":0,"width":280},{"id":"host","label":"Standalone permission checks","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP access profiles","x":640,"y":0,"width":280},{"id":"provider","label":"CLI / API provider","x":0,"y":160,"width":280},{"id":"actions","label":"Permitted game / file actions","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Where the data goes {#figure-1}

ReShade loads the observer. The standalone owns analysis, files and permission decisions.

```diagram
{"type":"flow","title":"Where the data goes","nodes":[{"id":"game","label":"Game + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Shared memory / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Standalone analysis","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profiles / mods","x":600,"y":160,"width":280},{"id":"ai","label":"AI provider / MCP agent","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
