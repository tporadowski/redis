# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M8 smoke (8.1): tls.c is linked (not the USE_OPENSSL=0 stub).
# Handshake + GET/SET is 8.3.
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

$ssl = Get-ChildItem -Path $BuildDir -Filter "libssl*.dll" -ErrorAction SilentlyContinue
$crypto = Get-ChildItem -Path $BuildDir -Filter "libcrypto*.dll" -ErrorAction SilentlyContinue
if (-not $ssl)   { throw "libssl*.dll not next to redis-server.exe (vcpkg OpenSSL not copied)" }
if (-not $crypto){ throw "libcrypto*.dll not next to redis-server.exe" }
Write-Host "ok OpenSSL DLLs $($ssl.Name) $($crypto.Name)"

$port = 26379
$work = Join-Path $BuildDir "smoke_tls_build_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log = Join-Path $work "server.log"
$conf = Join-Path $work "redis.conf"
@"
port $port
bind 127.0.0.1
protected-mode no
dir $work
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
    $proc = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try { if ((Invoke-Redis @("PING")) -eq "PONG") { $ready = $true; break } } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "server did not become ready`n$tail"
    }

    $set = Invoke-Redis @("CONFIG", "SET", "tls-port", "1")
    if ($set -match "TLS support is not compiled in") {
        throw "tls.c is the USE_OPENSSL=0 stub: $set"
    }
    # Linked TLS: apply fails on missing certs/key, not on missing conn type.
    if ($set -match "Unable to update TLS configuration" -or $set -match "ERR") {
        Write-Host "ok tls-port apply reached TLS configure ($set)"
    } elseif ($set -eq "OK") {
        Write-Host "ok tls-port set (unexpected without certs, but type is linked)"
    } else {
        throw "unexpected CONFIG SET tls-port: $set"
    }
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_tls_build ok"
