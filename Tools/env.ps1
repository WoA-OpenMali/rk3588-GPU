<#
.SYNOPSIS
    Shared environment detection for every WinMali build/test script.

.DESCRIPTION
    Dot-source from another script:

        . "$PSScriptRoot\env.ps1"

    After dot-sourcing, these variables are set in the caller's scope:

        $WinMaliRoot         - <repo>\rk3588-GPU
        $WinMaliRepoRoot     - <repo> (one level up; holds build/ and mesa/)
        $WinMaliBuildDir     - <repo>\build
        $WinMaliMesaRoot     - <repo>\mesa  (or $null if missing)
        $WinMaliMsBuild      - path to MSBuild.exe (or $null)
        $WinMaliClExe        - path to host x64 cl.exe (or $null)
        $WinMaliWdkRoot      - Windows Kits root (or $null)
        $WinMaliWdkVersion   - e.g. "10.0.26100.0" (or $null)
        $WinMaliPython       - path to a Python 3 interpreter (or $null)
        $WinMaliMeson        - path to meson.exe (or $null)
        $WinMaliNinja        - path to ninja.exe (or $null)

    Nothing throws on a missing tool; callers decide what is fatal.
    Call Show-WinMaliEnv to print a one-screen summary.
#>

$ErrorActionPreference = "Stop"

# --------------------------------------------------------------------------
# Repo roots
# --------------------------------------------------------------------------
$script:WinMaliRoot     = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:WinMaliRepoRoot = (Resolve-Path (Join-Path $WinMaliRoot "..")).Path
$script:WinMaliBuildDir = Join-Path $WinMaliRepoRoot "build"

$mesaCandidate = Join-Path $WinMaliRepoRoot "mesa"
$script:WinMaliMesaRoot = if (Test-Path (Join-Path $mesaCandidate "meson.build")) { $mesaCandidate } else { $null }

# --------------------------------------------------------------------------
# Visual Studio + MSBuild
# --------------------------------------------------------------------------
$script:WinMaliMsBuild = $null
$script:WinMaliClExe   = $null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    # NB: vswhere's `-products *` is special-cased CLI syntax; on a
    # -NoProfile shell PowerShell expands the bare * as a glob and
    # strips it. `-all` sidesteps that and we still filter post-facto.
    $vsRoot = & $vswhere -all -prerelease -requires Microsoft.Component.MSBuild -property installationPath 2>$null |
              Select-Object -First 1
    if ($vsRoot) {
        $mb = & $vswhere -all -prerelease -find "MSBuild\**\Bin\MSBuild.exe" 2>$null |
              Where-Object { $_ -like "*$vsRoot*" } |
              Select-Object -First 1
        if (-not $mb) {
            $mb = & $vswhere -all -prerelease -find "MSBuild\**\Bin\MSBuild.exe" 2>$null |
                  Select-Object -First 1
        }
        if ($mb -and (Test-Path $mb)) { $script:WinMaliMsBuild = $mb }

        # Host x64 cl.exe is fine for any preflight syntax-check we want
        # to add later. The cross-compilers (arm64-on-x64) live under
        # the same MSVC version directory.
        $hostCl = Get-ChildItem (Join-Path $vsRoot "VC\Tools\MSVC") -Filter "cl.exe" -Recurse -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -like "*\Hostx64\x64\cl.exe" } |
                  Sort-Object FullName -Descending |
                  Select-Object -First 1
        if ($hostCl) { $script:WinMaliClExe = $hostCl.FullName }
    }
}

# --------------------------------------------------------------------------
# WDK
# --------------------------------------------------------------------------
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
        $versions = Get-ChildItem (Join-Path $kitsRoot "Include") -Directory |
                    Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } |
                    Sort-Object Name -Descending
        foreach ($v in $versions) {
            # Only accept a version that actually shipped the WDK km headers.
            if (Test-Path (Join-Path $v.FullName "km\ntddk.h")) {
                $script:WinMaliWdkVersion = $v.Name
                break
            }
        }
    }
}

# --------------------------------------------------------------------------
# Python + meson + ninja (for the mesa build)
# --------------------------------------------------------------------------
$script:WinMaliPython = $null
$script:WinMaliMeson  = $null
$script:WinMaliNinja  = $null

$cmd = Get-Command python -ErrorAction SilentlyContinue
if ($cmd) { $script:WinMaliPython = $cmd.Source }

$cmd = Get-Command meson -ErrorAction SilentlyContinue
if ($cmd) { $script:WinMaliMeson = $cmd.Source }

$cmd = Get-Command ninja -ErrorAction SilentlyContinue
if ($cmd) { $script:WinMaliNinja = $cmd.Source }

# Fall back to Python's user-scripts dir for tools installed via
# `pip install --user meson` (or just `pip install meson` on systems
# where pip defaults to --user). This is the common state for fresh
# dev boxes that haven't added that path to PATH.
if (-not $script:WinMaliMeson -and $script:WinMaliPython) {
    try {
        $userScripts = & $script:WinMaliPython -c "import sysconfig, sys; sys.stdout.write(sysconfig.get_path('scripts', 'nt_user'))" 2>$null
    } catch { $userScripts = $null }

    if ($userScripts) {
        $candidate = Join-Path $userScripts "meson.exe"
        if (Test-Path $candidate) { $script:WinMaliMeson = $candidate }
        if (-not $script:WinMaliNinja) {
            $candidateNinja = Join-Path $userScripts "ninja.exe"
            if (Test-Path $candidateNinja) { $script:WinMaliNinja = $candidateNinja }
        }
    }
}

# --------------------------------------------------------------------------
# Pretty summary (PowerShell 5.1 compatible, no ?? operator)
# --------------------------------------------------------------------------
function _WinMaliOr($value, $fallback) {
    if ([string]::IsNullOrEmpty($value)) { return $fallback } else { return $value }
}

function Show-WinMaliEnv {
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host "WinMali environment" -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host ("  Repo root      : {0}" -f $script:WinMaliRepoRoot)
    Write-Host ("  Driver tree    : {0}" -f $script:WinMaliRoot)
    Write-Host ("  Build output   : {0}" -f $script:WinMaliBuildDir)
    Write-Host ("  Mesa source    : {0}" -f (_WinMaliOr $script:WinMaliMesaRoot    '<not found>'))
    Write-Host ("  MSBuild        : {0}" -f (_WinMaliOr $script:WinMaliMsBuild     '<not found>'))
    Write-Host ("  cl.exe         : {0}" -f (_WinMaliOr $script:WinMaliClExe       '<not found>'))
    Write-Host ("  WDK root       : {0}" -f (_WinMaliOr $script:WinMaliWdkRoot     '<not installed>'))
    Write-Host ("  WDK version    : {0}" -f (_WinMaliOr $script:WinMaliWdkVersion  '<n/a>'))
    Write-Host ("  Python         : {0}" -f (_WinMaliOr $script:WinMaliPython      '<not found>'))
    Write-Host ("  meson          : {0}" -f (_WinMaliOr $script:WinMaliMeson       '<not found>'))
    Write-Host ("  ninja          : {0}" -f (_WinMaliOr $script:WinMaliNinja       '<not found>'))
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
}
