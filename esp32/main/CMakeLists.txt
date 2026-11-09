# L 语言 ESP-IDF 主组件 CMakeLists.txt
# 使用 get_filename_component 规范化解析出仓库根目录绝对路径
get_filename_component(LANG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

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
