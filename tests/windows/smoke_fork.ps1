# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M6 smoke (6.3): RedisModule_Fork without SetForkChildFn returns -1.
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build"
}
$BuildDir = (Resolve-Path $BuildDir).Path

$server = Join-Path $BuildDir "redis-server.exe"
$cli    = Join-Path $BuildDir "redis-cli.exe"
$mod    = Join-Path $BuildDir "forkcheck.dll"
foreach ($p in @($server, $cli, $mod)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

$port = 16400
$work = Join-Path $BuildDir "smoke_fork_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log = Join-Path $work "smoke.log"
Remove-Item $log -ErrorAction SilentlyContinue

function Invoke-Redis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $work,
        "--logfile", $log,
        "--loadmodule", $mod
    ) -PassThru -WindowStyle Hidden

    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            if ((Invoke-Redis @("PING")) -eq "PONG") { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "server did not become ready (see $log)`n$tail"
    }

    $ok = Invoke-Redis @("FORKCHECK.OK")
    if ($ok -ne "1") { throw "FORKCHECK.OK: $ok" }
    if ((Get-Content $log -Raw) -notmatch "SetForkChildFn") {
        throw "log missing Fork-without-registration warning"
    }
    Write-Host "ok RedisModule_Fork returned -1 without SetForkChildFn"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_fork ok"
