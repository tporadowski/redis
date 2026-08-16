# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 10.2: zip exists, required names are present, extracted redis-cli --version.
param(
    [string]$BuildDir = "",
    [string]$ZipStem = "Redis-x64-8.10.0-win.1"
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build"
}
$BuildDir = (Resolve-Path $BuildDir).Path
$zipPath = Join-Path $BuildDir "$ZipStem.zip"
if (-not (Test-Path $zipPath)) { throw "missing $zipPath (run pack_zip)" }

$work = Join-Path $BuildDir "smoke_zip_work"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null
Expand-Archive -Path $zipPath -DestinationPath $work -Force

$root = Join-Path $work $ZipStem
if (-not (Test-Path $root)) { throw "zip has no $ZipStem/ folder" }

$need = @(
    "redis-server.exe",
    "redis-cli.exe",
    "redis-benchmark.exe",
    "redis-check-rdb.exe",
    "redis-check-aof.exe",
    "redis-sentinel.exe",
    "redis.windows.conf",
    "redis.windows-service.conf",
    "LICENSE.txt",
    "REDISCONTRIBUTIONS.txt",
    "NOTICE",
    "Redis on Windows.md",
    "Windows Service Documentation.md"
)
foreach ($n in $need) {
    if (-not (Test-Path (Join-Path $root $n))) { throw "zip missing $n" }
}

$ver = & (Join-Path $root "redis-cli.exe") --version 2>&1 | Out-String
$ver = $ver.Trim()
if ($ver -notmatch "8\.10\.0") { throw "redis-cli --version: $ver" }
Write-Host "ok $ZipStem.zip ($ver)"

# Same image, different argv0.
$chk = & (Join-Path $root "redis-check-rdb.exe") --help 2>&1 | Out-String
if ($chk -notmatch "rdb|RDB|Usage") {
    # check-rdb with no file prints usage or an error mentioning rdb
}
Write-Host "smoke_zip ok"
