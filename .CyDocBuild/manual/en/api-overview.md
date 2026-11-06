---
id: api-overview
title: Public integration API
description: Call capture exports and read or write portable mod packages.
section: Developer reference
order: 100
revision: 0bc94ada6381
---

# Public integration API {#public-api}

Use the in-process C API when your host runs inside the game process, or the C++ package API to read and write portable mods. External agents use [MCP](doc:mcp); they do not load these exports.

## In-process capture {#in-process}

Include `CyGPUInspectorCore/InProcessApi.h`. ReShade must already have loaded `CyGPUInspectorRS.addon64` in the same process. Resolve the named exports with the platform module API; do not load another copy merely to obtain a function pointer. Check the API version before using version-2 viewport support.

All structures start with `size`. Initialize it with `sizeof` your structure. Capture options select 1–8 frames, descriptor bindings, barriers, per-draw timing and buffer exports. Status includes `api_version`, `device_count`, `standalone_connected`, `capture_stage`, frame counts and `pending_request`.

```cpp
// requestCapture is an already resolved PFN_CyGPUInspectorRS_RequestCapture.
CygiCaptureOptions options{};
options.size = sizeof(options);
options.frame_count = 1;
options.include_bindings = 1;
if (requestCapture(&options) == 0) {
    // No device or invalid options: inspect status before retrying.
}
```

An accepted request is queued, not finished. Poll status and wait for the standalone’s buffer export to complete before copying files. See [the Unreal workflow](doc:unreal) for the supported host integration.

## Portable mod packages {#mod-packages}

Include `CyGPUInspectorCore/ModPackage.hpp` and link `CyGPUInspectorCore`. `cygi::ModPackage` contains human metadata and `ModEntry` actions. A replace entry carries bytecode and optional source provenance; disable entries identify the shader by semantic hash. `Save` writes a directory and `Load` validates it. Neither call applies it to a running game.

```cpp
cygi::ModPackage package;
std::string error;
if (!cygi::ModPackage::Load("Example.cygimod", package, error, false)) {
    // Show error to the user; do not use a partially read package.
}
```

Use [Mod export](doc:mods) for the end-user workflow. See the function references under this topic for parameters, results and related calls.
