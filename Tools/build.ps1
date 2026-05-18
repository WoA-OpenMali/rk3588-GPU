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

    [ValidateSet("All","Kmd","Umd","Mesa")]
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
    Write-Host "FATAL: MSBuild not found. Install Visual Studio 2022 + C++ workload." -ForegroundColor Red
    exit 2
}
if (-not $WinMaliWdkRoot) {
    Write-Host "FATAL: Windows Driver Kit not installed." -ForegroundColor Red
    Write-Host "  https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk" -ForegroundColor Yellow
    exit 3
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
$runUmd  = ($Project -in @("All","Umd"))

$overall = 0

# Mesa first: KMD's INF depends on WinMaliGL.dll being present in
# the staging dir at package-time when InfVerif/Inf2cat run.
if ($runMesa) {
    $mesaScript = Join-Path $PSScriptRoot "build-mesa.ps1"
    if (-not (Test-Path $mesaScript)) {
        Write-Host "FATAL: build-mesa.ps1 not found at $mesaScript" -ForegroundColor Red
        exit 4
    }
    Write-Host ""
    Write-Host "== Mesa ($Configuration) ==" -ForegroundColor Cyan
    $mesaArgs = @("-Configuration", $Configuration)
    if ($Rebuild) { $mesaArgs += "-Reconfigure" } else { $mesaArgs += "-SkipIfPresent" }
    & $mesaScript @mesaArgs
    if ($LASTEXITCODE -ne 0) {
        $overall = $LASTEXITCODE
        if ($StopOnFirstError) {
            Write-Host "Mesa build failed; stopping (StopOnFirstError)." -ForegroundColor Red
            exit $overall
        }
        Write-Host "Mesa build failed (exit=$LASTEXITCODE); continuing with KMD/UMD." -ForegroundColor Yellow
    }
}

if ($runUmd) {
    $code = Invoke-MsBuild (Join-Path $WinMaliRoot "UMD\WinMaliUmd.vcxproj") "WinMaliUmd"
    if ($code -ne 0 -and $StopOnFirstError) { exit $code }
    if ($code -ne 0) { $overall = $code }
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
