# Connect an external agent with MCP

## Configure the client
Run the standalone first. Configure your MCP client to launch App/CyGPUInspectorMCP.exe with stdio transport using its actual installed path. The proxy forwards newline-delimited JSON-RPC to the application’s local pipe; the standalone owns the session, data and permission decisions. The proxy does not capture a game or grant itself access.

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

The path above is an example: replace it with your installation. Client configuration syntax may vary. Connect a live producer or open a saved frame, then ask the agent to inspect the current session/frame before requesting expensive analysis.

## Select the external tool level
| Level | What it permits |
| --- | --- |
| Read Only (default) | Inspect, search, derive analysis and read data |
| Debug Control | Also disable/enable/highlight and capture |
| Shader Modification | Also compile, replace and restore |

Change the level in the standalone’s MCP panel. The client cannot raise it through a tool call. Inspect the activity log, refusals and arguments when diagnosing an operation. These levels do not replace the built-in assistant’s separate approval modes.

## Typical workflow
Ask for the frame graph, follow passes/resources to candidate shaders, then inspect bindings and timings. Decompilation can return pending: wait and ask again instead of assuming an empty result. Command state needs a completed deep capture; timing needs a suitable tracking level. Read caveat, source and confidence fields in tool results.

Only one proxy can use the pipe at a time. If connection fails, verify the application is open and another client is not holding it. External conversations are stored by that client, not by the built-in assistant. See [the complete tool inventory and transport reference](help:ref-MCP).
