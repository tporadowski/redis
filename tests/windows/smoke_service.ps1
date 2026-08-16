# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M5 smoke (5.1): console PING still works; --service-run without SCM exits.
# --service-install needs Administrator (UAC); skipped unless already elevated.
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

$port = 16395
$work = Join-Path $BuildDir "smoke_service_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$log = Join-Path $work "smoke.log"
Remove-Item $log -ErrorAction SilentlyContinue

function Invoke-Redis {
    param([string[]]$RedisArgs)
    $out = & $cli -p $port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

# --service-run is only valid when launched by the SCM.
$run = Start-Process -FilePath $server -ArgumentList @("--service-run") `
    -Wait -PassThru -NoNewWindow -RedirectStandardError (Join-Path $work "service-run.err")
if ($run.ExitCode -eq 0) {
    throw "--service-run without SCM should fail"
}
$err = Get-Content (Join-Path $work "service-run.err") -Raw -ErrorAction SilentlyContinue
if (-not $err -or $err -notmatch "StartServiceCtrlDispatcherA|HandleServiceCommands|1063") {
    Write-Host "--service-run stderr: $err"
    throw "--service-run did not report SCM dispatcher failure"
}
Write-Host "ok --service-run rejected outside SCM (exit $($run.ExitCode))"

$proc = $null
try {
    $proc = Start-Process -FilePath $server -ArgumentList @(
        "--port", "$port",
        "--bind", "127.0.0.1",
        "--protected-mode", "no",
        "--dir", $work,
        "--logfile", $log
    ) -PassThru -WindowStyle Hidden
    $ok = $false
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 200
        try {
            $pong = Invoke-Redis @("PING")
            if ($pong -eq "PONG") { $ok = $true; break }
        } catch {}
    }
    if (-not $ok) { throw "console server did not answer PING" }
    Write-Host "ok console PING"
} finally {
    if ($proc -and -not $proc.HasExited) {
        try { Invoke-Redis @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

Write-Host "smoke_service ok"
