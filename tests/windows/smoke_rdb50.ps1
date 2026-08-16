# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M10.1: load a Redis 5.0 encodings.rdb on 8.10 and read a known key.
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
$rdbSrc = Join-Path $PSScriptRoot "encodings-5.0.rdb"
if (-not (Test-Path $server)) { throw "missing $server" }
if (-not (Test-Path $cli))    { throw "missing $cli" }
if (-not (Test-Path $rdbSrc)) { throw "missing $rdbSrc" }

$port = 26384
$work = Join-Path $BuildDir "smoke_rdb50_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log  = Join-Path $work "server.log"
$rdb  = Join-Path $work "dump.rdb"
Copy-Item -Force $rdbSrc $rdb
Remove-Item $log -ErrorAction SilentlyContinue

$conf = Join-Path $work "redis.conf"
@"
port $port
bind 127.0.0.1
protected-mode no
dir $work
dbfilename dump.rdb
logfile $log
save ""
appendonly no
"@ | Set-Content -Path $conf -Encoding ASCII

function Invoke-Redis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

$proc = $null
try {
    $created = $null
    try {
        $created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
            CommandLine = "`"$server`" `"$conf`""
        }
    } catch { $created = $null }
    if ($created -and $created.ReturnValue -eq 0 -and $created.ProcessId) {
        $proc = Get-Process -Id $created.ProcessId -ErrorAction SilentlyContinue
    }
    if (-not $proc) {
        $proc = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden
    }
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try { if ((Invoke-Redis @("PING")) -eq "PONG") { $ready = $true; break } } catch {}
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "server did not load 5.0 RDB (see $log)`n$tail"
    }

    $hello = Invoke-Redis @("GET", "string")
    if ($hello -ne "Hello World") { throw "GET string from 5.0 RDB: $hello" }
    $num = Invoke-Redis @("GET", "number")
    if ($num -ne "10") { throw "GET number from 5.0 RDB: $num" }
    Write-Host "ok 5.0 encodings.rdb loaded (string=Hello World, number=10)"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_rdb50 ok"
