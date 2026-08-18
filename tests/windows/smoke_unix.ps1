# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M9 smoke (9.1): AF_UNIX listen + redis-cli -s PING/SET/GET.
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

$work = Join-Path $BuildDir "smoke_unix_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log  = Join-Path $work "server.log"
$conf = Join-Path $work "redis.conf"
$sock = Join-Path $work "redis.sock"
Remove-Item $log, $sock -ErrorAction SilentlyContinue

@"
port 0
bind 127.0.0.1
protected-mode no
dir $work
logfile $log
unixsocket $sock
unixsocketperm 700
save ""
appendonly no
"@ | Set-Content -Path $conf -Encoding ASCII

function Invoke-UnixRedis {
    param([string[]]$RedisArgs)
    $out = & $cli -s $sock @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            if ((Invoke-UnixRedis @("PING")) -eq "PONG") { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "unixsocket did not accept PING (see $log)`n$tail"
    }
    if (-not (Test-Path $sock)) { throw "socket file missing: $sock" }
    Write-Host "ok unixsocket PING ($sock)"

    # 14.3: unixsocketperm 700 is not Linux mode bits. CRT _chmod only
    # toggles the owner write flag; the ACL is the creating user's NTFS DACL.
    $item = Get-Item -LiteralPath $sock
    if (-not $item) { throw "Get-Item failed for $sock" }
    $acl = Get-Acl -LiteralPath $sock
    $owner = $acl.Owner
    if (-not $owner) { throw "socket has no NTFS owner" }
    Write-Host "ok unixsocket NTFS owner=$owner attributes=$($item.Attributes) (unixsocketperm is not 0700)"
    if ($acl.Access.Count -lt 1) { throw "socket DACL is empty" }

    $set = Invoke-UnixRedis @("SET", "k", "v")
    if ($set -ne "OK") { throw "SET over unixsocket: $set" }
    $get = Invoke-UnixRedis @("GET", "k")
    if ($get -ne "v") { throw "GET over unixsocket: $get" }
    Write-Host "ok unixsocket SET/GET"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-UnixRedis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_unix ok"
