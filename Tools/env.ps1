<#
.SYNOPSIS
    Shared environment detection for all WinMali build/test scripts.

.DESCRIPTION
    Dot-source this file from other scripts:

        . "$PSScriptRoot\env.ps1"

    After dot-sourcing, these variables are set:
        $WinMaliRoot          - full path to WinMali-rk3588 repo root
        $WinMaliMsBuild       - full path to MSBuild.exe, or $null
        $WinMaliClExe         - path to cl.exe for MSVC preflight, or $null
        $WinMaliWdkRoot       - path to installed WDK (Windows Kits\10), or $null
        $WinMaliWdkVersion    - e.g. "10.0.26100.0", or $null
        $WinMaliPython        - path to Python 3, or $null
        $WinMaliMesaRoot      - path to mesa\ (for shader assembler), or $null

    Nothing throws on missing pieces - callers decide what is fatal.
#>

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Repo root
# ---------------------------------------------------------------------------
$script:WinMaliRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# ---------------------------------------------------------------------------
# Visual Studio + MSBuild
# ---------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$script:WinMaliMsBuild = $null
$script:WinMaliClExe   = $null

if (Test-Path $vswhere) {
    # NOTE: vswhere's '-products *' is special-cased CLI syntax. Many
    # PowerShell hosts (esp. non-interactive -NoProfile) expand bare '*'
    # as filesystem glob and strip it, leaving vswhere with an empty
    # product filter and no match. Work around by using -all instead,
    # which doesn't require a product filter at all and still honours
    # -requires/-latest after the fact.
    $vsRoot = & $vswhere -all -prerelease -requires Microsoft.Component.MSBuild -property installationPath 2>$null `
              | Select-Object -First 1
    if ($vsRoot) {
        # -find honours -all; avoid -latest for the same reason as above.
        $mb = & $vswhere -all -prerelease -find "MSBuild\**\Bin\MSBuild.exe" 2>$null `
              | Where-Object { $_ -like "*$vsRoot*" } `
              | Select-Object -First 1
        if (-not $mb) {
            $mb = & $vswhere -all -prerelease -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
        }
        if ($mb -and (Test-Path $mb)) { $script:WinMaliMsBuild = $mb }

        # Find the ARM64 host-x64 cl.exe for preflight. Any toolset works - we
        # only use it for -Zs (syntax check).
        $hostCl = Get-ChildItem (Join-Path $vsRoot "VC\Tools\MSVC") -Filter "cl.exe" -Recurse -ErrorAction SilentlyContinue `
                  | Where-Object { $_.FullName -like "*\Hostx64\x64\cl.exe" } `
                  | Sort-Object FullName -Descending `
                  | Select-Object -First 1
        if ($hostCl) { $script:WinMaliClExe = $hostCl.FullName }
    }
}

# ---------------------------------------------------------------------------
# WDK
# ---------------------------------------------------------------------------
$script:WinMaliWdkRoot    = $null
$script:WinMaliWdkVersion = $null

$kitsReg = Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots" -ErrorAction SilentlyContinue
if (-not $kitsReg) {
    $kitsReg = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -ErrorAction SilentlyContinue
}
if ($kitsReg -and $kitsReg.KitsRoot10) {
    $kitsRoot = $kitsReg.KitsRoot10
    if (Test-Path (Join-Path $kitsRoot "Include")) {
        $script:WinMaliWdkRoot = $kitsRoot
        $versions = Get-ChildItem (Join-Path $kitsRoot "Include") -Directory `
                    | Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } `
                    | Sort-Object Name -Descending
        foreach ($v in $versions) {
            # Only accept a version that actually has WDK/km headers.
            if (Test-Path (Join-Path $v.FullName "km\ntddk.h")) {
                $script:WinMaliWdkVersion = $v.Name
                break
            }
        }
    }
}

# ---------------------------------------------------------------------------
# Python (for the Valhall assembler)
# ---------------------------------------------------------------------------
$script:WinMaliPython = $null
$cmd = Get-Command python -ErrorAction SilentlyContinue
if ($cmd) { $script:WinMaliPython = $cmd.Source }

# ---------------------------------------------------------------------------
# Mesa checkout (assembler lives under src/panfrost/compiler/bifrost/valhall)
# ---------------------------------------------------------------------------
$script:WinMaliMesaRoot = $null
$candidate = Join-Path (Split-Path -Parent $WinMaliRoot) "mesa"
if (Test-Path (Join-Path $candidate "src\panfrost\compiler\bifrost\valhall\asm.py")) {
    $script:WinMaliMesaRoot = $candidate
}

# ---------------------------------------------------------------------------
# Pretty summary (PowerShell 5.1 compatible - no ?? operator).
# ---------------------------------------------------------------------------
function _WinMaliOr($value, $fallback) {
    if ([string]::IsNullOrEmpty($value)) { return $fallback } else { return $value }
}

function Show-WinMaliEnv {
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host "WinMali environment" -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host ("  Repo root   : {0}" -f $script:WinMaliRoot)
    Write-Host ("  MSBuild     : {0}" -f (_WinMaliOr $script:WinMaliMsBuild    '<not found>'))
    Write-Host ("  cl.exe      : {0}" -f (_WinMaliOr $script:WinMaliClExe      '<not found>'))
    Write-Host ("  WDK root    : {0}" -f (_WinMaliOr $script:WinMaliWdkRoot    '<not installed>'))
    Write-Host ("  WDK version : {0}" -f (_WinMaliOr $script:WinMaliWdkVersion '<n/a>'))
    Write-Host ("  Python      : {0}" -f (_WinMaliOr $script:WinMaliPython     '<not found>'))
    Write-Host ("  Mesa root   : {0}" -f (_WinMaliOr $script:WinMaliMesaRoot   '<not found>'))
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
}
