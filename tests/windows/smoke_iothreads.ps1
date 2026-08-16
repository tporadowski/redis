# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M9 smoke (9.2): io-threads 4 + SET/GET load + BGSAVE + unixsocket PING.
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
if (-not (Test-Path $server)) { throw "missing $server" }
if (-not (Test-Path $cli))    { throw "missing $cli" }

$port = 26382
$work = Join-Path $BuildDir "smoke_iothreads_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log  = Join-Path $work "server.log"
$conf = Join-Path $work "redis.conf"
$sock = Join-Path $work "redis.sock"
$rdb  = Join-Path $work "dump.rdb"
Remove-Item $log, $sock, $rdb -ErrorAction SilentlyContinue

@"
port $port
bind 127.0.0.1
protected-mode no
dir $work
logfile $log
unixsocket $sock
unixsocketperm 700
io-threads 4
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
    # Prefer WMI Create so the server is not a member of this shell's Job
    # Object. Agent/CI Job Objects can starve extra IO threads after the
    # first client when redis-server is a direct child.
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
        throw "server did not become ready`n$tail"
    }

    $threads = Invoke-Redis @("CONFIG", "GET", "io-threads")
    if ($threads -notmatch "4") { throw "io-threads not 4: $threads" }
    Write-Host "ok io-threads 4 PING"

    foreach ($n in 1..5) {
        $set = Invoke-Redis @("SET", "k$n", "v$n")
        if ($set -ne "OK") { throw "SET k$n : $set" }
    }
    $get = Invoke-Redis @("GET", "k5")
    if ($get -ne "v5") { throw "GET after load: $get" }
    Write-Host "ok 5 SET/GET"

    $bg = Invoke-Redis @("BGSAVE")
    if ($bg -notmatch "Background saving started" -and $bg -ne "OK") {
        throw "BGSAVE: $bg"
    }
    $done = $false
    for ($i = 0; $i -lt 80; $i++) {
        if (Test-Path $log) {
            $raw = Get-Content $log -Raw
            if ($raw -match "BGSAVE done" -or $raw -match "Background saving terminated with success") {
                $done = $true
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $done) { throw "BGSAVE did not finish (see $log)" }
    Write-Host "ok BGSAVE with io-threads 4"

    $uping = & $cli -s $sock PING 2>&1 | Out-String
    $uping = $uping.Trim()
    if ($uping -ne "PONG") { throw "unixsocket PING: $uping" }
    Write-Host "ok unixsocket PING under io-threads 4"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_iothreads ok"
