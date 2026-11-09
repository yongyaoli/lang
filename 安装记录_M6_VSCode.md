# L 语言 M6 阶段与 VS Code 插件更新安装说明

## 1. 安装与更新概览

| 组件 | 版本 | 安装/更新方式 | 安装路径 / 生效位置 |
| :--- | :--- | :--- | :--- |
| **L 语言解释器与栈 VM 运行时** | `v0.1.0-m6` | Inno Setup 自动化静默安装 (`/VERYSILENT`) | `%LOCALAPPDATA%\Programs\lang\l.exe` |
| **VS Code 语言支持插件 (PHP 级 IDE)** | `v0.3.0` | 扩展目录就地部署并激活 | `%USERPROFILE%\.vscode\extensions\l-language-support-0.3.0` |

---

## 2. L 语言 M6 核心特性 (已落地)

1. **紧凑单字节指令集与字节码块 (Bytecode & Chunk)**：
   - 定义了 30+ 种紧凑单字节操作码（`OP_CONST`, `OP_GET/SET_LOCAL`, `OP_GET/SET_UPVALUE`, `OP_CLOSE_UPVALUE`, `OP_ADD`...`OP_MOD`, `OP_INVOKE`, `OP_CALL`, `OP_RETURN`, `OP_CLOSURE`, `OP_TRY`, `OP_THROW`, `OP_HALT` 等）。
   - 内置常量池（数值、字符串、闭包原型）及源码行号列号调试信息。
2. **栈式虚拟机 (Stack-based VM)**：
   - 单 C 循环驱动执行，消除 C 递归栈占用，彻底杜绝 MCU 任务栈溢出。
   - 固态操作数栈与平坦调用帧栈，执行前可安全释放 AST 抽象语法树节点，节省 SRAM。
3. **预编译二进制文件 (`.lc`) 序列化与直接执行**：
   - `l.exe -c 脚本.l [-o 产物.lc]`：一键编译并序列化为带有魔数 `LBC1` 的紧凑二进制字节码。
   - `l.exe 产物.lc`：零源码解析、零 AST 开销直接装载并在 VM 中全速执行。
   - `l.exe -d 脚本.l`：反汇编打印指令码、操作数与常量池调试信息。
   - `l.exe --vm 脚本.l`：以 VM 引擎直接解释执行。

---

## 3. VS Code 扩展 v0.3.0 升级说明 (对齐 PHP 级开发体验)

参考截图中 PHP 语言的高级 IDE 特性，本版本已全面完成如下升级：

1. **类与方法的 Hover 悬停提示 (带代码块高亮与文档注释)**：
   - 鼠标悬停在 `Hook::exec` 上的 `exec` 时，弹出签名卡片：
     ```l
     public static function Hook::exec($fun, $class = ""): void
     ```
     下方自动提取并呈现该方法或类的中文文档注释（支持 `/** ... */` 和 `// ...`）。
   - 悬停在 `Core`、`Hook` 等类名上时，显示 `class Hook` 及类文档注释。
   - 悬停在 `ESP32.status`、`Pin` 等内置硬件单例时，显示标准 API 说明与参数提示。
2. **定义精准跳转 (Ctrl + Click)**：
   - 按住 Ctrl 点击 `Hook::exec` 中的 `exec`，智能结合类作用域，精准跳转到对应类的内部方法定义行。
   - 点击类名跳转到 `class` 声明处。
   - 支持跨 `.l` 文件的工作区全局跳转。
3. **CodeLens 引用计数 (对齐图 1)**：
   - 在每个 `class` 和 `fn`/`function` 声明的上方，动态显示类似 `1 reference | 0 implementations`、`3 references | 0 overrides` 的引用计数镜头。
   - 点击该镜头即可执行查找所有引用（Peek References）。
4. **语法高亮增强**：
   - 支持 `public`, `protected`, `private`, `static`, `function`, `fn`, `class`, `new`, `echo`, `print`, `let`, `var`, `const` 等关键字高亮。
   - 支持 PHP 风格 `$variable` 变量标识符高亮。
   - 类名青色高亮，`::` 作用域操作符及方法调用精准着色。
