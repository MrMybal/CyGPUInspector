---
id: troubleshooting
title: Troubleshooting and known limits
description: Resolve connection, timing, preview, compiler and help problems.
section: User guide
order: 15
parent: welcome
revision: ba09078d40d4
---

# Troubleshooting and known limits {#troubleshooting-and-known-limits}

## No session {#no-session}
Confirm the process is 64-bit, uses a supported D3D11/12 path, has the full add-on-enabled ReShade build and actually loaded CyGPUInspectorRS. Check ReShade.log and refresh Connections. A running standalone alone is not a producer. Use the no-game demo to separate installation issues from application issues.

## No timings or missing events {#no-timings-or-missing-events}
Choose Pass Timing or a suitable focus/level and wait for delayed results. Check losses and timestamp capacity in Status. A zero/unknown frequency or missing marks must not become fabricated timing. Event buffering is bounded and drops data rather than blocking the game. A graph reconstructed from partial events can be incomplete.

## Black/unavailable preview {#black-unavailable-preview}
Check whether the session is live, the selected resource still exists, sharing uses the same adapter and the format/state is supported. Try a reasonable display range/channel and depth/reverse-Z settings. A normal offline capture has no pixels. Deep export can fail per buffer without invalidating every other file.

## Compilation or replacement refused {#compilation-or-replacement-refused}
Check entry point, stage/profile, resource signatures and compiler availability in Tools. Keep the provided DXC DLL pair together for SM6/DXIL signing. Read the diagnostic and compatibility list. Recompilation success does not prove identical behaviour. Restore is the direct rollback for an applied replacement; editor Undo only changes source text.

## AI / MCP unavailable {#ai-mcp-unavailable}
Check provider CLI login or API endpoint/model and whether a local server is running. API keys are session-only. For MCP, start the standalone, free the single proxy connection and inspect the host’s permission level/refusal log. Pending analysis needs a later query, not repeated overlapping requests.

## Help missing or partly translated {#help-missing-or-partly-translated}
Restore the complete generated Documentation folder beside the EXE, then reopen Documentation/Documentation.html; changing the working directory should not matter. The language selector is independent of application language. The distributed manual includes complete English and French editions. Missing images show a recovery message instead of preventing article reading.

## Fundamental limits {#fundamental-limits}
No frame replay, pixel history, shader stepping or original HLSL recovery. Pass names are inferred/user-provided; descriptor information and multi-list timings have explicit limits. Bounded histories and capture sizes are intentional. Consult [the full limits reference](doc:ref-limitations) before diagnosing unsupported behaviour as data loss.


## Illustrations {#illustrations}

### Choose the right capture {#figure-0}

Live observation, a saved single frame and deep buffer export serve different purposes; none replays a frame.

```diagram
{"type":"flow","title":"Choose the right capture","nodes":[{"id":"live","label":"Live history","x":0,"y":0,"width":280},{"id":"saved","label":"Saved single frame","x":300,"y":0,"width":280},{"id":"deep","label":"Deep command state","x":600,"y":0,"width":280},{"id":"bounded","label":"Bounded observations","x":0,"y":160,"width":280},{"id":"offline","label":"Offline, no live pixels","x":300,"y":160,"width":280},{"id":"export","label":"Separate PNG / DDS export","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```

### Two independent permission controls {#figure-1}

The built-in assistant’s modes and the external MCP tool levels are separate settings. The standalone enforces both paths.

```diagram
{"type":"flow","title":"Two independent permission controls","nodes":[{"id":"assistant","label":"Assistant: Auto / Ask / Full","x":0,"y":0,"width":280},{"id":"host","label":"Standalone permission checks","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP: Read / Debug / Modify","x":640,"y":0,"width":280},{"id":"provider","label":"CLI / API provider","x":0,"y":160,"width":280},{"id":"actions","label":"Permitted game / file actions","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```
