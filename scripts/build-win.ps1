# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Configure and build win-8.10 with clang-cl + Ninja (VS 2022+ SDK).
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with C++ tools not found" }

$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

$clang = $null
foreach ($c in @(
        "C:\Program Files\LLVM\bin\clang-cl.exe",
        (Join-Path $vs "VC\Tools\Llvm\x64\bin\clang-cl.exe")
    )) {
    if (Test-Path $c) { $clang = $c; break }
}
if (-not $clang) {
    Write-Warning "clang-cl not found; falling back to MSVC cl.exe"
}

$buildPath = Join-Path $Root $BuildDir
$quoted = {
    param($s)
    if ($s -match '[\s"]') { return '"' + ($s -replace '"', '\"') + '"' }
    return $s
}
$cmakeArgs = @(
    "-S", $Root,
    "-B", $buildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_MAKE_PROGRAM=$ninja"
)
if ($clang) {
    $cmakeArgs += "-DCMAKE_C_COMPILER=$clang"
    $cmakeArgs += "-DCMAKE_CXX_COMPILER=$clang"
}
$cmakeArgStr = ($cmakeArgs | ForEach-Object { & $quoted $_ }) -join ' '
$cmd = "`"$vcvars`" && cmake $cmakeArgStr && cmake --build `"$buildPath`" --config $Config"
Write-Host $cmd
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built $(Join-Path $Root $BuildDir)\redis-cli.exe"
