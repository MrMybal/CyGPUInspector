# Use the built-in AI assistant

## Connect a provider
Open AI assistant > Connection. Choose Codex CLI, Claude Code CLI, an OpenAI-compatible API or a local API. CLI providers use their existing login; the application embeds no model. For an API, supply the full chat/completions endpoint, model and key if required. HTTPS is required except for a local endpoint. Connection can make a small provider request; model usage depends on that provider.

The key remains in memory for the session. Provider/model/URL preferences can be retained. A remote provider receives the conversation and tool data it requests, potentially shader code. Use a local model when that is the intended data boundary. Do not paste credentials into the chat.

## Ask a specific question
Select the frame, pass or shader first. Ask, for example, which passes contribute to bloom, what the selected shader computes or what evidence suggests a depth dependency. The assistant calls the same host tools used by MCP and reports concrete IDs and confidence. Cancel request stops the current job. New conversation clears chat, not game changes.

## Choose the assistant permission mode
| Mode | Tool calls / operations per message | Approval |
| --- | --- | --- |
| Automatic | 8 / 1 | Within the requested operation |
| Ask each time | 8 / 1 | Each operation and shader-code disclosure |
| Full access | 16 / 8 | No per-operation prompt |

An operation changes the game or writes output; reading structure/timings is different. Changing mode cancels the running request. These modes are separate from [external MCP permission levels](help:mcp). Read the [permission diagram](figure:0).

## Shader analysis jobs
Shader code > AI Analysis offers clean-up, classification and explanation. Outputs retain model, origin, prompt version and confidence. A clean-up opens editable reconstructed HLSL; compile it and inspect compatibility before applying. A suggested tag is applied only by its action. Neither the assistant nor a decompiler recovers original source, and a plausible explanation is not a measured fact.

See [the full AI reference](help:ref-AI), including exact budgets, context limits and analysis snapshots.
