# Tests — run without a game and without a GPU.
#   CyGPUInspectorCoreTests    self checks of hashing, ring buffer, DXBC parsing, session directory
#   CyGPUInspectorFakeSession  a synthetic add-on, to exercise CyGPUInspectorApp end to end

add_executable(CyGPUInspectorCoreTests CyGPUInspectorCoreTests/Main.cpp)
target_link_libraries(CyGPUInspectorCoreTests PRIVATE CyGPUInspectorCore)
set_target_properties(CyGPUInspectorCoreTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}")

add_executable(CyGPUInspectorFakeSession CyGPUInspectorFakeSession/Main.cpp)
target_link_libraries(CyGPUInspectorFakeSession PRIVATE CyGPUInspectorCore d3d11 dxgi dxguid)
set_target_properties(CyGPUInspectorFakeSession PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}")

# Reuses the standalone's IPC code directly, so the test exercises exactly what the app runs.
add_executable(CyGPUInspectorIpcTests
    CyGPUInspectorIpcTests/Main.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Session/SessionModel.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Session/SessionClient.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Render/SharedTexture.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Analysis/FrameGraph.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Analysis/FrameTimeline.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Analysis/FrameTrack.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Session/CaptureArchive.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Mcp/McpBridge.cpp)
target_include_directories(CyGPUInspectorIpcTests PRIVATE ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source)
target_link_libraries(CyGPUInspectorIpcTests PRIVATE CyGPUInspectorCore d3d11 dxgi dxguid)
add_dependencies(CyGPUInspectorIpcTests CyGPUInspectorFakeSession CyGPUInspectorMCP)
# The in-process interface is tested on the real add-on DLL, the way the Unreal plugin loads it.
if(TARGET CyGPUInspectorRS)
    add_dependencies(CyGPUInspectorIpcTests CyGPUInspectorRS)
endif()
set_target_properties(CyGPUInspectorIpcTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}")

# Compiles real shaders with the real compilers, then disassembles and reflects them.
add_executable(CyGPUInspectorShaderTests
    CyGPUInspectorShaderTests/Main.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Render/PreviewRenderer.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Render/SharedTexture.cpp
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source/Render/CaptureBufferWriter.cpp)
target_include_directories(CyGPUInspectorShaderTests PRIVATE
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorApp/Source)
target_link_libraries(CyGPUInspectorShaderTests PRIVATE
    CyGPUInspectorCore CyGPUInspectorDecompiler CyGPUInspectorDatabase d3d11 dxgi dxguid)
set_target_properties(CyGPUInspectorShaderTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}")

add_test(NAME CoreTests COMMAND CyGPUInspectorCoreTests)
add_test(NAME ShaderTests COMMAND CyGPUInspectorShaderTests)
add_test(NAME IpcTests COMMAND CyGPUInspectorIpcTests)
