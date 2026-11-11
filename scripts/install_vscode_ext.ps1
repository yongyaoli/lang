<#
.SYNOPSIS
    一键安装 L 语言 VS Code 语法高亮扩展
#>
$ErrorActionPreference = "Stop"

$srcDir = Join-Path $PSScriptRoot "..\editors\vscode"

# 从 package.json 动态读取扩展名与版本, 避免目录名与实际版本漂移
$pkg = Get-Content (Join-Path $srcDir "package.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$extName = "$($pkg.name)-$($pkg.version)"
$destDir = Join-Path $env:USERPROFILE ".vscode\extensions\$extName"

Write-Host "正在安装 L 语言 VS Code 扩展 ($extName) 到: $destDir" -ForegroundColor Yellow

if (-not (Test-Path "$env:USERPROFILE\.vscode\extensions")) {
    New-Item -ItemType Directory -Path "$env:USERPROFILE\.vscode\extensions" -Force | Out-Null
}

# 清理同扩展的旧版本目录(版本号不同的同名扩展)
$extRoot = Join-Path $env:USERPROFILE ".vscode\extensions"
Get-ChildItem $extRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "$($pkg.name)-*" -and $_.Name -ne $extName } |
    ForEach-Object {
        Write-Host "清理旧版本: $($_.Name)" -ForegroundColor DarkGray
        Remove-Item $_.FullName -Recurse -Force
    }

# 复制扩展目录
Copy-Item -Path $srcDir -Destination $destDir -Recurse -Force

Write-Host "安装成功！" -ForegroundColor Green
Write-Host "提示: 若 VS Code 已在运行中，请在 VS Code 中按快捷键 Ctrl+Shift+P，输入并选择 'Developer: Reload Window' 重新加载窗口即可立即生效！" -ForegroundColor Cyan
