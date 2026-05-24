<#
.SYNOPSIS
  Clear the WinMali kill-switch: driver runs normally on next load.

.EXAMPLE
  .\unkill.ps1
  .\unkill.ps1 -Reboot        # clear and reboot (next boot will run the driver)
#>
param([switch]$Reboot)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cfg = Import-PowerShellDataFile -Path (Join-Path $scriptDir "target.psd1")
$endpoint = "$($cfg.User)@$($cfg.HostIp)"

$cmd = "reg add 'HKLM\System\CurrentControlSet\Services\WinMaliKmd\Parameters' /v Disabled /t REG_DWORD /d 0 /f"
Write-Host "Clearing kill-switch on $($cfg.HostIp)..." -ForegroundColor Cyan
& ssh -o BatchMode=yes $endpoint $cmd

if ($Reboot) {
  Write-Host "Rebooting..." -ForegroundColor Cyan
  & ssh -o BatchMode=yes $endpoint "shutdown /r /t 0 /f" 2>&1 | Out-Null
}
