# Resource tracking

*Also available in [French](fr/ResourceTracking.md).*

**State: implemented (milestone 1).**

## Identifiers

Every resource the game creates is given a stable, increasing session identifier, shown as-is in
the interface (`Resource #183`). An identifier is never reused: on destruction the record stays and
is marked not alive, because events from previous frames still refer to it.

Tracked: buffers, 1D/2D/3D textures, surfaces — so constant buffers, vertex buffers, index buffers
and structured buffers too, not only render targets.

## Metadata

`kind`, `format`, `width`, `height`, `depth_or_layers`, `mip_levels`, `samples`, `usage_flags`,
`buffer_size`, `created_frame`, `destroyed_frame`, native handle.

Formats travel as a `uint32_t` holding the `reshade::api::format` value, which is `DXGI_FORMAT`
compatible. The standalone therefore never has to include the ReShade SDK. The name table
(`FormatNames.inc`, 131 formats) is **generated** from the SDK header by
`Tools/generate_format_names.py`: it cannot drift silently.

`usage_flags` is a translation of `resource_usage`: render target, depth stencil, shader resource,
UAV, index / vertex / constant buffer, indirect argument, copy or resolve source and destination,
plus two of our own: back buffer and shared.

Back buffers are identified at `init_swapchain` by walking `get_back_buffer(i)`.

## Views

`init_resource_view` fills the `view → resource` table. It is indispensable: render target
bindings, clears and `generate_mipmaps` all work on views, not on resources. Without that table we
would not know *which* texture a draw writes.

## Per frame access

Every event carries up to two resources:

| Field | Contents |
|---|---|
| `primary_resource` | what is **written**: render target 0, copy or resolve destination, cleared resource |
| `secondary_resource` | what is **read**, or the depth target: copy source, depth target |

From those the standalone derives, per frame: writes, reads, the first and last write event, and
the running total. That is already the raw material of the dependency graph (§7): every edge
carries the index of the event that produced it.

A `bind_render_targets` event carries the whole render target set, which is what makes a GBuffer
visible: a draw event alone only names slot 0.

## Current limits

* Reads through an SRV are only tracked during a **deep capture**. They come through
  `push_descriptors` / `bind_descriptor_tables`, the most expensive events in the API, so in
  runtime mode "read" means copy source or depth target. See [CaptureModes.md](CaptureModes.md).
* Resource debug names (`set_resource_name`) are rarely supplied by games; the `resource_named`
  record exists in the protocol but is not emitted yet.
