# L 语言 ESP-IDF 主组件 CMakeLists.txt
# 使用 CMAKE_CURRENT_LIST_DIR 将父目录源码解析为绝对路径, 避免 Ninja 在 Docker/Linux 下出现路径寻址失败
set(LANG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
    SRCS
        "app_main.c"
        "${LANG_ROOT}/lexer.c"
        "${LANG_ROOT}/ast.c"
        "${LANG_ROOT}/parser.c"
        "${LANG_ROOT}/value.c"
        "${LANG_ROOT}/gc.c"
        "${LANG_ROOT}/interp.c"
        "${LANG_ROOT}/builtins.c"
        "${LANG_ROOT}/stdlib/hal.c"
        "${LANG_ROOT}/stdlib/hw.c"
        "${LANG_ROOT}/stdlib/string.c"
        "${LANG_ROOT}/stdlib/math.c"
        "${LANG_ROOT}/stdlib/time.c"
        "${LANG_ROOT}/stdlib/io.c"
        "${LANG_ROOT}/stdlib/sys.c"
    INCLUDE_DIRS
        "${LANG_ROOT}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-format-truncation
)
