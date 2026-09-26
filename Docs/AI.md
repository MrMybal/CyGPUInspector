# AI

*Also available in [French](fr/AI.md).*

**State: implemented** (milestone 9): the AI assistant built into the standalone, its shader jobs,
source labelling, tags, persistent annotations and the analysis snapshot. The external path, an
agent connected over MCP, is implemented too.

## Where it sits in the tool

The standalone **embeds no model**. A model reaches CyGPUInspector in one of two ways, and both use
the same tools, answered by the same code:

1. **The AI assistant panel**, built into the standalone. You pick a provider, connect, and ask in
   the chat. The assistant works on the frame with CyGPUInspector's tools, in a bounded loop, under
   a permission mode you choose.
2. **An external agent over MCP**: the agent runs in its own application and calls the tools
   described in [MCP.md](MCP.md). Its conversation stays in that application.

The assistant is designed like the one of CyRSAssistant, the same author's ReShade assistant: same
providers, same connection flow, same loop, same limits, same permission modes. Here the standalone
is already a process of its own, outside the game, so what CyRSAssistant does in a companion service
runs on worker threads of the standalone: the provider calls never block the interface, and the
game is never involved in them.

## The AI assistant panel

`View > AI assistant`. Three tabs: **Chat**, **Options**, **Connection**. The status line says
`READY`, `CONNECTING`, `CONNECTION ERROR` or `DISCONNECTED`, with the reason.

### Connection

| Provider | What it uses | What Connect checks |
|---|---|---|
| Codex (CLI session) | the `codex` CLI and the login it already has | `codex login status`, without calling the model |
| Claude Code (CLI session) | the `claude` CLI and the login it already has | `claude auth status`, without calling the model |
| OpenAI-compatible API | a full `/chat/completions` URL, a model, and a key if the server wants one | a small request the model must answer with `{"connected":true}` |
| Local model (API) | the same, on `localhost` (Ollama, LM Studio, llama.cpp server) | the same |

* The model can stay empty for a CLI: the CLI's default model is used.
* A CLI runs in a temporary folder of its own, with no project around it. Codex runs read-only,
  with no shell and without your `config.toml`. Claude Code runs with no tools, no MCP server,
  no hooks and no saved session. The CLI is only used to answer; it can act on nothing.
* An API needs HTTPS, except on `localhost`. Credentials in the URL and redirects are refused, so the
  key never goes anywhere but the server named. The API must accept
  `response_format: {"type":"json_object"}`.
* The provider, the model and the URL are saved in `CyGPUInspectorApp.settings.json`. **The key is
  not**: it stays in memory for the session.
* The CLI's error output is never shown: it can hold account details. Only its exit code is.

### Chat

Write a question and press **Send** (Ctrl+Enter). The assistant answers with one JSON object at a
time: either its reply, or one tool to call. CyGPUInspector runs the tool, gives the result back,
and so on, within a budget:

| Permission mode | Tool calls per message | Operations per message | Approvals |
|---|---|---|---|
| Automatic (default) | 8 | 1 | none beyond your request |
| Ask each time | 8 | 1 | each operation, and each shader code sent to the provider |
| Full access | 16 | 8 | none |

*Operations* are the tools that change the running game or write files: disable, enable, highlight,
restore, capture, deep capture, compile, replace. Reading the frame, the passes, the resources and the
timings is never an operation. In **Ask each time**, the request shows **Allow this action** /
**Deny this action** with the exact arguments (the HLSL of a replacement, for example). No mode gives a
terminal or access to your files. Changing the mode cancels the request in progress.

The context given at each step: your request, the last exchanges (16 messages, 24 000 characters),
the session and the frame currently held, what is selected in the interface (shader, resource,
command), the tools with their descriptions, the results so far and the remaining budget. The
answer comes in the interface language. The last 32 messages are kept in memory; **New
conversation** clears them without undoing anything done in the game. **Cancel request** stops the
request at once: a CLI is ended with every process it started, a late HTTP answer is ignored.

The examples under the chat are the questions of the brief (§84): *which part of the rendering
seems to produce the bloom*, *explain the pipeline*, *what does the selected shader do*.

### What is sent

Your messages and what the assistant reads with its tools: the structure of the frame, names,
timings and, when it asks for them, shader disassembly and decompiled HLSL. Never your files. With a
CLI or a remote API, this data goes to that provider; with a local model it stays on the machine.
**Ask each time** makes you approve every piece of shader code before it leaves.

## Shader jobs: the AI Analysis tab

Shader code panel > **AI Analysis**, for the selected shader:

* **Clean up the HLSL**: renamed registers and temporaries, comments, the same program otherwise;
* **Classify**: a likely role among the section 39 tags, a short name, the reasoning;
* **Explain**: what the shader does, its inputs, outputs and algorithm.

Each job sends the shader's description and bindings, its disassembly and each decompiler's
reconstruction with its verdict, waiting first for the disassembly and the decompilation when they
are not done yet. The result is stored as an annotation of the shader, **by signature**, with the
provider and model, the prompt version (`cygi-assistant-1`), the date and the confidence. A clean-up
opens in the HLSL editor, where it can be compiled and injected like any reconstruction. A
classification offers to apply its tag; it is only applied when you click.

## What the AI does, and from what

Always from concrete data the tool extracted: disassembly, pseudo-HLSL produced by a real
decompiler, bindings, the dependency graph, timings.

* rename variables and functions (`r0`, `r1` → `SceneColor`, `Exposure`, `Luminance`);
* comment, identify known algorithms, rebuild structures;
* guess the probable role of a resource;
* simplify generated code, explain mathematical operations;
* propose a pass name and a role (`Likely Role: Tonemapping — Confidence 92 %`).

## Source labelling, tags and versioning

**Source labelling.** An annotation always carries its origin — `user`, `derived` or `AI` — its
kind (`note`, `classification`, `cleanup`, `explanation`), its author (the model, for an AI
output), its prompt version, its date and its confidence. The three origins are shown in three
different colours. It is structurally impossible to store a sentence from a model without saying it
came from a model.

**Tags** (§39): Depth, GBuffer, Lighting, Shadow, GI, Reflection, SSR, AO, Fog, Volumetric,
Post Process, Bloom, Tonemap, Upscale, UI, Particles, Video, Unknown. Plus a free name given by the
user. All of it is persisted **by signature** in `notes.json`, so it survives restarting the game,
updating it, and even applies from one game to another.

**Versioning** (§58): every annotation keeps the model and the prompt version, which is what makes
it possible to know a result is stale and regenerate it.

## Non-negotiable rules

* Any AI output is labelled `AI reconstructed` and kept distinct from `Decompiler reconstructed`,
  which is itself kept distinct from `Original source` — which is never available (§31).
* A classification is **a hypothesis with a confidence**, never a fact (§40). Where a check is
  possible (disable the shader and look at the image), the assistant is told to propose it.
* The AI never replaces a decompiler: it works after one.
* The model and the prompt version are stored with the result, so it can be regenerated (§58).
* Shader code, names, tool results and earlier messages are **data** for the model, never
  instructions it should follow.
* The assistant reports only what a tool result confirms: "compiled" says nothing about what the
  game shows.

## Analysis Snapshot

**Implemented.** `AI analysis snapshot` writes a self-contained directory: a full capture
(`capture.json`, `events.bin`, `shaders/`) plus an `analysis/` folder holding

* `snapshot.md`: the derived passes in prose **with their origin and their confidence**, the table
  of the frame's shaders with their GPU times, and the warning that none of it is original source;
* the disassemblies and HLSL reconstructions already computed, one file per backend, each prefixed
  by its validation verdict.

That is exactly what would be given to a model — so it is what the user can re-read, correct or
archive **before** handing it over, rather than an opaque bundle.
