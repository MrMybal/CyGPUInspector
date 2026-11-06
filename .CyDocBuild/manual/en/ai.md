---
id: ai
title: Use the built-in AI assistant
description: Connect a provider, ask focused questions and choose assistant permissions.
section: User guide
order: 12
revision: 557287e63b46
---

# Use the built-in AI assistant {#use-the-built-in-ai-assistant}

## Connect a provider {#connect-a-provider}
Open AI assistant > Connection. Choose Codex CLI, Claude Code CLI, an OpenAI-compatible API or a local API. CLI providers use their existing login; the application embeds no model. For an API, supply the full chat/completions endpoint, model and key if required. HTTPS is required except for a local endpoint. Connection can make a small provider request; model usage depends on that provider.

The key remains in memory for the session. Provider/model/URL preferences can be retained. A remote provider receives the conversation and tool data it requests, potentially shader code. Use a local model when that is the intended data boundary. Do not paste credentials into the chat.

## Ask a specific question {#ask-a-specific-question}
Select the frame, pass or shader first. Ask, for example, which passes contribute to bloom, what the selected shader computes or what evidence suggests a depth dependency. The assistant calls the same host tools used by MCP and reports concrete IDs and confidence. Cancel request stops the current job. New conversation clears chat, not game changes.

## Choose the assistant permission mode {#choose-the-assistant-permission-mode}
| Mode | Tool calls / operations per message | Approval |
| --- | --- | --- |
| Automatic | 8 / 1 | Within the requested operation |
| Ask each time | 8 / 1 | Each operation and shader-code disclosure |
| Full access | 16 / 8 | No per-operation prompt |

An operation changes the game or writes output; reading structure/timings is different. Changing mode cancels the running request. These modes are separate from [external MCP permission levels](doc:mcp). Read the [permission diagram](doc:ai#figure-0).

## Shader analysis jobs {#shader-analysis-jobs}
Shader code > AI Analysis offers clean-up, classification and explanation. Outputs retain model, origin, prompt version and confidence. A clean-up opens editable reconstructed HLSL; compile it and inspect compatibility before applying. A suggested tag is applied only by its action. Neither the assistant nor a decompiler recovers original source, and a plausible explanation is not a measured fact.

See [the full AI reference](doc:ref-ai), including exact budgets, context limits and analysis snapshots.


## Illustrations {#illustrations}

### Two independent permission controls {#figure-0}

The built-in assistant’s modes and the external MCP tool levels are separate settings. The standalone enforces both paths.

```diagram
{"type":"flow","title":"Two independent permission controls","nodes":[{"id":"assistant","label":"Assistant: Auto / Ask / Full","x":0,"y":0,"width":280},{"id":"host","label":"Standalone permission checks","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP: Read / Debug / Modify","x":640,"y":0,"width":280},{"id":"provider","label":"CLI / API provider","x":0,"y":160,"width":280},{"id":"actions","label":"Permitted game / file actions","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Shader editing workflow {#figure-1}

Keep the original bytecode; review and compile a reconstruction before explicitly applying it.

```diagram
{"type":"flow","title":"Shader editing workflow","nodes":[{"id":"inspect","label":"1. Inspect bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruct / edit","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compile and review","x":600,"y":0,"width":280},{"id":"apply","label":"4. Apply to live session","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observe or restore","x":300,"y":160,"width":280},{"id":"save","label":"6. Save profile / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
