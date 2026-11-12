# 软件安装说明文档

根据用户环境构建需求，为生成 Windows 平台原生的 `.exe` 安装程序（支持图形化安装向导、自动配置 PATH 环境变量、自动卸载支持），系统已自动下载并安装了以下打包工具：

---

## 1. 安装软件信息

| 属性 | 详细信息 |
| :--- | :--- |
| **软件名称** | Inno Setup 6 |
| **软件版本** | 6.7.3 |
| **发行厂商** | JRSoftware (Jordan Russell) |
| **官方网站** | https://jrsoftware.org/isinfo.php |
| **安装方式** | Windows Package Manager (`winget install JRSoftware.InnoSetup`) |
| **安装路径** | `C:\Users\Administrator\AppData\Local\Programs\Inno Setup 6` |
| **核心编译器** | `C:\Users\Administrator\AppData\Local\Programs\Inno Setup 6\ISCC.exe` |

---

## 2. 软件用途与集成说明

- **用途**：用于将编译出的 `l.exe` 核心解释器、标准库 `stdlib/`、说明文档以及自动配置环境变量的脚本编译打包成单一可执行的 `lang-setup-windows-x86_64.exe` 安装包。
- **功能特性**：
  - 用户只需双击该 `.exe` 即可启动图形安装向导；
  - 自动检测并写入用户注册表 PATH 环境变量；
  - 广播 `WM_SETTINGCHANGE` 消息使新命令行窗口即刻全局识别 `l` 命令；
  - 在“控制面板” / “Windows 设置 -> 应用和功能”中完整注册，支持标准一键卸载；
  - 自动集成在项目的构建脚本 `scripts/package.ps1` 中，执行一键打包即可自动生成 `.exe` 安装包。
