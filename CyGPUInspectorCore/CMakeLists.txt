# CyGPUInspectorCore — code shared by the add-on, the standalone and the tools.
# No dependency beyond the Win32 API: this library is linked into game processes.

set(CYGI_CORE_SOURCES
    Include/CyGPUInspectorCore/Version.hpp
    Include/CyGPUInspectorCore/Protocol.hpp
    Include/CyGPUInspectorCore/Format.hpp
    Include/CyGPUInspectorCore/Sha256.hpp
    Include/CyGPUInspectorCore/SharedMemory.hpp
    Include/CyGPUInspectorCore/RingBuffer.hpp
    Include/CyGPUInspectorCore/SessionDirectory.hpp
    Include/CyGPUInspectorCore/ControlPipe.hpp
    Include/CyGPUInspectorCore/ShaderBlob.hpp
    Include/CyGPUInspectorCore/Json.hpp
    Include/CyGPUInspectorCore/Localization.hpp
    Include/CyGPUInspectorCore/ModPackage.hpp
    Source/Protocol.cpp
    Source/Format.cpp
    Source/Sha256.cpp
    Source/SharedMemory.cpp
    Source/RingBuffer.cpp
    Source/SessionDirectory.cpp
    Source/ControlPipe.cpp
    Source/ShaderBlob.cpp
    Source/Json.cpp
    Source/Localization.cpp
    Source/ModPackage.cpp)

add_library(CyGPUInspectorCore STATIC ${CYGI_CORE_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CYGI_CORE_SOURCES})

target_include_directories(CyGPUInspectorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(CyGPUInspectorCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(CyGPUInspectorCore PRIVATE WIN32_LEAN_AND_MEAN)

# The Unreal plugin is built by Unreal's own tool chain and carries its own copy of the in-process
# interface. Kept identical here, at configure time; configure_file only writes when it differs.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/CyGPUInspectorCore/InProcessApi.h
    ${CMAKE_SOURCE_DIR}/CyGPUInspectorUnreal/CyGPUInspector/Source/ThirdParty/CyGPUInspector/CyGPUInspectorInProcessApi.h
    COPYONLY)
