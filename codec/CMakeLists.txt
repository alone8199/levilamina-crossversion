cmake_minimum_required(VERSION 3.20)
project(CrossVersionCodec LANGUAGES CXX)

# 由 scripts/build_codecs.ps1 针对每个协议分支单独 configure 一次。
# 每个分支独立构建，避免不同分支的 Protocol target 同名冲突，也隔离 ABI 命名空间。
set(PROTOCOL_SOURCE_DIR "" CACHE PATH "Path to a checkout of SculkCatalystMC/Protocol (version-specific branch)")
set(CV_PROTOCOL_VERSION 1001 CACHE STRING "Network protocol version produced by this codec")
set(CV_ABI_NS "abi_v1001" CACHE STRING "ABI namespace of the Protocol branch (for diagnostics)")
set(CV_BRANCH "main" CACHE STRING "Protocol branch name (for diagnostics)")
option(CV_ENABLE_FULL_PACKET_MODEL "Use the full IPacket model (only validated on main/v1001)" OFF)

if(NOT PROTOCOL_SOURCE_DIR OR NOT EXISTS "${PROTOCOL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PROTOCOL_SOURCE_DIR must point to a Protocol checkout: '${PROTOCOL_SOURCE_DIR}'")
endif()

add_subdirectory("${PROTOCOL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/protocol" EXCLUDE_FROM_ALL)

add_library(codec_v${CV_PROTOCOL_VERSION} SHARED CodecExports.cpp)
target_compile_features(codec_v${CV_PROTOCOL_VERSION} PRIVATE cxx_std_23)
target_include_directories(codec_v${CV_PROTOCOL_VERSION} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${PROTOCOL_SOURCE_DIR}/include"
)
target_link_libraries(codec_v${CV_PROTOCOL_VERSION} PRIVATE Protocol::Protocol)
target_compile_definitions(codec_v${CV_PROTOCOL_VERSION} PRIVATE
    CV_CODEC_EXPORTS
    CV_PROTOCOL_VERSION=${CV_PROTOCOL_VERSION}
    CV_ABI_NS="${CV_ABI_NS}"
    CV_BRANCH="${CV_BRANCH}"
    NOMINMAX
    UNICODE
)

if(CV_ENABLE_FULL_PACKET_MODEL)
    target_compile_definitions(codec_v${CV_PROTOCOL_VERSION} PRIVATE CV_ENABLE_FULL_PACKET_MODEL=1)
endif()

if(MSVC)
    target_compile_options(codec_v${CV_PROTOCOL_VERSION} PRIVATE /utf-8 /W4 /EHa)
endif()

set_target_properties(codec_v${CV_PROTOCOL_VERSION} PROPERTIES
    OUTPUT_NAME "codec_v${CV_PROTOCOL_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)

message(STATUS "codec: protocol=${CV_PROTOCOL_VERSION} branch=${CV_BRANCH} abi=${CV_ABI_NS} full_model=${CV_ENABLE_FULL_PACKET_MODEL}")
