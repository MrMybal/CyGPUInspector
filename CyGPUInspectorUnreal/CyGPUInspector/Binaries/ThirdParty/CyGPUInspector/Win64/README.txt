CyGPUInspector — what goes in this folder
=========================================

Two files, which the plugin loads when the editor (or a Development / DebugGame build of the game)
starts, before the renderer creates its device:

  ReShade64.dll              ReShade 6.8.0, the official build WITH FULL ADD-ON SUPPORT
                             (https://reshade.me), unmodified.

  CyGPUInspectorRS.addon64   the CyGPUInspector add-on, from the Addons folder of the
                             CyGPUInspector package or from bin/Release after a build.

The release zip of the plugin already has both. From the source repository, which never carries
them, run:

  python Tools/fetch_reshade.py --with-addon

It downloads the pinned ReShade from reshade.me, checks it against the SHA-256 it records, reads
ReShade64.dll out of the installer's archive without running the installer, and puts it here with
the add-on.

ReShade is under the BSD 3-Clause licence: LICENSE-ReShade.txt, and NOTICES-ReShade.txt for the
libraries compiled into it. Both must stay next to ReShade64.dll wherever it goes. CyGPUInspector is
not affiliated with, nor endorsed by, ReShade or its author.

Either file can also live anywhere else: set its path in Project Settings > Plugins >
CyGPUInspector, or pass -CyGPUInspectorReShade=<path> / -CyGPUInspectorAddon=<path> on the command
line. A packaged Development or DebugGame build takes them along; Shipping never does.
