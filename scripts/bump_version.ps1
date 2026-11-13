<#
.SYNOPSIS
    L 语言版本号自动累加脚本
    规则: L 0.1.{小版本}.{时间戳}
#>
$ErrorActionPreference = "Stop"

$rootDir = Split-Path -Parent $PSScriptRoot
$counterFile = Join-Path $PSScriptRoot "patch_version.txt"

$counter = 1
if (Test-Path $counterFile) {
    $raw = [System.IO.File]::ReadAllText($counterFile).Trim()
    $parsed = 0
    if ([int]::TryParse($raw, [ref]$parsed) -and $parsed -gt 0) {
        $counter = $parsed + 1
    }
}
[System.IO.File]::WriteAllText($counterFile, $counter.ToString(), [System.Text.Encoding]::ASCII)

# 2. 获取当前时间戳 (年月日时分)
$ts = Get-Date -Format "yyyyMMddHHmm"

# 3. 构造全新版本号: 0.1.{小版本}.{时间戳}
$newVersion = "0.1." + $counter + "." + $ts
Write-Host ">>> 自动累加生成新版本号: L $newVersion" -ForegroundColor Cyan

# 4. 更新 version.h
$versionHPath = Join-Path $rootDir "src\include\version.h"
$versionHContent = @"
#ifndef L_VERSION_H
#define L_VERSION_H

#define L_VERSION "$newVersion"
#define L_COPYRIGHT_YEAR "2026"

#endif
"@
[System.IO.File]::WriteAllText($versionHPath, $versionHContent, [System.Text.Encoding]::UTF8)

# 5. 更新 scripts/setup.iss
$setupIssPath = Join-Path $PSScriptRoot "setup.iss"
if (Test-Path $setupIssPath) {
    $issContent = [System.IO.File]::ReadAllText($setupIssPath, [System.Text.Encoding]::UTF8)
    $issContent = [System.Text.RegularExpressions.Regex]::Replace($issContent, '(#define MyAppVersion\s+)"[^"]+"', "`$1`"$newVersion`"")
    [System.IO.File]::WriteAllText($setupIssPath, $issContent, [System.Text.Encoding]::UTF8)
}

return $newVersion
