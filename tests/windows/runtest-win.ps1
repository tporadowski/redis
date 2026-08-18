# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 13.1 runner: 5.0 RDB load + one-unit-at-a-time Tcl suite (wintest.tcl).
# Fences: QFORK_HEAP_BYTES, no default AF_UNIX, skip-list, leftover kill.
param(
    [string]$BuildDir = "",
    [string]$Single = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
$BuildDir = (Resolve-Path $BuildDir).Path

function Stop-RedisTestServers {
    Get-Process redis-server -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "killing leftover redis-server pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
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
        (Get-Command tclsh -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    )) {
    if ($c -and (Test-Path $c)) { $tclsh = $c; break }
}
if (-not $tclsh) {
    throw "tclsh not found (install Git for Windows mingw64 tclsh)"
}

$gitUsr = "C:\Program Files\Git\usr\bin"
$gitMingw = "C:\Program Files\Git\mingw64\bin"
$env:PATH = "$gitMingw;$gitUsr;$env:PATH"
$env:REDIS_SERVER = Join-Path $BuildDir "redis-server.exe"
if (-not $env:QFORK_HEAP_BYTES) { $env:QFORK_HEAP_BYTES = "512M" }
# Default off: AF_UNIX listen in start_server. Set to 1 to re-enable.
if (-not $env:REDIS_TEST_UNIXSOCKET) { $env:REDIS_TEST_UNIXSOCKET = "0" }

Set-Location $Root
$wintest = Join-Path $PSScriptRoot "wintest.tcl"

if ($Single) {
    $units = @($Single)
} else {
    $units = @(& $tclsh $wintest --list-units)
    if ($LASTEXITCODE -ne 0 -or $units.Count -eq 0) {
        throw "wintest --list-units failed"
    }
}

foreach ($u in $units) {
    $u = "$u".Trim()
    if (-not $u) { continue }
    Stop-RedisTestServers
    Write-Host "== wintest $u via $tclsh (QFORK_HEAP_BYTES=$($env:QFORK_HEAP_BYTES)) =="
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & $tclsh $wintest --build $BuildDir --single $u
    $code = $LASTEXITCODE
    $sw.Stop()
    Stop-RedisTestServers
    Write-Host "== $u exit=$code elapsed=$([int]$sw.Elapsed.TotalSeconds)s =="
    if ($code -ne 0) { exit $code }
}
Write-Host "runtest-win ok"
