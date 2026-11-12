# Regression tests: run tests/*.l and compare stdout with the sibling .out file.
# Usage: .\run_tests.ps1            run tests
#        .\run_tests.ps1 -Update    regenerate .out from current output
param([switch]$Update)

$utf8 = New-Object System.Text.UTF8Encoding($false)
$exe = Join-Path $PSScriptRoot "l.exe"
$tmp = Join-Path $env:TEMP "l_test_out.txt"
$pass = 0; $fail = 0

# TCP loopback echo server on 127.0.0.1:23075 (needed by tests/21_tcp_socket.l)
$echoJob = $null
if (Test-Path (Join-Path $PSScriptRoot "tests\21_tcp_socket.l")) {
    $echoJob = Start-Job -ScriptBlock {
        $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 23075)
        $l.Start()
        try {
            while ($true) {
                $c = $l.AcceptTcpClient()
                $s = $c.GetStream()
                $buf = New-Object byte[] 4096
                while (($n = $s.Read($buf, 0, $buf.Length)) -gt 0) { $s.Write($buf, 0, $n) }
                $c.Close()
            }
        } finally { $l.Stop() }
    }
    Start-Sleep -Milliseconds 400   # wait for listener
}

foreach ($src in Get-ChildItem (Join-Path $PSScriptRoot "tests") -Filter *.l | Sort-Object Name) {
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

if ($echoJob) {
    Stop-Job $echoJob -ErrorAction SilentlyContinue
    Remove-Job $echoJob -Force -ErrorAction SilentlyContinue
}

if (-not $Update) {
    Write-Host ""
    Write-Host "passed $pass, failed $fail"
    if ($fail -gt 0) { exit 1 }
}
