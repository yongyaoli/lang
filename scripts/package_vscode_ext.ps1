<#
.SYNOPSIS
    L 语言 VS Code 扩展打包与分发发布脚本
.DESCRIPTION
    1. 自动调用 @vscode/vsce 将 editors/vscode 打包为标准 .vsix 格式
    2. 将产物复制到 dist/ 目录 (供 Release 与离线下载)
    3. 支持 -Publish 参数，发布到 Visual Studio Marketplace 与 Open VSX
.EXAMPLE
    .\scripts\package_vscode_ext.ps1
    .\scripts\package_vscode_ext.ps1 -Publish -MarketplaceToken "<TOKEN>"
#>
param(
    [switch]$Publish,
    [string]$MarketplaceToken = "",
    [string]$OpenVSXToken = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vscodeDir = Join-Path $root "editors\vscode"
$distDir = Join-Path $root "dist"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  L 语言 VS Code 扩展打包工具" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. 确保 dist 目录存在
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}

# 2. 检查 node 与 npx
if (-not (Get-Command npx -ErrorAction SilentlyContinue)) {
    Write-Host "[错误] 未检测到 npx，请先安装 Node.js (https://nodejs.org/)" -ForegroundColor Red
    exit 1
}

# 3. 读取版本号
$pkg = Get-Content (Join-Path $vscodeDir "package.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$vsixName = "$($pkg.name)-$($pkg.version).vsix"
$vsixPath = Join-Path $vscodeDir $vsixName

Write-Host "[1/3] 正在打包扩展 $vsixName ..." -ForegroundColor Yellow
Push-Location $vscodeDir
try {
    cmd /c "npx --yes @vscode/vsce package"
    if ($LASTEXITCODE -ne 0) {
        throw "vsce package 失败，退出码: $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

# 4. 复制到 dist 目录
if (Test-Path $vsixPath) {
    Copy-Item $vsixPath -Destination (Join-Path $distDir $vsixName) -Force
    Write-Host "[2/3] 打包产物已输出到: dist\$vsixName" -ForegroundColor Green
} else {
    Write-Host "[错误] 未在 $vscodeDir 找到生成的 .vsix 文件" -ForegroundColor Red
    exit 1
}

# 5. 可选发布步骤
if ($Publish) {
    Write-Host "[3/3] 正在发布到扩展商店..." -ForegroundColor Yellow

    # (1) 发布到 Visual Studio Marketplace
    if ($MarketplaceToken) {
        Write-Host "  -> 发布到 Visual Studio Marketplace..." -ForegroundColor Cyan
        Push-Location $vscodeDir
        try {
            cmd /c "npx --yes @vscode/vsce publish -p $MarketplaceToken"
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "  [提示] 未指定 -MarketplaceToken，跳过 VS Marketplace 发布" -ForegroundColor DarkGray
    }

    # (2) 发布到 Open VSX (VSCodium / Eclipse Theia / Gitpod 等)
    if ($OpenVSXToken) {
        Write-Host "  -> 发布到 Open VSX Registry..." -ForegroundColor Cyan
        Push-Location $vscodeDir
        try {
            cmd /c "npx --yes ovsx publish $(Join-Path $distDir $vsixName) -p $OpenVSXToken"
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "  [提示] 未指定 -OpenVSXToken，跳过 Open VSX 发布" -ForegroundColor DarkGray
    }
} else {
    Write-Host "[3/3] 完成。如需发布到扩展商店，请运行:" -ForegroundColor DarkGray
    Write-Host "      .\scripts\package_vscode_ext.ps1 -Publish -MarketplaceToken <PAT>" -ForegroundColor DarkGray
}

Write-Host "`n一键安装命令 (本地安装):" -ForegroundColor White
Write-Host "  code --install-extension dist\$vsixName" -ForegroundColor Cyan
