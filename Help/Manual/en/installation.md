# Installation and first connection

## Portable package
Extract the entire Windows x64 release ZIP to a writable folder. Keep App, Addons, TestWithoutAGame, Unreal and the notices together. Launch App/CyGPUInspectorApp.exe. Keep App/Help beside the executable: it contains this guide, both languages and all illustrations. Reading needs a modern browser, with no web server, embedded browser runtime, Node.js or online login.

## Try without a game
Run TestWithoutAGame/Demo.cmd. It starts a synthetic producer and connects the application. Explore the frame timeline, shaders and resource preview. Close both programs afterwards. Run TestWithoutAGame/RunTests.cmd to execute the included native test suites; this is a diagnostic check, not a real-game compatibility test.

## Connect a real game
1. Install ReShade’s build with full add-on support using the game’s normal supported installation procedure.
2. Place Addons/CyGPUInspectorRS.addon64 next to the game’s ReShade installation, in its add-on search location.
3. Start the game and confirm in ReShade that the add-on loaded. It is ReShade that loads it.
4. Open the standalone, refresh Connections with F5 and select Connect for the intended process/device.
5. Choose a [tracking level](help:capture), then wait for complete frames and delayed timing results.

Several sessions can be listed. Verify the executable, process ID, API and adapter instead of choosing by position. A stopped session can leave its last frame visible; check the status strip before expecting live controls to work.

## Updating safely
Extract an update into a new folder. Back up your settings, docking layout, Database, Games, Captures, Images and mods before moving data you want to keep. Preserve the complete DLL and Help folders rather than copying only the EXE. API keys are not stored in the settings file. Keep license notices when redistributing the package.

For Unreal, use [the dedicated plugin instructions](help:unreal). If the application refuses ReShade, that game is unsupported by this capture path. Troubleshoot normal add-on loading; do not expect the inspector to bypass a protection.
