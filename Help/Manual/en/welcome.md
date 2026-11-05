# Start here

CyGPUInspector inspects live or saved GPU frames, shaders, resources and timings. ReShade is the capture agent inside a supported game; the standalone performs analysis outside it. The Windows x64 application supports the implemented Direct3D 11/12 workflows. Vulkan and OpenGL are not supported by this product yet.

## Your first task
1. Start with [the no-game demo](help:installation). It needs no provider account and changes no game.
2. Learn [the workspace](help:navigation) and click a measured pass in the frame timeline.
3. Follow that pass to its shaders, events and resources.
4. Save a frame if you need to return to the analysis later.

For a real game, install an add-on-enabled ReShade build, load the capture add-on, then connect the standalone. The [data-flow diagram](figure:1) explains which component does what.

## How to use this guide
F1 opens help for the active view; Help > User guide opens this home page. The application opens a bundled CyDoc reader through a local file URL in your browser, without a server or network request. English is the default; select Français in the reader. This selection does not change application preferences.

Search titles and full text, use the contents and section links, or follow an illustration link. Reading positions, bookmarks and notes use CyDoc’s local reading tools; their persistence depends on the browser’s file-origin storage policy. Export personal notes for backup. Images use minimal controls with zoom and annotations. Enlarge stays inside the documentation window; Escape reduces it. Touch controls remain available.

## Understand the evidence
The screenshots are genuine application renders using synthetic demo data. Pass names may be derived or user-assigned, HLSL is reconstructed, and timing intervals have limits. Neither live nor deep capture replays the frame. Read [limits and troubleshooting](help:troubleshooting) before drawing conclusions.

The User guide covers operation; Technical reference contains the full bundled architecture, protocol and feature documents. All help content is local. External web links are opened only when you deliberately click them and may require Internet access.
