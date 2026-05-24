<#
.SYNOPSIS
    Build the WinMali-rk3588 solution.

.DESCRIPTION
    Top-level orchestrator. Wraps MSBuild for the .vcxproj projects and
    calls build-mesa.ps1 for the meson side.

    Examples:
        .\Tools\build.ps1                          # Debug, auto platform, everything
        .\Tools\build.ps1 -Configuration Release
        .\Tools\build.ps1 -Project Kmd             # Just the KMD
        .\Tools\build.ps1 -Project Mesa            # Just the mesa ICD
        .\Tools\build.ps1 -SkipMesa                # KMD + UMD, no mesa
        .\Tools\build.ps1 -Rebuild

    Exits non-zero if any step fails so CI can chain.

.PARAMETER Configuration
    Debug (default) or Release.

.PARAMETER Platform
    ARM64 / x64 / auto. auto picks ARM64 if the WDK's ARM64 libs are
    installed, x64 otherwise (compile-test only).

.PARAMETER Project
    All (default), Kmd, Umd, Mesa.

.PARAMETER SkipMesa
    Skip the mesa step regardless of -Project.

.PARAMETER Rebuild
    /t:Rebuild instead of /t:Build (and -Reconfigure for mesa).

.PARAMETER SignMode
    Off (default), TestSign, LabCert. SignMode=Off also disables the
    Inf2cat task; flip to TestSign when you want a loadable .sys.

.PARAMETER InfVerif
    Enable InfVerif. Off by default (frequent missing on dev boxes).

.PARAMETER ApiValidate
    Enable ApiValidator. Off by default (same reason).
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug","Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("ARM64","x64","auto")]
    [string]$Platform = "auto",

    [ValidateSet("All","Kmd","Mesa")]
    [string]$Project = "All",

    [switch]$SkipMesa,
    [switch]$Rebuild,
    [switch]$StopOnFirstError,

    [ValidateSet("quiet","minimal","normal","detailed","diagnostic")]
    [string]$LogLevel = "minimal",

    [switch]$InfVerif,
    [switch]$ApiValidate,

    [ValidateSet("Off","TestSign","LabCert")]
    [string]$SignMode = "Off"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"
Show-WinMaliEnv

if (-not $WinMaliMsBuild) {
    Write-Host "FATAL: MSBuild not found. Install Visual Studio (2017+) + C++ workload." -ForegroundColor Red
    exit 2
}
if (-not $WinMaliWdkRoot) {
    Write-Host "FATAL: Windows Driver Kit not installed." -ForegroundColor Red
    Write-Host "  https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk" -ForegroundColor Yellow
    exit 3
}

# VS 2017 / v141 needs the Dev shell loaded so MSBuild can find VC lib\.
if (-not (Enter-WinMaliVsDevEnv)) {
    Write-Host "FATAL: failed to enter VS Dev environment." -ForegroundColor Red
    exit 5
}

# --- Resolve auto platform -------------------------------------------------
if ($Platform -eq "auto") {
    $arm64Dir = Join-Path $WinMaliWdkRoot ("lib\$WinMaliWdkVersion\um\arm64")
    $arm64Ok  = (Test-Path $arm64Dir) -and ((Get-ChildItem $arm64Dir -Filter *.lib -EA SilentlyContinue).Count -gt 0)
    if ($arm64Ok) {
        $Platform = "ARM64"
        Write-Host "auto-platform: ARM64 (WDK ARM64 libs present)" -ForegroundColor DarkCyan
    } else {
        $Platform = "x64"
        Write-Host "auto-platform: x64 (no ARM64 WDK libs - compile-test only)" -ForegroundColor Yellow
    }
}

# --- MSBuild projects ------------------------------------------------------
$msbuildTarget = if ($Rebuild) { "Rebuild" } else { "Build" }
$msbuildResults = @()

function Invoke-MsBuild([string]$projOrSln, [string]$label) {
    $args = @(
        $projOrSln,
        "/t:$msbuildTarget",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/nologo",
        "/v:$LogLevel",
        "/clp:ShowTimestamp;Summary"
    )
    if ($StopOnFirstError) { $args += "/p:StopOnFirstFailure=true" }
    if (-not $InfVerif)    { $args += "/p:SkipPackageVerification=true" }
    if (-not $ApiValidate) { $args += "/p:ApiValidator_Enable=false" }
    $args += "/p:SignMode=$SignMode"
    if ($SignMode -eq "Off") { $args += "/p:EnableInf2cat=false" }

    Write-Host ""
    Write-Host "== $label ($Configuration|$Platform, $msbuildTarget) ==" -ForegroundColor Cyan
    # Out-Host so MSBuild's output renders to the console immediately and
    # does NOT become part of the function's return value (which would
    # otherwise pollute the exit-code-only return contract).
    & $WinMaliMsBuild @args | Out-Host
    $code = $LASTEXITCODE
    $script:msbuildResults += [pscustomobject]@{ Label=$label; Code=$code }
    return $code
}

$runMesa = ($Project -in @("All","Mesa")) -and (-not $SkipMesa)
$runKmd  = ($Project -in @("All","Kmd"))

$overall = 0

# Mesa first: KMD's INF depends on WinMaliGL.dll being present in
# the staging dir at package-time when InfVerif/Inf2cat run.
#
# For ARM64 deployment packages we ALSO build the x64 mesa flavour
# and stage it next to the ARM64 binaries with a "64" suffix
# (WinMaliUmd64.dll etc). These are the WoW companions the INF
# registers under UserModeDriverNameWoW / VulkanDriverNameWoW /
# OpenGLDriversWoW, loaded by dxgkrnl into x64 processes running
# under Prism emulation on Windows 11 ARM64.
function Invoke-MesaBuild([string]$mesaPlatform) {
    Write-Host ""
    Write-Host "== Mesa ($Configuration|$mesaPlatform) ==" -ForegroundColor Cyan
    $mesaArgs = @{ Configuration = $Configuration; Platform = $mesaPlatform }
    if ($Rebuild) { $mesaArgs.Reconfigure = $true } else { $mesaArgs.SkipIfPresent = $true }
    & $mesaScript @mesaArgs
    return $LASTEXITCODE
}

#
# Stage emulator binaries alongside the ARM64-native ones.
#
# A modern WoA display driver ships three flavors of each user-mode
# binary, one per process bitness/emulation. Matching the Adreno layout:
#
#   ARM64-native       -> bin\<Cfg>\ARM64\WinMali*.dll      (DriverStore)
#   x86 (WOW64)        -> bin\<Cfg>\ARM64\WinMali*X86.dll   (SysWow64)
#   x64-via-Prism      -> bin\<Cfg>\ARM64\WinMali*Chpe.dll  (SyChpe32)
#
# We build each flavor from its own mesa tree (x64 + Win32) and copy
# the resulting WinMali*.dll into the ARM64 bin dir with a $suffix
# applied before the .dll extension. PDBs ride along so each binary
# stays debuggable in-place. The KMD .sys is ARM64-only and stays put.
#
function Copy-EmulatorCompanions([string]$nativePlatform,
                                 [string]$srcPlatform,
                                 [string]$suffix) {
    $srcDir = Join-Path $WinMaliBuildDir "bin\$Configuration\$srcPlatform"
    $dstDir = Join-Path $WinMaliBuildDir "bin\$Configuration\$nativePlatform"
    if (-not (Test-Path $srcDir)) {
        Write-Host "Copy-EmulatorCompanions: no $srcDir to stage from" -ForegroundColor Yellow
        return
    }
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    foreach ($f in Get-ChildItem $srcDir -Filter "WinMali*.dll") {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        # Skip already-suffixed binaries and the KMD.
        if ($base -match "(Chpe|X86)$") { continue }
        if ($base -eq "WinMaliKmd")     { continue }
        $newName = "$base$suffix.dll"
        Copy-Item -Path $f.FullName -Destination (Join-Path $dstDir $newName) -Force
        $pdb = [System.IO.Path]::ChangeExtension($f.FullName, ".pdb")
        if (Test-Path $pdb) {
            Copy-Item -Path $pdb -Destination (Join-Path $dstDir "$base$suffix.pdb") -Force
        }
        Write-Host "Staged $suffix companion: $dstDir\$newName" -ForegroundColor DarkGreen
    }
}

function Copy-VulkanIcdJsons([string]$nativePlatform) {
    # Drop the three Vulkan loader manifests alongside the binaries so
    # the INF's CopyFiles section finds them in the source dir at
    # Inf2cat / staging time.
    $srcDir = Join-Path $PSScriptRoot "..\KMD"
    $dstDir = Join-Path $WinMaliBuildDir "bin\$Configuration\$nativePlatform"
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    foreach ($json in @("WinMaliVk_icd.json",
                        "WinMaliVk_icd_x86.json")) {
        $src = Join-Path $srcDir $json
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination (Join-Path $dstDir $json) -Force
            Write-Host "Staged Vulkan ICD manifest: $dstDir\$json" -ForegroundColor DarkGreen
        } else {
            Write-Host "Vulkan ICD manifest missing: $src" -ForegroundColor Yellow
        }
    }
}

if ($runMesa) {
    $mesaScript = Join-Path $PSScriptRoot "build-mesa.ps1"
    if (-not (Test-Path $mesaScript)) {
        Write-Host "FATAL: build-mesa.ps1 not found at $mesaScript" -ForegroundColor Red
        exit 4
    }

    # When targeting ARM64, build the Win32 (x86) emulation flavor first
    # so the SysWow64 companions are present alongside the native ARM64
    # binaries before InfVerif / Inf2cat fire.
    #
    #   Win32 build -> staged as WinMali*X86.dll (WOW64, lands in SysWow64)
    #
    # x64-under-Prism is NOT handled here. SyChpe32 is the x86 CHPE
    # emulation directory, not an x64 slot - Microsoft Learn ("How
    # emulation works on Arm") spells this out: x64 emulated processes
    # share the OS's ARM64X system binaries from System32/DriverStore.
    # Covering x64-Prism would require building the user-mode DLLs as
    # ARM64X (or at least ARM64EC); -Platform ARM64EC is wired in
    # build-mesa.ps1 for that future step. For now native ARM64 only.
    if ($Platform -eq "ARM64") {
        foreach ($emuPlatform in @("Win32")) {
            $code = Invoke-MesaBuild $emuPlatform
            if ($code -ne 0) {
                $overall = $code
                if ($StopOnFirstError) {
                    Write-Host "Mesa $emuPlatform (emulator companion) build failed; stopping." -ForegroundColor Red
                    exit $overall
                }
                Write-Host "Mesa $emuPlatform (emulator companion) failed (exit=$code); continuing." -ForegroundColor Yellow
            }
        }
        Copy-EmulatorCompanions -nativePlatform "ARM64" -srcPlatform "Win32" -suffix "X86"
        Copy-VulkanIcdJsons     -nativePlatform "ARM64"
    }

    $code = Invoke-MesaBuild $Platform
    if ($code -ne 0) {
        $overall = $code
        if ($StopOnFirstError) {
            Write-Host "Mesa build failed; stopping (StopOnFirstError)." -ForegroundColor Red
            exit $overall
        }
        Write-Host "Mesa build failed (exit=$code); continuing with KMD." -ForegroundColor Yellow
    }
}

if ($runKmd) {
    $code = Invoke-MsBuild (Join-Path $WinMaliRoot "KMD\WinMaliKmd.vcxproj") "WinMaliKmd"
    if ($code -ne 0 -and $StopOnFirstError) { exit $code }
    if ($code -ne 0) { $overall = $code }
}

Write-Host ""
Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
foreach ($r in $msbuildResults) {
    $colour = if ($r.Code -eq 0) { 'Green' } else { 'Red' }
    Write-Host ("  {0,-14} {1}" -f $r.Label, $(if ($r.Code -eq 0) { 'OK' } else { "FAILED (exit=$($r.Code))" })) -ForegroundColor $colour
}
Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray

if ($overall -eq 0) {
    Write-Host "BUILD: OK" -ForegroundColor Green
} else {
    Write-Host "BUILD: FAILED (exit=$overall)" -ForegroundColor Red
}
exit $overall
