# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# QFork child must reconnect process-static roots (hash templates, Functions).
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

$port = 16393
$work = Join-Path $BuildDir "smoke_qfork_roots_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$rdb = Join-Path $work "dump.rdb"
$log = Join-Path $work "smoke.log"
Remove-Item $rdb, $log -ErrorAction SilentlyContinue

function Invoke-Redis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

function Wait-Ready {
    for ($i = 0; $i -lt 50; $i++) {
        try {
            if ((Invoke-Redis @("PING")) -eq "PONG") { return }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    throw "server did not become ready (see $log)"
}

function Wait-BgsaveOk {
    for ($i = 0; $i -lt 100; $i++) {
        $info = Invoke-Redis @("INFO", "persistence")
        $idle = $info -match "rdb_bgsave_in_progress:0"
        if ($idle -and ($info -match "rdb_last_bgsave_status:ok")) { return }
        if ($idle -and ($info -match "rdb_last_bgsave_status:err")) {
            throw "BGSAVE failed:`n$info`n--- log ---`n$(Get-Content $log -Raw -ErrorAction SilentlyContinue)"
        }
        Start-Sleep -Milliseconds 100
    }
    throw "BGSAVE did not finish (see $log)"
}

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $work,
        "--dbfilename", "dump.rdb",
        "--hash-min-template-entries", "0",
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden
    Wait-Ready

    # HIMPORT fieldsets are per-connection; keep PREPARE+SET on one CLI.
    # Write the session without a UTF-8 BOM — PowerShell piping adds one.
    $himportFile = Join-Path $work "himport.txt"
    $himport = "HIMPORT PREPARE fs age name`r`nHIMPORT SET user:1 fs 30 alice`r`nHIMPORT SET user:2 fs 40 bob`r`n"
    [System.IO.File]::WriteAllText($himportFile, $himport, [System.Text.UTF8Encoding]::new($false))
    $himportOut = cmd /c "type `"$himportFile`" | `"$cli`" -p $port" 2>&1 | Out-String
    if ($himportOut -notmatch "OK" -or $himportOut -match "ERR") {
        throw "HIMPORT session failed:`n$himportOut"
    }

    $lib = "#!lua name=qforklib`nredis.register_function('qforkecho', function(keys, args) return args[1] end)"
    $loaded = Invoke-Redis @("FUNCTION", "LOAD", $lib)
    if ($loaded -ne "qforklib") { throw "FUNCTION LOAD: $loaded" }
    if ((Invoke-Redis @("FCALL", "qforkecho", "0", "hello")) -ne "hello") {
        throw "FCALL before save failed"
    }

    $started = Invoke-Redis @("BGSAVE")
    if ($started -notmatch "Background saving started") {
        throw "BGSAVE: $started"
    }
    Wait-BgsaveOk

    if (-not (Test-Path $rdb)) { throw "dump.rdb missing" }
    $checkOut = & $check $rdb 2>&1 | Out-String
    if ($checkOut -notmatch "RDB looks OK") {
        throw "redis-check-rdb failed:`n$checkOut"
    }

    try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch { }
    if ($proc -and -not $proc.WaitForExit(5000)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    $proc = $null

    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $work,
        "--dbfilename", "dump.rdb",
        "--hash-min-template-entries", "0",
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden
    Wait-Ready

    $u1 = Invoke-Redis @("HGET", "user:1", "name")
    $u2 = Invoke-Redis @("HGET", "user:2", "age")
    if ($u1 -ne "alice") { throw "template hash user:1 name=$u1" }
    if ($u2 -ne "40") { throw "template hash user:2 age=$u2" }
    $echo = Invoke-Redis @("FCALL", "qforkecho", "0", "restored")
    if ($echo -ne "restored") { throw "FUNCTION not in RDB: $echo" }

    Write-Host "ok smoke_qfork_roots (template hashes + Functions via QFork BGSAVE)"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch { }
        if (-not $proc.WaitForExit(3000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
