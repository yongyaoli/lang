# L 语言统一构建入口 (MSVC / GCC / Clang 通用, ESP32 见 esp32/)
# 用法:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# 产物 l.exe / l 输出到仓库根目录 (与打包脚本约定一致)
cmake_minimum_required(VERSION 3.13)
project(l C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 让可执行文件输出到仓库根目录 (setup.iss / install 脚本均按此约定取 l.exe)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(l
    # 解释器核心
    src/core/lexer.c
    src/core/ast.c
    src/core/parser.c
    src/core/value.c
    src/core/l_heap.c
    src/core/gc.c
    src/core/interp.c
    src/core/builtins.c
    src/core/bytecode.c
    src/core/compiler.c
    src/core/vm.c
    # 标准库模块
    src/stdlib/hal.c
    src/stdlib/hw.c
    src/stdlib/str.c
    src/stdlib/math.c
    src/stdlib/time.c
    src/stdlib/io.c
    src/stdlib/sys.c
    src/stdlib/net.c
    src/stdlib/tcp_posix.c
    src/stdlib/tcp_win.c
    # 入口
    src/main/main.c
)

target_include_directories(l PRIVATE "${CMAKE_SOURCE_DIR}/src/include")

if(MSVC)
    target_compile_options(l PRIVATE /utf-8 /W3 /O2)
    target_link_libraries(l PRIVATE wininet ws2_32)
elseif(WIN32)
    target_link_libraries(l PRIVATE wininet ws2_32)
else()
    target_link_libraries(l PRIVATE m)
endif()
