---
id: graph
title: Frame graph and command inspection
description: Follow passes, resources and commands to investigate a rendering effect.
section: User guide
order: 5
parent: capture
revision: 6d1626113101
---

# Frame graph and command inspection {#frame-graph-and-command-inspection}

## Follow a rendering effect {#follow-a-rendering-effect}
Open Frame graph and inspect the derived pass list. Start from a resource you recognise, such as scene colour or a depth buffer. Follow its writers/readers and look for resolution changes, compute work, copies and composites. Select the corresponding pass and inspect its commands in Frame events.

The graph is reconstructed from observed usage; it is not an engine’s render graph. Runtime reads can be incomplete, especially for compute shaders. Run a deep capture when you need descriptor bindings to support a dependency claim. The observer does not receive game debug-marker pass names from the ReShade API.

## Verify a name {#verify-a-name}
An inferred Bloom, Lighting or Tonemap label is a useful lead, not proof. Check shader bindings, resource formats, pass ordering and the resulting image. User-assigned names and AI suggestions must remain distinguishable from derived classifications. Confidence describes that inference, not a guarantee that every dependency was captured.

## Inspect a command {#inspect-a-command}
In Frame events, select a draw, dispatch, copy, clear or other recorded command. Use shader/pass filters to reduce the list. Details shows the linked pipeline, resource and event information. Deep capture supplies per-command bindings, barriers and fixed-function state. Read unknown-field markers literally: absent state was not captured, not necessarily disabled.

The pipeline inspector is read-only and filterable. Integer fields retain their exact values. Shader reflection reports declarations, whereas the deep bindings describe observed use at a command. Compare both to answer which image occupied a given register.

For the data model and derivation rules, see [Frame graph](doc:ref-framegraph), [Resource tracking](doc:ref-resourcetracking) and [capture modes](doc:capture).


## Illustrations {#illustrations}

### Frame inspection {#figure-0}

Actual application render with synthetic demo data. Select a pass, shader or event to correlate the views.

```image
{"id":"figure-0","src":"assets/media/overview-en.png","alt":"Frame inspection","controls":"minimal"}
```

### Where the data goes {#figure-1}

ReShade loads the observer. The standalone owns analysis, files and permission decisions.

```diagram
{"type":"flow","title":"Where the data goes","nodes":[{"id":"game","label":"Game + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Shared memory / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Standalone analysis","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profiles / mods","x":600,"y":160,"width":280},{"id":"ai","label":"AI provider / MCP agent","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
