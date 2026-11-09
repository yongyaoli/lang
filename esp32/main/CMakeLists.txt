# L 语言 ESP-IDF 主组件 CMakeLists.txt
# 注意: main 组件不显式指定 REQUIRES, 以便 ESP-IDF 自动引入全部系统/驱动组件
idf_component_register(
    SRCS
        "app_main.c"
        "../../lexer.c"
        "../../ast.c"
        "../../parser.c"
        "../../value.c"
        "../../gc.c"
        "../../interp.c"
        "../../builtins.c"
        "../../stdlib/hal.c"
        "../../stdlib/hw.c"
        "../../stdlib/string.c"
        "../../stdlib/math.c"
        "../../stdlib/time.c"
        "../../stdlib/io.c"
        "../../stdlib/sys.c"
    INCLUDE_DIRS
        "../.."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-format-truncation
)
