---
id: mcp
title: Connect an external agent with MCP
description: Connect an external agent and choose the tool access allowed by the standalone.
section: User guide
order: 13
parent: ai
revision: 22ae847e939c
---

# Connect an external agent with MCP {#connect-an-external-agent-with-mcp}

## Configure the client {#configure-the-client}
Run the standalone first. Configure your MCP client to launch App/CyGPUInspectorMCP.exe with stdio transport using its actual installed path. The proxy forwards newline-delimited JSON-RPC to the application’s local pipe; the standalone owns the session, data and permission decisions. The proxy does not capture a game or grant itself access.

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

The path above is an example: replace it with your installation. Client configuration syntax may vary. Connect a live producer or open a saved frame, then ask the agent to inspect the current session/frame before requesting expensive analysis.

## Select the external tool level {#select-the-external-tool-level}
| Level | What it permits |
| --- | --- |
| Read Only (default) | Inspect, search, derive analysis and read data |
| Debug Control | Also disable/enable/highlight and capture |
| Shader Modification | Also compile, replace and restore |

Change the level in the standalone’s MCP panel. The client cannot raise it through a tool call. Inspect the activity log, refusals and arguments when diagnosing an operation. These levels do not replace the built-in assistant’s separate approval modes.

## Typical workflow {#typical-workflow}
Ask for the frame graph, follow passes/resources to candidate shaders, then inspect bindings and timings. Decompilation can return pending: wait and ask again instead of assuming an empty result. Command state needs a completed deep capture; timing needs a suitable tracking level. Read caveat, source and confidence fields in tool results.

Only one proxy can use the pipe at a time. If connection fails, verify the application is open and another client is not holding it. External conversations are stored by that client, not by the built-in assistant. See [the complete tool inventory and transport reference](doc:ref-mcp).


## Terminal commands and SDK transport {#terminal-sdk}
CyMCPSDK 0.7.0 now supplies the native registry and stdio transport. No argument still starts
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
apply the same host permission level. Scripts may explicitly launch the host with
`--mcp-permission=debug-control` or `--mcp-permission=shader-modification`; the default remains
read-only. Existing `--connect=<pid>`, tracking and view options are preserved.

In compact mode, read every returned chunk URI before treating a result as complete. Result
retention is five minutes in memory (64 MiB / 128 results). An expired reference is an error;
never repeat a mutation to recover its output. No filesystem search root is exposed.

## Illustrations {#illustrations}

### Two independent permission controls {#figure-0}

The built-in assistant’s modes and the external MCP tool levels are separate settings. The standalone enforces both paths.

```diagram
{"type":"flow","title":"Two independent permission controls","nodes":[{"id":"assistant","label":"Assistant: Auto / Ask / Full","x":0,"y":0,"width":280},{"id":"host","label":"Standalone permission checks","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP: Read / Debug / Modify","x":640,"y":0,"width":280},{"id":"provider","label":"CLI / API provider","x":0,"y":160,"width":280},{"id":"actions","label":"Permitted game / file actions","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Where the data goes {#figure-1}

ReShade loads the observer. The standalone owns analysis, files and permission decisions.

```diagram
{"type":"flow","title":"Where the data goes","nodes":[{"id":"game","label":"Game + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Shared memory / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Standalone analysis","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profiles / mods","x":600,"y":160,"width":280},{"id":"ai","label":"AI provider / MCP agent","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
