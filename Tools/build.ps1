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

function Copy-WowCompanions([string]$nativePlatform, [string]$wowPlatform) {
    # Copy each WinMali*.dll from <bin>\<Cfg>\<wow>\ into <bin>\<Cfg>\<native>\
    # renamed with a "64" suffix before the .dll extension. PDBs come
    # along for the ride so the Wow binaries are debuggable in-place.
    $srcDir = Join-Path $WinMaliBuildDir "bin\$Configuration\$wowPlatform"
    $dstDir = Join-Path $WinMaliBuildDir "bin\$Configuration\$nativePlatform"
    if (-not (Test-Path $srcDir)) {
        Write-Host "Copy-WowCompanions: no $srcDir to stage from" -ForegroundColor Yellow
        return
    }
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    foreach ($f in Get-ChildItem $srcDir -Filter "WinMali*.dll") {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        if ($base -like "*64") { continue }   # already a Wow companion
        if ($base -eq "WinMaliKmd") { continue }   # KMD is ARM64-only
        $newName = "$base`64.dll"
        Copy-Item -Path $f.FullName -Destination (Join-Path $dstDir $newName) -Force
        $pdb = [System.IO.Path]::ChangeExtension($f.FullName, ".pdb")
        if (Test-Path $pdb) {
            Copy-Item -Path $pdb -Destination (Join-Path $dstDir "$base`64.pdb") -Force
        }
        Write-Host "Staged Wow companion: $dstDir\$newName" -ForegroundColor DarkGreen
    }
}

if ($runMesa) {
    $mesaScript = Join-Path $PSScriptRoot "build-mesa.ps1"
    if (-not (Test-Path $mesaScript)) {
        Write-Host "FATAL: build-mesa.ps1 not found at $mesaScript" -ForegroundColor Red
        exit 4
    }

    # When targeting ARM64, build x64 first so we can stage its outputs
    # as the WoW companions (WinMaliUmd64.dll etc) inside the ARM64
    # bin dir before the InfVerif / Inf2cat tasks fire.
    if ($Platform -eq "ARM64") {
        $code = Invoke-MesaBuild "x64"
        if ($code -ne 0) {
            $overall = $code
            if ($StopOnFirstError) {
                Write-Host "Mesa x64 (Wow companion) build failed; stopping." -ForegroundColor Red
                exit $overall
            }
            Write-Host "Mesa x64 (Wow companion) failed (exit=$code); continuing." -ForegroundColor Yellow
        }
        Copy-WowCompanions -nativePlatform "ARM64" -wowPlatform "x64"
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
