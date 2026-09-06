# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Exact-image process control via redis-test-launcher.exe.
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("FindQForkChild", "Suspend", "Resume", "IsAlive", "IsOwned", "Image", "Terminate")]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedExecutable,

    [string]$Launcher = ""
)

$ErrorActionPreference = "Stop"
if (-not $Launcher) {
    $Launcher = $env:REDIS_TEST_LAUNCHER
}
if (-not $Launcher -or -not (Test-Path -LiteralPath $Launcher)) {
    throw "redis-test-launcher.exe not found (set REDIS_TEST_LAUNCHER)"
}

$expected = [System.IO.Path]::GetFullPath($ExpectedExecutable)
switch ($Action) {
    "IsAlive" {
        & $Launcher --is-alive $TargetProcessId
        exit $LASTEXITCODE
    }
    "IsOwned" {
        & $Launcher --is-owned $TargetProcessId $expected
        exit $LASTEXITCODE
    }
    "Image" {
        & $Launcher --image $TargetProcessId
        exit $LASTEXITCODE
    }
    "FindQForkChild" {
        & $Launcher --find-qfork-child $TargetProcessId $expected
        exit $LASTEXITCODE
    }
    "Suspend" {
        & $Launcher --suspend $TargetProcessId $expected
        exit $LASTEXITCODE
    }
    "Resume" {
        & $Launcher --resume $TargetProcessId $expected
        exit $LASTEXITCODE
    }
    "Terminate" {
        $token = & $Launcher --creation-token $TargetProcessId
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $token = "$token".Trim()
        & $Launcher --terminate $TargetProcessId --token $token $expected
        exit $LASTEXITCODE
    }
}
