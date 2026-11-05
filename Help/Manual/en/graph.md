# Frame graph and command inspection

## Follow a rendering effect
Open Frame graph and inspect the derived pass list. Start from a resource you recognise, such as scene colour or a depth buffer. Follow its writers/readers and look for resolution changes, compute work, copies and composites. Select the corresponding pass and inspect its commands in Frame events.

The graph is reconstructed from observed usage; it is not an engine’s render graph. Runtime reads can be incomplete, especially for compute shaders. Run a deep capture when you need descriptor bindings to support a dependency claim. The observer does not receive game debug-marker pass names from the ReShade API.

## Verify a name
An inferred Bloom, Lighting or Tonemap label is a useful lead, not proof. Check shader bindings, resource formats, pass ordering and the resulting image. User-assigned names and AI suggestions must remain distinguishable from derived classifications. Confidence describes that inference, not a guarantee that every dependency was captured.

## Inspect a command
In Frame events, select a draw, dispatch, copy, clear or other recorded command. Use shader/pass filters to reduce the list. Details shows the linked pipeline, resource and event information. Deep capture supplies per-command bindings, barriers and fixed-function state. Read unknown-field markers literally: absent state was not captured, not necessarily disabled.

The pipeline inspector is read-only and filterable. Integer fields retain their exact values. Shader reflection reports declarations, whereas the deep bindings describe observed use at a command. Compare both to answer which image occupied a given register.

For the data model and derivation rules, see [Frame graph](help:ref-FrameGraph), [Resource tracking](help:ref-ResourceTracking) and [capture modes](help:capture).
