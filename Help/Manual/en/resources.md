# Resources, previews and exported images

## Find an image
In Resources, inspect dimension, format, usage and lifetime information. Select a render target, depth buffer or other supported texture. Follow its readers/writers to the relevant pass and shader. Runtime tracking may not know all shader reads; a deep capture adds descriptor evidence.

## View live GPU data
Request Preview on the selected live resource. The add-on copies it into a shared texture and the standalone opens that texture on a compatible adapter. The game must still be running and producing updates. A mismatch between game and standalone adapters, unsupported format or unknown D3D12 state can prevent sharing; read the reported reason rather than assuming an empty image.

The image view supports Fit, zoom and pan. Display settings selects RGB/R/G/B/A, value range, mip and slice where available. For depth, inspect linearisation, near/far assumptions and reverse-Z. HDR values may need an adjusted range to become visible. A display conversion changes the preview, not the game texture.

## Save the right representation
Save PNG writes the displayed image with the chosen channel/range conversion. It is useful for illustrating an analysis but is not a raw HDR/depth data export. Deep capture exports supported buffers as both PNG (viewable) and DDS (data representation), with a manifest of formats, names and failures. Let export complete before copying the folder.

The final live image can appear beside the timeline. A normal offline frame carries no live GPU pixels: its Preview cannot reconnect to a texture from the closed session. Open separately exported buffer PNG/DDS files when that is what you need.

See [resource tracking](help:ref-ResourceTracking) and [GPU sharing](help:ref-GPUSharing) for exact format/state and lifetime constraints.
