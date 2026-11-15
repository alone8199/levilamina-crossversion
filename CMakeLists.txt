# CrossVersion — 独立（standalone）构建路径
#
# 与 xmake.lua 的区别：
#   xmake.lua  = 真机构建，链接 LeviLamina SDK + BDS 符号，产出可 hook 网络的插件。
#   CMakeLists = 无 SDK 构建，用 compat/ll/api/Logger.h 替代 ll::Logger，不链接 BDS，
#                产出 passthrough 骨架 DLL。用于 CI 验证「每个版本的注入头 + 路由表
#                + 配置 / codec 加载 / 翻译链」能编译、能链接、能跑自测。
#
# 两条路径共用同一份 src/，互不修改源码。
cmake_minimum_required(VERSION 3.20)
project(CrossVersion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CV_BUILD_TESTS "Build the router self-test (non-Windows)" ON)

set(CV_SOURCES
    src/CrossVersion.cpp
    src/NetworkHooks.cpp
    src/DllMain.cpp
)

add_library(CrossVersion SHARED ${CV_SOURCES})

target_include_directories(CrossVersion PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)

target_compile_definitions(CrossVersion PRIVATE
    CV_STANDALONE_BUILD=1
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    # /bigobj: nlohmann/json.hpp 单头文件在 MSVC 下会产生大量 section，
    # 缺了会报 C1128（对象文件节数超限）。
    target_compile_options(CrossVersion PRIVATE /utf-8 /W4 /EHa /permissive- /bigobj)
    set_target_properties(CrossVersion PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    target_compile_options(CrossVersion PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
    target_link_libraries(CrossVersion PRIVATE kernel32)
endif()

# 让 CI 的 ubuntu job 能跑真实断言，而不是只做「文件存在」检查。
if(CV_BUILD_TESTS AND NOT WIN32)
    add_executable(cv_verify tests/router_test.cpp)
    target_include_directories(cv_verify PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
    )
    target_link_libraries(cv_verify PRIVATE CrossVersion)
    target_compile_definitions(cv_verify PRIVATE CV_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    enable_testing()
    add_test(NAME router COMMAND cv_verify)
endif()
