/* 词法分析器测试入口:读取文件(或内置示例),打印所有 Token */
#include <stdio.h>
#include <stdlib.h>

#include "../src/include/lexer.h"

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
        "let s = \"hi\\n\"; // 注释\n"
        "if x >= 3.14 and not y { print(add(1, 2)); }\n"
        "let p = { name: \"L\" }; xs[0]; @\n";

    char *src = argc > 1 ? read_file(argv[1]) : NULL;
    if (argc > 1 && !src) return 1;

    Lexer lx;
    lexer_init(&lx, src ? src : demo);

    for (;;) {
        Token t = lexer_next(&lx);
        if (t.type == T_ERROR) {
            /* 出错后继续扫描,以便一次报出多个错误 */
            printf("%d:%d  ERROR  %s\n", t.line, t.col, t.error);
        } else {
            printf("%d:%d  %-10s '%.*s'\n", t.line, t.col,
                   token_type_name(t.type), (int)t.length, t.start);
        }
        if (t.type == T_EOF) break;
    }
    free(src);
    return 0;
}
