# CyGPUInspectorDecompiler — disassembly, reflection and (later) decompilation backends.
# Everything here runs in the standalone, never in the game process.

set(CYGI_DECOMPILER_SOURCES
    Include/CyGPUInspectorDecompiler/Disassembler.hpp
    Include/CyGPUInspectorDecompiler/Decompiler.hpp
    Source/Disassembler.cpp
    Source/Decompiler.cpp
    Source/DxbcTextDecompiler.cpp
    Source/MigotoDecompiler.cpp
    Source/DxilSpirvDecompiler.cpp
    Source/DxbcSpirvDecompiler.cpp
    Source/SpirvToHlsl.hpp
    Source/SpirvToHlsl.cpp)

# The 3Dmigoto decompiler, vendored unmodified under GPL-3.
# See ThirdParty/hlsldecompiler/ORIGIN.md for what was taken and how the licence is honoured.
set(CYGI_MIGOTO_ROOT ${CMAKE_SOURCE_DIR}/ThirdParty/hlsldecompiler)
set(CYGI_MIGOTO_SOURCES
    ${CYGI_MIGOTO_ROOT}/DecompileHLSL.cpp
    ${CYGI_MIGOTO_ROOT}/BinaryDecompiler/decode.cpp
    ${CYGI_MIGOTO_ROOT}/BinaryDecompiler/decodeDX9.cpp
    ${CYGI_MIGOTO_ROOT}/BinaryDecompiler/reflect.cpp
    ${CYGI_MIGOTO_ROOT}/shim/Shim.cpp)

# This code is from 2014 and was written for the MSVC dialect of its time. Our strict flags reject
# it, so they are relaxed for these files only: patching them would break the byte for byte
# correspondence with upstream that ORIGIN.md promises and makes auditable.
#   /permissive           the "for each" extension
#   /Zc:strictStrings-    assigning a string literal to a char*
#   /W0 /wd4996           warnings we are not going to fix in someone else's code
set_source_files_properties(${CYGI_MIGOTO_SOURCES} PROPERTIES
    COMPILE_OPTIONS "/W0;/permissive;/Zc:strictStrings-;/wd4996")

add_library(CyGPUInspectorDecompiler STATIC ${CYGI_DECOMPILER_SOURCES} ${CYGI_MIGOTO_SOURCES})

source_group("ThirdParty 3Dmigoto" FILES ${CYGI_MIGOTO_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CYGI_DECOMPILER_SOURCES})

target_include_directories(CyGPUInspectorDecompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(CyGPUInspectorDecompiler PRIVATE
    ${CYGI_MIGOTO_ROOT}
    ${CYGI_MIGOTO_ROOT}/shim
    ${CYGI_MIGOTO_ROOT}/BinaryDecompiler
    ${CYGI_MIGOTO_ROOT}/BinaryDecompiler/include)
target_compile_definitions(CyGPUInspectorDecompiler PRIVATE WIN32_LEAN_AND_MEAN)

# d3dcompiler and dxcompiler are loaded at run time, so only the import libraries needed by the
# COM helpers are linked here.
# dxil-spirv, dxbc-spirv (MIT) and SPIRV-Cross (Apache-2.0 / MIT) form the SPIR-V based
# backends; see ThirdParty.
target_link_libraries(CyGPUInspectorDecompiler
    PUBLIC CyGPUInspectorCore
    PRIVATE dxguid dxil-spirv-c-static dxbc-spirv-emitter spirv-cross-hlsl)
