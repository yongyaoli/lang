<#
.SYNOPSIS
    Windows 一键卸载脚本
#>
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\lang"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "正在卸载 L 语言..." -ForegroundColor Yellow

# 1. 移除 PATH 中的条目
$regPath = "Registry::HKEY_CURRENT_USER\Environment"
$currentPath = (Get-ItemProperty -Path $regPath -Name Path -ErrorAction SilentlyContinue).Path

if ($currentPath) {
    $normalizedInstallDir = $InstallDir.TrimEnd('\')
    $paths = $currentPath -split ';' | Where-Object { $_ -ne "" -and $_.TrimEnd('\') -ne $normalizedInstallDir }
    $newPath = $paths -join ';'
    Set-ItemProperty -Path $regPath -Name Path -Value $newPath
    Write-Host "已从用户 PATH 环境变量中移除: $InstallDir" -ForegroundColor Green
}

# 2. 删除安装目录
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
    Write-Host "已删除程序目录: $InstallDir" -ForegroundColor Green
}

Write-Host "卸载完成！`n" -ForegroundColor Green
