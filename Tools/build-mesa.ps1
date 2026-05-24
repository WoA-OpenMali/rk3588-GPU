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

    # WinMaliVk.dll wraps panvk-v10. The Windows port is still in
    # progress; pass -SkipVulkan to build just the GL+D3D10 targets
    # while panvk Windows porting catches up.
    [switch]$SkipVulkan,

    # ARM64EC: ARM64 code with x64-compatible ABI. Loadable into both
    # ARM64-native and Prism-emulated x64 processes (Microsoft's
    # recommended cover for both in one binary on Windows-on-Arm 11).
    # Used for user-mode driver components; the kernel miniport stays
    # pure ARM64.
    [ValidateSet("ARM64","ARM64EC","x64","Win32")]
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

# Per-platform build trees so x64 and ARM64 cross-builds don't fight.
# $env:WINMALI_BUILD_TAG appends to the tree name, useful when the
# previous tree is locked by Windows Defender or stale Explorer windows
# and we need to start fresh without deleting.
$buildTag  = $env:WINMALI_BUILD_TAG
$treeName  = "mesa\" + $Configuration.ToLower() + "-" + $Platform.ToLower() +
             $(if ($buildTag) { "-$buildTag" } else { "" })
$buildTree = Join-Path $WinMaliBuildDir $treeName
$binDir    = Join-Path $WinMaliBuildDir ("bin\$Configuration\$Platform")

# clang-cl --target value per requested platform. For ARM64 we cross-
# compile from an x64 host using the same clang-cl binary. Meson's
# autodetect runs `clang-cl --version` WITHOUT considering target
# flags, so we MUST bundle --target into the compiler invocation
# itself via the cross file's binaries entry (a list) - otherwise
# meson thinks we're still targeting x86_64 and emits /MACHINE:X64
# link flags, which break the ARM64 link step.
$clangTarget = switch ($Platform) {
    "ARM64"   { "aarch64-pc-windows-msvc" }
    "ARM64EC" { "arm64ec-pc-windows-msvc" }
    "x64"     { "x86_64-pc-windows-msvc" }
    "Win32"   { "i686-pc-windows-msvc" }
    default   { $null }
}

# The shared_library() name set by meson via gallium-{wgl,d3d10}-dll-name
# becomes the .dll basename. panvk's shared_library is hardcoded as
# vulkan_panfrost - we rename it at publish time.
$outDlls = @{
    "WinMaliGL.dll"  = "src\gallium\targets\wgl\WinMaliGL.dll"
    "WinMaliUmd.dll" = "src\gallium\targets\d3d10rk3588\WinMaliUmd.dll"
    "WinMaliVk.dll"  = "src\panfrost\vulkan\vulkan_panfrost.dll"
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

# mesa-modern path: real panvk-v10 for Vulkan, panfrost gallium driver
# with native CSF support for GL/D3D, WinMali kmod backend that ships
# panthor ioctls through D3DKMT_ESCAPE to the kernel-mode WDDM 2.0
# miniport on Windows ARM64.
#
# Build-time deps (LLVM dev libs + libclc + SPIRV-Tools) live in
# C:\mesa-deps - built by Tools\build-mesa-deps.ps1. The pkg-config-path
# + cmake-prefix-path let meson find them without a pkg-config binary.
$depsInstallPath = "C:\mesa-deps"
$mesonSetupArgs = @(
    "setup",
    $buildTree,
    $WinMaliMesaRoot,
    "--buildtype=$mesonBuildtype",
    "--cmake-prefix-path=$depsInstallPath",
    "--pkg-config-path=$depsInstallPath\lib\pkgconfig;$depsInstallPath\share\pkgconfig",
    "-Db_vscrt=md",
    "-Dplatforms=windows",
    "-Dgallium-drivers=panfrost",
    $(if ($SkipVulkan) { "-Dvulkan-drivers=" } else { "-Dvulkan-drivers=panfrost" }),
    "-Dgallium-d3d10umd=true",
    "-Dgallium-d3d10-rk3588=true",
    "-Dwinmali-wddm-kmod=true",
    "-Dgallium-wgl-dll-name=WinMaliGL",
    "-Dgallium-d3d10-dll-name=WinMaliUmd",
    "-Dglx=disabled",
    "-Dgles1=disabled",
    "-Dgles2=disabled",
    "-Dmicrosoft-clc=disabled",
    "-Dgallium-rusticl=false",
    "-Dstatic-libclc=all",
    "-Dbuild-tests=false",
    "-Dshared-glapi=enabled",
    "-Dvideo-codecs="
)

# Cross-build LLVM tooling is awkward: the libs in C:\mesa-deps are x64
# binaries and can't link into an ARM64 or x86 DLL. Panfrost / panvk-v10
# don't need LLVM at runtime, but the build step compiles precomp CL
# shaders into header constants, which requires mesa-clc + LLVM at
# build time only. For non-x64 targets we reuse the x64 mesa-clc /
# vtn_bindgen2 host tools (-Dmesa-clc=system) and disable LLVM entirely
# - no LLVM gets linked into the ARM64 / x86 DLLs (the panfrost /
# panvk targets simply don't pull it in).
if ($Platform -ne "x64") {
    # Reuse build-time host tools from the x64 build tree so the cross
    # build doesn't have to bring along its own LLVM.
    $hostTools = @(
        @{ Dir = "src\compiler\clc";       Tool = "mesa_clc.exe" },
        @{ Dir = "src\compiler\spirv";     Tool = "vtn_bindgen2.exe" },
        @{ Dir = "src\panfrost\clc";       Tool = "panfrost_compile.exe" }
    )
    foreach ($t in $hostTools) {
        $d = Join-Path $WinMaliBuildDir "mesa\debug-x64\$($t.Dir)"
        if ((Test-Path (Join-Path $d $t.Tool)) -and ($env:PATH -notlike "*$d*")) {
            $env:PATH = "$d;$env:PATH"
        }
    }
    $mesonSetupArgs += @(
        "-Dmesa-clc=system",
        "-Dprecomp-compiler=system",
        "-Dllvm=disabled",
        "-Dshader-cache=disabled"
    )
}

# VsDevCmd arch matters for LIB / INCLUDE: arm64 cross-build needs the
# ARM64 um/km/ucrt lib paths on LIB and the ARM64 cross-compiler's tools
# on PATH, but the host is x64 so host_arch stays x64.
#
# Mesa build uses VS 2022 Build Tools at C:\BuildTools when present, so
# the STL we compile against ABI-matches the LLVM 18.1.8 we built into
# C:\mesa-deps with VS 2022. The driver (KMD/UMD) build still uses
# VS 2017 + 1809 WDK via the default vswhere lookup.
$devArch = switch ($Platform) {
    "ARM64"   { "arm64" }
    "ARM64EC" { "arm64" }   # VsDevCmd has no arm64ec arch; the EC bits
                            # ship inside the arm64 install and we pull
                            # them onto LIB ourselves below.
    "Win32"   { "x86" }     # native x86 cross from an x64 host
    default   { "x64" }
}
$vsOverride = if (Test-Path "C:\BuildTools\Common7\Tools\VsDevCmd.bat") { "C:\BuildTools" } else { "" }
if (-not (Enter-WinMaliVsDevEnv -Arch $devArch -HostArch x64 -VsInstallOverride $vsOverride)) {
    Write-Host "Failed to enter VS Dev environment ($devArch)." -ForegroundColor Red
    exit 1
}

# ARM64 cross-build needs both Windows SDK ARM64 libs AND the MSVC
# ARM64 C runtime (msvcrt.lib / libcmt.lib / vcruntime.lib).
#
#   SDK side  -> Windows Kits\10\Lib\<ver>\um\arm64 + ucrt\arm64
#                (ships with any Win 10 SDK >= 17763)
#   MSVC side -> VC\Tools\MSVC\<ver>\lib\arm64\msvcrt.lib etc.
#                (separate VS BT component: "MSVC v143 - VS 2022 C++
#                 ARM64/ARM64EC build tools" - not on a default install)
#
# If the MSVC ARM64 libs aren't installed, lld-link fails with a
# cryptic "could not open msvcrt.lib" error. Bail out with a clear
# message so the user knows which install step they're missing.
if ($Platform -eq "ARM64" -or $Platform -eq "ARM64EC") {
    # MSVC v143 (14.4x) emits __guard_eh_cont_table / _count references
    # in its CFG load-config object (loadcfg.obj inside msvcrt.lib).
    # The link fails with LNK2001 on those symbols unless the linker
    # knows to synthesize them. Two parts to fix this:
    #
    #   1. Force clang-cl to use lld-link. The default linker
    #      (link.exe) on PATH is the VS 2017 14.16 build, which
    #      doesn't even recognize /guard:ehcont (LNK1117 "syntax
    #      error in option 'guard:ehcont'"). lld-link 18 always
    #      knows the option.
    #   2. Pass /guard:ehcont so lld-link emits the synthesized
    #      symbols. Matches what link.exe 14.4x does implicitly.
    $env:CFLAGS   = "$env:CFLAGS -fuse-ld=lld"
    $env:CXXFLAGS = "$env:CXXFLAGS -fuse-ld=lld"
    $env:LDFLAGS  = "$env:LDFLAGS /guard:ehcont"

    # alloca() is declared in <malloc.h> on Windows. clang-cl's x64
    # target pulls it in via transitive includes; the arm64/arm64ec
    # targets do not. Force-include malloc.h into every TU so the
    # dozens of mesa source files that call alloca() without explicit
    # include keep working. /FI is the MSVC-shaped force-include flag.
    $env:CFLAGS   = "$env:CFLAGS /FImalloc.h"
    $env:CXXFLAGS = "$env:CXXFLAGS /FImalloc.h"

    # Pick the newest SDK the host has installed that ships ARM64 libs.
    # ARM64EC uses the same ARM64 import libs (kernel32.lib, ws2_32.lib
    # etc. on modern WoA SDKs are themselves ARM64X hybrids that export
    # both ARM64 and ARM64EC variants).
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
    $candidateSdks = Get-ChildItem (Join-Path $sdkRoot 'Lib') -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.0\.\d+' -and (Test-Path (Join-Path $_.FullName 'ucrt\arm64')) } |
        Sort-Object Name -Descending
    if (-not $candidateSdks) {
        Write-Host "No Windows SDK with ARM64 libs found under $sdkRoot\Lib" -ForegroundColor Red
        exit 1
    }
    $sdkVer = $candidateSdks[0].Name
    Write-Host "$Platform SDK pick: $sdkVer" -ForegroundColor DarkCyan

    foreach ($p in @("$sdkRoot\Lib\$sdkVer\um\arm64",
                     "$sdkRoot\Lib\$sdkVer\ucrt\arm64")) {
        if ((Test-Path $p) -and ($env:LIB -notlike "*$p*")) {
            $env:LIB = "$p;$env:LIB"
        }
    }

    # MSVC C runtime. Order matters for the linker's first-match lookup:
    #
    #   ARM64EC   -> lib\arm64ec  must come before lib\arm64. The
    #                arm64ec dir only has startup objects (chkstk_arm64ec
    #                etc.); the rest of msvcrt.lib lives in lib\arm64,
    #                which is now an ARM64X hybrid library that exports
    #                both ARM64 and ARM64EC variants.
    #   ARM64     -> just lib\arm64.
    $vcArm64 = $null
    foreach ($vcVer in @(Get-ChildItem "C:\BuildTools\VC\Tools\MSVC" -ErrorAction SilentlyContinue)) {
        $candidate = Join-Path $vcVer.FullName "lib\arm64\msvcrt.lib"
        if (Test-Path $candidate) {
            $vcArm64 = Join-Path $vcVer.FullName "lib\arm64"
            $vcArm64ec = Join-Path $vcVer.FullName "lib\arm64ec"
            break
        }
    }
    if ($vcArm64 -and ($env:LIB -notlike "*$vcArm64*")) {
        $env:LIB = "$vcArm64;$env:LIB"
    }
    if ($Platform -eq "ARM64EC" -and $vcArm64ec -and (Test-Path $vcArm64ec) -and ($env:LIB -notlike "*$vcArm64ec*")) {
        $env:LIB = "$vcArm64ec;$env:LIB"
    }

    # ARM64EC needs an ARM64EC-format msvcrt.lib that MSVC 14.44 doesn't
    # ship by default. The lib\arm64ec directory only has startup objs
    # (chkstk_arm64ec.obj etc.); the runtime libs in lib\arm64 are
    # pure ARM64, not ARM64X, so lld-link rejects them with
    # "machine type arm64 conflicts with arm64ec". Detect the gap and
    # bail with a clear message so the user knows what install they
    # need before they go chasing the link error.
    if ($Platform -eq "ARM64EC") {
        $hasEcMsvcrt = $vcArm64ec -and (Test-Path (Join-Path $vcArm64ec 'msvcrt.lib'))
        if (-not $hasEcMsvcrt) {
            Write-Host @"
ARM64EC runtime libs missing.

MSVC ships ARM64EC startup objects (lib\arm64ec\chkstk_arm64ec.obj, ...)
but not msvcrt.lib / libucrt.lib for ARM64EC in this toolset version
($((Get-ChildItem 'C:\BuildTools\VC\Tools\MSVC' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name) -join ', ')).
lld-link fails on the link probe with:
  lld-link: error: msvcrt.lib(exe_main.obj): machine type arm64 conflicts with arm64ec

To unblock, install one of:
  - Visual Studio Build Tools 2022, "Individual components":
      * MSVC v143 - VS 2022 C++ ARM64EC Spectre-mitigated libs (Latest)
      * Windows 11 SDK (10.0.26100.0) - ARM64EC component (if available)
  - Visual Studio 2022 17.14 Preview or later, which ships full
    ARM64EC runtime libs.

After install, the path C:\BuildTools\VC\Tools\MSVC\<ver>\lib\arm64ec\msvcrt.lib
should exist.

For now, -Platform ARM64 + -Platform Win32 (the current shipping
combination) covers native ARM64 + WOW64-x86 callers. ARM64EC
unlocks x64-under-Prism — see docs/NEXT_STEPS.md.
"@ -ForegroundColor Yellow
            exit 1
        }
    }
    if (-not $vcArm64) {
        Write-Host @"
ARM64 MSVC runtime not found. The Windows SDK ARM64 libs are present
but the matching MSVC ARM64 C runtime (msvcrt.lib / vcruntime.lib /
libcmt.lib) is missing.

Install:
  1. Run the Visual Studio Installer for "Visual Studio Build Tools 2022"
     (or Visual Studio 2022 / Professional if you have it).
  2. Open "Individual components" tab.
  3. Tick:
       - MSVC v143 - VS 2022 C++ ARM64/ARM64EC build tools (latest)
       - MSVC v143 - VS 2022 C++ ARM64 Spectre-mitigated libs (optional)
       - Windows 11 SDK (10.0.22621.0) ARM64 component, if not already.
  4. Click Modify and let it install.

After it's done, re-run this script.
"@ -ForegroundColor Yellow
        exit 1
    }
    Write-Host "ARM64 LIB head: $($env:LIB.Substring(0, [Math]::Min($env:LIB.Length, 240)))..." -ForegroundColor DarkCyan
}

# VsDevCmd resets PATH. Reinstate ninja + win_flex_bison + python +
# LLVM bin which env.ps1 found - their dirs need to be on PATH for
# meson to discover them.
foreach ($extra in @(
    $WinMaliNinja,
    "C:\tools\winflexbison\win_flex.exe",
    "C:\Program Files\LLVM\bin\clang-cl.exe",
    $WinMaliPython
)) {
    if ($extra -and (Test-Path $extra)) {
        $extraDir = Split-Path $extra -Parent
        if ($env:PATH -notlike "*$extraDir*") {
            $env:PATH = "$extraDir;$env:PATH"
        }
    }
}

# Mesa's panfrost subtree relies on GCC builtins / __attribute__ /
# __inline__ that plain MSVC cl.exe does not understand. clang-cl
# (LLVM's MSVC-ABI compatible compiler) accepts those AND uses the
# MSVC ABI, so everything links cleanly with the rest of the MSVC-built
# mesa code. Two install locations are common:
#   - The VS "C++ Clang tools for Windows" component, under VSINSTALLDIR
#     (VC\Tools\Llvm\... — path layout differs between VS 2017 and 2019+)
#   - A standalone LLVM install from llvm.org under
#     %ProgramFiles%\LLVM\bin (preferred on VS 2017 boxes because the
#     VS-shipped clang is several major versions behind mesa's needs).
$clangCl = $null
$clangCandidates = @(
    (Join-Path $env:VSINSTALLDIR "VC\Tools\Llvm\x64\bin\clang-cl.exe"),
    (Join-Path $env:VSINSTALLDIR "VC\Tools\Llvm\bin\clang-cl.exe"),
    "$env:ProgramFiles\LLVM\bin\clang-cl.exe",
    "${env:ProgramFiles(x86)}\LLVM\bin\clang-cl.exe"
)
foreach ($cand in $clangCandidates) {
    if ($cand -and (Test-Path $cand)) { $clangCl = $cand; break }
}
if ($clangCl) {
    Write-Host "Using clang-cl: $clangCl" -ForegroundColor DarkGreen
    $env:CC = $clangCl
    $env:CXX = $clangCl
} else {
    Write-Host "WARNING: clang-cl not found in VS or standalone LLVM; mesa likely won't build." -ForegroundColor Yellow
}

# 1809 WDK quirk: d3dkmthk.h lives in km\, not shared\ (Microsoft
# moved it in 19041). The mesa panfrost winmali bridge transitively
# includes <d3dkmthk.h>, so put km\ on the compiler include path
# globally via CFLAGS/CXXFLAGS. meson bakes these into the compile
# lines at setup time, so this only matters on the first configure or
# after -Reconfigure.
if ($env:WindowsSdkDir -and $env:WindowsSDKVersion) {
    $kmInc = "$($env:WindowsSdkDir)Include\$($env:WindowsSDKVersion)km".TrimEnd('\')
    # WindowsSDKVersion is "10.0.17763.0\" (note trailing slash that
    # VsDevCmd sets); after the join above the path is correct.
    $kmInc = $kmInc -replace '\\\\','\'
    if (Test-Path $kmInc) {
        Write-Host "Adding WDK km\ include dir to mesa CFLAGS: $kmInc" -ForegroundColor DarkCyan
        $env:CFLAGS   = "$env:CFLAGS /I`"$kmInc`""
        $env:CXXFLAGS = "$env:CXXFLAGS /I`"$kmInc`""
    }
}

# Mesa is built with clang-cl 18 against VS 2022 BT's STL (so the ABI
# matches the LLVM 18 we built into C:\mesa-deps). Two version-check
# bypasses are needed:
#   -fms-compatibility-version=19.40   - LLVM headers reject < VS 2019
#   -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH - VS 2022 STL refuses
#       Clang < 19 in <yvals_core.h>. We ABI-match fine in practice.
$env:CFLAGS   = "$env:CFLAGS -fms-compatibility-version=19.40 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"
$env:CXXFLAGS = "$env:CXXFLAGS -fms-compatibility-version=19.40 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"

# For non-x64 targets, meson needs a cross-file so it knows host_machine
# is different and so VC_LibraryPath_* / lld-link / llvm-rc /
# llvm-lib are wired up with explicit --target=<triple>. We emit one
# per build tree.
if ($Platform -ne "x64" -and $clangCl -and $clangTarget) {
    $crossFile = Join-Path $buildTree "winmali-$($Platform.ToLower()).cross"
    $cpuFamily = if ($Platform -eq "ARM64") { "aarch64" } else { "x86" }
    $cpu       = $cpuFamily
    # Forward slashes in paths so meson's INI parser doesn't choke on
    # backslash-escape ambiguities; "&" -> %26 and quote everything.
    $clangFs   = $clangCl -replace '\\','/'
    $crossText = @"
# Auto-generated by build-mesa.ps1; do not edit. Regenerate by deleting
# the surrounding build tree and re-running with -Reconfigure.
[binaries]
c       = ['$clangFs', '--target=$clangTarget']
cpp     = ['$clangFs', '--target=$clangTarget']
ar      = 'llvm-lib'
windres = 'llvm-rc'

[host_machine]
system     = 'windows'
cpu_family = '$cpuFamily'
cpu        = '$cpu'
endian     = 'little'
"@
    New-Item -ItemType Directory -Force -Path $buildTree | Out-Null
    Set-Content -Path $crossFile -Value $crossText -Encoding ASCII
    Write-Host "Wrote meson cross-file: $crossFile" -ForegroundColor DarkCyan
    $mesonSetupArgs += @("--cross-file", $crossFile)
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
    "WinMaliUmd"
)
if (-not $SkipVulkan) {
    $mesonTargets += "vulkan_panfrost"
}

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
