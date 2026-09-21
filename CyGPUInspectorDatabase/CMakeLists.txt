# CyGPUInspectorDatabase — persistent cache of everything derived from a shader.
# Blobs on disk today; SQLite metadata and per game profiles come with the later milestones.

set(CYGI_DATABASE_SOURCES
    Include/CyGPUInspectorDatabase/ShaderStore.hpp
    Include/CyGPUInspectorDatabase/ShaderNotes.hpp
    Source/ShaderStore.cpp
    Source/ShaderNotes.cpp)

add_library(CyGPUInspectorDatabase STATIC ${CYGI_DATABASE_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CYGI_DATABASE_SOURCES})

target_include_directories(CyGPUInspectorDatabase PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_compile_definitions(CyGPUInspectorDatabase PRIVATE WIN32_LEAN_AND_MEAN)
target_link_libraries(CyGPUInspectorDatabase PUBLIC CyGPUInspectorCore)
