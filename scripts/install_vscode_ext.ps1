<#
.SYNOPSIS
    一键安装 L 语言 VS Code 语法高亮扩展
#>
$ErrorActionPreference = "Stop"

$extName = "l-language-support-0.2.0"
$srcDir  = Join-Path $PSScriptRoot "..\editors\vscode"
$destDir = Join-Path $env:USERPROFILE ".vscode\extensions\$extName"

Write-Host "正在安装 L 语言 VS Code 扩展到: $destDir" -ForegroundColor Yellow

if (-not (Test-Path "$env:USERPROFILE\.vscode\extensions")) {
    New-Item -ItemType Directory -Path "$env:USERPROFILE\.vscode\extensions" -Force | Out-Null
}

# 复制扩展目录
Copy-Item -Path $srcDir -Destination $destDir -Recurse -Force

Write-Host "安装成功！" -ForegroundColor Green
Write-Host "提示: 若 VS Code 已在运行中，请在 VS Code 中按快捷键 Ctrl+Shift+P，输入并选择 'Developer: Reload Window' 重新加载窗口即可立即生效！" -ForegroundColor Cyan
