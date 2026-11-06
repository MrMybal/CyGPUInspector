---
id: mods
title: Export and use a mod package
description: Export selected shader changes and apply the package with CyGPUInjector.
section: User guide
order: 11
parent: hlsl
revision: 42877cf8275f
---

# Export and use a mod package {#export-and-use-a-mod-package}

## Export an intentional change {#export-an-intentional-change}
1. Make and verify a live replacement or shader disable in the inspector.
2. Open Mod export and review the recorded actions. The latest decision for a shader replaces earlier ones.
3. Select only the entries you want to ship. Magenta highlights start unchecked because they are diagnostic.
4. Fill in the package metadata and export the complete .cygimod directory.
5. Inspect mod.json and the associated shader/source files before sharing.

Restore or Enable removes the corresponding recorded change. An export describes replace/disable decisions; it does not export every shader, arbitrary runtime state or a GPU replay.

## Apply without the inspector {#apply-without-the-inspector}
CyGPUInjector.addon64 is the smaller ReShade add-on that applies exported packages without the standalone or IPC. Install it through the normal add-on-enabled ReShade path. Follow the package placement instructions in the [full mod reference](doc:ref-modpackages); inspect its overlay and ReShade.log for loading/conflict messages.

Matching uses a semantic shader hash, so session ID changes do not break the package. Real code changes should stop an obsolete entry matching. A replacement takes effect when the pipeline is created, generally at loading time; selecting a package after that may need a restart/recreation. Disables affect matching draws/dispatches. When two packages target the same shader, alphabetical precedence decides and a warning is logged.

## Verify and roll back {#verify-and-roll-back}
Test the exported package independently on a repeatable scene. Keep the original source and package metadata for later review. Remove/deactivate the package and restart/recreate pipelines as needed to return to the unmodified run. The injector’s name does not mean it bypasses installation restrictions: it is still an add-on loaded by ReShade.


## Illustrations {#illustrations}

### Shader editing workflow {#figure-0}

Keep the original bytecode; review and compile a reconstruction before explicitly applying it.

```diagram
{"type":"flow","title":"Shader editing workflow","nodes":[{"id":"inspect","label":"1. Inspect bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruct / edit","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compile and review","x":600,"y":0,"width":280},{"id":"apply","label":"4. Apply to live session","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observe or restore","x":300,"y":160,"width":280},{"id":"save","label":"6. Save profile / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
