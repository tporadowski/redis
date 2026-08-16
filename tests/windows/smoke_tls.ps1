# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M8 smoke (8.3): tls-port handshake + GET/SET.
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
if (-not (Get-ChildItem -Path $BuildDir -Filter "libssl*.dll" -ErrorAction SilentlyContinue)) {
    throw "libssl*.dll missing next to redis-server (BUILD_TLS?)"
}

function Find-OpenSsl {
    $candidates = @(
        (Join-Path $BuildDir "vcpkg_installed\x64-windows\tools\openssl\openssl.exe")
        (Join-Path $BuildDir "vcpkg_installed\x64-windows\bin\openssl.exe")
    )
    foreach ($root in @($env:VCPKG_ROOT, "D:\xAI\vcpkg")) {
        if (-not $root) { continue }
        $candidates += Join-Path $root "installed\x64-windows\tools\openssl\openssl.exe"
        Get-ChildItem -Path (Join-Path $root "downloads\tools\perl") -Filter openssl.exe -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $candidates += $_.FullName }
    }
    $onPath = Get-Command openssl.exe -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    throw "openssl.exe not found (need it to mint a self-signed cert)"
}

$openssl = Find-OpenSsl
$port = 26381
$work = Join-Path $BuildDir "smoke_tls_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log  = Join-Path $work "server.log"
$conf = Join-Path $work "redis.conf"
$crt  = Join-Path $work "redis.crt"
$key  = Join-Path $work "redis.key"
$cnf  = Join-Path $work "openssl.cnf"
Remove-Item $log -ErrorAction SilentlyContinue

@"
[req]
distinguished_name = req
prompt = no
[req]
CN = 127.0.0.1
"@ | Set-Content -Path $cnf -Encoding ASCII

$env:OPENSSL_CONF = $cnf
& $openssl req -x509 -newkey rsa:2048 -keyout $key -out $crt -days 2 -nodes -subj "/CN=127.0.0.1"
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $crt) -or -not (Test-Path $key)) {
    throw "openssl failed to write $crt / $key"
}
Write-Host "ok minted $($crt) with $openssl"

@"
port 0
tls-port $port
bind 127.0.0.1
protected-mode no
dir $work
logfile $log
tls-cert-file $crt
tls-key-file $key
tls-ca-cert-file $crt
tls-auth-clients no
save ""
appendonly no
"@ | Set-Content -Path $conf -Encoding ASCII

function Invoke-TlsRedis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port --tls --cacert $crt --insecure @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            if ((Invoke-TlsRedis @("PING")) -eq "PONG") { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) {
        $tail = ""
        if (Test-Path $log) { $tail = Get-Content $log -Raw }
        throw "tls-port did not accept PING (see $log)`n$tail"
    }
    Write-Host "ok tls-port PING"

    $set = Invoke-TlsRedis @("SET", "k", "v")
    if ($set -ne "OK") { throw "SET over TLS: $set" }
    $get = Invoke-TlsRedis @("GET", "k")
    if ($get -ne "v") { throw "GET over TLS: $get" }
    Write-Host "ok tls-port SET/GET"

    if (Test-Path $log) {
        $raw = Get-Content $log -Raw
        if ($raw -notmatch "Ready to accept connections tls") {
            throw "log missing tls listener:`n$raw"
        }
    }
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-TlsRedis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_tls ok"
