cmake_minimum_required(VERSION 3.21)
project(CyGPUInspector VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# CyGPUInspector — GPU frame, shader and resource inspector powered by ReShade
#   CyGPUInspectorCore        shared code: protocol, IPC, hashing, formats
#   CyGPUInspectorRS          ReShade add-on (.addon64) loaded in the game
#   CyGPUInjector             ReShade add-on that applies exported modifications, without the rest
#   CyGPUInspectorApp         standalone analysis application (Win32 + D3D11 + Dear ImGui)
#   CyGPUInspectorDecompiler  disassembly / decompilation backends   (milestone 5-6)
#   CyGPUInspectorDatabase    SQLite metadata + blob store           (milestone 2+)
#   CyGPUInspectorMCP         local MCP server                       (milestone 10)
#   ThirdParty                vendored dependencies built from source (dxil-spirv, SPIRV-Cross)
#
# Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
# ---------------------------------------------------------------------------

if(NOT WIN32)
    message(FATAL_ERROR "CyGPUInspector targets Windows only (ReShade / DirectX).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CyGPUInspector must be built as a 64-bit binary (ReShade .addon64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the add-on is loaded inside arbitrary game processes, avoid any vcredist dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CYGI_BUILD_ADDON "Build the ReShade capture add-on" ON)
option(CYGI_BUILD_INJECTOR "Build the ReShade add-on that applies exported modifications" ON)
option(CYGI_BUILD_APP "Build the standalone application" ON)
option(CYGI_BUILD_TESTS "Build the self tests and the fake session tool" ON)
option(CYGI_BUILD_MCP "Build the local MCP server" ON)

set(CYGI_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>")

add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /MP /EHsc)

# Vendored dependencies first: they set their own options and must not inherit our warning level.
# Only the file name of the PDB in the binaries, not the path of the machine that built them.
if(MSVC)
    add_link_options(/PDBALTPATH:%_PDB%)
endif()

add_subdirectory(ThirdParty)

add_subdirectory(CyGPUInspectorCore)
add_subdirectory(CyGPUInspectorDecompiler)
add_subdirectory(CyGPUInspectorDatabase)
if(CYGI_BUILD_ADDON)
    add_subdirectory(CyGPUInspectorRS)
endif()
if(CYGI_BUILD_INJECTOR)
    add_subdirectory(CyGPUInjector)
endif()
if(CYGI_BUILD_APP)
    add_subdirectory(CyGPUInspectorApp)
endif()
if(CYGI_BUILD_MCP)
    add_subdirectory(CyGPUInspectorMCP)
endif()
if(CYGI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
