/* L 语言入口:l 文件.l 运行脚本;不带参数进入 REPL */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* 不包含 <windows.h>:它会和我们的 TokenType 重名,这里只声明要用的一个函数 */
__declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int cp);
#endif

#include "interp.h"
#include "l_hal.h"
#include "parser.h"
#include "version.h"
#include "compiler.h"
#include "vm.h"

static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t l1 = strlen(str);
    size_t l2 = strlen(suffix);
    if (l1 < l2) return 0;
    return strcmp(str + l1 - l2, suffix) == 0;
}

static int run_bytecode_file(const char *path) {
    Interp in;
    interp_init(&in);
    in.current_file = path;
    Proto *proto = vm_load_proto_from_file(&in, path);
    if (!proto) {
        fprintf(stderr, "错误: 无法解析或读取字节码文件 '%s' (魔数或格式无效)\n", path);
        interp_destroy(&in);
        return 74;
    }
    VM vm;
    vm_init(&vm, &in);
    int res = vm_run_proto(&vm, proto, 0) ? 70 : 0;
    vm_free(&vm);
    interp_destroy(&in);
    return res;
}

static int run_file_vm(const char *path) {
    char *src = read_file_text(path);
    if (!src) return 74;

    int had_error = 0;
    Node *prog = parse_program_file(src, path, &had_error);
    if (had_error) {
        free(src);
        return 65;
    }

    Interp in;
    interp_init(&in);
    in.current_file = path;
    int res = interp_run_vm(&in, prog, 0) ? 70 : 0;
    interp_destroy(&in);
    free(src);
    return res;
}

/* 运行脚本文件。退出码:0 成功,65 语法错误,70 运行时错误 */
static int run_file(const char *path) {
    if (ends_with(path, ".lc")) {
        return run_bytecode_file(path);
    }
    char *src = read_file_text(path);
    if (!src) return 74;

    int had_error = 0;
    Node *prog = parse_program_file(src, path, &had_error);
    if (had_error) {
        free(src);
        return 65;
    }

    Interp in;
    interp_init(&in);
    in.current_file = path;
    /* AST 不释放:函数值会一直引用其中的节点,进程退出时由系统回收 */
    int res = interp_run(&in, prog, 0) ? 70 : 0;
    interp_destroy(&in);
    return res;
}

/* 粗略统计未配对的 '{'(忽略字符串里的括号),用于多行输入 */
static int brace_balance(const char *s) {
    int bal = 0, in_str = 0;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) s++;
            else if (*s == '"') in_str = 0;
        } else if (*s == '"') in_str = 1;
        else if (*s == '/' && s[1] == '/') break;
        else if (*s == '{') bal++;
        else if (*s == '}') bal--;
    }
    return bal;
}

/* 打印启动信息,风格仿照 Python:
 *   L 0.1.0 (Nov  6 2026, 13:58:00) [GCC 16.2.0 64 bit] on win32
 *   Type "help", "copyright", "license" or "exit" for more information. */
static void print_banner(void) {
    char compiler[64];
#if defined(__clang__)
    snprintf(compiler, sizeof compiler, "Clang %s", __clang_version__);
#elif defined(__GNUC__)
    snprintf(compiler, sizeof compiler, "GCC %s", __VERSION__);
#elif defined(_MSC_VER)
    snprintf(compiler, sizeof compiler, "MSC v.%d", _MSC_VER);
#else
    snprintf(compiler, sizeof compiler, "unknown compiler");
#endif

#if defined(_WIN32)
    const char *platform = "win32";
#elif defined(__APPLE__)
    const char *platform = "darwin";
#elif defined(__linux__)
    const char *platform = "linux";
#else
    const char *platform = "unknown";
#endif

    printf("L %s (%s, %s) [%s %d bit] on %s\n", L_VERSION, __DATE__, __TIME__,
           compiler, (int)(sizeof(void *) * 8), platform);
    printf("Type \"help\", \"copyright\", \"license\" or \"exit\" for more information.\n");
    printf("ESP32 快捷命令: ports() | upload(\"COM3\", \"main.l\") | connect(\"COM3\") | help(\"esp32\")\n");
}

/* 去掉首尾空白后,与 word 完全相同? */
static int is_bare_word(const char *src, const char *word) {
    while (*src == ' ' || *src == '\t') src++;
    size_t n = strlen(word);
    if (strncmp(src, word, n) != 0) return 0;
    src += n;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n' || *src == ';') src++;
    return *src == '\0';
}

/* REPL 中直接输入 help / exit 等(不带括号)时,像 Python 一样给出提示。
 * 返回 1 表示已处理。copyright / license 则直接执行对应函数。 */
static int handle_bare_word(Interp *in, const char *src) {
    if (is_bare_word(src, "help")) {
        printf("输入 help() 查看完整帮助, 输入 help(\"esp32\") 查看 ESP32 串口烧录(upload)与控制指南。\n");
        return 1;
    }
    if (is_bare_word(src, "upload")) {
        printf("用法: upload(\"COM3\", \"main.l\") 烧录脚本到 ESP32; 或 upload(\"sim\", \"main.l\") 本地模拟演练。\n");
        return 1;
    }
    if (is_bare_word(src, "ports")) {
        int err = 0;
        Node *prog = parse_program("ports();", &err);
        interp_run(in, prog, 1);
        return 1;
    }
    if (is_bare_word(src, "connect")) {
        printf("用法: connect(\"COM3\") 进入 ESP32 串口交互终端(按 Ctrl+] 或输入 .exit 退出)。\n");
        return 1;
    }
    if (is_bare_word(src, "exit") || is_bare_word(src, "quit")) {
#ifdef _WIN32
        printf("Use exit() or Ctrl-Z plus Return to exit\n");
#else
        printf("Use exit() or Ctrl-D (i.e. EOF) to exit\n");
#endif
        return 1;
    }
    if (is_bare_word(src, "copyright") || is_bare_word(src, "license")) {
        int err = 0;
        Node *prog = parse_program(is_bare_word(src, "license") ? "license();" : "copyright();", &err);
        interp_run(in, prog, 0);
        return 1;
    }
    return 0;
}

static void repl(void) {
    Interp in;
    interp_init(&in);
    print_banner();

    char line[4096];
    for (;;) {
        /* 读一段完整输入:括号未配对就继续读下一行 */
        size_t cap = 1024, len = 0;
        char *src = malloc(cap);
        src[0] = '\0';
        int first = 1, eof = 0;
        do {
            printf(first ? ">>> " : "... ");
            fflush(stdout);
            if (!fgets(line, sizeof line, stdin)) { eof = 1; break; }
            size_t n = strlen(line);
            if (len + n + 2 > cap) { cap = (len + n + 2) * 2; src = realloc(src, cap); }
            memcpy(src + len, line, n + 1);
            len += n;
            first = 0;
        } while (brace_balance(src) > 0);
        if (eof) { free(src); break; }

        /* 末尾不是 ';' 或 '}' 时自动补 ';',方便直接输入表达式 */
        size_t e = len;
        while (e > 0 && (src[e - 1] == '\n' || src[e - 1] == '\r' || src[e - 1] == ' ')) e--;
        if (e == 0) { free(src); continue; }
        if (handle_bare_word(&in, src)) { free(src); continue; }
        if (src[e - 1] != ';' && src[e - 1] != '}') { src[e] = ';'; src[e + 1] = '\0'; }

        int had_error = 0;
        Node *prog = parse_program_file(src, "<repl>", &had_error);
        if (!had_error) interp_run(&in, prog, 1);
        /* src 与 prog 都不释放:之前定义的函数/字符串仍可能引用它们 */
    }
    interp_destroy(&in);
    printf("\n");               /* 与 Python 一致:EOF 退出时只换行 */
}

static void print_usage(const char *prog) {
    printf("L 语言 %s\n\n用法:\n", L_VERSION);
    printf("  %s                          进入交互模式(REPL)\n", prog);
    printf("  %s 脚本.l [参数...]          运行脚本文件(参数可用 sys_args() 读取)\n", prog);
    printf("  %s 脚本.lc [参数...]         直接加载并运行预编译致密字节码文件\n", prog);
    printf("  %s --vm 脚本.l              强制启用栈式虚拟机(VM)执行脚本\n", prog);
    printf("  %s -c 脚本.l [-o 产物.lc]    编译源码为紧凑二进制字节码文件(.lc)\n", prog);
    printf("  %s -d, --disasm 脚本.l       反汇编并查看编译生成的单字节操作码序列\n", prog);
    printf("  %s -p, --ports              扫描并列出本机可用串口(如 COM3)\n", prog);
    printf("  %s -u, --upload 串口 脚本.l  将脚本通过串口烧录到 ESP32 裸分区并运行\n", prog);
    printf("  %s -s, --serial 串口 [波特率] 连接 ESP32 串口交互终端(默认 115200)\n", prog);
    printf("  %s -h, --help               显示本帮助\n", prog);
    printf("  %s -v, --version            显示版本号\n\n", prog);
    printf("退出码: 0 成功, 65 语法错误, 70 运行时错误, 74 文件无法读取\n");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);      /* 控制台使用 UTF-8,中文才不乱码 */
#endif
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
        printf("L %s\n", L_VERSION);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        const char *src_file = argv[2];
        char out_buf[256];
        const char *out_file = NULL;
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            out_file = argv[4];
        } else {
            snprintf(out_buf, sizeof out_buf, "%s", src_file);
            char *dot = strrchr(out_buf, '.');
            if (dot) strcpy(dot, ".lc");
            else strcat(out_buf, ".lc");
            out_file = out_buf;
        }

        char *src = read_file_text(src_file);
        if (!src) return 74;
        int had_err = 0;
        Node *prog = parse_program_file(src, src_file, &had_err);
        if (had_err) { free(src); return 65; }

        Interp in;
        interp_init(&in);
        in.current_file = src_file;
        Proto *proto = compile_to_proto(&in, prog, src_file, 0);
        int inst_count = proto->chunk.count;
        int rc = vm_dump_proto_to_file(proto, out_file);
        interp_destroy(&in);
        free(src);

        if (rc != 0) {
            fprintf(stderr, "错误: 写入字节码产物 '%s' 失败\n", out_file);
            return 70;
        }
        printf("已成功编译为致密字节码: %s (%d 字节指令)\n", out_file, inst_count);
        return 0;
    }
    if (argc >= 3 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--disasm") == 0)) {
        const char *src_file = argv[2];
        char *src = read_file_text(src_file);
        if (!src) return 74;
        int had_err = 0;
        Node *prog = parse_program_file(src, src_file, &had_err);
        if (had_err) { free(src); return 65; }

        Interp in;
        interp_init(&in);
        in.current_file = src_file;
        Proto *proto = compile_to_proto(&in, prog, src_file, 0);
        chunk_disassemble(&proto->chunk, src_file);
        interp_destroy(&in);
        free(src);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--vm") == 0) {
        l_script_argc = argc - 2;
        l_script_argv = argv + 2;
        return run_file_vm(argv[2]);
    }
    if (argc == 2 && (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "--ports") == 0)) {
        char names[32][16];
        int n = l_hal_serial_list_ports(names, 32);
        if (n == 0) {
            printf("未检测到可用串口 (请检查 ESP32 USB 数据线或驱动)。\n");
        } else {
            printf("检测到 %d 个可用串口:\n", n);
            for (int i = 0; i < n; i++) printf("  %s\n", names[i]);
        }
        return 0;
    }
    if (argc >= 4 && (strcmp(argv[1], "-u") == 0 || strcmp(argv[1], "--upload") == 0)) {
        const char *port = argv[2];
        const char *file = argv[3];
        int baud = (argc >= 5) ? atoi(argv[4]) : 115200;
        char *src = read_file_text(file);
        if (!src) return 74;
        char err_buf[256] = {0};
        int rc = l_hal_serial_upload(port, baud, src, strlen(src), err_buf, sizeof err_buf);
        free(src);
        if (rc < 0) {
            fprintf(stderr, "烧录错误: %s\n", err_buf);
            return 70;
        }
        return 0;
    }
    if (argc >= 3 && (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--serial") == 0)) {
        const char *port = argv[2];
        int baud = (argc >= 4) ? atoi(argv[3]) : 115200;
        char err_buf[256] = {0};
        if (l_hal_serial_monitor(port, baud, err_buf, sizeof err_buf) < 0) {
            fprintf(stderr, "串口错误: %s\n", err_buf);
            return 70;
        }
        return 0;
    }
    if (argc >= 2) {
        /* 脚本路径及其后的参数都交给 sys_args() */
        l_script_argc = argc - 1;
        l_script_argv = argv + 1;
        return run_file(argv[1]);
    }
    repl();
    return 0;
}
