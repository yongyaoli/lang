# 运行 tests/*.l,把标准输出与同名 .out 文件对比
# 用法: .\run_tests.ps1            运行测试
#       .\run_tests.ps1 -Update    用当前输出重新生成 .out(确认输出正确后再用)
param([switch]$Update)

$utf8 = New-Object System.Text.UTF8Encoding($false)
$exe = Join-Path $PSScriptRoot "l.exe"
$tmp = Join-Path $env:TEMP "l_test_out.txt"
$pass = 0; $fail = 0

foreach ($src in Get-ChildItem (Join-Path $PSScriptRoot "tests") -Filter *.l | Sort-Object Name) {
    if ($src.Name -notmatch '^\d\d_') { continue }
    $expectFile = [IO.Path]::ChangeExtension($src.FullName, ".out")
    cmd /c "`"$exe`" `"$($src.FullName)`" > `"$tmp`" 2>nul"
    $actual = [IO.File]::ReadAllText($tmp, $utf8).Replace("`r`n", "`n")

    if ($Update) {
        [IO.File]::WriteAllText($expectFile, $actual, $utf8)
        Write-Host "已更新  $($src.Name)"
        continue
    }
    if (-not (Test-Path $expectFile)) { Write-Host "缺少期望文件  $($src.Name)" -ForegroundColor Yellow; $fail++; continue }
    $expected = [IO.File]::ReadAllText($expectFile, $utf8).Replace("`r`n", "`n")
    if ($actual -eq $expected) { Write-Host "通过  $($src.Name)" -ForegroundColor Green; $pass++ }
    else { Write-Host "失败  $($src.Name)" -ForegroundColor Red; $fail++ }
}

if (-not $Update) {
    Write-Host "`n通过 $pass,失败 $fail"
    if ($fail -gt 0) { exit 1 }
}
