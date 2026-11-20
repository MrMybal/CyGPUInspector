# CyGPUInspectorMCP

## Shape

`CyGPUInspectorMCP.exe` uses CyMCPSDK 0.7.2 for MCP over stdio and forwards operations to the
running standalone over its local named pipe. The standalone owns live state and permissions;
requests execute on its interface thread. The proxy may retain bounded snapshots of large results.
It does not grant itself host permissions. The standalone also serves the same operations through
local HTTP at `http://127.0.0.1:41823/mcp`. Free uses the SDK unauthenticated loopback constructor
and `Policy::allow_all()`; schema validation remains enabled.

No argument, or `mcp`, starts the full 111-tool catalog. `mcp-compact` exposes five discovery and
execution gateways with resource-backed large results. Discovery is paginated: follow `nextCursor`
instead of assuming the first `tools/list` response is complete. Follow returned `firstUri` and
`nextUri` when reading result chunks. JSON-RPC input is one object per line; stdout is protocol only.
The SDK handles initialization, notifications, discovery, tool calls and compact resources.
Supported protocols include 2025-06-18, 2025-11-25 and the SDK's 2026-07-28 discovery path;
2024 clients are not supported by this integration.

For scripts, use `--help`, `list`, `describe NAME`, `invoke NAME JSON` or
`invoke-file NAME FILE`. A running standalone is required for application invocations.
See [the automation guide](Automation.md) for exact commands and recovery workflows.

## Permission levels

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

| Level | Scope |
|---|---|
| Read Only | Frame, shader, resource, application state, selections and workspace queries classified as reads |
| Debug Control | Permitted capture/debug actions and application mutations classified at this level |
| Shader Modification | Shader compile, replacement, restoration and operations requiring the highest level |

The precise permission of every one of the 111 tools is part of its definition. Use `describe`
or discovery rather than inferring it from its name. Mutation calls are logged in the standalone.
The 34 GPU definitions are in `McpTools.hpp`; 77 application definitions are in `AutomationTools.hpp`.
Keys and permission grants are not exposed as callable tools.

## Why these tools

Walk concrete evidence: get the frame graph, find readers of the HDR resource, inspect downsample
and blur shaders, then report IDs, timings and confidence. With the required permission, disable
a shader and observe the game to test a hypothesis. Application tools can select that shader,
open its editor and preserve a draft before editing; they reuse the same handlers as the UI.

## Answers

Results identify their shaders, resources and commands. Derived pass names, reconstructed HLSL
and timing intervals retain their caveats; they are not original source or proof of visual output.
Application replies use `{ok,result}` or a structured failure. MCP failures set `isError`.
The script CLI returns 0 on success, 1 for business failures and 2 for invalid input or transport errors.
Large results are immutable snapshots, read in chunks (32 KiB by default). They expire after five
minutes by default and the store is bounded to 64 MiB and 128 results. Follow the returned resource
links before expiry; do not assume handles survive a proxy restart.

## Limits

* Disassembly and decompilation may start background work: inspect again when it completes.
* One local proxy can occupy the named pipe at a time.
* Timings require profiling; command state requires a deep capture. Tools report missing prerequisites.
* Mutations are never cached or automatically replayed. Cancellation does not undo completed actions.
* Optional tuning: `CYGI_MCP_CACHE_TTL_MS` (30000), `CYGI_MCP_RESULT_TTL_MS` (300000),
  `CYGI_MCP_RESULT_BYTES` (67108864), `CYGI_MCP_CHUNK_BYTES` (32768).
  `CYGI_MCP_METRICS_FILE` enables a private metadata-only performance log; it is off by default.
* Static catalogs and immutable result snapshots do not require change subscriptions.
