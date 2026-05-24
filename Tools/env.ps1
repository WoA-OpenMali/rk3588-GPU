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

# Prefer <repo>\mesa-modern\mesa over <repo>\mesa. The "modern" tree is the
# clean recent upstream clone with real v10/CSF + panvk-v10 support; the
# unsuffixed mesa\ is the legacy Mali-WoA branch we ported against
# previously, kept around for reference.
$script:WinMaliMesaRoot = $null
foreach ($cand in @(
    (Join-Path $WinMaliRepoRoot "mesa-modern\mesa"),
    (Join-Path $WinMaliRepoRoot "mesa-modern"),
    (Join-Path $WinMaliRepoRoot "mesa")
)) {
    if (Test-Path (Join-Path $cand "meson.build")) {
        $script:WinMaliMesaRoot = $cand
        break
    }
}

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

# Standalone LLVM: add its bin to PATH if it isn't already, so meson
# can discover lld-link.exe / llvm-rc.exe alongside clang-cl. The
# installer adds it to the *machine* PATH, but existing shells don't
# pick that up until restarted.
foreach ($llvmRoot in @("$env:ProgramFiles\LLVM", "${env:ProgramFiles(x86)}\LLVM")) {
    $llvmBin = Join-Path $llvmRoot "bin"
    if ((Test-Path (Join-Path $llvmBin "clang-cl.exe")) -and ($env:PATH -notlike "*$llvmBin*")) {
        $env:PATH = "$llvmBin;$env:PATH"
        break
    }
}

# win_flex_bison: mesa needs flex/bison-equivalents to build
# src/compiler/glsl. There is no first-party Windows package, so we
# probe a handful of common drop-in locations.
foreach ($wfbDir in @("C:\tools\winflexbison", "$env:ProgramFiles\winflexbison", "$env:LOCALAPPDATA\winflexbison")) {
    if ((Test-Path (Join-Path $wfbDir "win_flex.exe")) -and ($env:PATH -notlike "*$wfbDir*")) {
        $env:PATH = "$wfbDir;$env:PATH"
        break
    }
}

# pkg-config: our shim at C:\tools\pkgconfig\ (a small Python script
# that parses .pc files and answers the queries meson issues). meson
# requires a pkg-config binary on PATH even when --pkg-config-path is
# supplied; pkgconf isn't packaged for Windows separately so we ship
# a minimal stand-in.
if ((Test-Path "C:\tools\pkgconfig\pkg-config.bat") -and ($env:PATH -notlike "*C:\tools\pkgconfig*")) {
    $env:PATH = "C:\tools\pkgconfig;$env:PATH"
}

# mesa-deps (built by build-mesa-deps.ps1): puts llvm-config.exe,
# mesa_clc.exe, SPIRV-Tools binaries, libclc bitcode etc. on PATH.
# mesa's meson configure runs llvm-config to discover LLVM headers/libs;
# without bin/ on PATH it falls through to a subproject fallback that
# we don't ship.
if ((Test-Path "C:\mesa-deps\bin\llvm-config.exe") -and ($env:PATH -notlike "*C:\mesa-deps\bin*")) {
    $env:PATH = "C:\mesa-deps\bin;$env:PATH"
}

$cmd = Get-Command python -ErrorAction SilentlyContinue
if ($cmd) { $script:WinMaliPython = $cmd.Source }

# Fall back to standard per-user / machine-wide install dirs when a
# freshly-installed python hasn't propagated to the current shell's
# PATH yet. Prefer the newest 3.x found.
if (-not $script:WinMaliPython) {
    $pyCandidates = @()
    foreach ($root in @(
        "$env:LOCALAPPDATA\Programs\Python",
        "$env:ProgramFiles\Python",
        "${env:ProgramFiles(x86)}\Python"
    )) {
        if (Test-Path $root) {
            $pyCandidates += Get-ChildItem $root -Directory -Filter "Python3*" -EA SilentlyContinue |
                             ForEach-Object { Join-Path $_.FullName "python.exe" }
        }
    }
    # @( ... ) forces an array even when the pipeline yields a single
    # string — otherwise $pyCandidates[0] returns "C" (first char) when
    # only one path matches.
    $pyCandidates = @($pyCandidates |
                      Where-Object { Test-Path $_ } |
                      Sort-Object -Descending)
    if ($pyCandidates.Count -gt 0) { $script:WinMaliPython = $pyCandidates[0] }
}

# meson's find_program('python3') looks for a literal `python3.exe`,
# which the python.org Windows installer doesn't create (only
# python.exe + pythonw.exe + py.exe). Drop a sibling copy so meson is
# happy. Cheap (~20 MB copy) and only happens once.
if ($script:WinMaliPython) {
    $pyDir = Split-Path $script:WinMaliPython -Parent
    $py3   = Join-Path $pyDir "python3.exe"
    if (-not (Test-Path $py3)) {
        try { Copy-Item -Path $script:WinMaliPython -Destination $py3 -ErrorAction Stop } catch { }
    }
    if ($env:PATH -notlike "*$pyDir*") {
        $env:PATH = "$pyDir;$env:PATH"
    }
}

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
# VS Dev shell loader (shared between build.ps1 and build-mesa.ps1).
#
# VS 2017 v141 MSBuild does not put VC's lib\<arch>\ on LIB unless the
# Dev shell is loaded (whereas VS 2022 v143 props bake in absolute lib
# paths and link without it). Calling MSBuild.exe directly without
# loading the dev shell produces "LNK1104 cannot open MSVCRT*.lib" on
# v141. Always load it before building.
# --------------------------------------------------------------------------
function Enter-WinMaliVsDevEnv {
    [CmdletBinding()]
    param(
        [string]$Arch = "x64",
        [string]$HostArch = "x64",
        # SDK version VsDevCmd should pin to. Default matches the
        # toolchain target in every WinMali .vcxproj. Without this,
        # VsDevCmd picks the latest installed SDK (e.g. 19041 on this
        # box), which has no `d3d10umddi.h` and breaks the mesa build.
        [string]$WinSdk = "10.0.17763.0",
        # Path to a manually-installed VS (typically VS 2022 Build
        # Tools at C:\BuildTools) to use instead of whatever vswhere
        # returns. Used by the mesa build path which needs VS 2022's
        # STL to ABI-match LLVM 18.1.8 in C:\mesa-deps. Leave empty
        # for the default (vswhere finds VS 2017 for KMD builds).
        [string]$VsInstallOverride = ""
    )

    # Already inside a dev shell matching the requested arch? Don't
    # double-load. The arch match is critical: when the caller is
    # cross-compiling Win32 from a parent shell that previously set
    # up an x64 dev env, the inherited LIB has x64 paths only, and
    # link.exe / lld-link picks the wrong msvcrt.lib. Force a re-init
    # when VSCMD_ARG_TGT_ARCH doesn't match $Arch.
    if (($env:VSCMD_ARG_HOST_ARCH -or $env:VSINSTALLDIR) -and
        ($env:VSCMD_ARG_TGT_ARCH -eq $Arch)) {
        return $true
    }
    # If we're going to re-init, scrub the inherited LIB / INCLUDE /
    # PATH dev-shell pieces so the next VsDevCmd's output isn't
    # appended to a stale environment. Don't touch user PATH entries -
    # only the ones VsDevCmd would set.
    if ($env:VSCMD_ARG_TGT_ARCH -and ($env:VSCMD_ARG_TGT_ARCH -ne $Arch)) {
        $env:LIB     = ""
        $env:INCLUDE = ""
        $env:LIBPATH = ""
    }

    $vsRoot = $null
    if ($VsInstallOverride -and (Test-Path (Join-Path $VsInstallOverride "Common7\Tools\VsDevCmd.bat"))) {
        $vsRoot = $VsInstallOverride
    }

    if (-not $vsRoot) {
        $vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vsw)) {
            Write-Host "vswhere.exe not found at $vsw" -ForegroundColor Red
            return $false
        }

        $vsRoot = & $vsw -all -prerelease -requires Microsoft.Component.MSBuild -property installationPath 2>$null |
                  Select-Object -First 1
        if (-not $vsRoot) {
            Write-Host "vswhere found no VS installation with MSBuild." -ForegroundColor Red
            return $false
        }
    }

    # VS 2019+ ships Microsoft.VisualStudio.DevShell.dll for the
    # PowerShell-native Enter-VsDevShell. VS 2017 only ships
    # VsDevCmd.bat (cmd-only). Prefer the cmdlet, fall back to the
    # bat (importing its env vars into this process).
    $devShell = Join-Path $vsRoot "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (Test-Path $devShell) {
        Import-Module $devShell -ErrorAction Stop | Out-Null
        Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation `
            -DevCmdArguments "-arch=$Arch -host_arch=$HostArch -winsdk=$WinSdk" | Out-Null
        return $true
    }

    $vsDevCmd = Join-Path $vsRoot "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        Write-Host "Neither DevShell.dll nor VsDevCmd.bat found under $vsRoot" -ForegroundColor Red
        return $false
    }

    # Run VsDevCmd.bat in a child cmd and dump its environment back to
    # us. `set` lines after the marker are the post-VsDevCmd env.
    # cmd.exe's `echo X && cmd2` includes the space before && in the
    # echoed text, so the marker line ends with a trailing space. Match
    # by Trim() not equality.
    $marker = "__WINMALI_VSDEVCMD_DONE__"
    $output = & cmd.exe /c "`"$vsDevCmd`" -arch=$Arch -host_arch=$HostArch -winsdk=$WinSdk -no_logo >nul && echo $marker&& set"
    $seen = $false
    foreach ($line in $output) {
        if (-not $seen) {
            if ($line.Trim() -eq $marker) { $seen = $true }
            continue
        }
        if ($line -match '^([^=]+)=(.*)$') {
            $name  = $matches[1]
            $value = $matches[2]
            # Skip a few process-only / read-only vars.
            if ($name -in @('PROCESSOR_ARCHITECTURE','PROCESSOR_IDENTIFIER','PROCESSOR_LEVEL','PROCESSOR_REVISION','PROMPT','=ExitCode')) {
                continue
            }
            Set-Item -Path "Env:$name" -Value $value -ErrorAction SilentlyContinue
        }
    }
    if (-not $env:VSINSTALLDIR) {
        Write-Host "VsDevCmd.bat ran but VSINSTALLDIR not set; bailing." -ForegroundColor Red
        return $false
    }
    return $true
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
