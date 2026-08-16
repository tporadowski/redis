# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# M7 smoke (7.2): 3-node CLUSTER MEET + slot assign + replica failover.
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

$work = Join-Path $BuildDir "smoke_cluster_work"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$ports = @(27001, 27002, 27003)
$replicaPort = 27004
$procs = @()

function Invoke-Redis {
    param([int]$Port, [string[]]$RedisArgs)
    $out = & $cli -p $Port @RedisArgs 2>&1 | Out-String
    return $out.Trim()
}

function Wait-Ping {
    param([int]$Port, [int]$Tries = 50)
    for ($i = 0; $i -lt $Tries; $i++) {
        try {
            if ((Invoke-Redis -Port $Port -RedisArgs @("PING")) -eq "PONG") { return }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    throw "node on $Port did not become ready"
}

function Start-ClusterNode {
    param([int]$Port, [switch]$Replica)
    $dir = Join-Path $work "n$Port"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $conf = Join-Path $dir "redis.conf"
    $log  = Join-Path $dir "redis.log"
    $nodes = Join-Path $dir "nodes.conf"
    @"
port $Port
bind 127.0.0.1
protected-mode no
dir $dir
logfile $log
cluster-enabled yes
cluster-config-file $nodes
cluster-node-timeout 2000
cluster-announce-ip 127.0.0.1
cluster-announce-port $Port
save ""
appendonly no
"@ | Set-Content -Path $conf -Encoding ASCII
    $p = Start-Process -FilePath $server -ArgumentList @($conf) -PassThru -WindowStyle Hidden
    $script:procs += $p
    Wait-Ping -Port $Port
    $mode = Invoke-Redis -Port $Port -RedisArgs @("INFO", "server")
    if ($mode -notmatch "redis_mode:cluster") {
        throw "port $Port not in cluster mode:`n$mode"
    }
    Write-Host "ok node $Port up (cluster)"
}

try {
    foreach ($p in $ports) { Start-ClusterNode -Port $p }

    $meet2 = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "MEET", "127.0.0.1", "27002")
    $meet3 = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "MEET", "127.0.0.1", "27003")
    if ($meet2 -ne "OK") { throw "CLUSTER MEET 27002: $meet2" }
    if ($meet3 -ne "OK") { throw "CLUSTER MEET 27003: $meet3" }
    Write-Host "ok CLUSTER MEET"

    $known = $false
    for ($i = 0; $i -lt 40; $i++) {
        $info = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "INFO")
        if ($info -match "cluster_known_nodes:3") { $known = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $known) {
        $nodes = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "NODES")
        throw "expected 3 known nodes:`n$info`n$nodes"
    }
    Write-Host "ok cluster_known_nodes:3"

    $ids = @{}
    foreach ($p in $ports) {
        $id = Invoke-Redis -Port $p -RedisArgs @("CLUSTER", "MYID")
        if ($id.Length -lt 40) { throw "bad MYID on $p : $id" }
        $ids[$p] = $id
    }

    $null = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "ADDSLOTSRANGE", "0", "5460")
    $null = Invoke-Redis -Port 27002 -RedisArgs @("CLUSTER", "ADDSLOTSRANGE", "5461", "10922")
    $null = Invoke-Redis -Port 27003 -RedisArgs @("CLUSTER", "ADDSLOTSRANGE", "10923", "16383")

    $ok = $false
    for ($i = 0; $i -lt 40; $i++) {
        $info = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "INFO")
        if ($info -match "cluster_state:ok") { $ok = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ok) { throw "cluster_state not ok:`n$info" }
    Write-Host "ok cluster_state:ok"

    Start-ClusterNode -Port $replicaPort
    $meet4 = Invoke-Redis -Port 27001 -RedisArgs @("CLUSTER", "MEET", "127.0.0.1", "$replicaPort")
    if ($meet4 -ne "OK") { throw "CLUSTER MEET $replicaPort : $meet4" }
    $sawMaster = $false
    for ($i = 0; $i -lt 40; $i++) {
        $nodes = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "NODES")
        if ($nodes -match $ids[27001]) { $sawMaster = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $sawMaster) { throw "replica never learned master id:`n$nodes" }
    $repl = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "REPLICATE", $ids[27001])
    if ($repl -ne "OK") { throw "CLUSTER REPLICATE: $repl" }

    $role = $false
    for ($i = 0; $i -lt 60; $i++) {
        $nodes = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "NODES")
        if ($nodes -match "myself,slave" -or $nodes -match "myself,replica") {
            $role = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $role) { throw "replica did not join:`n$nodes" }
    Write-Host "ok replica joined $($ids[27001])"

    $fo = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "FAILOVER")
    if ($fo -ne "OK") {
        Write-Host "CLUSTER FAILOVER: $fo (trying TAKEOVER)"
        $fo = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "FAILOVER", "TAKEOVER")
        if ($fo -ne "OK") { throw "CLUSTER FAILOVER TAKEOVER: $fo" }
    }

    $promoted = $false
    for ($i = 0; $i -lt 40; $i++) {
        $nodes = Invoke-Redis -Port $replicaPort -RedisArgs @("CLUSTER", "NODES")
        if ($nodes -match "myself,master") { $promoted = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $promoted) { throw "failover did not promote replica:`n$nodes" }
    Write-Host "ok failover: $replicaPort is master"

    $set = Invoke-Redis -Port 27002 -RedisArgs @("SET", "k", "v")
    if ($set -ne "OK") { throw "SET after failover: $set" }
    Write-Host "ok SET after failover"
} finally {
    foreach ($p in $procs) {
        if ($p -and -not $p.HasExited) {
            try { Invoke-Redis -Port 27001 -RedisArgs @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
            try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
        }
    }
    foreach ($port in @($ports + $replicaPort)) {
        try { Invoke-Redis -Port $port -RedisArgs @("SHUTDOWN", "NOSAVE") | Out-Null } catch {}
    }
}

Write-Host "smoke_cluster ok"
