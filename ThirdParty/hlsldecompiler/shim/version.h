// Shim for the 3Dmigoto decompiler's version stamp.
//
// The vendored decompiler writes "// ---- Created with 3Dmigoto v<version>" at the top of the HLSL
// it produces. That line is kept, because it is honest about where the reconstruction comes from,
// but the version reported is the one of the sources actually vendored here.
//
// This file is NOT from 3Dmigoto.
//
// Copyright (C) 2026 CyberAlien. Licensed under the GNU GPL v3 or later.
#pragma once

// Commit 8f329bd94fecc9bbcb9211ffd42a95dd7fe6b43e of bo3b/3Dmigoto, see ORIGIN.md.
#define VER_FILE_VERSION_STR "1.4.1 (vendored in CyGPUInspector, commit 8f329bd)"
