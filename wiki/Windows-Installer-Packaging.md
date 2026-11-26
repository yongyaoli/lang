# Windows 独立安装包制作 (Inno Setup)

本项目包含一套工业级、面向终端用户的 Windows 一键全自动安装向导配置脚本（[`scripts/setup.iss`](file:///d:/github/langgit/scripts/setup.iss)）。

---

## 1. 安装包核心集成内容
* **`l.exe` 核心解释器与 CLI 工具**；
* **预编译固件包**：`l-esp32-merged.bin`、`l-esp32s3-merged.bin`、`l-esp32c3-merged.bin` 等；
* **完整的实战示例代码**：`examples/` 目录下全部用例；
* **VSCode 插件自动安装**：检测本机 VSCode 并静默安装语言包；
* **自动注入系统 PATH 环境变量**：安装完成后直接在任意目录打开终端即可输入 `l`。

---

## 2. 编译生成安装包

本地安装 Inno Setup 6 后，执行如下命令：

```powershell
# 1. 确保最新可执行程序已编译
gcc -O2 -Isrc/include src/core/*.c src/stdlib/*.c -lws2_32 -lwininet -o l.exe

# 2. 调用 Inno Setup 编译器
iscc scripts/setup.iss
```

编译完成后，将在 `scripts/Output/` 目录下生成类似 `L-Language-Setup-v0.1.14.exe` 的安装向导可执行程序，可直接对外发布与分发！
