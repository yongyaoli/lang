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

# 确保 dist 目录存在（不直接全部删除，避免占用报错，仅清理旧产物）
if (-not (Test-Path $distPath)) {
    New-Item -ItemType Directory -Path $distPath -Force | Out-Null
}

# ----------------------------------------------------
# 1. 编译最新的根目录 l.exe
# ----------------------------------------------------
Write-Host "`n[1/3] 正在编译 L 语言独立解释器核心 (l.exe)..." -ForegroundColor Yellow
Push-Location $rootDir
try {
    & gcc -O2 -Wall -Wextra -I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/*.c main.c -static -o "$rootDir\l.exe"
    if ($LASTEXITCODE -ne 0) { throw "Windows 二进制编译失败！" }
} finally {
    Pop-Location
}
Write-Host "编译成功: $rootDir\l.exe" -ForegroundColor Green

# ----------------------------------------------------
# 2. 生成 Windows 独立可执行安装程序 (.exe 安装包)
# ----------------------------------------------------
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

# ----------------------------------------------------
# 3. 生成 Windows 免安装/便携 Zip 与 Linux/macOS Unix 安装包
# ----------------------------------------------------
Write-Host "`n[3/3] 正在打包便携 Zip 与 Linux/macOS 安装包..." -ForegroundColor Yellow

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
    "builtins.c", "main.c", "version.h",
    "README.md", "INSTALL.md"
)

foreach ($f in $sourceFiles) {
    Copy-Item "$rootDir\$f" "$unixPkgDir\" -Force
}

New-Item -ItemType Directory -Path "$unixPkgDir\stdlib" -Force | Out-Null
Copy-Item "$rootDir\stdlib\*.c" "$unixPkgDir\stdlib\" -Force
Copy-Item "$PSScriptRoot\install.sh" "$unixPkgDir\" -Force
Copy-Item "$PSScriptRoot\uninstall.sh" "$unixPkgDir\" -Force

$nl = "`n"
$tab = "`t"
$makefileContent = "CC ?= gcc" + $nl +
"CFLAGS ?= -O2 -Wall -Wextra -I." + $nl +
"PREFIX ?= /usr/local" + $nl + $nl +
"SRCS = lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/str.c stdlib/math.c stdlib/time.c stdlib/io.c stdlib/sys.c main.c" + $nl +
"TARGET = l" + $nl + $nl +
"all: `$(TARGET)" + $nl + $nl +
"`$(TARGET): `$(SRCS)" + $nl +
$tab + "`$(CC) `$(CFLAGS) -o `$(TARGET) `$(SRCS)" + $nl + $nl +
"install: `$(TARGET)" + $nl +
$tab + "mkdir -p `$(DESTDIR)`$(PREFIX)/bin" + $nl +
$tab + "cp -f `$(TARGET) `$(DESTDIR)`$(PREFIX)/bin/`$(TARGET)" + $nl +
$tab + "chmod 755 `$(DESTDIR)`$(PREFIX)/bin/`$(TARGET)" + $nl + $nl +
"uninstall:" + $nl +
$tab + "rm -f `$(DESTDIR)`$(PREFIX)/bin/`$(TARGET)" + $nl + $nl +
"clean:" + $nl +
$tab + "rm -f `$(TARGET)" + $nl + $nl +
".PHONY: all install uninstall clean" + $nl

[System.IO.File]::WriteAllText("$unixPkgDir\Makefile", $makefileContent, (New-Object System.Text.UTF8Encoding($false)))

$unixZip = "$distPath\lang-unix.zip"
Compress-Archive -Path "$unixPkgDir\*" -DestinationPath $unixZip -Force

Write-Host "`n打包全部完成！生成的安装与分发程序如下:" -ForegroundColor Cyan
Get-ChildItem $distPath | Select-Object Name, @{Name="Size(KB)";Expression={[math]::Round($_.Length/1KB,2)}}, LastWriteTime | Format-Table -AutoSize
