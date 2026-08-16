# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M6 smoke (6.2): in-tree vector-sets VADD / VCARD / VSIM.
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
foreach ($p in @($server, $cli)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

$port = 16399
$work = Join-Path $BuildDir "smoke_vecset_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log = Join-Path $work "smoke.log"
Remove-Item $log -ErrorAction SilentlyContinue

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
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden

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
        throw "server did not become ready (see $log)`n$tail"
    }

    $cmd = Invoke-Redis @("COMMAND", "INFO", "VADD")
    if ($cmd -notmatch "vadd") { throw "VADD not registered:`n$cmd" }

    $a = Invoke-Redis @("VADD", "vs", "VALUES", "3", "0.1", "0.2", "0.3", "a")
    if ($a -ne "1") { throw "VADD a: $a" }
    $b = Invoke-Redis @("VADD", "vs", "VALUES", "3", "0.15", "0.2", "0.25", "b")
    if ($b -ne "1") { throw "VADD b: $b" }
    $card = Invoke-Redis @("VCARD", "vs")
    if ($card -ne "2") { throw "VCARD: $card" }
    $dim = Invoke-Redis @("VDIM", "vs")
    if ($dim -ne "3") { throw "VDIM: $dim" }
    $sim = Invoke-Redis @("VSIM", "vs", "VALUES", "3", "0.1", "0.2", "0.3", "COUNT", "1")
    if ($sim -notmatch "a") { throw "VSIM expected a, got: $sim" }
    Write-Host "ok VADD/VCARD/VDIM/VSIM"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_vecset ok"
