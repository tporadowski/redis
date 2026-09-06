# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# UTF-8 dir / dump / argv / preload-file drive-absolute path.
# Non-ASCII names are built from code points so the script stays ASCII.
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

$port = 16395
$work = Join-Path $BuildDir "smoke_utf8_paths_work"
$dirName = "data-" + [char]0x8DEF + [char]0x5F84
$unicodeDir = Join-Path $work $dirName
New-Item -ItemType Directory -Force -Path $unicodeDir | Out-Null
$rdb = Join-Path $unicodeDir "dump.rdb"
$log = Join-Path $unicodeDir "smoke.log"
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

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $unicodeDir,
        "--dbfilename", "dump.rdb",
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden
    Wait-Ready

    $key = ([char]0x952E) + "-" + ([char]0x952E)
    if ((Invoke-Redis @("SET", $key, "utf8-value")) -ne "OK") {
        throw "SET unicode key failed"
    }
    if ((Invoke-Redis @("GET", $key)) -ne "utf8-value") {
        throw "GET unicode key mismatch (argv/UTF-8 path?)"
    }

    $started = Invoke-Redis @("BGSAVE")
    if ($started -notmatch "Background saving started") {
        throw "BGSAVE: $started"
    }
    $ok = $false
    for ($i = 0; $i -lt 100; $i++) {
        $info = Invoke-Redis @("INFO", "persistence")
        if ($info -match "rdb_bgsave_in_progress:0" -and
            $info -match "rdb_last_bgsave_status:ok") { $ok = $true; break }
        if ($info -match "rdb_bgsave_in_progress:0" -and
            $info -match "rdb_last_bgsave_status:err") {
            throw "BGSAVE failed:`n$info`n$(Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue)"
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ok) { throw "BGSAVE did not finish" }
    if (-not (Test-Path -LiteralPath $rdb)) { throw "dump.rdb missing under unicode dir" }

    $checkOut = & $check $rdb 2>&1 | Out-String
    if ($checkOut -notmatch "RDB looks OK") {
        throw "redis-check-rdb failed:`n$checkOut"
    }

    try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch { }
    if ($proc -and -not $proc.WaitForExit(5000)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    $proc = $null

    $drivePath = [System.IO.Path]::GetFullPath($rdb)
    $drivePath = $drivePath.Replace([char]92, [char]47)
    $preload = "rdb:" + $drivePath
    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $unicodeDir,
        "--dbfilename", "dump.rdb",
        "--preload-file", $preload,
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden
    Wait-Ready
    $got = Invoke-Redis @("GET", $key)
    if ($got -ne "utf8-value") {
        throw "preload-file $preload did not restore key: $got"
    }

    Write-Host "ok smoke_utf8_paths (unicode dir + argv key + drive-absolute preload)"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch { }
        if (-not $proc.WaitForExit(3000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
