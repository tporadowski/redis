# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Run the native interop + LLP64 smokes.
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build"
}
$BuildDir = (Resolve-Path $BuildDir).Path

$llp64 = Join-Path $BuildDir "llp64_smoke.exe"
$interop = Join-Path $BuildDir "interop_smoke.exe"
foreach ($p in @($llp64, $interop)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

Write-Host "== llp64_smoke =="
& $llp64
if ($LASTEXITCODE -ne 0) { throw "llp64_smoke failed: $LASTEXITCODE" }

Write-Host "== interop_smoke =="
Push-Location $BuildDir
try {
    & $interop
    if ($LASTEXITCODE -ne 0) { throw "interop_smoke failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host "smoke_interop ok"
