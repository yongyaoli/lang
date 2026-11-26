# VSCode 插件与语言服务配置

为了提供媲美主流语言的开发体验，L 语言配备了官方 VSCode 语法高亮与智能提示插件（位于仓库 [`editors/vscode/`](file:///d:/github/langgit/editors/vscode)）。

---

## 1. 核心功能
* **`.l` 代码语法高亮**：关键词、字符串、函数声明、内置类颜色区分；
* **代码片段与补全 (Snippets)**：输入 `pin`、`wifi`、`mqtt`、`ota` 自动生成样板代码；
* **一键烧录与串口监视快捷键**：在 VSCode 编辑器内直接一键把当前打开的文件烧录到 ESP32。

---

## 2. 手动安装插件

如果您未通过 Windows 全自动安装包安装，可以手动打包安装 `.vsix` 文件：

```powershell
# 进入插件目录
cd d:\github\langgit\editors\vscode

# 使用 vsce 打包为 vsix 文件
vsce package

# 在 VSCode 终端安装
code --install-extension l-language-0.1.14.vsix
```

安装完成后，在 VSCode 中打开任意 `.l` 文件即可享受全彩高亮与语法提示！
