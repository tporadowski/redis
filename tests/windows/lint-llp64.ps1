# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 10.4: regression net for LLP64 landmines (sizeof(long), clzl, PORT_LONG).
# First-wave fixes were M1 (arch_bits). This is not a long-rename campaign.
param(
    [string]$SourceDir = "",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $SourceDir) {
    $SourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
$SourceDir = (Resolve-Path $SourceDir).Path
if (-not $BuildDir) {
    $cand = Join-Path $SourceDir "build"
    if (Test-Path $cand) { $BuildDir = (Resolve-Path $cand).Path }
}

$allowPath = Join-Path $PSScriptRoot "llp64-allow.txt"
$allow = @()
Get-Content $allowPath | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { return }
    $parts = $line.Split("|", 2)
    if ($parts.Count -ne 2) { throw "bad allow-list line: $line" }
    $allow += [pscustomobject]@{ Rel = $parts[0].Replace("\", "/"); Snip = $parts[1] }
}

function Test-Allowed([string]$rel, [string]$text) {
    foreach ($a in $allow) {
        if ($a.Rel -eq $rel -and $text.Contains($a.Snip)) { return $true }
    }
    return $false
}

$roots = @(
    (Join-Path $SourceDir "src")
)
$hits = New-Object System.Collections.Generic.List[string]
$forbidArch = [regex]"arch_bits\s*=\s*.*sizeof\s*\(\s*long\s*\)"
$patSizeof = [regex]"sizeof\s*\(\s*long\s*\)"
$patClzl = [regex]"__builtin_clzl\s*\("
$patPort = [regex]"\bPORT_LONG(?:LONG)?\b"
$patDefLong = [regex]"#\s*define\s+long\b"

foreach ($root in $roots) {
    Get-ChildItem -Path $root -Recurse -Include *.c,*.h,*.cpp | ForEach-Object {
        $rel = $_.FullName.Substring($SourceDir.Length).TrimStart("\", "/").Replace("\", "/")
        $n = 0
        foreach ($raw in [IO.File]::ReadAllLines($_.FullName)) {
            $n++
            if ($forbidArch.IsMatch($raw)) {
                $hits.Add("${rel}:${n}: FORBIDDEN arch_bits from sizeof(long): $raw")
                continue
            }
            if ($patDefLong.IsMatch($raw)) {
                $hits.Add("${rel}:${n}: FORBIDDEN #define long: $raw")
                continue
            }
            $need = $false
            if ($patSizeof.IsMatch($raw)) { $need = $true }
            if ($patClzl.IsMatch($raw)) { $need = $true }
            if ($patPort.IsMatch($raw)) { $need = $true }
            if (-not $need) { continue }
            if (Test-Allowed $rel $raw) { continue }
            $hits.Add("${rel}:${n}: $raw")
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "LLP64 lint failed ($($hits.Count) unallowed hits):"
    $hits | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "ok LLP64 lint (no unallowed sizeof(long) / clzl / PORT_LONG)"

if ($BuildDir -and (Test-Path (Join-Path $BuildDir "redis-server.exe")) -and
    (Test-Path (Join-Path $BuildDir "redis-cli.exe"))) {
    $cli = Join-Path $BuildDir "redis-cli.exe"
    $srv = Join-Path $BuildDir "redis-server.exe"
    $port = 26386
    $work = Join-Path $BuildDir "lint_llp64_work"
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
"@ | Set-Content $conf -Encoding ASCII
    $proc = $null
    try {
        $proc = Start-Process -FilePath $srv -ArgumentList @($conf) -PassThru -WindowStyle Hidden
        $info = $null
        for ($i = 0; $i -lt 40; $i++) {
            try { $info = & $cli -p $port INFO server 2>$null } catch {}
            if ($info -match "arch_bits:64") { break }
            Start-Sleep -Milliseconds 100
        }
        $joined = if ($info -is [array]) { $info -join "`n" } else { [string]$info }
        if ($joined -notmatch "arch_bits:64") {
            throw "arch_bits is not 64:`n$joined"
        }
        Write-Host "ok INFO arch_bits:64"
    } finally {
        if ($proc -and -not $proc.HasExited) {
            try { & $cli -p $port SHUTDOWN NOSAVE | Out-Null } catch {}
            if (-not $proc.WaitForExit(3000)) { Stop-Process -Id $proc.Id -Force }
        }
    }
}

Write-Host "lint_llp64 ok"
