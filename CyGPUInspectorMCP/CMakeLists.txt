# CyGPUInspectorMCP — the MCP server an agent launches. A stdio proxy, nothing more.

add_executable(CyGPUInspectorMCP Source/Main.cpp Source/CyGPUInspectorMCP.rc)

target_link_libraries(CyGPUInspectorMCP PRIVATE CyGPUInspectorCore)
target_compile_definitions(CyGPUInspectorMCP PRIVATE WIN32_LEAN_AND_MEAN)

set_target_properties(CyGPUInspectorMCP PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CYGI_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY     "${CYGI_OUTPUT_DIR}")
