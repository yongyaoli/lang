/* 内置函数:直接用 C 实现 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "interp.h"
#include "bytecode.h"

/* 参数类型检查 */
void need_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i,
              ValueType t, const char *tname) {
    if (argv[i].type != t)
        rt_error(in, at, "%s() 的第 %d 个参数应为 %s,实际是 %s",
                 fn, i + 1, tname, type_name(argv[i]));
}

static Value b_print(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at;
    for (int i = 0; i < argc; i++) {
        char *s = value_to_string(argv[i], 0);
        printf(i ? " %s" : "%s", s);
        free(s);
    }
    putchar('\n');
    return val_nil();
}

static Value b_str(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc;
    char *s = value_to_string(argv[0], 0);
    Str *res = str_new(in, s, strlen(s));
    free(s);
    return val_str(res);
}

/* num("3.5") → 3.5;无法转换返回 nil */
static Value b_num(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc;
    if (argv[0].type == V_INT || argv[0].type == V_NUM) return argv[0];
    if (argv[0].type == V_STR) {
        char *end;
        const char *s = val_get_cstr(argv[0]);
        double d = strtod(s, &end);
        if (end != s && *end == '\0') {
            /* 不含小数点/指数且在 int32 范围内 → int, 否则 float */
            if (strpbrk(s, ".eEnNiI") == NULL && d >= -2147483648.0 && d <= 2147483647.0)
                return val_int((int32_t)d);
            return val_num(d);
        }
    }
    return val_nil();
}

static Value b_len(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    switch (argv[0].type) {
    case V_STR:   return val_int((int32_t)(argv[0].as.str ? argv[0].as.str->len : 0));
    case V_BYTES: return val_int((int32_t)(argv[0].as.bytes ? argv[0].as.bytes->len : 0));
    case V_ARRAY: return val_int(argv[0].as.array->count);
    case V_MAP:   return val_int(argv[0].as.map->count);
    default:
        rt_error(in, at, "len() 不支持 %s 类型", type_name(argv[0]));
    }
}

static Value b_type(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc;
    const char *t = type_name(argv[0]);
    return val_str(str_new(in, t, strlen(t)));
}

static Value b_push(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "push", argv, 0, V_ARRAY, "array");
    array_push(argv[0].as.array, argv[1]);
    return val_nil();
}

static Value b_pop(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "pop", argv, 0, V_ARRAY, "array");
    Array *a = argv[0].as.array;
    return a->count ? a->items[--a->count] : val_nil();
}

static Value b_keys(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "keys", argv, 0, V_MAP, "map");
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = 0; i < argv[0].as.map->count; i++) {
        if (argv[0].as.map->keys[i]) {
            array_push(a, val_str(argv[0].as.map->keys[i]));
        }
    }
    gc_pop_roots(in, 1);
    return aval;
}

/* input([提示]):读一行,去掉换行;输入结束返回 nil */
static Value b_input(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at;
    if (argc == 1) {
        char *p = value_to_string(argv[0], 0);
        printf("%s", p);
        fflush(stdout);
        free(p);
    }
    char buf[4096];
    if (!fgets(buf, sizeof buf, stdin)) return val_nil();
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return val_str(str_new(in, buf, n));
}

static Value b_clock(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_num((double)clock() / CLOCKS_PER_SEC);
}

/* read_file(路径):返回内容字符串,失败返回 nil */
static Value b_read_file(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "read_file", argv, 0, V_STR, "string");
    FILE *f = fopen(val_get_cstr(argv[0]), "rb");
    if (!f) return val_nil();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    Str *s = str_new(in, buf, n);
    free(buf);
    return val_str(s);
}

/* write_file(路径, 内容):成功返回 true */
static Value b_write_file(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)argc;
    need_arg(in, at, "write_file", argv, 0, V_STR, "string");
    need_arg(in, at, "write_file", argv, 1, V_STR, "string");
    FILE *f = fopen(val_get_cstr(argv[0]), "wb");
    if (!f) return val_bool(0);
    const char *content = val_get_cstr(argv[1]);
    size_t len = argv[1].as.str ? argv[1].as.str->len : 0;
    int ok = fwrite(content, 1, len, f) == len;
    fclose(f);
    return val_bool(ok);
}

static Value b_exit(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at;
    fflush(stdout);
    exit(argc == 1 && val_is_number(argv[0]) ? (int)val_to_double(argv[0]) : 0);
}

static Value b_help(Interp *in, const Node *at, int argc, Value *argv);
static Value b_copyright(Interp *in, const Node *at, int argc, Value *argv);
static Value b_license(Interp *in, const Node *at, int argc, Value *argv);

/* 内置函数表:同时是 help 的数据来源,新增内置函数时一并写好 usage 和 doc */
static const Native core_natives[] = {
    {"print", 0, -1, b_print, "print(值, ...)", "以空格分隔打印所有参数,并换行"},
    {"str", 1, 1, b_str, "str(值)", "把任意值转换为字符串"},
    {"num", 1, 1, b_num, "num(值)", "把字符串转换为数字,无法转换返回 nil"},
    {"len", 1, 1, b_len, "len(值)", "字符串的字节数 / 数组元素个数 / map 键个数"},
    {"type", 1, 1, b_type, "type(值)", "返回类型名:nil bool int float string array map function"},
    {"push", 2, 2, b_push, "push(数组, 值)", "在数组末尾追加一个元素"},
    {"pop", 1, 1, b_pop, "pop(数组)", "弹出并返回最后一个元素,空数组返回 nil"},
    {"keys", 1, 1, b_keys, "keys(map)", "返回 map 所有键组成的数组"},
    {"input", 0, 1, b_input, "input([提示])", "读取一行输入,输入结束返回 nil"},
    {"clock", 0, 0, b_clock, "clock()", "程序已用的 CPU 时间(秒),可用于计时"},
    {"read_file", 1, 1, b_read_file, "read_file(路径)", "读取文件内容为字符串,失败返回 nil"},
    {"write_file", 2, 2, b_write_file, "write_file(路径, 内容)", "写入文件,成功返回 true"},
    {"exit", 0, 1, b_exit, "exit([退出码])", "退出程序,默认退出码 0"},
    {"quit", 0, 1, b_exit, "quit([退出码])", "同 exit"},
    {"help", 0, 1, b_help, "help([主题])", "显示帮助;主题可以是函数,或字符串如 \"keywords\""},
    {"copyright", 0, 0, b_copyright, "copyright()", "显示版权信息"},
    {"license", 0, 0, b_license, "license()", "显示许可信息"},
};

const NativeGroup group_core = {
    "core", "基础", core_natives, sizeof core_natives / sizeof core_natives[0]
};

/* 所有分组(支持通过 l_config.h 中的 L_WITH_STDLIB_* 宏按需裁剪) */
static const NativeGroup *const groups[] = {
    &group_core,
#if L_WITH_STDLIB_STRING
    &group_string,
#endif
#if L_WITH_STDLIB_MATH
    &group_math,
#endif
#if L_WITH_STDLIB_TIME
    &group_time,
#endif
#if L_WITH_STDLIB_IO
    &group_io,
#endif
#if L_WITH_STDLIB_SYS
    &group_sys,
#endif
#if L_WITH_STDLIB_HW
    &group_hw,
#endif
#if L_WITH_STDLIB_NET
    &group_net,
#endif
};
#define GROUP_COUNT (sizeof groups / sizeof groups[0])

int int_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i) {
    if (argv[i].type == V_INT) return argv[i].as.i;
    if (argv[i].type != V_NUM)
        rt_error(in, at, "%s() 的第 %d 个参数应为整数,实际是 %s", fn, i + 1, type_name(argv[i]));
    double d = argv[i].as.num;
    if (d != floor(d) || fabs(d) > 2e9)
        rt_error(in, at, "%s() 的第 %d 个参数应为整数,实际是 %g", fn, i + 1, d);
    return (int)d;
}

/* 接受 int 或 float, 统一返回 double */
double num_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i) {
    if (!val_is_number(argv[i]))
        rt_error(in, at, "%s() 的第 %d 个参数应为数字,实际是 %s", fn, i + 1, type_name(argv[i]));
    return val_to_double(argv[i]);
}

void builtins_install(Interp *in) {
    for (size_t g = 0; g < GROUP_COUNT; g++) {
        for (size_t i = 0; i < groups[g]->count; i++) {
            Value v = val_nil();
            v.type = V_NATIVE;
            v.as.native = &groups[g]->items[i];
            env_define(in->globals, groups[g]->items[i].name, v);
        }
    }
    env_define(in->globals, "PI", val_num(3.14159265358979323846));
    env_define(in->globals, "E", val_num(2.71828182845904523536));
    /* 注册模块名变量, 允许用户在 REPL 中免引号直接输入 help(sys), help(io), help(math) 等 */
    for (size_t g = 0; g < GROUP_COUNT; g++) {
        const char *gname = groups[g]->name;
        if (!env_find(in->globals, gname)) {
            env_define(in->globals, gname, val_str(str_new(in, gname, strlen(gname))));
        }
    }
#if L_WITH_STDLIB_HW
    hw_install_globals(in);
    /* 内置常量 sim: 离线模拟烧录端口名, 允许 REPL 中免引号输入 upload(sim, "main.l")
     * 并让 help(sim) 可用 */
    if (!env_find(in->globals, "sim"))
        env_define(in->globals, "sim", val_str(str_new(in, "sim", 3)));
#endif
    /* 隐藏内置: 字节码 import 语句通过它执行导入, 不出现在 help 列表中 */
    {
        static Native import_native = {
            "__import", 1, 2, interp_import_builtin,
            "__import(路径[, 别名])", "内部接口: 由 import 语句编译生成"
        };
        Value v = val_nil();
        v.type = V_NATIVE;
        v.as.native = &import_native;
        env_define(in->globals, "__import", v);
    }
}


/* ================= help / copyright / license ================= */

#include "version.h"

/* 语言主题:help("keywords") 等 */
static const struct { const char *name; const char *title; const char *text; } topics[] = {
    {"keywords", "关键字",
     "  let        声明变量            let x = 1;\n"
     "  fn         声明函数            fn add(a, b) { return a + b; }\n"
     "  return     从函数返回          return x;\n"
     "  if / else  条件分支            if x > 0 { ... } else { ... }\n"
     "  while      循环                while i < 3 { ... }\n"
     "  for        循环                for let i = 0; i < 3; i = i + 1 { ... }\n"
     "  break      跳出循环\n"
     "  continue   进入下一轮循环\n"
     "  try catch throw                异常捕获与抛出: try { ... } catch (e) { ... }\n"
     "  true false nil                 布尔值与空值\n"
     "  and or not                     逻辑运算(and / or 短路求值)\n"
     "  & | ^ ~ << >>                  位运算(仅整数); int/int 为整数除法(7/2 为 3)\n"},
    {"types", "数据类型",
     "  int        32 位整数(支持 0x 0b)  1   0xFF  0b101\n"
     "  float      64 位浮点数           3.14   1.0\n"
     "  string     字符串(不可变)       \"hi\\n\"   支持 \\n \\t \\r \\\" \\\\\n"
     "  bool       布尔值                true  false\n"
     "  nil        空值\n"
     "  array      数组                  [1, 2, 3]       xs[0]\n"
     "  map        字典(键是字符串)     { name: \"L\" }   m.name  m[\"name\"]\n"
     "  function   函数(一等值,可闭包)  fn(x) { return x; }\n"
     "\n"
     "  真值规则:只有 false 和 nil 为假,0 和 \"\" 都为真。\n"},
    {"operators", "运算符(优先级从低到高)",
     "  =                      赋值(右结合)\n"
     "  or                     逻辑或\n"
     "  and                    逻辑与\n"
     "  ==  !=                 相等(不同类型一定不等;数组/map 比较引用)\n"
     "  <  <=  >  >=           比较(两个数字或两个字符串)\n"
     "  +  -                   加减;+ 也用于两个字符串拼接\n"
     "  *  /  %                乘 除 取模\n"
     "  not  !  -(一元)        取反\n"
     "  ()  []  .              调用 下标 成员访问\n"},
    {"syntax", "语法要点",
     "  语句以 ; 结尾;if / while / for / fn 的主体必须用 { }。\n"
     "  if、while 的条件不需要括号。\n"
     "  注释:// 单行    /* 多行 */\n"
     "  闭包:函数会记住它定义时所在的作用域。\n"
     "  完整文法见项目中的 L_EBNF.md。\n"},
    {"repl", "交互模式(REPL)",
     "  直接输入表达式即可看到结果,末尾的 ; 可以省略。\n"
     "  括号 { 未配对时会继续读取下一行(提示符变为 ...)。\n"
     "  ESP32 快捷操作(直接在 REPL 中调用):\n"
     "    ports()                        扫描本机可用串口(如 [\"COM3\"])\n"
     "    upload(\"COM3\", \"main.l\")       通过串口将脚本烧录到 ESP32 并运行\n"
     "    upload(\"sim\", \"main.l\")        无硬件时在本地模拟 Flash 分区演练烧录与运行\n"
     "    connect(\"COM3\")                进入 ESP32 串口远程交互终端(Ctrl+] 或 .exit 退出)\n"
     "    ESP32.pinout()                 打印当前芯片完整 GPIO 引脚状态与限制速查表\n"
     "  退出:exit()  quit()  或 Ctrl+Z 回车(Windows)/ Ctrl+D(其他系统)。\n"},
    {"sim", "离线模拟烧录(sim)",
     "  sim 是内置常量(\"sim\"),用作 upload() 的串口参数,表示本地离线模拟演练。\n"
     "  无 ESP32 硬件时可用它验证烧录流程:脚本被写入 PC 端模拟的裸 Flash 分区\n"
     "  (带 CRC32 校验),随后自动执行,语义等同真机\"烧录后上电运行\"。\n"
     "\n"
     "  用法:\n"
     "    upload(sim, \"main.l\")          免引号写法\n"
     "    upload(\"sim\", \"main.l\")        等价写法(\"SIM\"/\"loopback\" 亦可)\n"
     "\n"
     "  注意: sim 只是端口名常量,不是可调用的函数;真正的离线演练由 upload() 完成。\n"},
    {"esp32", "ESP32 固件与串口烧录指南",
     "  1. 扫描串口:\n"
     "     REPL 内输入: ports()               命令行: l -p\n"
     "  2. 烧录 .l 脚本到 ESP32 (写入 l_script 裸分区, 自动校验 CRC32 并运行):\n"
     "     REPL 内输入: upload(\"COM3\", \"main.l\")\n"
     "     离线模拟测试: upload(\"sim\", \"main.l\")\n"
     "     命令行直接烧: l -u COM3 main.l\n"
     "  3. 连接 ESP32 串口交互终端 (远程 REPL):\n"
     "     REPL 内输入: connect(\"COM3\")       命令行: l -s COM3\n"
     "     (在远程终端中按 Ctrl+] 或输入 .exit 回车即可返回本机)\n"
     "  4. 芯片与外设控制:\n"
     "     ESP32.info()  ESP32.pin_info(2)  ESP32.pinout()  help(ESP32)\n"
     "     pin(2).on()   PWM(2, 5000, 512)  ADC(34)  I2C()  SPI()  UART()\n"},
};
#define TOPIC_COUNT ((int)(sizeof topics / sizeof topics[0]))

const Native *builtins_find_native(const char *name) {
    for (size_t g = 0; g < GROUP_COUNT; g++)
        for (size_t i = 0; i < groups[g]->count; i++)
            if (strcmp(groups[g]->items[i].name, name) == 0) return &groups[g]->items[i];
    return NULL;
}

/* 按显示宽度补空格对齐:中文占 2 列,ASCII 占 1 列(printf 的 %-Ns 按字节算,中文会错位) */
static void print_padded(const char *s, int width) {
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x80) w += 1;                  /* ASCII */
        else if (*p >= 0xE0) w += 2;            /* 3 字节字符的首字节:按全角算 */
    }
    printf("%s", s);
    for (; w < width; w++) putchar(' ');
}

static void help_overview(void) {
    printf("L 语言 %s 帮助\n\n", L_VERSION);
    printf("内置函数分组(用 help(\"分组名\") 查看详细说明):\n");
    for (size_t g = 0; g < GROUP_COUNT; g++) {
        printf("  ");
        print_padded(groups[g]->name, 8);
        print_padded(groups[g]->title, 16);
        int col = 26;                           /* 函数名列表自动换行 */
        for (size_t i = 0; i < groups[g]->count; i++) {
            const char *name = groups[g]->items[i].name;
            int len = (int)strlen(name) + 1;
            if (col + len > 76) { printf("\n%26s", ""); col = 26; }
            printf("%s ", name);
            col += len;
        }
        printf("\n");
    }
    printf("\n常量与全局对象:  PI  E  ESP32 (输入 help(ESP32) 查看全部芯片与 Flash 方法)\n");
    printf("\nESP32 串口与烧录快捷操作:\n");
    printf("  ports()                        扫描本机可用串口(如 [\"COM3\"])\n");
    printf("  upload(\"COM3\", \"main.l\")       将脚本烧录到 ESP32 裸分区并运行(\"sim\" 为离线演练)\n");
    printf("  connect(\"COM3\")                连接 ESP32 串口交互终端(Ctrl+] 或 .exit 退出)\n");
    printf("\n语言主题:  help(\"主题名\")\n");
    for (int i = 0; i < TOPIC_COUNT; i++)
        printf("  %-12s %s\n", topics[i].name, topics[i].title);
    printf("\n查看单个函数或对象:help(print)  help(upload)  help(ESP32)  help(\"esp32\")\n");
}

static void help_native(const Native *n) {
    printf("%s\n    %s\n", n->usage, n->doc);
}

static void help_group(const NativeGroup *g) {
    printf("%s(%s)\n", g->title, g->name);
    for (size_t i = 0; i < g->count; i++) {
        printf("  ");
        print_padded(g->items[i].usage, 30);
        printf(" %s\n", g->items[i].doc);
    }
}

static void help_topic(const char *name) {
    /* 分组名优先于同名函数(如 time):想看函数本身请用 help(time) */
    for (size_t g = 0; g < GROUP_COUNT; g++) {
        if (strcmp(groups[g]->name, name) == 0) { help_group(groups[g]); return; }
    }
    const Native *n = builtins_find_native(name);
    if (n) { help_native(n); return; }
    for (int i = 0; i < TOPIC_COUNT; i++) {
        if (strcmp(topics[i].name, name) == 0) {
            printf("%s\n%s", topics[i].title, topics[i].text);
            return;
        }
    }
    printf("没有关于 \"%s\" 的帮助。输入 help() 查看全部主题。\n", name);
}

static Value b_help(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at;
    if (argc == 0) { help_overview(); return val_nil(); }
    Value v = argv[0];
    switch (v.type) {
    case V_NATIVE: help_native(v.as.native); break;
    case V_STR:    help_topic(val_get_cstr(v)); break;
    case V_FN: {
        Func *f = v.as.fn;
        if (!f) break;
        if (f->decl) {
            /* AST 模式: 直接读函数声明节点 */
            const NameList *ps = &f->decl->as.fn.params;
            printf("用户函数 %s(", f->decl->as.fn.name ? f->decl->as.fn.name : "<anonymous>");
            for (int i = 0; i < ps->count; i++) printf(i ? ", %s" : "%s", ps->items[i]);
            printf(")\n");
        } else if (f->proto) {
            /* VM 模式: 只有字节码原型, 没有 AST 声明 */
            Proto *p = f->proto;
            printf("用户函数 %s(", p->name ? p->name : "<anonymous>");
            if (p->param_names) {
                for (int i = 0; i < p->param_count; i++)
                    printf(i ? ", %s" : "%s", p->param_names[i] ? p->param_names[i] : "_");
            } else if (p->arity > 0) {
                printf("%d 个参数", p->arity);
            }
            printf(")\n");
        } else {
            printf("这是一个无效的函数对象\n");
        }
        break;
    }
    case V_OBJ: {
        const ObjClass *cls = v.as.obj ? v.as.obj->cls : NULL;
        if (cls) {
            char *s = value_to_string(v, 1);
            printf("%s 对象: %s\n方法列表:\n", cls->name, s);
            free(s);
            for (size_t i = 0; i < cls->method_count; i++) {
                printf("  ");
                print_padded(cls->methods[i].usage, 26);
                printf(" %s\n", cls->methods[i].doc);
            }
            break;
        }
    }
    /* fallthrough */
    default: {
        char *s = value_to_string(v, 1);
        printf("这是一个 %s 类型的值: %s\n", type_name(v), s);
        free(s);
    }
    }
    return val_nil();
}

static Value b_copyright(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    printf("L %s\n", L_VERSION);
    printf("Copyright (c) %s Yongyao Li (L 语言项目)。保留所有权利。\n", L_COPYRIGHT_YEAR);
    printf("输入 license() 查看开源许可证信息。\n");
    return val_nil();
}

static Value b_license(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    printf("L 语言采用 MIT 许可证开源发布。\n");
    printf("完整许可证文本见项目根目录的 LICENSE 文件。\n");
    return val_nil();
}
