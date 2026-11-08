/* 语法分析器测试入口:解析文件(或内置示例),打印 AST */
#include <stdio.h>
#include <stdlib.h>

#include "parser.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "无法打开文件: %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    static const char *demo =
        "fn add(a, b) { return a + b; }\n"
        "let x = 1 + 2 * 3 - 4;\n"
        "let ok = not a and b or c == d;\n"
        "a = b = 5;\n"
        "while i < 3 { if i == 1 { continue; } else if i == 2 { break; } else { i = i + 1; } }\n"
        "for let j = 0; j < 10; j = j + 1 { print(j); }\n"
        "fn counter() { let n = 0; return fn() { n = n + 1; return n; }; }\n"
        "let xs = [1, 2, 3,]; let p = { name: \"L\", \"ver\": 0.1 };\n"
        "print(xs[0], p.name, -x);\n"
        "xs[1] = 9; p.name = \"M\";\n";

    char *src = argc > 1 ? read_file(argv[1]) : NULL;
    if (argc > 1 && !src) return 1;

    int had_error = 0;
    Node *prog = parse_program(src ? src : demo, &had_error);
    if (!had_error) ast_print(prog);
    else fprintf(stderr, "解析失败。\n");

    ast_free(prog);
    free(src);
    return had_error ? 1 : 0;
}
