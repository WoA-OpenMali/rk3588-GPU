<#
.SYNOPSIS
  Set the WinMali kill-switch: driver early-exits in DriverEntry on next load.

.DESCRIPTION
  Writes HKLM\System\CurrentControlSet\Services\WinMaliKmd\Parameters\Disabled = 1.
  The kill-switch only takes effect on the next driver load, which usually means
  the next reboot. (Disable-PnpDevice + Enable-PnpDevice also re-loads.)

  Use this when the last build bugchecks but the target is currently bootable
  (kill-switch was set before the last reboot, or someone scrubbed the SSD).
  Cleaner than pulling the SSD next time.

.EXAMPLE
  .\kill.ps1                  # set kill switch
  .\kill.ps1 -Reboot          # set kill switch and reboot now
#>
param([switch]$Reboot)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cfg = Import-PowerShellDataFile -Path (Join-Path $scriptDir "target.psd1")
$endpoint = "$($cfg.User)@$($cfg.HostIp)"

$cmd = "reg add 'HKLM\System\CurrentControlSet\Services\WinMaliKmd\Parameters' /v Disabled /t REG_DWORD /d 1 /f"
Write-Host "Setting kill-switch on $($cfg.HostIp)..." -ForegroundColor Cyan
& ssh -o BatchMode=yes $endpoint $cmd

if ($Reboot) {
  Write-Host "Rebooting..." -ForegroundColor Cyan
  & ssh -o BatchMode=yes $endpoint "shutdown /r /t 0 /f" 2>&1 | Out-Null
}
