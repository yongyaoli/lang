# 快速安装与运行 L 语言

支持 Windows、macOS、Linux。安装程序会自动检测系统、部署可执行程序与标准库，并**全自动配置 PATH 环境变量**。安装完成后打开终端即可全局使用 `l` 命令。

---

## 一、Windows 安装指南

### 方式 1：双击运行 EXE 安装程序 (最简单推荐)
1. 下载/生成独立安装程序：
   - `dist/lang-setup-windows-x86_64.exe`
2. **直接双击运行该 `.exe` 文件**；
3. 按照简体中文向导点击“下一步”直到完成；
   - 默认安装到 `%LOCALAPPDATA%\Programs\lang`（无需管理员权限）；
   - **自动勾选并配置 PATH 环境变量**，即刻全局生效；
   - 自动在 Windows “应用和功能” 中注册，可在设置或控制面板中随时一键卸载。
4. 打开新的终端（CMD 或 PowerShell），直接输入 `l` 即可运行！

### 方式 2：脚本/命令行快速安装
在仓库根目录直接运行：
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```
如需静默卸载：
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

---

## 二、Linux & macOS 安装指南

在终端中执行一键安装脚本：
```bash
bash ./scripts/install.sh
```
> **自动处理特性**：
> 1. 若有 sudo 权限优先安装到 `/usr/local/bin/l`；
> 2. 无 root/sudo 权限时自动回退安装到 `~/.local/bin/l`；
> 3. 自动检测并在 `~/.bashrc`、`~/.zshrc` 或 `~/.profile` 中补充 `export PATH` 环境变量。

如需卸载：
```bash
bash ./scripts/uninstall.sh
```

---

## 三、生成分发与安装包

在 Windows 下执行打包命令：
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1
```
执行后将在 `dist/` 目录下生成：
1. `lang-setup-windows-x86_64.exe`：**Windows 原生 EXE 独立安装程序**（双击即用）；
2. `lang-windows-x86_64.zip`：Windows 免安装便携包；
3. `lang-unix.zip`：Linux & macOS 跨平台自动构建安装包。

---

## 四、验证全局命令

安装完成后，打开新的 CMD 或 PowerShell 窗口测试：

```bash
# 查看版本
l --version

# 进入交互式 Python 风格 REPL
l

# 直接执行脚本
l 你的脚本.l
```
