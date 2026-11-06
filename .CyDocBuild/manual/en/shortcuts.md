---
id: shortcuts
title: Keyboard and navigation reference
description: Choose the right keyboard shortcut for the focused editor or workspace.
section: User guide
order: 16
parent: navigation
revision: e26095751dab
---

# Keyboard and navigation reference {#keyboard-and-navigation-reference}

## Context matters {#context-matters}
| Key/action | Context | Effect |
| --- | --- | --- |
| F1 | Application | Open help for the focused view |
| F5 | Workspace, outside editor/text entry | Refresh connections |
| Ctrl+S | Workspace, outside editor/text entry | Save the current frame |
| F7 | Focused HLSL editor | Compile without replacing |
| F5 | Focused HLSL editor, live session | Compile & Inject |
| Ctrl+O | Focused HLSL editor | Open source file |
| Ctrl+S | Focused HLSL editor | Save source as a file |
| Ctrl+Z / Ctrl+Y | Focused HLSL editor | Undo / Redo text edits |
| Ctrl+F | Focused editor | Find/replace |
| Ctrl+A | Focused editor | Select all text |
| Ctrl+Enter | Assistant chat input | Send request |
| Ctrl+K | Active help window | Focus full-text article search |
| Wheel / drag | Timeline or image view | Zoom / pan |
| Double-click / Fit | Timing view | Restore useful range |

F5 deliberately has different meanings: the focused editor takes its compile-and-apply shortcut; the workspace refresh shortcut must not intercept editing. Use buttons when you are unsure which view owns focus. A disabled action indicates missing live state or another unmet condition, not a reason to increase unrelated permissions.

## Help navigation {#help-navigation}
Select a topic in the left index, or search words from its body. English and French retain the same topic ID. Use the contents and explicit section links, browser Back/Forward and CyDoc reading tools. Images and static SVG diagrams are embedded with the article. Image enlargement remains inside the window; Escape returns to reading. These actions do not change game state.

## Restore your workspace {#restore-your-workspace}
Use View > Panels to focus a hidden panel. Reset the layout only when you want the default arrangement; it replaces the docking arrangement but is not a reset of capture data, database, profiles or source history.


## Illustrations {#illustrations}

### Frame inspection {#figure-0}

Actual application render with synthetic demo data. Select a pass, shader or event to correlate the views.

```image
{"id":"figure-0","src":"assets/media/overview-en.png","alt":"Frame inspection","controls":"minimal"}
```
