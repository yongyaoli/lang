# Regression tests: run tests/*.l and compare stdout with the sibling .out file.
# Usage: .\scripts\run_tests.ps1            run tests
#        .\scripts\run_tests.ps1 -Update    regenerate .out from current output
param([switch]$Update)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$utf8 = New-Object System.Text.UTF8Encoding($false)
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "l.exe"
$tmp = Join-Path $env:TEMP "l_test_out.txt"
$pass = 0; $fail = 0

# TCP loopback echo server on 127.0.0.1:23075 (needed by tests/21_tcp_socket.l)
$echoProc = $null
if (Test-Path (Join-Path $root "tests\21_tcp_socket.l")) {
    $echoScript = Join-Path $PSScriptRoot "echo_server.l"
    $echoProc = Start-Process -FilePath $exe -ArgumentList "`"$echoScript`"" -WindowStyle Hidden -PassThru
    # 动态探测直至回显监听器就绪 (最多等待 2 秒)
    for ($i = 0; $i -lt 20; $i++) {
        try {
            $probe = [System.Net.Sockets.TcpClient]::new()
            $probe.Connect([System.Net.IPAddress]::Loopback, 23075)
            $probe.Close()
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
}

foreach ($src in Get-ChildItem (Join-Path $root "tests") -Filter *.l | Sort-Object Name) {
    if ($src.Name -notmatch '^\d\d_') { continue }
    $expectFile = [IO.Path]::ChangeExtension($src.FullName, ".out")
    cmd /c "`"$exe`" `"$($src.FullName)`" > `"$tmp`" 2>nul"
    $actual = [IO.File]::ReadAllText($tmp, $utf8).Replace("`r`n", "`n")

    if ($Update) {
        [IO.File]::WriteAllText($expectFile, $actual, $utf8)
        Write-Host "UPDATE  $($src.Name)"
        continue
    }
    if (-not (Test-Path $expectFile)) { Write-Host "MISSING  $($src.Name)" -ForegroundColor Yellow; $fail++; continue }
    $expected = [IO.File]::ReadAllText($expectFile, $utf8).Replace("`r`n", "`n")
    if ($actual -eq $expected) { Write-Host "PASS  $($src.Name)" -ForegroundColor Green; $pass++ }
    else { Write-Host "FAIL  $($src.Name)" -ForegroundColor Red; $fail++ }
}

if ($echoProc) {
    try { $echoProc.Kill() } catch {}
}

if (-not $Update) {
    Write-Host ""
    Write-Host "passed $pass, failed $fail"
    if ($fail -gt 0) { exit 1 }
}
