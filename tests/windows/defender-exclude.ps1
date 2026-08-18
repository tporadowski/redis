# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Defender exclusions for the 13.x Tcl runner. Needs an elevated
# Windows PowerShell (this box has no `pwsh` / PowerShell 7):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\windows\defender-exclude.ps1
# Re-enable later: Remove-MpPreference -ExclusionPath/-ExclusionProcess ...
param(
    [string]$Repo = ""
)

$ErrorActionPreference = "Stop"
if (-not $Repo) { $Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }

$paths = @(
    (Join-Path $Repo "build"),
    (Join-Path $Repo "tests\tmp")
)
foreach ($p in $paths) {
    if (-not (Test-Path $p)) { New-Item -ItemType Directory -Path $p | Out-Null }
    Write-Host "ExclusionPath $p"
    Add-MpPreference -ExclusionPath $p
}
Write-Host "ExclusionProcess redis-server.exe"
Add-MpPreference -ExclusionProcess "redis-server.exe"
Write-Host "defender-exclude ok"
