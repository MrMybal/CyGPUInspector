# CyGPUInspectorDatabase — persistent cache of everything derived from a shader.
# Blobs and JSON on disk: the shader store by signature, and a profile per game.

set(CYGI_DATABASE_SOURCES
    Include/CyGPUInspectorDatabase/ShaderStore.hpp
    Include/CyGPUInspectorDatabase/ShaderNotes.hpp
    Include/CyGPUInspectorDatabase/GameProfile.hpp
    Source/ShaderStore.cpp
    Source/ShaderNotes.cpp
    Source/GameProfile.cpp)

add_library(CyGPUInspectorDatabase STATIC ${CYGI_DATABASE_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CYGI_DATABASE_SOURCES})

target_include_directories(CyGPUInspectorDatabase PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_compile_definitions(CyGPUInspectorDatabase PRIVATE WIN32_LEAN_AND_MEAN)
target_link_libraries(CyGPUInspectorDatabase PUBLIC CyGPUInspectorCore)
