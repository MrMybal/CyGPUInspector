# CyGPUInjector — native ReShade add-on (.addon64) that applies CyGPUInspector modifications.
#
# Deliberately small: no IPC, no tracking, no analysis. It reads mod packages at start-up and
# answers a handful of ReShade callbacks. The log and settings helpers are shared with
# CyGPUInspectorRS rather than copied, with their prefix and ini section set per add-on.
enable_language(RC)

set(CYGI_THIRDPARTY ${CMAKE_SOURCE_DIR}/ThirdParty)
set(CYGI_RS_ADDON ${CMAKE_SOURCE_DIR}/CyGPUInspectorRS/Source/Addon)

set(CYGI_INJECTOR_SOURCES
    Source/AddonMain.cpp
    Source/ModLibrary.hpp
    Source/ModLibrary.cpp
    Source/Apply.hpp
    Source/Apply.cpp
    Source/Overlay.hpp
    Source/Overlay.cpp
    Source/CyGPUInjector.rc
    ${CYGI_RS_ADDON}/Log.hpp
    ${CYGI_RS_ADDON}/Log.cpp
    ${CYGI_RS_ADDON}/Config.hpp
    ${CYGI_RS_ADDON}/Config.cpp)

set_source_files_properties(${CYGI_INJECTOR_SOURCES} PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")

add_library(CyGPUInjector SHARED ${CYGI_INJECTOR_SOURCES})

# The two shared helpers live under CyGPUInspectorRS, so they are grouped separately rather than
# forced under a tree they do not belong to.
source_group("Source" FILES ${CYGI_INJECTOR_SOURCES})
source_group("Shared" FILES
    ${CYGI_RS_ADDON}/Log.hpp ${CYGI_RS_ADDON}/Log.cpp
    ${CYGI_RS_ADDON}/Config.hpp ${CYGI_RS_ADDON}/Config.cpp)

target_include_directories(CyGPUInjector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CYGI_RS_ADDON}
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorCore/Include
    ${CYGI_THIRDPARTY}/reshade/include
    ${CYGI_THIRDPARTY}/reshade_imgui)

target_compile_definitions(CyGPUInjector PRIVATE
    ImTextureID=ImU64              # required by reshade_overlay.hpp (resource_view handle == 64-bit)
    CYGI_ADDON_NAME="CyGPUInjector"
    CYGI_CONFIG_SECTION="CYGPUINJECTOR")

target_link_libraries(CyGPUInjector PRIVATE CyGPUInspectorCore
    advapi32 shell32 user32 version)

set_target_properties(CyGPUInjector PROPERTIES
    OUTPUT_NAME "CyGPUInjector"
    PREFIX ""
    SUFFIX ".addon64"
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY     "${CYGI_OUTPUT_DIR}")

# Keep symbols in Release too: crash dumps from inside games are much easier to read.
target_compile_options(CyGPUInjector PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(CyGPUInjector PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
