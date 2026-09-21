# CyGPUInspectorApp — standalone analysis application (Win32 + D3D11 + Dear ImGui docking)
enable_language(RC)

set(CYGI_THIRDPARTY ${CMAKE_SOURCE_DIR}/ThirdParty)

set(CYGI_IMGUI_SOURCES
    ${CYGI_THIRDPARTY}/imgui/imgui.cpp
    ${CYGI_THIRDPARTY}/imgui/imgui_draw.cpp
    ${CYGI_THIRDPARTY}/imgui/imgui_tables.cpp
    ${CYGI_THIRDPARTY}/imgui/imgui_widgets.cpp
    ${CYGI_THIRDPARTY}/imgui/imgui_demo.cpp
    ${CYGI_THIRDPARTY}/imgui/backends/imgui_impl_dx11.cpp
    ${CYGI_THIRDPARTY}/imgui/backends/imgui_impl_win32.cpp)
# Dear ImGui is third party code: do not fail our /W4 build on its warnings.
set_source_files_properties(${CYGI_IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")

set(CYGI_APP_SOURCES
    Source/Main.cpp
    Source/CyGPUInspectorApp.rc
    Source/App/Application.hpp
    Source/App/Application.cpp
    Source/App/CapturePanels.cpp
    Source/App/ContinuousTimeline.cpp
    Source/App/TimelineDrawing.hpp
    Source/App/ModExportPanel.cpp
    Source/App/CaptureFiles.cpp
    Source/Mod/ModRecorder.hpp
    Source/Mod/ModRecorder.cpp
    Source/Analysis/FrameGraph.hpp
    Source/Analysis/FrameGraph.cpp
    Source/Analysis/FrameTimeline.hpp
    Source/Analysis/FrameTimeline.cpp
    Source/Analysis/FrameTrack.hpp
    Source/Analysis/FrameTrack.cpp
    Source/Analysis/ShaderAnalysisService.hpp
    Source/Analysis/ShaderAnalysisService.cpp
    Source/Mcp/McpBridge.hpp
    Source/Mcp/McpBridge.cpp
    Source/Render/PreviewRenderer.hpp
    Source/Render/PreviewRenderer.cpp
    Source/Render/SharedTexture.hpp
    Source/Render/SharedTexture.cpp
    Source/Render/CaptureBufferWriter.hpp
    Source/Render/CaptureBufferWriter.cpp
    Source/Session/CaptureArchive.hpp
    Source/Session/CaptureArchive.cpp
    Source/Session/SessionModel.hpp
    Source/Session/SessionModel.cpp
    Source/Session/SessionClient.hpp
    Source/Session/SessionClient.cpp)

add_executable(CyGPUInspectorApp WIN32 ${CYGI_APP_SOURCES} ${CYGI_IMGUI_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "Source" FILES ${CYGI_APP_SOURCES})
source_group("ThirdParty\\imgui" FILES ${CYGI_IMGUI_SOURCES})

target_include_directories(CyGPUInspectorApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorCore/Include
    ${CYGI_THIRDPARTY}/imgui
    ${CYGI_THIRDPARTY}/imgui/backends)

target_compile_definitions(CyGPUInspectorApp PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

target_link_libraries(CyGPUInspectorApp PRIVATE CyGPUInspectorCore CyGPUInspectorDecompiler CyGPUInspectorDatabase
    d3d11 dxgi dxguid user32 gdi32 shell32 advapi32 ole32 windowscodecs)

set_target_properties(CyGPUInspectorApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY     "${CYGI_OUTPUT_DIR}")

target_compile_options(CyGPUInspectorApp PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(CyGPUInspectorApp PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

# The interface is written in English and translated by the catalogues in Lang/. They are copied
# beside the executable because that is where the application looks for them at start-up, and
# because a translator has to be able to edit one and restart without rebuilding anything.
#
# A target of its own rather than a POST_BUILD step: a POST_BUILD only fires when the executable
# relinks, so editing a catalogue alone would leave a stale copy next to the binary — which is
# exactly the kind of thing nobody notices until a correction appears not to have worked.
add_custom_target(CyGPUInspectorLang ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Lang" "${CYGI_OUTPUT_DIR}/Lang"
    COMMENT "Copying the translation catalogues"
    VERBATIM)
add_dependencies(CyGPUInspectorApp CyGPUInspectorLang)
