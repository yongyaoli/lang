# L 语言 Windows 环境重装说明文档

根据系统规范与用户要求，已对本地 Windows 11 环境中的 L 语言程序完成旧版完全卸载并重新安装最新构建版本。

---

## 1. 卸载与安装基本信息

| 属性 | 详细信息 |
| :--- | :--- |
| **操作类型** | 旧版本完全卸载 + 最新版本全新安装 |
| **操作系统** | Windows 11 (x86_64) |
| **程序名称** | L 语言解释器 (l.exe) |
| **程序版本** | L 0.1.0 (构建时间: 2026-11-09 11:34:17) |
| **安装目录** | `C:\Users\Administrator\AppData\Local\Programs\lang` |
| **环境变量** | 已自动加入用户 `PATH`（支持终端直接运行 `l`） |

---

## 2. 操作过程记录

1. **旧版本卸载**：
   - 执行脚本：`.\scripts\uninstall.ps1`
   - 清理内容：从用户注册表 `HKCU\Environment\Path` 中移除旧路径，并物理删除原 `C:\Users\Administrator\AppData\Local\Programs\lang` 目录（旧版本为 2026-11-08 构建）。
2. **新版本重新安装**：
   - 执行脚本：`.\scripts\install.ps1`
   - 安装最新编译包含 M1 ~ M4 全部功能（含 ESP32/Pin/PWM/ADC/I2C/SPI/UART、help免引号别名机制、12 项 sys 标准库函数等）的 `l.exe`（243 KB）。
   - 重新配置并刷新 Windows 环境变量。

---

## 3. 安装功能验证

* 命令行版本验证：`l -v` 输出 `L 0.1.0`
* 交互帮助测试：执行 `help(sys)`、`help(io)` 免引号直接输出完整模块文档，未再出现未定义变量错误。
