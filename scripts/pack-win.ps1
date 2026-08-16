# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Stage product files and write Redis-x64-8.10.0-win.N.zip (10.2).
param(
    [string]$BuildDir = "",
    [string]$SourceDir = "",
    [string]$ZipStem = "Redis-x64-8.10.0-win.1"
)

$ErrorActionPreference = "Stop"
if (-not $SourceDir) { $SourceDir = Split-Path -Parent $PSScriptRoot }
if (-not $BuildDir) { $BuildDir = Join-Path $SourceDir "build" }
$BuildDir = (Resolve-Path $BuildDir).Path
$SourceDir = (Resolve-Path $SourceDir).Path

$stageRoot = Join-Path $BuildDir "pack"
$stage = Join-Path $stageRoot $ZipStem
$zipPath = Join-Path $BuildDir "$ZipStem.zip"

if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$exes = @(
    "redis-server.exe",
    "redis-cli.exe",
    "redis-benchmark.exe",
    "redis-check-rdb.exe",
    "redis-check-aof.exe",
    "redis-sentinel.exe"
)
foreach ($e in $exes) {
    $src = Join-Path $BuildDir $e
    if (-not (Test-Path $src)) { throw "missing $src (build first)" }
    Copy-Item -Force $src (Join-Path $stage $e)
}

$dlls = @("libssl-3-x64.dll", "libcrypto-3-x64.dll")
foreach ($d in $dlls) {
    $src = Join-Path $BuildDir $d
    if (Test-Path $src) {
        Copy-Item -Force $src (Join-Path $stage $d)
    }
}

$docs = @(
    "redis.windows.conf",
    "redis.windows-service.conf",
    "LICENSE.txt",
    "REDISCONTRIBUTIONS.txt",
    "NOTICE",
    "Redis on Windows.md",
    "Redis on Windows Release Notes.md",
    "Windows Service Documentation.md"
)
foreach ($f in $docs) {
    $src = Join-Path $SourceDir $f
    if (-not (Test-Path $src)) { throw "missing $src" }
    Copy-Item -Force $src (Join-Path $stage $f)
}

# Compress-Archive stores the folder name as the zip root.
Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host "wrote $zipPath"
Get-ChildItem $stage | ForEach-Object { Write-Host ("  {0}" -f $_.Name) }
