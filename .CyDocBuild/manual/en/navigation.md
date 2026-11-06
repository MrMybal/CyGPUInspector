---
id: navigation
title: Workspace, appearance and language
description: Find panels, follow selections and adjust appearance or language.
section: User guide
order: 2
parent: welcome
revision: f6189a643482
---

# Workspace, appearance and language {#workspace-appearance-and-language}

## Find the right panel {#find-the-right-panel}
Inspect, Captures, Shaders and AI assistant in the top bar focus the relevant tabs without replacing your docking layout. View > Panels reaches the other panels. Panels are dockable; drag a tab to arrange your workspace. View > Reset the layout restores the default arrangement if a panel is lost.

Connections chooses the producer. Frame timeline and Frame graph describe the frame. Shaders and Resources are inventories. Frame events lists the command stream. Details follows the selected object. Shader code provides disassembly, bindings, HLSL, AI analysis and compiler tools. Preview shows a live shared image. Deep capture, Captures, Game profile, Mod export, MCP and Status complete the workflow.

## Selection and status {#selection-and-status}
Click a pass, shader, resource or command to inspect its relationships. A selection is not a modification: Disable, Restore, Compile & Inject and similar controls are separate actions. The bottom strip distinguishes LIVE, DISCONNECTED, OFFLINE CAPTURE and SESSION STOPPED. It shows process, frame and timing information when available.

## Appearance {#appearance}
View > Appearance provides Navy, Mid Grey, Light Grey, Amber Night and Violet themes, Compact/Comfortable density and interface scale from 85% to 175%. Monitor DPI is applied too. These preferences are additive: changing them preserves provider options, language and docking. Narrow tables scroll horizontally; find preview channel/range controls under Display settings.

## Two language choices {#two-language-choices}
View > Language changes the application language (English or French provided). The CyDoc reader has its own English/Français selector and starts in English. Reading French documentation does not switch the application or save preferences. Widget and window identities remain stable across language changes.

See [keyboard shortcuts](doc:shortcuts) and [the full standalone reference](doc:ref-standaloneapp).


## Illustrations {#illustrations}

### Frame inspection {#figure-0}

Actual application render with synthetic demo data. Select a pass, shader or event to correlate the views.

```image
{"id":"figure-0","src":"assets/media/overview-en.png","alt":"Frame inspection","controls":"minimal"}
```

### Reconstructed HLSL {#figure-1}

A decompiler reconstruction shown in the light theme; this is not the game’s original source.

```image
{"id":"figure-1","src":"assets/media/hlsl-en.png","alt":"Reconstructed HLSL","controls":"minimal"}
```
