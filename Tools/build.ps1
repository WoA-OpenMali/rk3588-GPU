<#
.SYNOPSIS
    Build the WinMali-rk3588 solution with MSBuild.

.DESCRIPTION
    Wraps vswhere + MSBuild so you can do:

        .\Tools\build.ps1                      # Debug|ARM64, everything
        .\Tools\build.ps1 -Configuration Release
        .\Tools\build.ps1 -Project KMD         # KMD only
        .\Tools\build.ps1 -Project UMD
        .\Tools\build.ps1 -Rebuild
        .\Tools\build.ps1 -StopOnFirstError    # -p:StopOnFirstFailure=true
        .\Tools\build.ps1 -LogLevel normal     # quiet|minimal|normal|detailed|diagnostic

    Exits with a non-zero code if the build fails, so CI-style scripts
    can chain this with test.ps1.

.PARAMETER Configuration
    Debug (default) or Release.

.PARAMETER Project
    "KMD", "UMD", or "All" (default).

.PARAMETER Rebuild
    Pass /t:Rebuild instead of /t:Build.

.PARAMETER LogLevel
    MSBuild /v: verbosity level. Default: minimal.

.PARAMETER InfVerif
    Enable the InfVerif task. Off by default because the desktop-only SDK
    on some machines ships without InfVerif.dll.

.EXAMPLE
    .\Tools\build.ps1 -Project KMD -Rebuild -LogLevel normal
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    # ARM64 is the real deployment target. x64 is kept as a "does it
    # compile/link at all" smoke test for CI boxes that don't ship the
    # ARM64 SDK libraries. Default is "auto": pick ARM64 if libs exist,
    # x64 otherwise.
    [ValidateSet("ARM64", "x64", "auto")]
    [string]$Platform = "auto",

    [ValidateSet("KMD", "UMD", "Diag", "Disp", "DispDiag", "All")]
    [string]$Project = "All",

    [switch]$Rebuild,

    [switch]$StopOnFirstError,

    [ValidateSet("quiet", "minimal", "normal", "detailed", "diagnostic")]
    [string]$LogLevel = "minimal",

    # Off by default because the desktop-SDK-only WDK on typical dev
    # machines doesn't ship InfVerif.dll / ApiExtractor.exe.
    [switch]$InfVerif,
    [switch]$ApiValidate,

    # Turn on when you actually want to load the driver on hardware;
    # TestSign produces a self-signed .sys that can load with
    # bcdedit /set testsigning on.
    [ValidateSet("Off", "TestSign", "LabCert")]
    [string]$SignMode = "Off"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"
Show-WinMaliEnv

if (-not $WinMaliMsBuild) {
    Write-Host "FATAL: MSBuild not found. Install Visual Studio 2022 with C++ workload." -ForegroundColor Red
    exit 2
}
if (-not $WinMaliWdkRoot) {
    Write-Host "FATAL: Windows Driver Kit (WDK) not installed." -ForegroundColor Red
    Write-Host "Install from https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk" -ForegroundColor Yellow
    Write-Host "You need the WDK matching your SDK version, plus the VS extension." -ForegroundColor Yellow
    exit 3
}

$target = if ($Rebuild) { "Rebuild" } else { "Build" }

# Resolve auto-selected platform based on available SDK libs.
if ($Platform -eq "auto") {
    $arm64LibDir = Join-Path $WinMaliWdkRoot ("lib\$WinMaliWdkVersion\um\arm64")
    $arm64LibPresent = (Test-Path $arm64LibDir) -and `
                      ((Get-ChildItem $arm64LibDir -Filter *.lib -ErrorAction SilentlyContinue).Count -gt 0)
    if ($arm64LibPresent) {
        $Platform = "ARM64"
        Write-Host "auto-platform: ARM64 (SDK ARM64 libs found)" -ForegroundColor DarkCyan
    } else {
        $Platform = "x64"
        Write-Host "auto-platform: x64 (ARM64 SDK libs missing - install via VS Installer / Individual Components / 'Windows 11 SDK (...) ARM64' to enable real target)" -ForegroundColor Yellow
    }
}

# Pick the project(s) to build.
$slnOrProj = switch ($Project) {
    "KMD"      { Join-Path $WinMaliRoot "KMD\WinMaliKmd.vcxproj" }
    "UMD"      { Join-Path $WinMaliRoot "UMD\WinMaliUmd.vcxproj" }
    "Diag"     { Join-Path $WinMaliRoot "Tools\winmali-diag\winmali-diag.vcxproj" }
    "Disp"     { Join-Path $WinMaliRoot "Rk3588DispKmd\Rk3588DispKmd.vcxproj" }
    "DispDiag" { Join-Path $WinMaliRoot "Tools\rk3588disp-diag\rk3588disp-diag.vcxproj" }
    "All"      { Join-Path $WinMaliRoot "WinMali-rk3588.sln" }
}

$msbuildArgs = @(
    $slnOrProj,
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/nologo",
    "/v:$LogLevel",
    "/clp:ShowTimestamp;Summary"
)

if ($StopOnFirstError) {
    $msbuildArgs += "/p:StopOnFirstFailure=true"
}

# Users without InfVerif.dll / ApiExtractor.exe get a soft-skip by
# default; explicit opt-in via the switches.
if (-not $InfVerif)    { $msbuildArgs += "/p:SkipPackageVerification=true" }
if (-not $ApiValidate) { $msbuildArgs += "/p:ApiValidator_Enable=false" }
# Default SignMode=Off disables the Inf2Cat+SignTool chain (the cat
# file errors on incomplete desktop-SDK installs). Flip with -SignMode.
$msbuildArgs += "/p:SignMode=$SignMode"
if ($SignMode -eq "Off") { $msbuildArgs += "/p:EnableInf2cat=false" }

Write-Host ""
Write-Host "Building $Project ($Configuration|$Platform, target=$target)..." -ForegroundColor Cyan
Write-Host ""

& $WinMaliMsBuild @msbuildArgs
$code = $LASTEXITCODE

Write-Host ""
if ($code -eq 0) {
    Write-Host "BUILD: OK" -ForegroundColor Green
} else {
    Write-Host "BUILD: FAILED (exit=$code)" -ForegroundColor Red
}

exit $code
