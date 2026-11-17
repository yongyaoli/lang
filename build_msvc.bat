@echo off
rem MSVC 构建脚本: 自动探测 Visual Studio 安装位置, 支持在任意目录执行
setlocal enabledelayedexpansion

set "DP0=%~dp0"
if "%DP0:~-1%"=="\" set "DP0=%DP0:~0,-1%"
cd /d "%DP0%"

set "VSROOT="
set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=C:\Program Files (x86)"
set "PF64=%ProgramFiles%"
if not defined PF64 set "PF64=C:\Program Files"

rem 1. 优先通过 vswhere 定位最新版 Visual Studio (pushd 避免在 for 括号内展开带括号路径)
if exist "%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" (
    pushd "%PF86%\Microsoft Visual Studio\Installer"
    for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "VSROOT=%%i"
    )
    popd
) else if exist "%PF64%\Microsoft Visual Studio\Installer\vswhere.exe" (
    pushd "%PF64%\Microsoft Visual Studio\Installer"
    for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "VSROOT=%%i"
    )
    popd
)

rem 2. 回退到常见安装位置 (避免在括号集内直接使用含括号的环境变量名)
if not defined VSROOT (
    for %%V in (
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
        "C:\Program Files\Microsoft Visual Studio\18\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        "C:\Program Files\Microsoft Visual Studio\2022\Community"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    ) do (
        if not defined VSROOT if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%%~V"
    )
)

if not defined VSROOT (
    echo [提示] 未找到 Visual Studio [需要 C++ 桌面开发工作负载]
    echo        也可改用 MinGW gcc 编译, 参见 README.md "快速开始".
    exit /b 1
)

echo 使用 Visual Studio: !VSROOT!
call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

rem 3. 编译图标资源 (assets/l.ico 嵌入 l.exe), 失败则跳过
set "RES="
rc /nologo /fo assets\l.res assets\l.rc >nul 2>&1 && set "RES=assets\l.res"

cl /nologo /utf-8 /O2 /W3 /Isrc\include src\core\*.c src\stdlib\*.c src\main\main.c %RES% /Fe:l.exe /link wininet.lib ws2_32.lib
set RC=%ERRORLEVEL%
echo CMDEXIT=%RC%
exit /b %RC%
