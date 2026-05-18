<#
.SYNOPSIS
    Build the mesa-side WinMali UMD/ICDs for WinMali-rk3588.

.DESCRIPTION
    Configures mesa via meson for the WinMali (panthor-on-WDDM) path and
    builds three DLLs:

        WinMaliGL.dll   - OpenGL ICD (targets/wgl with panfrost backend)
        WinMaliUmd.dll  - D3D10/11 UMD (targets/d3d10rk3588, mesa d3d10umd
                          frontend; virtio-style)
        WinMaliVk.dll   - Vulkan ICD placeholder (targets/winmali_vk;
                          returns INCOMPATIBLE_DRIVER until panvk grows
                          v10/CSF + Windows support)

    Intermediate meson build tree at <repo>\build\mesa\<cfg>\; final
    DLLs copied into <repo>\build\bin\<Configuration>\<Platform>\.

    Examples:
        .\Tools\build-mesa.ps1                  # Debug, build if stale
        .\Tools\build-mesa.ps1 -Configuration Release
        .\Tools\build-mesa.ps1 -Reconfigure     # blow away meson cache, then build
        .\Tools\build-mesa.ps1 -Clean           # delete build tree
        .\Tools\build-mesa.ps1 -SkipIfPresent   # cheap incremental for MSBuild
        .\Tools\build-mesa.ps1 -ConfigureOnly   # meson setup only, no compile

.PARAMETER Configuration
    Debug (default) or Release. Maps to meson's --buildtype.

.PARAMETER Reconfigure
    Delete the meson build directory before running setup.

.PARAMETER Clean
    Delete the meson build directory and exit.

.PARAMETER SkipIfPresent
    If all three target DLLs already exist for this configuration, exit 0.
    Used by WinMaliMesa.vcxproj's Build target.

.PARAMETER ConfigureOnly
    Run `meson setup` and exit; don't compile. Useful for iterating on
    meson configure errors without paying full compile time.
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug","Release")]
    [string]$Configuration = "Debug",

    [switch]$Reconfigure,
    [switch]$Clean,
    [switch]$SkipIfPresent,
    [switch]$ConfigureOnly,

    [ValidateSet("ARM64","x64","Win32")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"

# --------------------------------------------------------------------------
# Sanity
# --------------------------------------------------------------------------
if (-not $WinMaliMesaRoot) {
    Write-Host "FATAL: mesa source not found at $WinMaliRepoRoot\mesa" -ForegroundColor Red
    exit 1
}
if (-not $WinMaliMeson) {
    Write-Host "FATAL: meson.exe not on PATH. Install with: pip install meson" -ForegroundColor Red
    exit 1
}
if (-not $WinMaliNinja) {
    Write-Host "FATAL: ninja.exe not on PATH. Install ninja-build." -ForegroundColor Red
    exit 1
}
if (-not $WinMaliPython) {
    Write-Host "FATAL: python not on PATH. mesa needs python 3.x." -ForegroundColor Red
    exit 1
}

$buildTree = Join-Path $WinMaliBuildDir ("mesa\" + $Configuration.ToLower())
$binDir    = Join-Path $WinMaliBuildDir ("bin\$Configuration\$Platform")

$outDlls = @{
    "WinMaliGL.dll"  = "src\gallium\targets\wgl\WinMaliGL.dll"
    "WinMaliUmd.dll" = "src\gallium\targets\d3d10rk3588\d3d10_rk3588.dll"
    "WinMaliVk.dll"  = "src\gallium\targets\winmali_vk\WinMaliVk.dll"
}

if ($Clean) {
    if (Test-Path $buildTree) {
        Write-Host "Removing mesa build tree: $buildTree" -ForegroundColor Cyan
        Remove-Item -Recurse -Force $buildTree
    }
    foreach ($name in $outDlls.Keys) {
        $p = Join-Path $binDir $name
        if (Test-Path $p) {
            Write-Host "Removing $p" -ForegroundColor Cyan
            Remove-Item -Force $p
        }
    }
    exit 0
}

if ($SkipIfPresent) {
    $allPresent = $true
    foreach ($name in $outDlls.Keys) {
        if (-not (Test-Path (Join-Path $binDir $name))) { $allPresent = $false; break }
    }
    if ($allPresent) {
        Write-Host "All WinMali mesa DLLs present; skipping build ($binDir)" -ForegroundColor DarkGray
        exit 0
    }
}

# --------------------------------------------------------------------------
# meson setup (idempotent unless -Reconfigure)
# --------------------------------------------------------------------------
if ($Reconfigure -and (Test-Path $buildTree)) {
    Write-Host "Reconfigure: removing $buildTree" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $buildTree
}

New-Item -ItemType Directory -Force -Path $binDir | Out-Null

$mesonBuildtype = $Configuration.ToLower()   # 'debug' | 'release'

# Panthor / panfrost backed targets:
#   gallium-drivers=panfrost      backends the GL ICD + d3d10 UMD
#   gallium-frontends=wgl,d3d10umd
#   vulkan-drivers=               (panvk-on-Windows not ready; using own stub target)
#   winmali-wddm-kmod=true        enables panfrost pan_kmod_winmali path + wgl + d3d10rk3588 wiring
#   gallium-d3d10umd=true / gallium-d3d10-rk3588=true   build the D3D10 UMD target
#   gallium-windows-dll-name=WinMaliGL   names wgl target output WinMaliGL.dll directly
$mesonSetupArgs = @(
    "setup",
    $buildTree,
    $WinMaliMesaRoot,
    "--buildtype=$mesonBuildtype",
    "-Dplatforms=windows",
    "-Dgallium-drivers=panfrost",
    "-Dgallium-d3d10umd=true",
    "-Dgallium-d3d10-rk3588=true",
    "-Dwinmali-wddm-kmod=true",
    "-Dvulkan-drivers=",
    "-Dgallium-windows-dll-name=WinMaliGL",
    "-Dglx=disabled",
    "-Dosmesa=false",
    "-Dgles1=disabled",
    "-Dgles2=disabled",
    "-Dmicrosoft-clc=disabled",
    "-Dbuild-tests=false",
    "-Dshared-glapi=enabled",
    "-Dllvm=disabled"
)

function Enter-VsDevEnv {
    # Already inside a VS dev shell?
    if ($env:VSCMD_ARG_HOST_ARCH -or $env:VSINSTALLDIR) { return $true }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        Write-Host "vswhere.exe not found; cannot load VS Dev environment." -ForegroundColor Red
        return $false
    }
    $vsRoot = & $vswhere -all -prerelease -requires Microsoft.Component.MSBuild -property installationPath 2>$null |
              Select-Object -First 1
    if (-not $vsRoot) {
        Write-Host "vswhere found no MSBuild installations." -ForegroundColor Red
        return $false
    }

    $devShell = Join-Path $vsRoot "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (-not (Test-Path $devShell)) {
        Write-Host "Microsoft.VisualStudio.DevShell.dll missing at $devShell" -ForegroundColor Red
        return $false
    }

    Import-Module $devShell -ErrorAction Stop | Out-Null
    Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
    return $true
}

if (-not (Enter-VsDevEnv)) {
    Write-Host "Failed to enter VS Dev environment." -ForegroundColor Red
    exit 1
}

# Mesa's panfrost subtree relies on GCC builtins / __attribute__ /
# __inline__ that plain MSVC cl.exe does not understand. clang-cl
# (LLVM's MSVC-ABI compatible compiler, ships with the VS "C++ Clang
# tools for Windows" component) accepts those AND uses the MSVC ABI, so
# everything links cleanly with the rest of the MSVC-built mesa code.
$clangCl = Join-Path $env:VSINSTALLDIR "VC\Tools\Llvm\x64\bin\clang-cl.exe"
if (-not (Test-Path $clangCl)) {
    $clangCl = Join-Path $env:VSINSTALLDIR "VC\Tools\Llvm\bin\clang-cl.exe"
}
if (Test-Path $clangCl) {
    Write-Host "Using clang-cl: $clangCl" -ForegroundColor DarkGreen
    $env:CC = $clangCl
    $env:CXX = $clangCl
}

if (-not (Test-Path (Join-Path $buildTree "build.ninja"))) {
    Write-Host "meson setup ($buildTree)" -ForegroundColor Cyan
    & $WinMaliMeson @mesonSetupArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "meson setup failed (exit=$LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

if ($ConfigureOnly) {
    Write-Host "Configure complete; -ConfigureOnly was set, skipping compile." -ForegroundColor Yellow
    exit 0
}

# --------------------------------------------------------------------------
# Compile all three targets
# --------------------------------------------------------------------------
$mesonTargets = @(
    "WinMaliGL",
    "d3d10_rk3588",
    "WinMaliVk"
)

Write-Host "meson compile ($($mesonTargets -join ', '))" -ForegroundColor Cyan
& $WinMaliMeson "compile" "-C" $buildTree @mesonTargets
if ($LASTEXITCODE -ne 0) {
    Write-Host "meson compile failed (exit=$LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

# --------------------------------------------------------------------------
# Publish: copy each DLL (and matching .pdb if present) into bin\
# --------------------------------------------------------------------------
$published = 0
foreach ($shipName in $outDlls.Keys) {
    $relPath = $outDlls[$shipName]
    $builtDll = Join-Path $buildTree $relPath
    if (-not (Test-Path $builtDll)) {
        # Fall back to searching by basename — meson sometimes nests the
        # output under a target subdir we didn't anticipate.
        $baseName = [System.IO.Path]::GetFileName($relPath)
        $found = Get-ChildItem -Recurse -Path $buildTree -Filter $baseName -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTimeUtc -Descending |
                 Select-Object -First 1
        if ($found) { $builtDll = $found.FullName }
    }

    if (-not (Test-Path $builtDll)) {
        Write-Host "Mesa produced no $relPath under $buildTree" -ForegroundColor Yellow
        continue
    }

    $outDll = Join-Path $binDir $shipName
    Copy-Item -Path $builtDll -Destination $outDll -Force
    Write-Host "Published: $outDll" -ForegroundColor Green
    $published++

    $pdbCandidate = [System.IO.Path]::ChangeExtension($builtDll, ".pdb")
    if (Test-Path $pdbCandidate) {
        $outPdb = [System.IO.Path]::ChangeExtension($outDll, ".pdb")
        Copy-Item -Path $pdbCandidate -Destination $outPdb -Force
    }
}

if ($published -eq 0) {
    Write-Host "No mesa DLLs were published. Check the meson compile log." -ForegroundColor Red
    exit 5
}

exit 0
