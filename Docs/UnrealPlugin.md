# The Unreal plugin

*Also available in [French](fr/UnrealPlugin.md).*

`CyGPUInspectorUnreal/CyGPUInspector` is an Unreal Engine plugin (5.3 and later) that lets you
capture a frame of **the editor**, and of a **Development or DebugGame build** of the game, with
CyGPUInspector: from a toolbar button, a console command or a Blueprint node, without leaving the
editor, and with the names Unreal gives its render targets.

## Why a plugin, and why it still needs ReShade

CyGPUInspector observes a frame through the ReShade add-on API: that is where CyGPUInspectorRS
lives, and it is a hard rule of the project that it does not grow a graphics interception of its
own. ReShade, though, only sees a device it was there to see created. In a game it is installed
next to the executable as `dxgi.dll` and Windows loads it. The editor is different: one executable,
`UnrealEditor.exe`, shared by every project, in the engine's own folder.

So the plugin loads ReShade itself, **at `PostConfigInit`**, before Unreal creates its D3D12
device — the same moment, and the same way, Unreal's own RenderDoc plugin loads `renderdoc.dll`.
From then on everything graphics related is ReShade's own doing: the plugin hooks, patches and
intercepts nothing. It then puts CyGPUInspectorRS in ReShade's add-on folder, and ReShade loads it
like any add-on it finds there. That matters: ReShade unloads its add-ons when its last device goes
and loads them again with the next one, and Unreal creates a device on every adapter before it
keeps one. An add-on registered from outside does not survive that; one in ReShade's folder does.

**Yes, it needs a ReShade — and the right one: the build *with full add-on support*.** The standard
build switches add-ons off in any program with "high network activity", to keep them out of
multiplayer games, and the editor talks to its derived data cache, to Zen, to Live Coding over
sockets all the time. The full add-on build does not do that. It is the one reshade.me offers as
"with full add-on support"; its `ReShade64.dll` (or the `dxgi.dll` it installs for a game, renamed)
is what the plugin loads.

## Installing

1. Copy `CyGPUInspectorUnreal/CyGPUInspector` into the project's `Plugins/` folder (a C++ project
   builds it; for a Blueprint-only project, use a build made with `RunUAT BuildPlugin`).
2. The release zip of the plugin already has what it needs in
   `Plugins/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/`: `ReShade64.dll` (the
   official full add-on build of ReShade 6.8.0, with its licence notices) and
   `CyGPUInspectorRS.addon64`. From the source repository, which never carries ReShade, run
   `python Tools/fetch_reshade.py --with-addon`: it downloads the pinned ReShade from reshade.me,
   checks it against the SHA-256 it records, reads `ReShade64.dll` out of the installer without
   running it, and puts it there with the add-on. Either file can also be elsewhere: set its path
   in **Project Settings > Plugins > CyGPUInspector**.
3. Set the path of `CyGPUInspectorApp.exe` in the same settings, unless the add-on comes straight
   from a CyGPUInspector package: the standalone is then found in the `App` folder beside `Addons`.
4. Restart the editor. The Output Log says what was loaded, from where, or why nothing was
   (`LogCyGPUInspectorLoader`).

ReShade keeps its `ReShade.ini` and `ReShade.log` in the project's
`Saved/CyGPUInspector/ReShade/` (through `RESHADE_BASE_PATH_OVERRIDE`) rather than in the engine's
`Binaries` folder, which every project shares. If ReShade is already installed next to the
executable (a `dxgi.dll` that is ReShade), the plugin uses that one instead of loading a second.

## Capturing

| From | How |
|---|---|
| the level editor toolbar | **Capture** — the settings' frame count and options |
| the **CyGPUInspector** menu beside it | Capture 1, 2 or 4 frames; toggle buffers, descriptors, barriers, timestamps; open the standalone; settings; status |
| Tools | *Capture a Frame with CyGPUInspector*, *Open CyGPUInspector* |
| the console | `CyGPUInspector.Capture [frames]`, `CyGPUInspector.Status`, `CyGPUInspector.OpenStandalone` |
| Blueprints | `Capture Frames`, `Is CyGPUInspector Available`, `Get CyGPUInspector Status`, `Open CyGPUInspector` |

A capture needs the standalone: it is what receives the frames and saves them. When none is
connected, the plugin starts it with `--connect=<editor PID>`, waits for it to connect (30 seconds
at most), then asks the add-on for the capture — in-process, through the three functions of
[`InProcessApi.h`](../CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h), not through
the game window. A notification says when the capture is armed, when it has finished and when it
failed. The files land where every deep capture's do: `Images/<process>_<date>_<time>_frame<N>/`
next to the standalone, see [CaptureModes.md](CaptureModes.md#what-it-saves-to-disk).

**What is captured is the 3D render of the main viewport, and nothing else.** The main viewport is
Play In Editor while it runs, and otherwise the level viewport last worked in (in a game, the game
viewport). The editor's own interface — panels, menus, tooltips, the other windows — is left out.

How: through a *scene view extension*, the engine's own hook into a viewport's render, the plugin
clears a 1x1 texture of its own right before that viewport's 3D render and another right after it.
Those two clears are ordinary rendering commands: the add-on sees them go by like any other,
through ReShade, and keeps only what lies between them — the commands, their bindings, the buffers
they wrote. The extension also tells the add-on which texture the render ends in (the viewport's
render target in the editor), and that texture becomes the final image, shown live in the
standalone and saved as `final.png`, instead of the whole editor window. The markers cost two
one-pixel clears per frame of that viewport, and are only added while the add-on is in the process.

A capture waits for a frame that has that render: a level viewport that only redraws when
something changes is asked to redraw, and a capture gives up after about ten seconds without one
("is the viewport visible, and drawing?"). The timeline of the standalone still shows every
command of each frame; the Deep capture panel says which commands the capture was limited to. In a
packaged game the viewport draws straight into the window, so its final image includes the game's
own interface (UMG) drawn over the scene.

**Frames are counted on the main window.** The editor presents one swap chain per window. The
add-on ends a frame only on the largest one; the others go through untouched. The first version
counted every present as a frame: the shared final image was re-created at every present with a
new size — and destroyed while the GPU could still be copying into it, which hung the GPU and froze
the machine. Replaced shared textures are now retired and destroyed several frames later, and on
Direct3D 12 nothing is ever copied out of a texture whose state no transition has revealed.

## Debug builds of the game

The loader and everything that talks to ReShade are built for **Development and DebugGame**, never
for **Shipping**: the loader module is denied Shipping in the `.uplugin`, and the rest compiles out
there. A project can therefore keep the Blueprint nodes in its code: in Shipping, and on platforms
other than Windows, they do nothing and say so.

A packaged Development or DebugGame build takes `ReShade64.dll` and the add-on along when they are
in the plugin's `ThirdParty` folder (they are declared as runtime dependencies), and loads them at
start-up the same way. **Project Settings > Plugins > CyGPUInspector > Load In Game** turns that
off; `-NoCyGPUInspector` turns it off for one run, `-CyGPUInspector` forces it on.

## The names

In the editor, Development and DebugGame, Unreal names every GPU resource it creates through
`ID3D12Object::SetName`: `SceneDepthZ`, `GBufferA`, `SceneColorDeferred`, `HZBFurthest`… During a
deep capture the add-on reads those names from the native objects ReShade hands it — reading, not
intercepting — and sends them to the standalone. They appear in the Resources panel, in the name
of each saved buffer (`03_rt_1920x1080_r16g16b16a16_float_SceneColorDeferred.png`) and in
`capture.json`. This works for any Direct3D 11 or 12 program that names its resources, not only
Unreal; Shipping builds generally do not.

What the plugin cannot give, yet, is the names of **passes**. Unreal emits them as debug markers
(`BeginEvent`) when asked to, but ReShade has no add-on event for a marker the application emits,
so the add-on cannot see them. The standalone keeps deriving pass names from what each pass does.

## Command line

| Switch | Effect |
|---|---|
| `-CyGPUInspector` | load even when the settings say not to |
| `-NoCyGPUInspector` | do not load, whatever the settings say |
| `-CyGPUInspectorReShade=<path>` | ReShade to load |
| `-CyGPUInspectorAddon=<path>` | add-on to load |

Nothing is loaded in a commandlet (cooking, `-run=`), with `-nullrhi` or `-server`.

## Where it lives

| Piece | File |
|---|---|
| Loading ReShade and the add-on | `CyGPUInspectorUnreal/CyGPUInspector/Source/CyGPUInspectorLoader/` |
| Settings, console commands, Blueprint nodes, the capture flow | `.../Source/CyGPUInspector/` |
| Toolbar, menus, notifications | `.../Source/CyGPUInspectorEditor/` |
| The in-process interface of the add-on | `CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h`, `CyGPUInspectorRS/Source/Addon/InProcess.{hpp,cpp}` |
| Resource names | `CyGPUInspectorRS/Source/Tracking/ResourceNames.{hpp,cpp}` |

The plugin carries its own copy of `InProcessApi.h`
(`Source/ThirdParty/CyGPUInspector/CyGPUInspectorInProcessApi.h`), because Unreal builds it with
its own tool chain; CMake rewrites that copy whenever the original changes.
