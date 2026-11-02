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
# CyUI owns the shared syntax engines and the optional C++20 editor backend.
set_source_files_properties(${CYGI_IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")

set(CYGI_APP_SOURCES
    Source/Main.cpp
    Source/CyGPUInspectorApp.rc
    Source/App/Application.hpp
    Source/App/Application.cpp
    Source/App/Appearance.cpp
    Source/App/SharedViews.cpp
    Source/App/CapturePanels.cpp
    Source/App/ContinuousTimeline.cpp
    Source/App/TimelineDrawing.hpp
    Source/App/ModExportPanel.cpp
    Source/App/CaptureFiles.cpp
    Source/App/AssistantPanel.cpp
    Source/App/HlslEditorPanel.cpp
    Source/App/GameProfilePanel.cpp
    Source/App/TimingFocus.cpp
    Source/App/LayoutHelpers.hpp
    Source/Assistant/AssistantText.hpp
    Source/Assistant/AssistantText.cpp
    Source/Assistant/AssistantProvider.hpp
    Source/Assistant/AssistantProvider.cpp
    Source/Assistant/AssistantAgent.hpp
    Source/Assistant/AssistantAgent.cpp
    Source/Assistant/Assistant.hpp
    Source/Assistant/Assistant.cpp
    Source/Editor/CompilerMessages.hpp
    Source/Editor/CompilerMessages.cpp
    Source/Editor/HlslEditor.hpp
    Source/Editor/HlslEditor.cpp
    Source/Mod/ModRecorder.hpp
    Source/Mod/ModRecorder.cpp
    Source/Analysis/FrameGraph.hpp
    Source/Analysis/FrameGraph.cpp
    Source/Analysis/FrameTimeline.hpp
    Source/Analysis/FrameTimeline.cpp
    Source/Analysis/FrameTrack.hpp
    Source/Analysis/FrameTrack.cpp
    Source/Analysis/ShaderAnalysisService.hpp
    Source/Analysis/ShaderQuery.hpp
    Source/Analysis/ShaderQuery.cpp
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

# rc.exe includes Version.hpp, but ninja does not see a resource compiler's includes: without
# this, a version bump leaves the file properties of the binary at the previous version.
set_source_files_properties(Source/CyGPUInspectorApp.rc PROPERTIES OBJECT_DEPENDS
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorCore/Include/CyGPUInspectorCore/Version.hpp)

add_library(CyGPUInspectorImGui STATIC ${CYGI_IMGUI_SOURCES})
target_include_directories(CyGPUInspectorImGui PUBLIC ${CYGI_THIRDPARTY}/imgui ${CYGI_THIRDPARTY}/imgui/backends)
set(CYUI_IMGUI_TARGET CyGPUInspectorImGui CACHE STRING "Dear ImGui target for CyUI" FORCE)
set(CYUI_BUILD_DEMO OFF CACHE BOOL "Build only embedded CyUI modules" FORCE)
set(CYUI_BUILD_AUDIO OFF CACHE BOOL "No audio UI in the GPU inspector" FORCE)
option(CYGI_BUILD_UI_GALLERY "Enable the optional CyUI developer gallery" OFF)
set(CYUI_BUILD_GALLERY ${CYGI_BUILD_UI_GALLERY} CACHE BOOL "No gallery in the production application" FORCE)
set(CYUI_BUILD_WEB_CEF OFF CACHE BOOL "No browser runtime" FORCE)
set(CYUI_MODULES "CodeEditor;Timeline;Plots;Images;Inspector;Diagnostics;DocumentViews" CACHE STRING "GPU inspector presentation modules" FORCE)
add_subdirectory(${CYGI_THIRDPARTY}/CyUI ${CMAKE_CURRENT_BINARY_DIR}/CyUI)

add_executable(CyGPUInspectorApp WIN32 ${CYGI_APP_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "Source" FILES ${CYGI_APP_SOURCES})
source_group("ThirdParty\\imgui" FILES ${CYGI_IMGUI_SOURCES})

target_include_directories(CyGPUInspectorApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorCore/Include
    ${CYGI_THIRDPARTY}/imgui
    ${CYGI_THIRDPARTY}/imgui/backends)

target_compile_definitions(CyGPUInspectorApp PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

target_link_libraries(CyGPUInspectorApp PRIVATE CyGPUInspectorCore CyGPUInspectorDecompiler CyGPUInspectorDatabase
    CyUI::Core CyUI::CodeEditor CyUI::Timeline CyUI::Plots CyUI::Images
    CyUI::Inspector CyUI::Diagnostics CyUI::DocumentViews d3d11 dxgi dxguid user32 gdi32 shell32 advapi32 ole32 windowscodecs winhttp comdlg32)

if(CYGI_BUILD_UI_GALLERY)
    target_link_libraries(CyGPUInspectorApp PRIVATE CyUI::Gallery)
endif()

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
