# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M7 smoke (7.1): Sentinel starts; notification-script is CreateProcessA + reaped.
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build"
}
$BuildDir = (Resolve-Path $BuildDir).Path

$server   = Join-Path $BuildDir "redis-server.exe"
$sentinel = Join-Path $BuildDir "redis-sentinel.exe"
$cli      = Join-Path $BuildDir "redis-cli.exe"
foreach ($p in @($server, $cli)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}
if (-not (Test-Path $sentinel)) { $sentinel = $server }

$port = 26401
$work = Join-Path $BuildDir "smoke_sentinel_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log    = Join-Path $work "sentinel.log"
$marker = Join-Path $work "marker.txt"
$script = Join-Path $work "notify.cmd"
$conf   = Join-Path $work "sentinel.conf"
Remove-Item $log, $marker -ErrorAction SilentlyContinue

@"
@echo off
echo ran>> "$marker"
"@ | Set-Content -Path $script -Encoding ASCII

@"
port $port
bind 127.0.0.1
protected-mode no
dir $work
logfile $log
sentinel monitor m 127.0.0.1 1 1
sentinel down-after-milliseconds m 1000
sentinel notification-script m $script
"@ | Set-Content -Path $conf -Encoding ASCII

function Invoke-Redis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

$proc = $null
try {
    $exe = $sentinel
    $args = @($conf)
    if ($sentinel -eq $server) { $args = @($conf, "--sentinel") }
    $proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru -WindowStyle Hidden

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
        throw "sentinel did not become ready (see $log)`n$tail"
    }

    $info = Invoke-Redis @("INFO", "server")
    if ($info -notmatch "redis_mode:sentinel") {
        throw "not in sentinel mode:`n$info"
    }
    Write-Host "ok sentinel PING / redis_mode:sentinel"

    $saw = $false
    for ($i = 0; $i -lt 40; $i++) {
        if (Test-Path $marker) { $saw = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $saw) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "notification-script did not run (see $log)`n$tail"
    }
    Write-Host "ok notification-script ran"

    $pending = Invoke-Redis @("SENTINEL", "PENDING-SCRIPTS")
    Write-Host "PENDING-SCRIPTS $pending"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_sentinel ok"
