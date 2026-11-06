---
id: offline
title: Save, reopen, compare and share captures
description: Save a frame, reopen it offline, compare captures and share a complete case.
section: User guide
order: 9
parent: capture
revision: 155c2f5e61aa
---

# Save, reopen, compare and share captures {#save-reopen-compare-and-share-captures}

## Save a frame {#save-a-frame}
With a live frame available, use Capture frame, File > Save frame to disk or Ctrl+S outside a focused text editor. The capture contains one frame, its events and the bytecode of the shaders it used. It is a directory with a versioned manifest and binary payloads, not a video or a GPU replay.

## Reopen offline {#reopen-offline}
Open Captures, refresh the list if needed, then open a saved entry. The status changes to OFFLINE CAPTURE. Inspect its shaders, command stream, derived graph and available analysis without the game. Live operations and texture previews are unavailable. Return to the live session when you need to apply a change.

## Compare {#compare}
Choose the two saved captures in the Captures comparison controls. Matching uses shader signatures rather than session-local identifiers. Use the differences to identify changed or new shaders and activity, then open each capture to verify the underlying evidence. Changed capture conditions can explain a difference; it does not automatically imply a regression.

## Share a reproducible case {#share-a-reproducible-case}
Copy the entire capture directory, not just capture.json. Include separately exported deep buffers when pixels are relevant. An AI analysis snapshot adds the already computed disassembly, decompiler reconstructions and a readable summary to a full saved frame. It does not run a remote model or finish missing analysis by itself.

Inspect the data before sending it: shader code, resource names and inferred structure can be part of the case. The synthetic demo is preferable for public screenshots. Keep a note of application version, capture mode, API and the exact question you want reproduced.

If an older/newer capture version is refused, keep the original directory and use a compatible application version; do not edit a version number to force parsing. See [the complete standalone reference](doc:ref-standaloneapp) and [capture limits](doc:ref-limitations).


## Illustrations {#illustrations}

### Choose the right capture {#figure-0}

Live observation, a saved single frame and deep buffer export serve different purposes; none replays a frame.

```diagram
{"type":"flow","title":"Choose the right capture","nodes":[{"id":"live","label":"Live history","x":0,"y":0,"width":280},{"id":"saved","label":"Saved single frame","x":300,"y":0,"width":280},{"id":"deep","label":"Deep command state","x":600,"y":0,"width":280},{"id":"bounded","label":"Bounded observations","x":0,"y":160,"width":280},{"id":"offline","label":"Offline, no live pixels","x":300,"y":160,"width":280},{"id":"export","label":"Separate PNG / DDS export","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```
