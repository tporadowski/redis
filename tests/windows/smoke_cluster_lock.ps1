# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Two cluster nodes cannot share one nodes.conf; lock lives on nodes.conf.lock.
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

$work = Join-Path $BuildDir "smoke_cluster_lock_work"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

function Wait-Ping {
    param([int]$Port, [int]$Tries = 50)
    for ($i = 0; $i -lt $Tries; $i++) {
        try {
            $pong = & $cli -p $Port PING 2>&1 | Out-String
            if ($pong.Trim() -eq "PONG") { return $true }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Write-NodeConf {
    param([string]$Dir, [int]$Port, [string]$NodesPath, [string]$LogPath)
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    $conf = Join-Path $Dir "redis.conf"
    @"
port $Port
bind 127.0.0.1
protected-mode no
dir $Dir
logfile $LogPath
cluster-enabled yes
cluster-config-file $NodesPath
cluster-node-timeout 2000
save ""
appendonly no
"@ | Set-Content $conf -Encoding ASCII
    return $conf
}

$portA = 27101
$portB = 27102
$dirA = Join-Path $work "a"
$dirB = Join-Path $work "b"
$nodes = Join-Path $dirA "nodes.conf"
$lock = "$nodes.lock"
$logA = Join-Path $dirA "redis.log"
$logB = Join-Path $dirB "redis.log"
$confA = Write-NodeConf -Dir $dirA -Port $portA -NodesPath $nodes -LogPath $logA
$confB = Write-NodeConf -Dir $dirB -Port $portB -NodesPath $nodes -LogPath $logB

$procA = $null
$procB = $null
try {
    $procA = Start-Process -FilePath $server -ArgumentList @($confA) -PassThru -WindowStyle Hidden
    if (-not (Wait-Ping -Port $portA)) { throw "node A did not become ready" }
    if (-not (Test-Path $lock)) { throw "missing sidecar lock $lock" }

    $procB = Start-Process -FilePath $server -ArgumentList @($confB) -PassThru -WindowStyle Hidden
    $secondReady = Wait-Ping -Port $portB -Tries 20
    if ($secondReady) {
        throw "second node started on a nodes.conf that is already locked"
    }
    if (-not $procB.WaitForExit(8000)) {
        throw "second node did not exit after failing to take the lock"
    }
    $logText = ""
    if (Test-Path $logB) { $logText = [IO.File]::ReadAllText($logB) }
    if ($logText -notmatch "already used by a different Redis Cluster node") {
        throw "second node log missing lock conflict:`n$logText"
    }

    $meet = & $cli -p $portA CLUSTER MYID 2>&1 | Out-String
    if ($meet.Trim().Length -lt 40) { throw "node A MYID failed: $meet" }
    if (-not (Test-Path $lock)) { throw "sidecar lock disappeared after CLUSTER use" }

    & $cli -p $portA SHUTDOWN NOSAVE | Out-Null
    if (-not $procA.WaitForExit(8000)) {
        throw "node A did not exit after SHUTDOWN"
    }
    $procA = $null

    $procB = Start-Process -FilePath $server -ArgumentList @($confB) -PassThru -WindowStyle Hidden
    if (-not (Wait-Ping -Port $portB)) {
        $logText = ""
        if (Test-Path $logB) { $logText = [IO.File]::ReadAllText($logB) }
        throw "node B did not start after A released the lock:`n$logText"
    }
    Write-Host "smoke_cluster_lock ok"
} finally {
    foreach ($item in @(
        @{ Proc = $procB; Port = $portB },
        @{ Proc = $procA; Port = $portA }
    )) {
        if ($item.Proc -and -not $item.Proc.HasExited) {
            try { & $cli -p $item.Port SHUTDOWN NOSAVE | Out-Null } catch {}
            if (-not $item.Proc.WaitForExit(3000)) {
                Stop-Process -Id $item.Proc.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
