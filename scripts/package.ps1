<#
.SYNOPSIS
    L 语言多平台打包构建脚本 (生成可直接执行的 exe 安装包以及分发包)
#>
param(
    [string]$DistDir = "$PSScriptRoot\..\dist"
)

$ErrorActionPreference = "Stop"

$rootDir = Split-Path -Parent $PSScriptRoot
$distPath = [System.IO.Path]::GetFullPath($DistDir)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "      L 语言多平台安装分发包生成程序      " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 确保 dist 目录存在
if (-not (Test-Path $distPath)) {
    New-Item -ItemType Directory -Path $distPath -Force | Out-Null
}

# 0. 自动递增累加小版本号与时间戳
$currentVersion = & "$PSScriptRoot\bump_version.ps1"

# 1. 编译最新的根目录 l.exe
Write-Host "`n[1/3] 正在编译 L 语言独立解释器核心 (l.exe) 版本 $currentVersion..." -ForegroundColor Yellow
Push-Location $rootDir
try {
    & gcc -O2 -Wall -Wextra -I. l_heap.c lexer.c ast.c parser.c value.c gc.c interp.c builtins.c bytecode.c compiler.c vm.c stdlib/*.c main.c -lwininet -lws2_32 -static -o "$rootDir\l.exe"
    if ($LASTEXITCODE -ne 0) { throw "Windows 二进制编译失败！" }
} finally {
    Pop-Location
}
Write-Host "编译成功: $rootDir\l.exe" -ForegroundColor Green

# 2. 生成 Windows 独立可执行安装程序 (.exe 安装包)
Write-Host "`n[2/3] 正在生成 Windows 独立 .exe 安装程序 (Inno Setup)..." -ForegroundColor Yellow

$isccCandidates = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)

$isccPath = $null
foreach ($c in $isccCandidates) {
    if ($c -and (Test-Path $c)) {
        $isccPath = $c
        break
    }
}

if ($isccPath) {
    Write-Host "使用 Inno Setup 编译器: $isccPath" -ForegroundColor DarkGray
    & $isccPath "$PSScriptRoot\setup.iss"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Inno Setup 编译失败！"
    } else {
        $exeInstaller = Join-Path $distPath "lang-setup-windows-x86_64.exe"
        Write-Host "成功生成 Windows 独立安装包: $exeInstaller" -ForegroundColor Green
    }
} else {
    Write-Warning "未检测到 Inno Setup 编译器 (ISCC.exe)，跳过 .exe 安装程序生成。可运行 winget install JRSoftware.InnoSetup 安装。"
}

# 3. 生成 Windows 便携 Zip 与跨平台源码包
Write-Host "`n[3/3] 正在打包便携 Zip 与跨平台源码包..." -ForegroundColor Yellow
Write-Host "说明: 本脚本只能产出本机平台的二进制。" -ForegroundColor DarkGray
Write-Host "      - lang-windows-*: Windows 预编译安装/便携包 (本机生成)" -ForegroundColor DarkGray
Write-Host "      - lang-unix.zip : 源码包, Linux / macOS 用户解压后运行 install.sh 自动编译" -ForegroundColor DarkGray
Write-Host "      - Linux x86_64 与 macOS (arm64/x86_64) 的预编译 tar.gz 无法在 Windows 上交叉编译," -ForegroundColor DarkGray
Write-Host "        请推送 v* 标签或在 GitHub Actions 手动运行 'Release PC Packages' 产出。" -ForegroundColor DarkGray

# Windows 便携文件夹
$winPkgDir = Join-Path $distPath "lang-windows-x86_64"
New-Item -ItemType Directory -Path $winPkgDir -Force | Out-Null
Copy-Item "$rootDir\l.exe" "$winPkgDir\" -Force
Copy-Item "$rootDir\README.md" "$winPkgDir\" -Force
Copy-Item "$rootDir\INSTALL.md" "$winPkgDir\" -Force
Copy-Item "$PSScriptRoot\install.ps1" "$winPkgDir\" -Force
Copy-Item "$PSScriptRoot\uninstall.ps1" "$winPkgDir\" -Force

$batContent = @"
@echo off
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
pause
"@
[System.IO.File]::WriteAllText("$winPkgDir\install.bat", $batContent, [System.Text.Encoding]::ASCII)

$winZip = "$distPath\lang-windows-x86_64.zip"
Compress-Archive -Path "$winPkgDir\*" -DestinationPath $winZip -Force

# Linux & macOS 通用源码与安装脚本包
$unixPkgDir = Join-Path $distPath "lang-unix"
New-Item -ItemType Directory -Path $unixPkgDir -Force | Out-Null

$sourceFiles = @(
    "lexer.h", "lexer.c", "ast.h", "ast.c",
    "parser.h", "parser.c", "value.h", "value.c",
    "gc.h", "gc.c", "interp.h", "interp.c",
    "bytecode.h", "bytecode.c", "compiler.h", "compiler.c",
    "vm.h", "vm.c",
    "builtins.c", "main.c", "version.h",
    "l_config.h", "l_hal.h", "l_heap.h", "l_heap.c",
    "README.md", "INSTALL.md"
)

foreach ($f in $sourceFiles) {
    if (Test-Path "$rootDir\$f") {
        Copy-Item "$rootDir\$f" "$unixPkgDir\" -Force
    }
}

New-Item -ItemType Directory -Path "$unixPkgDir\stdlib" -Force | Out-Null
Copy-Item "$rootDir\stdlib\*.c" "$unixPkgDir\stdlib\" -Force
Copy-Item "$PSScriptRoot\install.sh" "$unixPkgDir\" -Force
Copy-Item "$PSScriptRoot\uninstall.sh" "$unixPkgDir\" -Force

$unixZip = "$distPath\lang-unix.zip"
Compress-Archive -Path "$unixPkgDir\*" -DestinationPath $unixZip -Force

Write-Host "`n本地打包全部完成！生成的安装与分发程序如下:" -ForegroundColor Cyan
Write-Host "(macOS arm64 / x86_64 预编译包由 GitHub Actions 'Release PC Packages' 工作流产出, 推送 v* 标签即自动构建)" -ForegroundColor DarkGray
Get-ChildItem $distPath | Select-Object Name, @{Name="Size(KB)";Expression={[math]::Round($_.Length/1KB,2)}}, LastWriteTime | Format-Table -AutoSize
