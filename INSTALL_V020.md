# L 语言 v0.2.0 安装与组件说明文档

本文档详细记录了本次 L 语言 v0.2.0 的安装与配置信息，包括 Windows 客户端、VS Code 语法高亮扩展以及 ESP32 固件更新。

---

## 1. 软件安装基本信息

| 属性 | 详情 |
| :--- | :--- |
| **软件名称** | L 语言解释器与工具链 (L Language) |
| **版本号** | `v0.2.0` (64-bit) |
| **构建时间** | 2026-11-09 |
| **操作系统** | Windows 11 (x86_64) |
| **安装方式** | 独立 Inno Setup 离线安装程序 (`lang-setup-windows-x86_64.exe`) |
| **安装目录** | `C:\Users\Administrator\AppData\Local\Programs\lang` |
| **环境变量** | 已自动加入当前用户 `PATH`（可直接在任意终端运行 `l`） |

---

## 2. 安装的内容清单

### 2.1 主程序与依赖
- **`l.exe`** (v0.2.0 核心二进制):
  - 集成基础解释器、垃圾回收器、REPL 交互终端。
  - 集成 ESP32 串口烧录与监视工具（`-p` / `-u` / `-s`）。
  - 集成 WinINet / WinSock2 模拟网络栈及 ESP32 芯片模拟器。
  - 新增特性：支持 REPL 模块直接免引号查询（如 `help(sys)`、`help(math)`）。
- **`unins000.exe` / `unins000.dat`**:
  - Windows 原生卸载程序，可通过 Windows“设置 -> 应用 -> 已安装的应用”一键安全卸载。

### 2.2 VS Code 语法高亮插件
- **插件名称**: `l-language-support`
- **版本**: `0.2.0`
- **安装路径**: `C:\Users\Administrator\.vscode\extensions\l-language-support-0.2.0`
- **支持文件**: `.l` 扩展名文件
- **高亮特性**:
  - **语言关键字**: `fn`, `var`, `if`, `else`, `while`, `for`, `in`, `return`, `try`, `catch`, `import`, `true`, `false`, `nil` 等。
  - **ESP32 外设及网络对象**: `ESP32`, `Pin`, `PWM`, `ADC`, `DAC`, `I2C`, `SPI`, `UART`, `WiFi`, `HTTP`, `SNTP`。
  - **内置系统与网络函数**: `wifi_connect`, `wifi_ip`, `http_get`, `http_post`, `print`, `help`, `len`, `type`, `ports`, `upload`, `connect` 等。
  - **注释与符号配对**: 自动处理 `//` 行注释、`/* */` 块注释，括号及引号自动闭合。
- **生效方法**:
  - 打开 VS Code，按下快捷键 `Ctrl+Shift+P`，输入并运行：
    ```
    Developer: Reload Window
    ```
  - 打开任何 `.l` 文件即可自动识别并呈现语法色彩高亮。

---

## 3. 本次新增功能与核心变更说明

### 3.1 ESP32 完整硬件看板输出 (`ESP32.status()`)
在 ESP32 模块中新增了 `ESP32.status()` 方法，终端可一目了然查看硬件、WiFi 与 Flash 状态：
```
>>> ESP32.status()
====================================================
  ESP32 系统状态与网络信息概览
====================================================
  [硬件规格]
    芯片型号:      ESP32 (Xtensa LX6)
    CPU 核心/主频: 2 核, 240 MHz
    SRAM / 堆内存: 320 KB / 剩余 0 字节
    固件 / IDF:    L 0.2.0 (ESP-IDF v5.3-sim)

  [无线网络 (WiFi)]
    物理 MAC:      24:0A:C4:B8:31:AA
    连接状态:      已连接 (Connected)
    局域网 IP:     192.168.48.1

  [裸 Flash 存储 (l_script)]
    分区容量:      64 KB
    脚本状态:      空 (未烧录脚本)
====================================================
```
同时 `ESP32.info()` 字典中也已新增 `wifi_connected`、`wifi_ip`、`ip`、`mac` 字段。

### 3.2 固件分区容量扩容
针对 M5 无线网络模块（WiFi + mbedTLS + HTTP Client）编译后固件体量增大的情况，分区表 `esp32/partitions.csv` 进行了结构优化：
- `factory` 应用分区：由 **1 MB** 扩容至 **2 MB**（`0x200000`）。
- `l_script` 脚本裸存储分区：扩容至 **128 KB**（`0x20000`）。
- 彻底解决了 GitHub Actions 云端构建提示 `app partition is too small` 的溢出报错。

### 3.3 GitHub 自动发布 Release
- 本次代码打入 Git Tag `v0.2.0` 并推送到远程仓库。
- GitHub Actions 触发后将自动编译 `esp32`、`esp32c3`、`esp32s3` 全型号合并固件（`.bin`），并在构建完成后自动生成 GitHub Release 并挂载固件下载资产。

---

## 4. 验证与运行测试

通过终端执行如下指令进行安装验证：
```powershell
# 1. 验证版本号
l -v
# 输出: L 0.2.0

# 2. 验证帮助及无引号交互
# 输入 help(sys) 可直接输出 sys 系统库文档，无需加引号
```
