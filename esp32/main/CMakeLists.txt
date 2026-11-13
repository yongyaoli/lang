# L 语言 ESP-IDF 主组件 CMakeLists.txt
# 使用 get_filename_component 规范化解析出仓库根目录绝对路径
get_filename_component(LANG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

idf_component_register(
    SRCS
        "app_main.c"
        "${LANG_ROOT}/src/core/l_heap.c"
        "${LANG_ROOT}/src/core/lexer.c"
        "${LANG_ROOT}/src/core/ast.c"
        "${LANG_ROOT}/src/core/parser.c"
        "${LANG_ROOT}/src/core/value.c"
        "${LANG_ROOT}/src/core/gc.c"
        "${LANG_ROOT}/src/core/interp.c"
        "${LANG_ROOT}/src/core/builtins.c"
        "${LANG_ROOT}/src/core/bytecode.c"
        "${LANG_ROOT}/src/core/compiler.c"
        "${LANG_ROOT}/src/core/vm.c"
        "${LANG_ROOT}/src/stdlib/hal.c"
        "${LANG_ROOT}/src/stdlib/hw.c"
        "${LANG_ROOT}/src/stdlib/str.c"
        "${LANG_ROOT}/src/stdlib/math.c"
        "${LANG_ROOT}/src/stdlib/time.c"
        "${LANG_ROOT}/src/stdlib/io.c"
        "${LANG_ROOT}/src/stdlib/sys.c"
        "${LANG_ROOT}/src/stdlib/net.c"
        "${LANG_ROOT}/src/stdlib/tcp_posix.c"
    INCLUDE_DIRS
        "${LANG_ROOT}/src/include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-error=format
    -Wno-format
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-format-truncation
)
