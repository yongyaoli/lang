@echo off
rem MSVC 构建脚本: 自动探测 Visual Studio 安装位置, 支持在任意目录执行
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem 1. 优先通过 vswhere 定位最新版 Visual Studio (Community / Professional / Enterprise)
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSROOT=%%i"
)

rem 2. 回退到常见安装位置
if not defined VSROOT (
    for %%V in (
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
        "C:\Program Files\Microsoft Visual Studio\18\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        "C:\Program Files\Microsoft Visual Studio\2022\Community"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    ) do (
        if not defined VSROOT if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%%~V"
    )
)

if not defined VSROOT (
    echo [错误] 未找到 Visual Studio (需要 C++ 桌面开发工作负载).
    echo        也可改用 MinGW gcc 编译, 参见 README.md "快速开始".
    exit /b 1
)

echo 使用 Visual Studio: !VSROOT!
call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cl /nologo /utf-8 /O2 /W3 /I. l_heap.c lexer.c ast.c parser.c value.c gc.c interp.c builtins.c bytecode.c compiler.c vm.c stdlib\*.c main.c /Fe:l.exe /link wininet.lib ws2_32.lib
set RC=%ERRORLEVEL%
echo CMDEXIT=%RC%
exit /b %RC%
