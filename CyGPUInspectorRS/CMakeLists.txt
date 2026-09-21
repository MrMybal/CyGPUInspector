# CyGPUInspectorRS — native ReShade add-on (.addon64)
enable_language(RC)

set(CYGI_THIRDPARTY ${CMAKE_SOURCE_DIR}/ThirdParty)

set(CYGI_RS_SOURCES
    Source/Addon/AddonMain.cpp
    Source/Addon/Log.hpp
    Source/Addon/Log.cpp
    Source/Addon/Config.hpp
    Source/Addon/Config.cpp
    Source/Addon/DeviceContext.hpp
    Source/Addon/DeviceContext.cpp
    Source/Addon/InProcess.hpp
    Source/Addon/InProcess.cpp
    Source/Addon/CyGPUInspectorRS.rc
    Source/Tracking/CommandListState.hpp
    Source/Tracking/ShaderTracker.hpp
    Source/Tracking/ShaderTracker.cpp
    Source/Tracking/ResourceTracker.hpp
    Source/Tracking/ResourceTracker.cpp
    Source/Tracking/FrameRecorder.hpp
    Source/Tracking/DeepCapture.hpp
    Source/Tracking/DeepCapture.cpp
    Source/Tracking/StateShadow.hpp
    Source/Tracking/StateShadow.cpp
    Source/Tracking/ResourceNames.hpp
    Source/Tracking/ResourceNames.cpp
    Source/Tracking/FrameRecorder.cpp
    Source/Control/PipelineBlueprint.hpp
    Source/Control/PipelineBlueprint.cpp
    Source/Control/RuntimeControl.hpp
    Source/Control/RuntimeControl.cpp
    Source/Preview/PreviewBridge.hpp
    Source/Preview/CaptureBuffers.hpp
    Source/Preview/CaptureBuffers.cpp
    Source/Preview/PreviewBridge.cpp
    Source/Profiling/GpuTimer.hpp
    Source/Profiling/GpuTimer.cpp
    Source/Ipc/IpcServer.hpp
    Source/Ipc/IpcServer.cpp
    Source/UI/Overlay.hpp
    Source/UI/Overlay.cpp)

set_source_files_properties(${CYGI_RS_SOURCES} PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")

add_library(CyGPUInspectorRS SHARED ${CYGI_RS_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "Source" FILES ${CYGI_RS_SOURCES})

target_include_directories(CyGPUInspectorRS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorCore/Include
    ${CYGI_THIRDPARTY}/reshade/include
    ${CYGI_THIRDPARTY}/reshade_imgui)

target_compile_definitions(CyGPUInspectorRS PRIVATE
    ImTextureID=ImU64              # required by reshade_overlay.hpp (resource_view handle == 64-bit)
    CYGI_ADDON_NAME="CyGPUInspectorRS")

target_link_libraries(CyGPUInspectorRS PRIVATE CyGPUInspectorCore
    advapi32 shell32 user32 version psapi)

set_target_properties(CyGPUInspectorRS PROPERTIES
    OUTPUT_NAME "CyGPUInspectorRS"
    PREFIX ""
    SUFFIX ".addon64"
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY     "${CYGI_OUTPUT_DIR}")

# Keep symbols in Release too: crash dumps from inside games are much easier to read.
target_compile_options(CyGPUInspectorRS PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(CyGPUInspectorRS PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
