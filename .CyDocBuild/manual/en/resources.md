---
id: resources
title: Resources, previews and exported images
description: Preview supported textures, adjust channels and export the right image format.
section: User guide
order: 8
parent: capture
revision: abb2846609db
---

# Resources, previews and exported images {#resources-previews-and-exported-images}

## Find an image {#find-an-image}
In Resources, inspect dimension, format, usage and lifetime information. Select a render target, depth buffer or other supported texture. Follow its readers/writers to the relevant pass and shader. Runtime tracking may not know all shader reads; a deep capture adds descriptor evidence.

## View live GPU data {#view-live-gpu-data}
Request Preview on the selected live resource. The add-on copies it into a shared texture and the standalone opens that texture on a compatible adapter. The game must still be running and producing updates. A mismatch between game and standalone adapters, unsupported format or unknown D3D12 state can prevent sharing; read the reported reason rather than assuming an empty image.

The image view supports Fit, zoom and pan. Display settings selects RGB/R/G/B/A, value range, mip and slice where available. For depth, inspect linearisation, near/far assumptions and reverse-Z. HDR values may need an adjusted range to become visible. A display conversion changes the preview, not the game texture.

## Save the right representation {#save-the-right-representation}
Save PNG writes the displayed image with the chosen channel/range conversion. It is useful for illustrating an analysis but is not a raw HDR/depth data export. Deep capture exports supported buffers as both PNG (viewable) and DDS (data representation), with a manifest of formats, names and failures. Let export complete before copying the folder.

The final live image can appear beside the timeline. A normal offline frame carries no live GPU pixels: its Preview cannot reconnect to a texture from the closed session. Open separately exported buffer PNG/DDS files when that is what you need.

See [resource tracking](doc:ref-resourcetracking) and [GPU sharing](doc:ref-gpusharing) for exact format/state and lifetime constraints.


## Illustrations {#illustrations}

### Live image preview {#figure-0}

Synthetic resource data and a live shared image. Real previews depend on compatible GPU sharing.

```image
{"id":"figure-0","src":"assets/media/resources-en.png","alt":"Live image preview","controls":"minimal"}
```

### Choose the right capture {#figure-1}

Live observation, a saved single frame and deep buffer export serve different purposes; none replays a frame.

```diagram
{"type":"flow","title":"Choose the right capture","nodes":[{"id":"live","label":"Live history","x":0,"y":0,"width":280},{"id":"saved","label":"Saved single frame","x":300,"y":0,"width":280},{"id":"deep","label":"Deep command state","x":600,"y":0,"width":280},{"id":"bounded","label":"Bounded observations","x":0,"y":160,"width":280},{"id":"offline","label":"Offline, no live pixels","x":300,"y":160,"width":280},{"id":"export","label":"Separate PNG / DDS export","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```
