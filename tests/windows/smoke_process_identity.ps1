# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Exact-image PID ownership via redis-test-launcher.exe.
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
$launcher = Join-Path $BuildDir "redis-test-launcher.exe"
foreach ($p in @($server, $cli, $launcher)) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}

$port = 16397
$work = Join-Path $BuildDir "smoke_process_identity_work"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$stdout = Join-Path $work "stdout.log"
$stderr = Join-Path $work "stderr.log"
$logfile = Join-Path $work "redis.log"
Remove-Item $stdout, $stderr, $logfile -ErrorAction SilentlyContinue

function Invoke-Launcher {
    param([string[]]$LauncherArgs)
    $out = & $launcher @LauncherArgs 2>&1
    return @{ Code = $LASTEXITCODE; Text = ("$out").Trim() }
}

$pidText = Invoke-Launcher @(
    $stdout, $stderr, "--", $server,
    "--port", "$port",
    "--bind", "127.0.0.1",
    "--protected-mode", "no",
    "--dir", $work,
    "--dbfilename", "dump.rdb",
    "--logfile", $logfile,
    "--enable-debug-command", "yes"
)
if ($pidText.Code -ne 0 -or $pidText.Text -notmatch '^\d+$') {
    throw "launcher failed to start server: $($pidText.Text)"
}
$serverPid = [int]$pidText.Text

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

    $alive = Invoke-Launcher @("--is-alive", "$serverPid")
    if ($alive.Code -ne 0) { throw "started server is not alive" }

    $owned = Invoke-Launcher @("--is-owned", "$serverPid", $server)
    if ($owned.Code -ne 0) { throw "started server is not owned by $server" }

    $wrong = Invoke-Launcher @("--is-owned", "$serverPid", $cli)
    if ($wrong.Code -eq 0) { throw "server PID matched redis-cli.exe image" }

    $image = Invoke-Launcher @("--image", "$serverPid")
    if ($image.Code -ne 0) { throw "image query failed" }
    $actual = [System.IO.Path]::GetFullPath($image.Text)
    $expected = [System.IO.Path]::GetFullPath($server)
    if (-not [String]::Equals($actual, $expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "image mismatch: $actual vs $expected"
    }

    $token = Invoke-Launcher @("--creation-token", "$serverPid")
    if ($token.Code -ne 0 -or $token.Text -notmatch '^\d+$') {
        throw "creation token failed: $($token.Text)"
    }
    $tokOwned = Invoke-Launcher @(
        "--is-owned", "$serverPid", "--token", $token.Text, $server
    )
    if ($tokOwned.Code -ne 0) { throw "token ownership failed" }

    $null = & $cli -p $port CONFIG SET rdb-key-save-delay 100000
    $null = & $cli -p $port SET smoke 1
    $null = & $cli -p $port BGSAVE
    $child = $null
    for ($i = 0; $i -lt 50; $i++) {
        $found = Invoke-Launcher @("--find-qfork-child", "$serverPid", $server)
        if ($found.Code -eq 0 -and $found.Text -match '^\d+$') {
            $child = [int]$found.Text
            break
        }
        Start-Sleep -Milliseconds 20
    }
    if (-not $child) { throw "QFork child not found by exact image" }
    $childOwned = Invoke-Launcher @("--is-owned", "$child", $server)
    if ($childOwned.Code -ne 0) { throw "QFork child not owned" }

    $term = Invoke-Launcher @(
        "--terminate", "$serverPid", "--token", $token.Text, $server
    )
    if ($term.Code -ne 0) { throw "terminate failed" }

    $dead = $false
    for ($i = 0; $i -lt 50; $i++) {
        $again = Invoke-Launcher @(
            "--is-owned", "$serverPid", "--token", $token.Text, $server
        )
        if ($again.Code -ne 0) { $dead = $true; break }
        Start-Sleep -Milliseconds 50
    }
    if (-not $dead) { throw "terminated instance still owned" }

    Write-Host "ok smoke_process_identity (launch + owned + token + qfork child + terminate)"
} finally {
    $alive = Invoke-Launcher @("--is-owned", "$serverPid", $server)
    if ($alive.Code -eq 0) {
        try { & $cli -p $port SHUTDOWN NOSAVE | Out-Null } catch { }
        Invoke-Launcher @("--is-alive", "$serverPid") | Out-Null
        $token = Invoke-Launcher @("--creation-token", "$serverPid")
        if ($token.Code -eq 0) {
            Invoke-Launcher @(
                "--terminate", "$serverPid", "--token", $token.Text, $server
            ) | Out-Null
        }
    }
}
