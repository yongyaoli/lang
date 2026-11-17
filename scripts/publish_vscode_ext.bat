@echo off
rem L 语言 VS Code 扩展一键发布快捷启动脚本
setlocal enabledelayedexpansion
set "DP0=%~dp0"
if "%DP0:~-1%"=="\" set "DP0=%DP0:~0,-1%"
cd /d "%DP0%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%DP0%\publish_vscode_ext.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"

if not "%1"=="" goto end
if "%EXIT_CODE%"=="0" (
    echo.
    echo 执行完毕。
) else (
    echo.
    echo 执行出错，退出码: %EXIT_CODE%
)
pause

:end
exit /b %EXIT_CODE%
