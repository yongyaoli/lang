# L 语言

一门用 C 实现的小型动态类型脚本语言,用来学习如何从零写一门编程语言。

- **只依赖一个 C 编译器**(gcc / clang / MSVC),不使用 lex、yacc、LLVM 等任何工具
- 实现方式:**树遍历解释器**(词法分析 → 递归下降语法分析 → AST → 直接求值)
- 当前版本:**0.1.0**

## 目录

1. [快速开始](#快速开始)
2. [语言速览](#语言速览)
3. [内置函数](#内置函数)
4. [交互模式 REPL](#交互模式-repl)
5. [错误信息与退出码](#错误信息与退出码)
6. [项目结构与实现原理](#项目结构与实现原理)
7. [测试](#测试)
8. [已知限制](#已知限制)
9. [后续计划](#后续计划)

---

## 快速开始

### 编译

```
gcc -Wall -Wextra -I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/*.c main.c -o l
```

Windows 下会生成 `l.exe`。编译时建议加上 `-Wall -Wextra`,目前零警告。

### 运行

```
l                 进入交互模式(REPL)
l 脚本.l          运行脚本文件
l -h, --help      显示命令行帮助
l -v, --version   显示版本号
```

### 第一个程序

新建 `hello.l`:

```
fn greet(name) {
    return "你好," + name + "!";
}
print(greet("L 语言"));
```

运行 `l hello.l`,输出 `你好,L 语言!`。

---

## 语言速览

> 完整文法见 [L_EBNF.md](L_EBNF.md);在 REPL 里也可以用 `help("keywords")`、`help("types")` 等随时查看。

### 注释

```
// 单行注释
/* 多行注释(不可嵌套) */
```

### 数据类型

| 类型 | 示例 | 说明 |
|---|---|---|
| int | `1` `0xFF` `0b101` | 32 位有符号整数,溢出回绕;`7 / 2` 为 `3`(C 语义整除) |
| float | `3.14` `1.0` | 64 位浮点;int 与 float 混算结果为 float;`type(7 / 2.0)` 为 `float` |
| string | `"hi\n"` | 不可变;转义:`\n` `\t` `\r` `\"` `\\` |
| bool | `true` `false` | |
| nil | `nil` | 空值 |
| array | `[1, "a", [2]]` | `xs[0]` 读取,`xs[0] = 9` 修改 |
| map | `{ name: "L", "ver": 0.1 }` | 键是字符串;`m.name` 或 `m["name"]` |
| function | `fn(x) { return x; }` | 一等值,支持闭包 |

**真值规则**:只有 `false` 和 `nil` 为假,`0` 和 `""` 都为真。

### 变量与作用域

```
let x = 10;          // 声明
let y;               // 未初始化,值为 nil
x = x + 1;           // 赋值
{
    let x = "内层";  // 花括号块创建新作用域,可遮蔽外层变量
}
```

使用未声明的变量会报运行时错误。

### 运算符(优先级从低到高)

| 运算符 | 说明 |
|---|---|
| `=` | 赋值,右结合,目标可以是变量、`a[i]`、`a.b` |
| `? :` | 三元条件运算符,右结合,短路求值:`cond ? then_expr : else_expr` |
| `or` | 逻辑或,短路,返回决定结果的那个操作数 |
| `and` | 逻辑与,短路 |
| `==` `!=` | 不同类型一定不相等;数组、map、函数按引用比较 |
| `<` `<=` `>` `>=` | 两个数字或两个字符串 |
| `+` `-` | `+` 还可用于两个字符串拼接;字符串与数字混用会报错,请用 `str()` |
| `*` `/` `%` | 除数为 0 会报错 |
| `not` `!` `-` | 一元运算符 |
| `()` `[]` `.` | 调用、下标、成员访问 |

### 控制流

```
if x > 0 {
    print("正数");
} else if x == 0 {
    print("零");
} else {
    print("负数");
}

while i < 10 { i = i + 1; }

for let i = 0; i < 3; i = i + 1 {
    if i == 1 { continue; }
    print(i);
}
```

- `if` / `while` / `for` 的条件**不加括号**,主体**必须用 `{}`**
- 语句以 `;` 结尾
- `break`、`continue` 只能出现在循环中;`return` 只能出现在函数中

### 函数与闭包

```
fn add(a, b) { return a + b; }       // 具名函数
let sq = fn(n) { return n * n; };    // 匿名函数

fn counter() {                       // 闭包:函数记住定义时的作用域
    let n = 0;
    return fn() { n = n + 1; return n; };
}
let c = counter();
print(c(), c(), c());                // 1 2 3
```

- 参数个数必须与调用时传入的个数一致
- 没有 `return`(或 `return;`)时返回 `nil`
- 递归深度上限 1000 层,超过会报"调用栈溢出"

### 数组与 map

```
let xs = [1, 2, 3];
push(xs, 4);
print(xs[0], len(xs), pop(xs));      // 1 4 4

let p = { name: "L" };
p.version = "0.1";                   // 新增或修改键
print(p.name, p.missing);            // L nil(不存在的键返回 nil)
print(keys(p));                      // ["name", "version"]
```

数组下标越界会报错;map 的键必须是字符串。

### 模块系统 (import)

支持以相对路径或绝对路径导入其他 `.l` 脚本文件：
- 模块拥有独立的全局执行环境，不会污染当前文件的顶层作用域。
- 模块顶层定义的变量和函数会被收集为一个 Map 对象导出。
- 内置模块单例缓存，多次导入同一模块不会重复执行。

```javascript
// 导入并按文件名推导变量名 (如 ./helpers.l 绑定为 helpers)
import "./helpers";
print(helpers.add(1, 2));

// 导入并指定别名
import "./utils/math.l" as m;
print(m.pi);
```

---

## 内置函数

在 REPL 里输入 `help()` 可以查看分组总览,`help("string")`、`help("math")`、`help("time")` 查看某一组的详细说明。

| 函数 | 说明 |
|---|---|
| `print(值, ...)` | 以空格分隔打印所有参数,并换行 |
| `str(值)` | 转换为字符串 |
| `num(值)` | 字符串转数字,无法转换返回 `nil` |
| `len(值)` | 字符串字节数 / 数组元素数 / map 键数 |
| `type(值)` | 返回类型名:`nil` `bool` `int` `float` `string` `array` `map` `function` |
| `push(数组, 值)` | 在数组末尾追加元素 |
| `pop(数组)` | 弹出并返回最后一个元素,空数组返回 `nil` |
| `keys(map)` | 返回所有键组成的数组 |
| `input([提示])` | 读取一行输入,输入结束返回 `nil` |
| `clock()` | 程序已用的 CPU 时间(秒) |
| `read_file(路径)` | 读取文件为字符串,失败返回 `nil` |
| `write_file(路径, 内容)` | 写入文件,成功返回 `true` |
| `exit([退出码])` / `quit([退出码])` | 退出程序 |
| `help([主题])` | 显示帮助 |
| `copyright()` / `license()` | 版权 / 许可信息 |

### 标准库:字符串与数组

字符串按**字节**处理(与 `len`、下标一致);`split(s, "")`、`chr`、`ord` 按 UTF-8 字符处理。带 `new` 的函数返回新值,不修改原数组。

| 函数 | 说明 |
|---|---|
| `split(字符串, 分隔符)` | 拆成数组;分隔符为 `""` 时按字符拆,`split("你好", "")` 得 `["你", "好"]` |
| `join(数组, [分隔符])` | 把元素连接成字符串,元素按 `print` 的方式转文本 |
| `substr(字符串, 起点, [长度])` | 截取子串;起点可为负数(从末尾算);越界自动截断 |
| `upper(s)` / `lower(s)` | 转大写 / 小写(仅 ASCII 字母) |
| `trim(s)` | 去掉首尾空白 |
| `contains(容器, 值)` | 字符串含子串 / 数组含元素 / map 含键 |
| `find(容器, 值)` | 返回位置(字符串或数组),找不到返回 `-1` |
| `replace(s, 旧, 新)` | 替换所有出现的子串 |
| `starts_with(s, 前缀)` / `ends_with(s, 后缀)` | 判断开头 / 结尾 |
| `repeat(s, 次数)` | 重复拼接 |
| `chr(码点)` / `ord(s)` | 码点与字符互转,`chr(65)` 得 `"A"`,`ord("你")` 得 `20320` |
| `range(止)` / `range(起, 止, [步长])` | 数字数组,左闭右开,如 `range(5)` 得 `[0, 1, 2, 3, 4]` |
| `slice(数组, 起, [止])` | 截取数组的一段(new),负数从末尾算 |
| `reverse(数组)` | 反转(new) |
| `sort(数组, [比较函数])` | 排序(new),无比较器时要求同类元素;支持自定义 `fn(a, b)` |
| `map(数组, 函数)` | 对每个元素应用 `fn(item)`，返回新数组 |
| `filter(数组, 谓词函数)` | 保留使 `pred(item)` 为真的元素，返回新数组 |
| `reduce(数组, 函数, [初始值])` | 用函数折叠数组，不提供初始值时取第一个元素 |
| `ulen(字符串)` | 按 UTF-8 字符计算长度，如 `ulen("你好")` 为 2 |
| `usubstr(字符串, 起点, [长度])` | 按 UTF-8 字符切片，不会截断多字节字符 |
| `uat(字符串, 索引)` | 获取指定索引字符，负数从末尾算，越界返回 `nil` |
| `format(模板, 值...)` | 按 `{}` 占位符格式化插值字符串，`{{` 转义为 `{` |

### 标准库:文件输入输出 (io)

| 函数 | 说明 |
|---|---|
| `io_exists(路径)` | 检查文件或目录是否存在 |
| `io_remove(路径)` | 删除文件, 成功返回 `true` |
| `io_append(路径, 内容)` | 向文件追加写入字符串 |
| `io_read_lines(路径)` | 按行读取文件为字符串数组 |
| `io_isdir(路径)` | 是否为目录 |
| `io_size(路径)` | 文件字节数,失败返回 `nil` |
| `io_mkdir(路径)` / `io_rmdir(路径)` | 创建单层目录 / 删除空目录 |
| `io_rename(旧, 新)` / `io_copy(源, 目标)` | 重命名(移动) / 复制文件 |
| `io_listdir(路径)` | 列出目录条目(已排序),失败返回 `nil` |
| `io_join(a, b)` / `io_basename(p)` / `io_dirname(p)` / `io_ext(p)` | 路径拼接与拆分 |

### 标准库:系统与运行时 (sys)

| 函数 | 说明 |
|---|---|
| `sys_platform()` | 返回系统名: `"win32"` / `"linux"` / `"darwin"` |
| `sys_version()` | 返回语言版本号 |
| `sys_getenv(变量名)` | 获取环境变量，不存在返回 `nil` |
| `sys_exec(shell命令)` | 执行系统 shell 命令并返回退出状态码 |
| `sys_cwd()` | 获取当前工作目录路径 |
| `sys_gc_collect()` | 手动触发一次完整垃圾回收 |
| `sys_allocated()` | 获取当前 GC 托管分配的堆内存字节数 |
| `sys_args()` | 命令行参数数组 `[脚本路径, 参数...]`(运行 `l 脚本.l a b`) |
| `sys_script()` | 当前脚本路径,REPL 下为 `nil` |
| `sys_setenv(名, 值)` | 设置环境变量 |
| `sys_chdir(路径)` | 切换工作目录 |
| `sys_pid()` | 当前进程号 |

### 标准库:硬件外设 (`Pin` / ESP32 GPIO)

| 构造 / 方法 | 说明 |
|---|---|
| `Pin(引脚号, [模式="out"], [初值])` / `pin(...)` | 创建 GPIO 引脚对象(模式可选 `"out"` `"in"` `"in_pullup"` `"in_pulldown"` `"open_drain"`) |
| `p.on()` / `p.high()` | 输出高电平(`1`),返回 `p` 支持链式调用 |
| `p.off()` / `p.low()` | 输出低电平(`0`),返回 `p` |
| `p.toggle()` | 翻转当前电平(`0 <-> 1`),返回 `p` |
| `p.value([电平])` | 无参读取电平(`0`/`1`);有参设置电平并返回 `p` |
| `p.mode([模式])` | 无参读取当前模式字符串;有参切换模式并返回 `p` |
| `p.pin()` / `p.info()` | 返回引脚编号 / 返回 `{pin, mode, value}` 字典 |

```
let led = Pin(2, "out").on();
led.toggle();
pin(4).on();
```

### 方法调用语法 (`obj.method(...)`)

- **字符串方法**:`"  hi, l  ".trim().upper().split(", ")`、`.len()`、`.ulen()`、`.substr()`、`.replace()`、`.find()`、`.starts_with()`、`.ends_with()`
- **数组方法**:`[3, 1, 2].sort().reverse().join("-")`、`.push(v)`、`.pop()`、`.slice()`、`.map(fn)`、`.filter(fn)`、`.reduce(fn, init)`、`.len()`
- **字典方法与对象 `self`**:`.has(k)`、`.remove(k)`、`.keys()`、`.len()`;字典中的函数作为方法调用时自动绑定 `self`(同时支持隐式 `self.x` 与显式 `fn(self, ...)` 参数)。

### 异常处理与精确错误定位 (`try / catch / throw`)

```
try {
    let x = 10 / 0;
} catch (e) {
    print(e.type, e.message, e.file, e.line, e.col, e.fn, e.source);
}
```

- 支持主动抛出字符串或字典:`throw "传感器超时";` 或 `throw { code: 404, message: "I2C 异常" };`
- 未捕获的错误会自动打印 `[文件:行:列]`、出错源码行与 `^` 指示符以及完整调用栈回溯。

### 标准库:数学

常量 `PI`、`E`。

| 函数 | 说明 |
|---|---|
| `abs(x)` `floor(x)` `ceil(x)` `round(x)` | 绝对值、向下取整、向上取整、四舍五入 |
| `sqrt(x)` `pow(x, y)` | 平方根(`x` 不能为负)、乘方 |
| `min(x, ...)` `max(x, ...)` | 最小 / 最大值,也可以传一个数字数组 |
| `random()` | `[0, 1)` 的随机小数 |
| `randint(下界, 上界)` | 闭区间内的随机整数 |
| `random_seed(整数)` | 固定种子,使随机结果可重复 |

### 标准库:日期与时间

**时间戳**是从 1970-01-01 00:00:00 UTC 起的秒数(数字);日期时间一律按**本地时区**解释。

| 函数 | 说明 |
|---|---|
| `time()` | 当前时间戳 |
| `date([时间戳])` | `"2026-11-06"`,缺省为今天 |
| `datetime([时间戳])` | `"2026-11-06 14:30:05"`,缺省为现在 |
| `format_time(时间戳, 格式)` | 按 `strftime` 格式化,支持中文字面量,如 `format_time(t, "%Y年%m月%d日")` |
| `localtime([时间戳])` | 拆成 map:`year` `month`(1-12)`day` `hour` `minute` `second` `weekday`(0=星期日)`yearday`(1-366) |
| `make_time(年, 月, 日, [时, 分, 秒])` | 构造时间戳;日期不存在(如 2 月 30 日)返回 `nil` |
| `parse_time(字符串)` | 解析 `"YYYY-MM-DD"` 或 `"YYYY-MM-DD HH:MM:SS"`,失败返回 `nil` |
| `sleep(秒)` | 暂停,可以是小数 |

```
let t = parse_time("2026-11-06 14:30:05");
print(datetime(t), localtime(t).weekday);           // 2026-11-06 14:30:05 5
print((parse_time("2026-12-25") - time()) / 86400); // 距离圣诞节还有多少天
print(join(reverse(split("a,b,c", ",")), "-"));     // c-b-a
```

---

## 交互模式 REPL

不带参数运行 `l` 进入 REPL,启动信息仿照 Python:

```
L 0.1.0 (Nov  6 2026, 13:59:24) [GCC 16.2.0 64 bit] on win32
Type "help", "copyright", "license" or "exit" for more information.
>>> 1 + 2
3
>>> let name = "L"
>>> "hello " + name
"hello L"
>>> fn sq(n) {
...     return n * n;
... }
>>> sq(7)
49
```

- 直接输入表达式会显示结果(`nil` 不显示),行尾的 `;` 可以省略
- 花括号 `{` 未配对时继续读取下一行,提示符变为 `...`
- 错误不会让 REPL 退出,之前定义的变量和函数仍然保留
- 输入 `help`(不带括号)会给出提示,输入 `help()` 才是完整帮助
- 输入 `copyright`、`license` 直接显示对应信息
- 退出:`exit()` / `quit()`,或 Ctrl+Z 回车(Windows)/ Ctrl+D(其他系统)

### help 的用法

```
help()                 总览:所有内置函数和语言主题
help(print)            查看某个函数
help("len")            同上,用字符串也可以
help("keywords")       语言主题:keywords types operators syntax repl
help("string")         函数分组:core string math time(分组名优先于同名函数,看 time 函数本身用 help(time))
help(my_function)      查看用户自定义函数的签名
```

---

## 错误信息与退出码

错误都带有 `[行:列]` 位置:

```
[1:9] 运行时错误: '+' 只能用于两个数字或两个字符串,实际是 int 和 string
[3:5] 语法错误(在 '}' 附近): 表达式后期望 ';'
```

语法分析遇到错误后会自动恢复并继续检查,一次报告多个错误。

| 退出码 | 含义 |
|---|---|
| 0 | 成功 |
| 64 | 命令行参数错误 |
| 65 | 语法错误(脚本不会被执行) |
| 70 | 运行时错误 |
| 74 | 脚本文件无法读取 |

---

## 项目结构与实现原理

```
源码 → Lexer → Token 流 → Parser → AST → Interpreter → 结果
```

| 文件 | 职责 |
|---|---|
| `lexer.h/.c` | 词法分析:源码 → Token,带行列号,识别关键字、数字、字符串转义、注释 |
| `ast.h/.c` | AST 节点定义、释放、S 表达式打印(调试用) |
| `parser.h/.c` | 递归下降语法分析;二元运算用优先级爬升;`setjmp/longjmp` 实现错误恢复 |
| `gc.h/.c` | 标记-清除垃圾回收器(Mark-Sweep GC),管理字符串、数组、字典、闭包与作用域环境 |
| `value.h/.c` | 运行时值(nil/bool/int/float/string/array/map/function)、相等、转文本 |
| `interp.h/.c` | 树遍历解释器:环境、求值、语句执行、函数调用与闭包、运行时错误 |
| `builtins.c` | 核心内置函数与 `help` 系统(函数表同时是帮助文档的数据来源) |
| `stdlib/` | 标准库独立目录:包含 `str.c`、`math.c`、`time.c` |
| `main.c` | 命令行入口、REPL、启动信息 |
| `version.h` | 版本号 |
| `lexer_main.c` `parser_main.c` | 词法/语法分析器的单独调试入口,打印 Token 或 AST |
| `L_EBNF.md` | 语言的完整 EBNF 文法 |
| `tests/` `run_tests.ps1` | 回归测试 |

### 设计要点

- **环境(Environment)**:每个作用域是一张变量表加父作用域指针;函数调用创建新环境,闭包 = 函数 + 定义时的环境
- **控制流**:`break` / `continue` / `return` 通过 `exec` 的返回信号传递,不使用异常
- **错误处理**:运行时错误通过 `longjmp` 跳回 `interp_run`,REPL 因此可以在出错后继续
- **内存**:没被闭包捕获的作用域在离开时会释放;字符串和 AST 不回收(见下文限制)
- **新增内置函数**:在对应的 `.c` 文件里写一个 C 函数,并在该文件的函数表里加一行(名字、参数个数、函数指针、usage、doc),`help` 会自动收录。要新增一个分组:新建 `stdlib_xxx.c` 导出 `NativeGroup`,在 `interp.h` 声明,并加入 `builtins.c` 的 `groups[]`

---

## 测试

`tests/` 下每个 `.l` 脚本配一个同名 `.out` 文件保存期望的标准输出。

```
.\run_tests.ps1            运行全部测试
.\run_tests.ps1 -Update    用当前输出重新生成 .out(请先人工确认输出正确)
```

| 测试 | 内容 |
|---|---|
| `01_basic.l` | 算术、比较、逻辑、`if` / `while` / `for`、递归 |
| `02_closure.l` | 闭包、作用域、高阶函数 |
| `03_collections.l` | 数组、map、内置函数 |
| `04_help.l` | help 系统 |
| `05_stdlib_string.l` | 字符串与数组函数 |
| `06_stdlib_math_time.l` | 数学与日期时间函数(固定时间戳,结果与时区无关) |

新增特性时请同时加一个测试。

---

## 已知限制

- **已实现自动垃圾回收(Mark-Sweep GC)**: 字符串拼接、临时数组与 Map、闭包均自动回收并维护生命周期。
- **字符串按字节处理**:`len("你好")` 是 6,`"你好"[0]` 取到的是单个字节;`substr` 同理,可能切断中文字符(`split(s, "")` 按字符拆则是安全的)
- **`upper` / `lower` 只处理 ASCII 字母**
- **时间函数按本地时区**,不支持指定时区;Windows 上 `make_time` 不支持 1970 年之前的日期
- **map 用线性查找**:键很多时较慢
- **语句开头不能直接写匿名函数调用**:`fn() {...}();` 不行,请写成 `let f = fn() {...}; f();`
- **语法错误后**,已解析了一半的 AST 节点会泄漏(进程随后退出,无影响)
- 没有模块系统、没有错误捕获(`try`)机制、没有面向对象

## 后续计划

- 标准库补充:按字符(而非字节)处理字符串的函数、`sort` 自定义比较函数、`map` / `filter` / `reduce`、`format` 字符串格式化
- 垃圾回收(标记清除)
- 字节码虚拟机,提升执行速度
- 模块 `import`
