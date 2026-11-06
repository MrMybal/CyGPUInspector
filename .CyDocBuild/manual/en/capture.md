---
id: capture
title: Tracking levels and the capture modes
description: Choose runtime observation, a saved frame or a bounded deep capture.
section: User guide
order: 3
revision: 7d3d8efcddd1
---

# Tracking levels and the capture modes {#tracking-levels-and-the-capture-modes}

## Start with runtime observation {#start-with-runtime-observation}
Connect a live session and select the lowest tracking level that answers your question. Inventories and command tracking supply progressively more data. Pass Timing adds measurements per pass; Draw Timing is more expensive. At a level without timestamps, bars can represent command order/count: read the ruler instead of treating every width as milliseconds.

## Choose your output {#choose-your-output}
| Need | Use | What remains |
| --- | --- | --- |
| Watch recent performance | Runtime / Frame timeline | Bounded live history |
| Resume analysis later | Save frame to disk | One offline frame and its used shader bytecode |
| Inspect actual bindings and buffers | Deep capture | Bounded command state plus separately exported PNG/DDS files |
| Give another analyst a prepared case | AI analysis snapshot | A saved frame and available analysis files |

## Capture a deep frame {#capture-a-deep-frame}
1. Connect to the intended live producer and open Deep capture.
2. Arm the capture with the panel’s action. Recording starts at a frame boundary and stops after the requested bounded interval.
3. Wait for completion and inspect the reported range, command/descriptors counts and incomplete/dropped markers.
4. Select a command to inspect bindings, pipeline fields and barriers.
5. Wait for exported buffers to finish before copying the capture directory. The list reports files not copied or failed.

Final image and buffers are exported as viewable PNG and data-preserving DDS where supported, with capture.json describing them. A regular saved offline frame does not contain these pixels. The [capture diagram](doc:capture#figure-0) distinguishes the three paths.

## What it cannot do {#what-it-cannot-do}
No mode replays rendering commands, gives pixel history, steps through a shader or recovers original HLSL. Deep capture has bounded command/descriptor capacity and can miss state set before arming. A partial capture says so; unknown is not the same as empty. See [capture internals and exact bounds](doc:ref-capturemodes) and [limitations](doc:ref-limitations).


## Illustrations {#illustrations}

### Choose the right capture {#figure-0}

Live observation, a saved single frame and deep buffer export serve different purposes; none replays a frame.

```diagram
{"type":"flow","title":"Choose the right capture","nodes":[{"id":"live","label":"Live history","x":0,"y":0,"width":280},{"id":"saved","label":"Saved single frame","x":300,"y":0,"width":280},{"id":"deep","label":"Deep command state","x":600,"y":0,"width":280},{"id":"bounded","label":"Bounded observations","x":0,"y":160,"width":280},{"id":"offline","label":"Offline, no live pixels","x":300,"y":160,"width":280},{"id":"export","label":"Separate PNG / DDS export","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```

### Frame inspection {#figure-1}

Actual application render with synthetic demo data. Select a pass, shader or event to correlate the views.

```image
{"id":"figure-1","src":"assets/media/overview-en.png","alt":"Frame inspection","controls":"minimal"}
```
