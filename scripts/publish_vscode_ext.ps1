<#
.SYNOPSIS
    L 语言 VS Code 扩展一键发布脚本 (Visual Studio Marketplace & Open VSX Registry)

.DESCRIPTION
    本脚本用于将 L 语言 VS Code 扩展一键发布至全球两大扩展生态：
    1. Visual Studio Marketplace (微软官方 VS Code 扩展商店)
    2. Open VSX Registry (开源无版权扩展市场，服务于 VSCodium, Cursor, Gitpod, Eclipse Theia 等)

========================================================================================
                          📖 Token 获取与发布指南 (请仔细阅读)
========================================================================================

【第一部分：Visual Studio Marketplace (官方 VS Code 商店)】
----------------------------------------------------------------------------------------
1. 创建发布者账号 (Publisher)：
   - 打开管理后台: https://marketplace.visualstudio.com/manage
   - 使用微软账号登录。
   - 点击 "Create publisher" 创建发布者。
   - 【极其重要】：发布者 ID (Publisher ID) 必须与 package.json 中的 "publisher" 字段完全一致！
     当前项目的 publisher 为: "yongyaoli"

2. 获取 Personal Access Token (PAT)：
   - 打开 Azure DevOps: https://dev.azure.com/
   - 登录后点击右上角用户设置图标 -> 展开菜单点击 "Personal access tokens"。
   - 点击 "+ New Token"：
     * Name: 填入自定义名称 (如 "vsce-publish-token")
     * Organization: 【重点中的重点】必须选择 "All accessible organizations" (不能选单一组织，否则报 401 认证失败)
     * Expiration: 选择过期时间 (建议 90 天或 1 年)
     * Scopes: 点击下方 "Show all scopes"：
       找到 "Marketplace" 项，勾选 "Acquire" 与 "Manage" (或直接选 "Full access")
   - 点击 "Create" 生成 Token，立即复制保存该 52 位的 Token (刷新后不再可见)。

3. 发布指令与环境变量：
   - 命令行传参: .\scripts\publish_vscode_ext.ps1 -MarketplaceToken "<你的PAT>"
   - 或持久化环境变量: $env:VSCE_PAT = "<你的PAT>" (之后直接运行脚本无需重复敲 Token)


【第二部分：Open VSX Registry (开源/第三方 VS Code 生态)】
----------------------------------------------------------------------------------------
1. 创建命名空间 (Namespace)：
   - 打开 Open VSX 官网: https://open-vsx.org/
   - 点击右上角 "Log in"，使用 GitHub 账号授权登录。
   - 进入个人主页或 Namespace 列表，申请创建命名空间。
   - 【极其重要】：Namespace 名称必须与 package.json 中的 "publisher" 完全一致！
     当前项目的 namespace 为: "yongyaoli"
   - 点击 "Claim Ownership" 确认拥有该命名空间。

2. 获取 Access Token：
   - 点击右上角个人头像 -> 进入 "Settings" -> 切换到 "Access Tokens" 标签页。
   - 在 "Create a new token" 处输入 Description (如 "ovsx-token") 并点击 "Create"。
   - 复制生成的 Token。

3. 发布指令与环境变量：
   - 命令行传参: .\scripts\publish_vscode_ext.ps1 -OpenVSXToken "<你的TOKEN>"
   - 或持久化环境变量: $env:OVSX_PAT = "<你的TOKEN>"


【第三部分：常见发布问题排查 (FAQ)】
----------------------------------------------------------------------------------------
Q1: 报 "401 Unauthorized" 错误？
    A: 检查 Azure DevOps PAT 的 Organization 是否选了 "All accessible organizations"，
       并确认 Scopes 勾选了 Marketplace -> Manage 权限。

Q2: 报 "Publisher not found" 或 "You are not authorized to publish under this publisher"？
    A: 检查 package.json 里的 publisher 是否已经在商店后台创建，且当前登录账号是该 Publisher 的 Owner。

Q3: 报 "Version x.x.x already exists"？
    A: 每次发布新版本必须递增 package.json 中的 "version" 字段 (如 0.3.0 -> 0.3.1)。

Q4: 发布后为什么搜不到？
    A: 首次发布后平台需要 2 ~ 5 分钟的自动化安全扫描与 CDN 同步，稍等片刻即可搜索到。

========================================================================================

.PARAMETER MarketplaceToken
    Visual Studio Marketplace 的 Personal Access Token (PAT)。
    若未传参，脚本会自动从环境变量 $env:VSCE_PAT 读取；若仍未设置，将提示手动交互输入。

.PARAMETER OpenVSXToken
    Open VSX Registry 的 Access Token。
    若未传参，脚本会自动从环境变量 $env:OVSX_PAT 读取；若仍未设置，将提示手动交互输入。

.PARAMETER VsixPath
    指定要发布的 .vsix 文件路径。默认自动查找或编译当前版本的 dist/ 产物。

.PARAMETER SkipMarketplace
    跳过 Visual Studio Marketplace 发布。

.PARAMETER SkipOpenVSX
    跳过 Open VSX Registry 发布。

.PARAMETER HelpGuide
    仅打印上述详细的 Token 获取与发布指南并退出。

.EXAMPLE
    # 查看详细获取 Token 指南:
    .\scripts\publish_vscode_ext.ps1 -HelpGuide

    # 交互式运行 (提示输入 Token，直接回车可跳过某个平台):
    .\scripts\publish_vscode_ext.ps1

    # 双平台一键全自动发布:
    .\scripts\publish_vscode_ext.ps1 -MarketplaceToken "<VSCE_PAT>" -OpenVSXToken "<OVSX_PAT>"

    # 仅发布到 Open VSX:
    .\scripts\publish_vscode_ext.ps1 -SkipMarketplace -OpenVSXToken "<OVSX_PAT>"
#>

param(
    [string]$MarketplaceToken = "",
    [string]$OpenVSXToken = "",
    [string]$VsixPath = "",
    [switch]$SkipMarketplace,
    [switch]$SkipOpenVSX,
    [switch]$HelpGuide
)

$ErrorActionPreference = "Stop"

# 根目录与路径解析
$scriptDir = $PSScriptRoot
$repoRoot  = Split-Path -Parent $scriptDir
$vscodeDir = Join-Path $repoRoot "editors\vscode"
$distDir   = Join-Path $repoRoot "dist"
$pkgJsonPath = Join-Path $vscodeDir "package.json"

# 打印使用指南
if ($HelpGuide) {
    Get-Help $PSCommandPath -Detailed
    exit 0
}

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "   🚀 L 语言 VS Code 扩展一键发布工具 (Marketplace & Open VSX)" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

# 1. 检查 package.json 与元数据
if (-not (Test-Path $pkgJsonPath)) {
    Write-Host "[错误] 未找到扩展配置文件: $pkgJsonPath" -ForegroundColor Red
    exit 1
}

$pkg = Get-Content $pkgJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$extName      = $pkg.name
$extVersion   = $pkg.version
$extPublisher = $pkg.publisher
$targetVsixName = "$extName-$extVersion.vsix"

Write-Host "  扩展名称:    $($pkg.displayName) ($extName)" -ForegroundColor White
Write-Host "  扩展版本:    v$extVersion" -ForegroundColor Yellow
Write-Host "  发布者(ID):  $extPublisher" -ForegroundColor White
Write-Host "-----------------------------------------------------------------" -ForegroundColor Gray

# 2. 检查 Node.js / npx 环境
if (-not (Get-Command npx -ErrorAction SilentlyContinue)) {
    Write-Host "[错误] 未找到 npx 命令！发布扩展需要 Node.js 环境。" -ForegroundColor Red
    Write-Host "       请先前往 https://nodejs.org/ 下载并安装 Node.js LTS 版本。" -ForegroundColor Yellow
    exit 1
}

# 3. 确定或生成 .vsix 打包文件
if (-not $VsixPath) {
    $defaultDistVsix = Join-Path $distDir $targetVsixName
    if (Test-Path $defaultDistVsix) {
        $VsixPath = $defaultDistVsix
    } else {
        Write-Host "[提示] 未在 dist\ 目录下检测到 $targetVsixName，正在自动执行打包..." -ForegroundColor Yellow
        $packageScript = Join-Path $scriptDir "package_vscode_ext.ps1"
        if (Test-Path $packageScript) {
            & $packageScript
        } else {
            Push-Location $vscodeDir
            try {
                cmd /c "npx --yes @vscode/vsce package --out `"$defaultDistVsix`""
            } finally {
                Pop-Location
            }
        }
        $VsixPath = $defaultDistVsix
    }
}

if (-not (Test-Path $VsixPath)) {
    Write-Host "[错误] 找不到待发布的 .vsix 文件: $VsixPath" -ForegroundColor Red
    exit 1
}

$fileItem = Get-Item $VsixPath
Write-Host "[准备就绪] 待发布安装包: $($fileItem.FullName) ($([math]::Round($fileItem.Length / 1KB, 1)) KB)" -ForegroundColor Green
Write-Host ""

# 4. 获取与解析 Token
# (1) Visual Studio Marketplace Token
if (-not $SkipMarketplace) {
    if (-not $MarketplaceToken -and $env:VSCE_PAT) {
        $MarketplaceToken = $env:VSCE_PAT
        Write-Host "[配置] 已自动从环境变量 `$env:VSCE_PAT 获取 VS Marketplace Token" -ForegroundColor DarkGray
    }
    if (-not $MarketplaceToken) {
        Write-Host "-----------------------------------------------------------------" -ForegroundColor Gray
        Write-Host "【1/2】Visual Studio Marketplace (官方 VS Code 商店发布)" -ForegroundColor Cyan
        Write-Host "如需获取 PAT Token，请访问: https://dev.azure.com/ -> Personal Access Tokens" -ForegroundColor DarkGray
        Write-Host "(权限需包含: Marketplace -> Manage；组织必须选 All accessible organizations)" -ForegroundColor DarkGray
        $inputToken = Read-Host "请输入 VS Marketplace PAT Token (直接回车可跳过此平台)"
        if ($inputToken.Trim()) {
            $MarketplaceToken = $inputToken.Trim()
        } else {
            Write-Host "  -> 已跳过 Visual Studio Marketplace 发布" -ForegroundColor Yellow
            $SkipMarketplace = $true
        }
    }
}

# (2) Open VSX Registry Token
if (-not $SkipOpenVSX) {
    if (-not $OpenVSXToken -and $env:OVSX_PAT) {
        $OpenVSXToken = $env:OVSX_PAT
        Write-Host "[配置] 已自动从环境变量 `$env:OVSX_PAT 获取 Open VSX Token" -ForegroundColor DarkGray
    }
    if (-not $OpenVSXToken) {
        Write-Host "-----------------------------------------------------------------" -ForegroundColor Gray
        Write-Host "【2/2】Open VSX Registry (开源生态发布: VSCodium/Cursor/Gitpod)" -ForegroundColor Cyan
        Write-Host "如需获取 Access Token，请访问: https://open-vsx.org/ -> Settings -> Access Tokens" -ForegroundColor DarkGray
        $inputToken = Read-Host "请输入 Open VSX Access Token (直接回车可跳过此平台)"
        if ($inputToken.Trim()) {
            $OpenVSXToken = $inputToken.Trim()
        } else {
            Write-Host "  -> 已跳过 Open VSX Registry 发布" -ForegroundColor Yellow
            $SkipOpenVSX = $true
        }
    }
}

Write-Host "-----------------------------------------------------------------" -ForegroundColor Gray

# 5. 执行发布逻辑
$successCount = 0
$failCount = 0

# (A) 发布至 Visual Studio Marketplace
if (-not $SkipMarketplace -and $MarketplaceToken) {
    Write-Host "[1/2] 正在发布到 Visual Studio Marketplace..." -ForegroundColor Cyan
    try {
        # 使用 @vscode/vsce publish 直接发布指定 packagePath
        cmd /c "npx --yes @vscode/vsce publish --packagePath `"$($fileItem.FullName)`" -p `"$MarketplaceToken`""
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✓ 成功发布至 Visual Studio Marketplace!" -ForegroundColor Green
            Write-Host "    主页地址: https://marketplace.visualstudio.com/items?itemName=$extPublisher.$extName" -ForegroundColor Cyan
            $successCount++
        } else {
            Write-Host "  ✗ 发布至 Visual Studio Marketplace 失败 (退出码: $LASTEXITCODE)" -ForegroundColor Red
            Write-Host "    排查提示: 检查 PAT 是否过期或权限是否缺失；检查版本号是否已存在" -ForegroundColor Yellow
            $failCount++
        }
    } catch {
        Write-Host "  ✗ 执行发布时发生异常: $_" -ForegroundColor Red
        $failCount++
    }
} else {
    Write-Host "[1/2] 跳过 Visual Studio Marketplace 发布" -ForegroundColor DarkGray
}

Write-Host ""

# (B) 发布至 Open VSX Registry
if (-not $SkipOpenVSX -and $OpenVSXToken) {
    Write-Host "[2/2] 正在发布到 Open VSX Registry..." -ForegroundColor Cyan
    try {
        # 使用 ovsx publish 直接发布
        cmd /c "npx --yes ovsx publish `"$($fileItem.FullName)`" -p `"$OpenVSXToken`""
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✓ 成功发布至 Open VSX Registry!" -ForegroundColor Green
            Write-Host "    主页地址: https://open-vsx.org/extension/$extPublisher/$extName" -ForegroundColor Cyan
            $successCount++
        } else {
            Write-Host "  ✗ 发布至 Open VSX Registry 失败 (退出码: $LASTEXITCODE)" -ForegroundColor Red
            Write-Host "    排查提示: 确认命名空间 $extPublisher 是否已在 open-vsx.org 认领；检查 Token 正确性" -ForegroundColor Yellow
            $failCount++
        }
    } catch {
        Write-Host "  ✗ 执行发布时发生异常: $_" -ForegroundColor Red
        $failCount++
    }
} else {
    Write-Host "[2/2] 跳过 Open VSX Registry 发布" -ForegroundColor DarkGray
}

# 6. 发布总结
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "  发布汇总: 成功 $successCount 个, 失败 $failCount 个" -ForegroundColor $(if ($failCount -gt 0) { "Yellow" } else { "Green" })
Write-Host "=================================================================" -ForegroundColor Cyan

if ($successCount -gt 0) {
    Write-Host "🎉 扩展已推送至应用商店审核/分发网络！" -ForegroundColor Green
    Write-Host "   注意：新版本上线通常需要 2 ~ 5 分钟的安全扫描与 CDN 预热，之后即可在全球编辑器中搜索安装。" -ForegroundColor White
}

if ($failCount -gt 0) {
    exit 1
}
