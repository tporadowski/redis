# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M3 smoke (3.4): SET/GET + BGSAVE + redis-check-rdb.
# Same steps as smoke_bgsave.tcl (no tclsh required).
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
$check  = Join-Path $BuildDir "redis-check-rdb.exe"
foreach ($p in @($server, $cli, $check)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

$port = 16391
$work = Join-Path $BuildDir "smoke_bgsave_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$rdb = Join-Path $work "dump.rdb"
$log = Join-Path $work "smoke.log"
Remove-Item $rdb, $log -ErrorAction SilentlyContinue

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
        "--dbfilename", "dump.rdb",
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden

    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            if ((Invoke-Redis @("PING")) -eq "PONG") { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "server did not become ready (see $log)" }

    if ((Invoke-Redis @("SET", "smoke:key", "smoke-value")) -ne "OK") {
        throw "SET failed"
    }
    if ((Invoke-Redis @("GET", "smoke:key")) -ne "smoke-value") {
        throw "GET mismatch"
    }

    $started = Invoke-Redis @("BGSAVE")
    if ($started -notmatch "Background saving started") {
        throw "BGSAVE: $started"
    }

    $ok = $false
    for ($i = 0; $i -lt 100; $i++) {
        $info = Invoke-Redis @("INFO", "persistence")
        $idle = $info -match "rdb_bgsave_in_progress:0"
        if ($idle -and ($info -match "rdb_last_bgsave_status:ok")) { $ok = $true; break }
        if ($idle -and ($info -match "rdb_last_bgsave_status:err")) {
            throw "BGSAVE failed:`n$info"
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ok) { throw "BGSAVE did not finish" }

    if (-not (Test-Path $rdb)) { throw "dump.rdb missing" }

    $checkOut = & $check $rdb 2>&1 | Out-String
    if ($checkOut -notmatch "RDB looks OK") {
        throw "redis-check-rdb failed:`n$checkOut"
    }

    Write-Host "ok smoke_bgsave (SET/GET + BGSAVE + redis-check-rdb)"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch { }
        if (-not $proc.WaitForExit(3000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
