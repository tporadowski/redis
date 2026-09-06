# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# DEBUG SEGFAULT must produce a native SEH stack dump in the server log.
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build"
}
$BuildDir = (Resolve-Path $BuildDir).Path

$server = Join-Path $BuildDir "redis-server.exe"
$cli = Join-Path $BuildDir "redis-cli.exe"
foreach ($p in @($server, $cli)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

$port = 16398
$work = Join-Path $BuildDir "smoke_stacktrace_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$logfile = Join-Path $work "redis.log"
$conf = Join-Path $work "redis.conf"
Remove-Item $logfile -ErrorAction SilentlyContinue
@"
port $port
bind 127.0.0.1
protected-mode no
dir $work
dbfilename dump.rdb
logfile $logfile
save ""
appendonly no
crash-memcheck-enabled no
enable-debug-command yes
"@ | Set-Content $conf -Encoding ASCII

$proc = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden

try {
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $pong = & $cli -p $port PING 2>&1 | Out-String
            if ($pong.Trim() -eq "PONG") { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "server did not become ready" }

    try { & $cli -p $port DEBUG SEGFAULT | Out-Null } catch { }

    if (-not $proc.WaitForExit(15000)) {
        throw "server did not exit after DEBUG SEGFAULT"
    }

    if (-not (Test-Path $logfile)) { throw "missing $logfile" }
    $text = [IO.File]::ReadAllText($logfile)
    if ($text -notmatch "EXCEPTION_ACCESS_VIOLATION") {
        throw "log missing EXCEPTION_ACCESS_VIOLATION:`n$text"
    }
    if ($text -notmatch "--- STACK TRACE") {
        throw "log missing STACK TRACE:`n$text"
    }
    if ($text -notmatch "redis-server") {
        throw "log missing redis-server module frame:`n$text"
    }
    if ($text -notmatch "debugCommand") {
        throw "log missing debugCommand frame (PDB?):`n$text"
    }
    if ($text -notmatch "REDIS BUG REPORT (START|END)") {
        throw "log missing bug report banner:`n$text"
    }
    if ($text -notmatch "github.com/tporadowski/redis/issues") {
        throw "crash report must point at tporadowski/redis, not redis/redis:`n$text"
    }
    Write-Host "smoke_stacktrace ok"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { & $cli -p $port SHUTDOWN NOSAVE | Out-Null } catch {}
        if (-not $proc.WaitForExit(3000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
