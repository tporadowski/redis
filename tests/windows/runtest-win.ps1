# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 13.1 runner: 5.0 RDB load + one-unit-at-a-time Tcl suite (wintest.tcl).
# Fences: QFORK_HEAP_BYTES, no default AF_UNIX, skip-list, leftover kill.
# -Tls is off by default. It builds TclTLS on first use, generates tests/tls
# when the certificate set is missing, and forwards --tls. The 117-unit run
# stays on plain TCP.
param(
    [string]$BuildDir = "",
    [string]$Single = "",
    [switch]$Tls
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
$BuildDir = (Resolve-Path $BuildDir).Path

function Stop-RedisTestServers {
    $expected = [System.IO.Path]::GetFullPath((Join-Path $BuildDir "redis-server.exe"))
    Get-CimInstance Win32_Process -Filter "Name = 'redis-server.exe'" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $img = $_.ExecutablePath
            if (-not $img) { return }
            $full = [System.IO.Path]::GetFullPath($img)
            if ([String]::Equals($full, $expected, [StringComparison]::OrdinalIgnoreCase)) {
                Write-Host "killing leftover redis-server pid=$($_.ProcessId)"
                Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            }
        }
}

function Format-TclLibPath([string]$Path) {
    $p = $Path -replace '\\', '/'
    if ($p -match '[\s\{\}]') { return "{$p}" }
    return $p
}

# tests/tls is gitignored. Regenerate when a required file is missing or the
# server/SAN certificates predate the loopback and redis.local names.
function Ensure-TlsCerts {
    $dir = Join-Path $Root "tests\tls"
    $need = @(
        "ca.crt", "ca.key", "server.crt", "server.key",
        "client.crt", "client.key", "redis.crt", "redis.key",
        "san.crt", "san.key", "redis.dh"
    )
    $stale = $false
    foreach ($n in $need) {
        if (-not (Test-Path (Join-Path $dir $n))) { $stale = $true }
    }
    if (-not $stale) {
        $serverSan = & openssl x509 -in (Join-Path $dir "server.crt") -noout -ext subjectAltName 2>$null
        $sanSan = & openssl x509 -in (Join-Path $dir "san.crt") -noout -ext subjectAltName 2>$null
        if ("$serverSan" -notmatch "127\.0\.0\.1" -or "$sanSan" -notmatch "DNS:redis\.local" -or "$sanSan" -notmatch "DNS:cluster\.local") {
            $stale = $true
        }
    }
    if (-not $stale) { return }
    $bash = "C:\Program Files\Git\bin\bash.exe"
    if (-not (Test-Path $bash)) { throw "Git bash is required to generate tests/tls" }
    if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
        throw "openssl.exe not found (Git ucrt64 openssl is required for tests/tls)"
    }
    Write-Host "== generating tests/tls (dhparam 2048 runs only when redis.dh is absent) =="
    # Git bash rewrites a leading /O= subject into a filesystem path.
    $savedNoConv = $env:MSYS_NO_PATHCONV
    $savedExcl = $env:MSYS2_ARG_CONV_EXCL
    $env:MSYS_NO_PATHCONV = "1"
    $env:MSYS2_ARG_CONV_EXCL = "*"
    & $bash --noprofile --norc ./utils/gen-test-certs.sh
    $genCode = $LASTEXITCODE
    if ($null -eq $savedNoConv) { Remove-Item Env:MSYS_NO_PATHCONV -ErrorAction SilentlyContinue } else { $env:MSYS_NO_PATHCONV = $savedNoConv }
    if ($null -eq $savedExcl) { Remove-Item Env:MSYS2_ARG_CONV_EXCL -ErrorAction SilentlyContinue } else { $env:MSYS2_ARG_CONV_EXCL = $savedExcl }
    if ($genCode -ne 0) { throw "utils/gen-test-certs.sh failed ($genCode)" }
    foreach ($n in $need) {
        if (-not (Test-Path (Join-Path $dir $n))) { throw "gen-test-certs.sh did not write tests/tls/$n" }
    }
}

$rdbSmoke = Join-Path $PSScriptRoot "smoke_rdb50.ps1"
Write-Host "== smoke_rdb50 (5.0 RDB load) =="
& powershell -NoProfile -ExecutionPolicy Bypass -File $rdbSmoke -BuildDir $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$tclsh = $null
foreach ($c in @(
        "C:\Program Files\Git\mingw64\bin\tclsh.exe",
        "C:\Program Files\Git\mingw64\bin\tclsh86.exe",
        "C:\Program Files\Git\ucrt64\bin\tclsh.exe",
        "C:\Program Files\Git\ucrt64\bin\tclsh86.exe",
        (Get-Command tclsh -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    )) {
    if ($c -and (Test-Path $c)) { $tclsh = $c; break }
}
if (-not $tclsh) {
    throw "tclsh not found (install Git for Windows mingw64 or ucrt64 tclsh)"
}

$gitUsr = "C:\Program Files\Git\usr\bin"
$gitMingw = "C:\Program Files\Git\mingw64\bin"
$gitUcrt = "C:\Program Files\Git\ucrt64\bin"
$env:PATH = "$gitMingw;$gitUcrt;$gitUsr;$env:PATH"
$env:REDIS_SERVER = Join-Path $BuildDir "redis-server.exe"
$env:REDIS_TEST_LAUNCHER = Join-Path $BuildDir "redis-test-launcher.exe"
if (-not $env:QFORK_HEAP_BYTES) { $env:QFORK_HEAP_BYTES = "512M" }
$defaultHeap = $env:QFORK_HEAP_BYTES
# Default off: AF_UNIX listen in start_server. Set to 1 to re-enable.
if (-not $env:REDIS_TEST_UNIXSOCKET) { $env:REDIS_TEST_UNIXSOCKET = "0" }

Set-Location $Root
$wintest = Join-Path $PSScriptRoot "wintest.tcl"
$forward = @()
if ($Tls) {
    & (Join-Path $PSScriptRoot "ensure-tcltls.ps1") -BuildDir $BuildDir -Tclsh $tclsh
    $env:TCLLIBPATH = Format-TclLibPath (Join-Path $PSScriptRoot ".tcltls\lib")
    Ensure-TlsCerts
    $forward = @("--tls")
}

# Heavy cases (blocking pops, SCAN write-load, long KEYS) run. Do not refuse
# a unit by name. $watchdogDeny stays empty so a full list is not blocked.
$watchdogDeny = @()

if ($Single) {
    $units = @($Single)
} else {
    $units = @(& $tclsh $wintest --list-units)
    if ($LASTEXITCODE -ne 0 -or $units.Count -eq 0) {
        throw "wintest --list-units failed"
    }
}

if ($env:REDIS_TEST_UNSAFE -ne "1") {
    foreach ($u in $units) {
        $name = "$u".Trim()
        if ($watchdogDeny -contains $name) {
            throw "refusing $name (watchdog/reboot history). Set REDIS_TEST_UNSAFE=1 only for a fenced subset, never a full unit."
        }
    }
}

foreach ($u in $units) {
    $u = "$u".Trim()
    if (-not $u) { continue }
    Stop-RedisTestServers
    # replication-buffer holds 20000 x 10KB values plus a slow replica's
    # repl buffer. replication diskless swapdb keeps a second copy of
    # 2000 x 100KB values. corrupt-dump-fuzzer traffic can grow one string
    # to just under proto-max-bulk-len (512MB); the 512M heap aborts that
    # allocation. Those units use 2G when the runner default is 512M.
    if (($u -eq "integration/replication-buffer" -or $u -eq "integration/replication" -or $u -eq "integration/corrupt-dump-fuzzer") -and $defaultHeap -eq "512M") {
        $env:QFORK_HEAP_BYTES = "2G"
    } else {
        $env:QFORK_HEAP_BYTES = $defaultHeap
    }
    Write-Host "== wintest $u via $tclsh (QFORK_HEAP_BYTES=$($env:QFORK_HEAP_BYTES)) =="
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & $tclsh $wintest --build $BuildDir --single $u @forward
    $code = $LASTEXITCODE
    $sw.Stop()
    Stop-RedisTestServers
    Write-Host "== $u exit=$code elapsed=$([int]$sw.Elapsed.TotalSeconds)s =="
    if ($code -ne 0) { exit $code }
}
Write-Host "runtest-win ok"
