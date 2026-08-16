# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 10.1 runner: 5.0 RDB load + 5.0-first Tcl suite (wintest.tcl).
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
$BuildDir = (Resolve-Path $BuildDir).Path

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

Set-Location $Root
Write-Host "== wintest.tcl (5.0-first units) via $tclsh =="
& $tclsh (Join-Path $PSScriptRoot "wintest.tcl") --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "runtest-win ok"
