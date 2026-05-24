<#
.SYNOPSIS
  Build KMD, push to target, install, reboot. The fast iteration loop.

.DESCRIPTION
  Reads endpoint info from Tools\target.psd1. Steps:
    1. (optional) Build the KMD via build.ps1.
    2. scp WinMaliKmd.{sys,inf,cat} -> target:C:\drv\
    3. ssh: pnputil /delete-driver oem*.inf  (uninstall any prior)
       ssh: pnputil /add-driver C:\drv\WinMaliKmd.inf /install
    4. ssh: shutdown /r /t 0 /f, then poll port 22 until it answers.

.PARAMETER SkipBuild
  Don't re-run build.ps1; use whatever's already in build\bin\.

.PARAMETER NoReboot
  Install but don't reboot. Useful when you want to attach kd.exe first.

.PARAMETER NoUninstall
  Skip the uninstall step. (First-ever install on a fresh target.)

.PARAMETER Safe
  Set the kill-switch (Parameters\Disabled=1) BEFORE rebooting so the new
  driver early-exits on next load. Use when pushing changes you suspect
  may bugcheck - target stays SSH-able after reboot; clear the switch
  manually (`unkill.ps1 -Reboot`) once you're ready to actually test.

.EXAMPLE
  .\flash.ps1
  .\flash.ps1 -SkipBuild
  .\flash.ps1 -Safe              # arm new build but don't run it on this boot
#>
param(
  [string]$Configuration = "Debug",
  [string]$Platform      = "ARM64",
  [switch]$SkipBuild,
  [switch]$NoReboot,
  [switch]$NoUninstall,
  [switch]$Safe,
  [int]$RebootTimeoutSec = 180
)

$ErrorActionPreference = "Stop"
$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverRoot    = Split-Path -Parent $scriptDir                  # ...\rk3588-GPU
$workspaceRoot = Split-Path -Parent $driverRoot                 # ...\WinMaliBringup (build\ lives here)

# --- target config ---
$cfgPath = Join-Path $scriptDir "target.psd1"
if (-not (Test-Path $cfgPath)) {
  throw "Missing $cfgPath - copy target.psd1.example and fill in values from setup-target.ps1."
}
$T = Import-PowerShellDataFile -Path $cfgPath
$endpoint = "$($T.User)@$($T.HostIp)"

function Test-Port {
  param([string]$HostName, [int]$Port, [int]$TimeoutMs = 1500)
  try {
    $c = [System.Net.Sockets.TcpClient]::new()
    $iar = $c.BeginConnect($HostName, $Port, $null, $null)
    if (-not $iar.AsyncWaitHandle.WaitOne($TimeoutMs)) { $c.Close(); return $false }
    $c.EndConnect($iar); $c.Close(); return $true
  } catch { return $false }
}

function Step([string]$m) { Write-Host "`n== $m ==" -ForegroundColor Cyan }

# 1. Build
if (-not $SkipBuild) {
  Step "Build $Configuration|$Platform KMD"
  & (Join-Path $scriptDir "build.ps1") -Configuration $Configuration -Platform $Platform -Project KMD
  if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
}

$binDir   = Join-Path $workspaceRoot "build\bin\$Configuration\$Platform"
$stageDir = Join-Path $workspaceRoot "build\stage\$Configuration\$Platform\WinMaliKmd"

if (-not (Test-Path (Join-Path $binDir "WinMaliKmd.sys"))) {
  throw "Missing artefact: $binDir\WinMaliKmd.sys"
}
if (-not (Test-Path (Join-Path $binDir "WinMaliKmd.inf"))) {
  throw "Missing artefact: $binDir\WinMaliKmd.inf"
}

# 2. Stage into a clean dir for Inf2Cat. Strict file set so the .cat hashes
#    exactly what we ship.
Step "Stage driver package -> $stageDir"
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$candidateGlobs = @("WinMaliKmd.sys", "WinMaliKmd.inf",
                    "WinMali*.dll", "WinMali*.json")
$excludeExt = @(".pdb",".lib",".exp",".ilk")

$stagedFiles = New-Object System.Collections.Generic.List[System.IO.FileInfo]
foreach ($g in $candidateGlobs) {
  Get-ChildItem -Path (Join-Path $binDir $g) -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -notin $excludeExt } |
    ForEach-Object {
      Copy-Item -Path $_.FullName -Destination $stageDir -Force
      $stagedFiles.Add($_)
    }
}
Write-Host "  $($stagedFiles.Count) files staged"
$stagedFiles | ForEach-Object {
  Write-Host ("    {0,-28} {1,10:N0} bytes" -f $_.Name, $_.Length) -ForegroundColor DarkGray
}

# 3. Generate + sign WinMaliKmd.cat
Step "Sign driver package"
& (Join-Path $scriptDir "sign-package.ps1") -PackageDir $stageDir
if ($LASTEXITCODE -ne 0) { throw "sign-package.ps1 failed" }

# 4. Push the staged dir
Step "Push to ${endpoint}:C:/drv/"
Get-ChildItem -Path $stageDir -File | ForEach-Object {
  & scp -o BatchMode=yes -o StrictHostKeyChecking=accept-new $_.FullName "${endpoint}:C:/drv/$($_.Name)"
  if ($LASTEXITCODE -ne 0) { throw "scp failed for $($_.Name)" }
}

# 3. Uninstall + install
if (-not $NoUninstall) {
  Step "Uninstall prior WinMaliKmd"
  $uninstallScript = @'
$ErrorActionPreference = "SilentlyContinue"
$drivers = pnputil /enum-drivers
$current = $null
$matches = @()
foreach ($line in $drivers) {
  if ($line -match "Published Name\s*:\s*(oem\d+\.inf)") { $current = @{Published=$Matches[1]} }
  elseif ($current -and $line -match "Original Name\s*:\s*(.+)$") { $current.Original = $Matches[1].Trim() }
  elseif ($current -and $line -match "Driver Version\s*:") { }
  elseif ($current -and $line.Trim() -eq "") {
    if ($current.Original -match "WinMaliKmd\.inf") { $matches += $current.Published }
    $current = $null
  }
}
foreach ($oem in $matches) {
  Write-Host "  pnputil /delete-driver $oem /uninstall /force"
  pnputil /delete-driver $oem /uninstall /force
}
'@
  & ssh -o BatchMode=yes $endpoint $uninstallScript
}

Step "Install C:\drv\WinMaliKmd.inf"
& ssh -o BatchMode=yes $endpoint "pnputil /add-driver C:\drv\WinMaliKmd.inf /install"
if ($LASTEXITCODE -ne 0) { throw "pnputil install failed" }

# 3.5. Optionally arm kill-switch BEFORE reboot so the new driver early-exits.
if ($Safe) {
  Step "Set kill-switch (Safe mode)"
  & ssh -o BatchMode=yes $endpoint "reg add 'HKLM\System\CurrentControlSet\Services\WinMaliKmd\Parameters' /v Disabled /t REG_DWORD /d 1 /f"
  Write-Host "  Driver will not run after reboot. Clear with: .\Tools\unkill.ps1 -Reboot" -ForegroundColor Yellow
}

# 4. Reboot
if (-not $NoReboot) {
  Step "Reboot target"
  # ssh will die mid-command when the box goes down - that's fine.
  & ssh -o BatchMode=yes $endpoint "shutdown /r /t 0 /f" 2>&1 | Out-Null

  Write-Host "  Waiting for SSH to come back on $($T.HostIp):22..."
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $back = $false
  while ($sw.Elapsed.TotalSeconds -lt $RebootTimeoutSec) {
    Start-Sleep -Seconds 3
    if (Test-Port -HostName $T.HostIp -Port 22) {
      $back = $true; break
    }
  }
  if (-not $back) { throw "Target did not return within $RebootTimeoutSec s" }
  Write-Host "  Target back after $([int]$sw.Elapsed.TotalSeconds)s." -ForegroundColor Green
}

Write-Host "`nDone." -ForegroundColor Green
