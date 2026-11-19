# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Build TclTLS 1.7.22 for the Git ucrt64 tclsh (package require tls 1.6).
# Installs under tests/windows/.tcltls, which is gitignored. The DLL links
# the vcpkg OpenSSL used by redis-server, and those OpenSSL DLLs are copied
# beside tcltls.dll so Tcl's loader finds that build.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Tclsh
)

$ErrorActionPreference = "Stop"
$BuildId = "1"
$TlsVersion = "1.7.22"
$Cache = Join-Path $PSScriptRoot ".tcltls"
$LibDir = Join-Path $Cache "lib"
$PkgDir = Join-Path $LibDir "tls$TlsVersion"
$SrcDir = Join-Path $Cache "src"
$ObjDir = Join-Path $Cache "build"

function Format-TclLibPath([string]$Path) {
    $p = $Path -replace '\\', '/'
    if ($p -match '[\s\{\}]') { return "{$p}" }
    return $p
}

function Test-TclTlsReady {
    $idFile = Join-Path $PkgDir "build-id.txt"
    foreach ($n in @("tcltls.dll", "tls.tcl", "pkgIndex.tcl", "libssl-3-x64.dll", "libcrypto-3-x64.dll")) {
        if (-not (Test-Path (Join-Path $PkgDir $n))) { return $false }
    }
    $id = (Get-Content $idFile -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($id -ne $BuildId) { return $false }
    $env:TCLLIBPATH = Format-TclLibPath $LibDir
    $probe = Join-Path $Cache "probe.tcl"
    @"
if {[catch {package require tls 1.6} err]} {
    puts stderr `"REQUIRE-FAIL `$err`"
    exit 1
}
exit 0
"@ | Set-Content -Path $probe -Encoding ascii
    $pref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Tclsh $probe
    $code = $LASTEXITCODE
    $ErrorActionPreference = $pref
    return $code -eq 0
}

if (Test-TclTlsReady) { exit 0 }

$clang = "C:\Program Files\LLVM\bin\clang-cl.exe"
$bash = "C:\Program Files\Git\bin\bash.exe"
if (-not (Test-Path $clang)) { throw "clang-cl not found at $clang" }
if (-not (Test-Path $bash)) { throw "Git bash not found at $bash" }
if (-not (Test-Path $Tclsh)) { throw "tclsh not found at $Tclsh" }

$sslInc = Join-Path $BuildDir "vcpkg_installed\x64-windows\include"
$sslLib = Join-Path $BuildDir "vcpkg_installed\x64-windows\lib"
$sslBin = Join-Path $BuildDir "vcpkg_installed\x64-windows\bin"
foreach ($f in @(
        (Join-Path $sslInc "openssl\ssl.h"),
        (Join-Path $sslLib "libssl.lib"),
        (Join-Path $sslLib "libcrypto.lib"),
        (Join-Path $sslBin "libssl-3-x64.dll"),
        (Join-Path $sslBin "libcrypto-3-x64.dll")
    )) {
    if (-not (Test-Path $f)) {
        throw "missing $f (build redis-server with the default TLS/vcpkg OpenSSL first)"
    }
}

New-Item -ItemType Directory -Force -Path $SrcDir, $ObjDir, $PkgDir | Out-Null
$log = Join-Path $ObjDir "build.log"
Set-Content -Path $log -Value "" -Encoding ascii

function Get-CachedArchive([string]$Dest, [string]$Url, [string]$Label) {
    if (Test-Path $Dest) { return }
    Write-Host "== downloading $Label =="
    & curl.exe -fsSL -L -o $Dest $Url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Dest)) {
        Remove-Item $Dest -ErrorAction SilentlyContinue
        throw "download failed: $Url"
    }
}

$tlsTar = Join-Path $SrcDir "tcltls-$TlsVersion.tar.gz"
$tclTar = Join-Path $SrcDir "tcl-core-8-6-18.tar.gz"
Get-CachedArchive $tlsTar "https://core.tcl-lang.org/tcltls/uv/tcltls-$TlsVersion.tar.gz" "TclTLS $TlsVersion"
Get-CachedArchive $tclTar "https://github.com/tcltk/tcl/archive/refs/tags/core-8-6-18.tar.gz" "Tcl 8.6.18 headers"

$tlsSrc = Join-Path $SrcDir "tcltls-$TlsVersion"
$tclSrc = Join-Path $SrcDir "tcl-core-8-6-18"
if (-not (Test-Path (Join-Path $tlsSrc "tls.c"))) {
    & tar.exe -xzf $tlsTar -C $SrcDir
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $tlsTar" }
}
if (-not (Test-Path (Join-Path $tclSrc "generic\tclStubLib.c"))) {
    & tar.exe -xzf $tclTar -C $SrcDir
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $tclTar" }
}

$dh = Join-Path $tlsSrc "dh_params.h"
if (-not (Test-Path $dh)) {
    Write-Host "== generating TclTLS DH params =="
    Push-Location $tlsSrc
    & $bash --noprofile --norc -c "./gen_dh_params fallback > dh_params.h"
    $dhCode = $LASTEXITCODE
    Pop-Location
    if ($dhCode -ne 0 -or -not (Test-Path $dh)) { throw "gen_dh_params fallback failed" }
}

$tclH = Join-Path $tlsSrc "tls.tcl.h"
if (-not (Test-Path $tclH)) {
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $tlsSrc "tls.tcl"))
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("/* generated from tls.tcl */")
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if (($i % 16) -eq 0) { [void]$sb.Append("`n    ") }
        [void]$sb.AppendFormat("0x{0:x2}, ", $bytes[$i])
    }
    [System.IO.File]::WriteAllText($tclH, $sb.ToString())
}

function Invoke-Clang([string[]]$ClangArgs) {
    # clang-cl writes deprecation notes to stderr. Merging that stream must
    # not abort the build when the compiler still exits 0.
    $pref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & $clang @ClangArgs 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $pref
    $out | Out-File -Append -FilePath $log -Encoding utf8
    if ($code -ne 0) {
        $out | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
        throw "clang-cl failed ($code); see $log"
    }
}

Write-Host "== building TclTLS $TlsVersion =="
$verHeader = Join-Path $ObjDir "pkgver.h"
Set-Content -Path $verHeader -Value "#define PACKAGE_VERSION `"$TlsVersion`"" -Encoding ascii
$tclInc = @(
    "/I", (Join-Path $tclSrc "generic"),
    "/I", (Join-Path $tclSrc "win")
)
Invoke-Clang (@(
    "/nologo", "/c", "/O2", "/MD", "/W1",
    "/DUSE_TCL_STUBS", "/DTCL_THREADS=1", "/D_WIN32", "/DWIN32",
    "/Fo$(Join-Path $ObjDir 'tclStubLib.obj')"
) + $tclInc + @((Join-Path $tclSrc "generic\tclStubLib.c")))

$tlsFlags = @(
    "/nologo", "/c", "/O2", "/MD", "/W1",
    "/DUSE_TCL_STUBS", "/FI$verHeader",
    "/D__WIN32__", "/D_WIN32", "/DWIN32",
    "/DNO_SSL2", "/DNO_SSL3", "/DHAVE_TLS_METHOD",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/I", $tlsSrc,
    "/I", $sslInc
) + $tclInc
foreach ($c in @("tls.c", "tlsBIO.c", "tlsIO.c", "tlsX509.c")) {
    $obj = Join-Path $ObjDir ([IO.Path]::GetFileNameWithoutExtension($c) + ".obj")
    Invoke-Clang ($tlsFlags + @("/Fo$obj", (Join-Path $tlsSrc $c)))
}

$def = Join-Path $ObjDir "tcltls.def"
@"
LIBRARY tcltls
EXPORTS
    Tls_Init
    Tls_SafeInit
"@ | Set-Content -Path $def -Encoding ascii
$dll = Join-Path $ObjDir "tcltls.dll"
Invoke-Clang @(
    "/nologo", "/LD", "/MD", "/Fe$dll",
    (Join-Path $ObjDir "tclStubLib.obj"),
    (Join-Path $ObjDir "tls.obj"),
    (Join-Path $ObjDir "tlsBIO.obj"),
    (Join-Path $ObjDir "tlsIO.obj"),
    (Join-Path $ObjDir "tlsX509.obj"),
    "/link", "/DEF:$def", "/LIBPATH:$sslLib",
    "libssl.lib", "libcrypto.lib",
    "ws2_32.lib", "crypt32.lib", "user32.lib", "advapi32.lib"
)

Copy-Item $dll (Join-Path $PkgDir "tcltls.dll") -Force
Copy-Item (Join-Path $sslBin "libssl-3-x64.dll") (Join-Path $PkgDir "libssl-3-x64.dll") -Force
Copy-Item (Join-Path $sslBin "libcrypto-3-x64.dll") (Join-Path $PkgDir "libcrypto-3-x64.dll") -Force
Copy-Item (Join-Path $tlsSrc "tls.tcl") (Join-Path $PkgDir "tls.tcl") -Force
@"
package ifneeded tls $TlsVersion [list apply {{dir} {
    load [file join `$dir tcltls.dll] Tls
    set tlsTclInitScript [file join `$dir tls.tcl]
    if {[file exists `$tlsTclInitScript]} {
        source `$tlsTclInitScript
    }
}} `$dir]
"@ | Set-Content -Path (Join-Path $PkgDir "pkgIndex.tcl") -Encoding ascii
Set-Content -Path (Join-Path $PkgDir "build-id.txt") -Value $BuildId -Encoding ascii

if (-not (Test-TclTlsReady)) {
    throw "TclTLS built but ``package require tls 1.6`` failed in $Tclsh"
}
Write-Host "tcltls $TlsVersion ready ($PkgDir)"
