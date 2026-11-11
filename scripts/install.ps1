<#
.SYNOPSIS
    Windows 一键自动安装脚本 (自动安装 l.exe 并配置用户 PATH 环境变量)
#>
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\lang",
    [switch]$ForceRebuild
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "      L 语言 Windows 一键自动安装程序     " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$srcDir = Split-Path -Parent $PSScriptRoot
$targetExe = Join-Path $InstallDir "l.exe"

# 优先查找当前目录（离线包目录）下的 l.exe，其次查找上级源码根目录
$localExe = Join-Path $PSScriptRoot "l.exe"
if (-not (Test-Path $localExe)) {
    $localExe = Join-Path $srcDir "l.exe"
}

# 1. 检查或编译可执行文件
if ($ForceRebuild -or -not (Test-Path $localExe)) {
    Write-Host "[1/4] 正在编译 l.exe..." -ForegroundColor Yellow
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        throw "未找到 gcc 编译器，请先安装 MinGW/GCC 或直接使用预打包的二进制包。"
    }
    Push-Location $srcDir
    try {
        & gcc -O2 -Wall -Wextra -I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/*.c main.c -lwininet -lws2_32 -static -o l.exe
        if ($LASTEXITCODE -ne 0) {
            throw "编译失败，错误代码: $LASTEXITCODE"
        }
        $localExe = Join-Path $srcDir "l.exe"
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[1/4] 找到现有的 l.exe 可执行文件。" -ForegroundColor Green
}

# 2. 创建安装目录并复制文件
Write-Host "[2/4] 正在复制程序到: $InstallDir" -ForegroundColor Yellow
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

Copy-Item -Path $localExe -Destination $targetExe -Force

# 3. 添加到用户 PATH 环境变量 (若尚未添加)
Write-Host "[3/4] 正在配置系统 PATH 环境变量..." -ForegroundColor Yellow

# 读取用户级 PATH
$regPath = "Registry::HKEY_CURRENT_USER\Environment"
$currentPath = (Get-ItemProperty -Path $regPath -Name Path -ErrorAction SilentlyContinue).Path

if (-not $currentPath) {
    $newPath = $InstallDir
    Set-ItemProperty -Path $regPath -Name Path -Value $newPath
    Write-Host "已创建用户 PATH 并写入: $InstallDir" -ForegroundColor Green
} else {
    $paths = $currentPath -split ';' | Where-Object { $_ -ne "" }
    $normalizedInstallDir = $InstallDir.TrimEnd('\')
    $alreadyInPath = $false

    foreach ($p in $paths) {
        if ($p.TrimEnd('\') -eq $normalizedInstallDir) {
            $alreadyInPath = $true
            break
        }
    }

    if (-not $alreadyInPath) {
        $newPath = "$currentPath;$InstallDir"
        Set-ItemProperty -Path $regPath -Name Path -Value $newPath
        Write-Host "已将 $InstallDir 添加到用户 PATH 环境变量。" -ForegroundColor Green
    } else {
        Write-Host "$InstallDir 已经存在于用户 PATH 中，无需重复添加。" -ForegroundColor Gray
    }
}

# 同步刷新当前 PowerShell 会话的环境变量
if ($env:PATH -notlike "*$InstallDir*") {
    $env:PATH = "$InstallDir;$env:PATH"
}

# 广播 WM_SETTINGCHANGE 消息通知 Windows Shell 环境变量更新
try {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class EnvNotifier {
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
}
"@ -ErrorAction SilentlyContinue
    $result = [UIntPtr]::Zero
    [EnvNotifier]::SendMessageTimeout([IntPtr]0xffff, 0x001A, [UIntPtr]::Zero, "Environment", 2, 2000, [ref]$result) | Out-Null
} catch {}

# 4. 安装 VS Code 语法高亮扩展 (可选, 失败不影响主程序安装)
Write-Host "[4/4] 正在安装 VS Code 语法高亮扩展..." -ForegroundColor Yellow
try {
    & (Join-Path $PSScriptRoot "install_vscode_ext.ps1")
} catch {
    Write-Warning "VS Code 扩展安装已跳过: $_"
}

Write-Host "`n安装成功！" -ForegroundColor Green
Write-Host "----------------------------------------"
& $targetExe --version
Write-Host "----------------------------------------"
Write-Host "你可以直接在当前终端或新打开的终端中输入 'l' 运行交互式命令行，或者输入 'l 脚本.l' 执行脚本！`n" -ForegroundColor Cyan

