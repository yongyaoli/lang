<#
.SYNOPSIS
    一键安装 L 语言 VS Code 语法高亮扩展
#>
$ErrorActionPreference = "Stop"

$srcDir = Join-Path $PSScriptRoot "..\editors\vscode"

# 从 package.json 动态读取扩展名与版本, 避免目录名与实际版本漂移
$pkg = Get-Content (Join-Path $srcDir "package.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$extName = "$($pkg.name)-$($pkg.version)"
$destDir = Join-Path $env:USERPROFILE ".vscode\extensions\$extName"

Write-Host "正在安装 L 语言 VS Code 扩展 ($extName) 到: $destDir" -ForegroundColor Yellow

if (-not (Test-Path "$env:USERPROFILE\.vscode\extensions")) {
    New-Item -ItemType Directory -Path "$env:USERPROFILE\.vscode\extensions" -Force | Out-Null
}

# 支持的编辑器扩展目录 (VS Code 及其衍生分支, 如 CodeBuddy CN 使用 .codebuddycn\extensions)
$extRoots = @(
    @{ Dir = Join-Path $env:USERPROFILE ".vscode\extensions";        Name = "VS Code";       Settings = Join-Path $env:APPDATA "Code\User\settings.json" },
    @{ Dir = Join-Path $env:USERPROFILE ".codebuddycn\extensions";   Name = "CodeBuddy CN";  Settings = Join-Path $env:APPDATA "CodeBuddy CN\User\settings.json" },
    @{ Dir = Join-Path $env:USERPROFILE ".codebuddy\extensions";     Name = "CodeBuddy";     Settings = Join-Path $env:APPDATA "CodeBuddy\User\settings.json" }
)

foreach ($root in $extRoots) {
    $extRoot = $root.Dir
    if (-not (Test-Path $extRoot)) { continue }

    # 清理该编辑器下同扩展的旧版本目录(版本号不同的同名扩展)
    Get-ChildItem $extRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "$($pkg.name)-*" -and $_.Name -ne $extName } |
        ForEach-Object {
            Write-Host "清理旧版本 ($($root.Name)): $($_.Name)" -ForegroundColor DarkGray
            Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
        }

    # 复制扩展目录
    Write-Host "正在安装扩展到 $($root.Name): $extRoot" -ForegroundColor Yellow
    Copy-Item -Path $srcDir -Destination (Join-Path $extRoot $extName) -Recurse -Force

    # 自动启用 L 文件图标主题 (仅当用户未自定义图标主题时写入)
    $settingsPath = $root.Settings
    if (Test-Path $settingsPath) {
        $raw = Get-Content $settingsPath -Raw -Encoding UTF8
        if ($raw -notmatch '"workbench\.iconTheme"') {
            $trimmed = $raw.TrimEnd().TrimEnd('}')
            if ($trimmed.TrimEnd().EndsWith(',')) { $trimmed = $trimmed.TrimEnd().TrimEnd(',') }
            $raw = $trimmed + ",`n    `"workbench.iconTheme`": `"l-file-icons`"`n}`n"
            Set-Content -Path $settingsPath -Value $raw -Encoding UTF8
            Write-Host "已为 $($root.Name) 启用 L 文件图标主题" -ForegroundColor Green
        } else {
            Write-Host "$($root.Name) 已有自定义图标主题设置, 未改动 (如需 L 图标: 设置 workbench.iconTheme 为 l-file-icons)" -ForegroundColor DarkGray
        }
    }
}

Write-Host "安装成功！" -ForegroundColor Green
Write-Host "提示: 若编辑器已在运行中，请按 Ctrl+Shift+P 选择 'Developer: Reload Window' 重新加载窗口即可立即生效！" -ForegroundColor Cyan
