# ThirdParty — vendored dependencies that build as part of CyGPUInspector.
#
# Only the DXIL decompilation chain is built here. The other vendored components (ReShade SDK
# headers, Dear ImGui) are header/source drops consumed directly by the targets that need them.
#
# See THIRD-PARTY.md at the repository root, and the ORIGIN.md beside each component, for the
# exact provenance and the licence obligations.
#
# Copyright (C) 2026 CyberAlien. Licensed under the GNU GPL v3 or later.

# Someone else's code is not held to our warning level. It is not ours to clean up, and keeping
# it warning-free would mean patching files we promise are byte for byte upstream.
add_compile_options(/W0)

# ---------------------------------------------------------------------------------------------
# dxil-spirv (MIT) — DXIL -> SPIR-V.
# Built as the static C API library only: no CLI, and the builtin LLVM bitcode reader rather than
# a full LLVM, which is why this dependency stays at ~10 MB of sources instead of a gigabyte.
# ---------------------------------------------------------------------------------------------
set(DXIL_SPIRV_CLI OFF CACHE BOOL "" FORCE)
set(DXIL_SPIRV_NATIVE_LLVM OFF CACHE BOOL "" FORCE)
set(DXIL_SPV_MISC_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(dxil-spirv EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------------
# SPIRV-Cross (Apache-2.0 / MIT) — SPIR-V -> HLSL.
# Pinned to the commit dxil-spirv itself uses, so we consume the pair upstream tests together.
# Only the GLSL and HLSL backends are built: MSL, C++, the JSON reflection target, the C API and
# the CLI are all dead weight here.
# ---------------------------------------------------------------------------------------------
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------------
# dxbc-spirv (MIT) — DXBC -> SPIR-V, the `dxbc-spirv` decompilation backend.
#
# It already builds: dxil-spirv links it for its own DXBC path, so `dxbc-spirv` is a target we
# inherit. What dxil-spirv does not compile is the SPIR-V emitter, because it feeds the IR into
# its own. Those two files are added here, in a target of ours, rather than by editing the
# vendored CMakeLists of dxil-spirv — which has to stay diff-clean against upstream.
# ---------------------------------------------------------------------------------------------
set(CYGI_DXBC_SPV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dxil-spirv/subprojects/dxbc-spirv)
add_library(dxbc-spirv-emitter STATIC
    ${CYGI_DXBC_SPV_ROOT}/spirv/spirv_builder.cpp
    ${CYGI_DXBC_SPV_ROOT}/spirv/spirv_mapping.cpp)
target_link_libraries(dxbc-spirv-emitter PUBLIC dxbc-spirv)
target_include_directories(dxbc-spirv-emitter PUBLIC ${CYGI_DXBC_SPV_ROOT})
target_compile_features(dxbc-spirv-emitter PUBLIC cxx_std_17)
