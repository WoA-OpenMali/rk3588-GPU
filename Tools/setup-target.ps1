#requires -RunAsAdministrator
<#
.SYNOPSIS
  One-time setup of the RK3588 target machine for KMD iteration.

.DESCRIPTION
  Run this ONCE on the target (RK3588 Win11 Pro on the WTG SSD) in an
  elevated PowerShell. It:

    - Installs/enables OpenSSH Server, opens port 22.
    - Sets LocalAccountTokenFilterPolicy so SSH gets the full admin token.
    - Sets PowerShell as the default SSH shell.
    - Enables /debug on. Leaves existing dbgsettings alone unless you
      explicitly pass -SerialDebugPort. (This box debugs over COM, so
      we do NOT clobber the working serial config.)
    - Disables boot-failure recovery + auto-restart on BSOD so the box
      always comes back to a usable state we can SSH into.
    - Enables test-signing so an unsigned WinMaliKmd.sys can load.
    - Configures small minidumps.
    - Creates C:\drv (where flash.ps1 drops binaries).
    - Prepares administrators_authorized_keys (you paste the dev box's
      public key into it at the end).

.PARAMETER SerialDebugPort
  OPTIONAL. The target-side COM port NUMBER (just the integer, e.g. 1
  if the UART is COM1 on the target). If passed, we run
  'bcdedit /dbgsettings serial debugport:<N> baudrate:<rate>' to set the
  kernel debugger transport. If omitted, current dbgsettings are left
  untouched.

.PARAMETER SerialBaud
  Baud rate when -SerialDebugPort is passed. Default 115200.

.EXAMPLE
  .\setup-target.ps1                           # leaves dbgsettings alone
  .\setup-target.ps1 -SerialDebugPort 1        # sets serial debug on COM1@115200
#>
param(
  [int]$SerialDebugPort,
  [int]$SerialBaud = 115200
)

$ErrorActionPreference = "Stop"

function Step([string]$msg) { Write-Host "`n== $msg ==" -ForegroundColor Cyan }

# ---------- 1. OpenSSH Server ----------
Step "Installing OpenSSH Server"
$cap = Get-WindowsCapability -Online -Name "OpenSSH.Server*"
if ($cap.State -ne "Installed") {
  Add-WindowsCapability -Online -Name $cap.Name | Out-Null
}
Set-Service -Name sshd -StartupType Automatic
Start-Service sshd
if (-not (Get-NetFirewallRule -Name "OpenSSH-Server-In-TCP" -ErrorAction SilentlyContinue)) {
  New-NetFirewallRule -Name "OpenSSH-Server-In-TCP" -DisplayName "OpenSSH Server (TCP-In)" `
    -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}
Write-Host "  sshd running, port 22 open."

# ---------- 2. Token policy ----------
Step "Setting LocalAccountTokenFilterPolicy = 1"
$key = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"
New-ItemProperty -Path $key -Name LocalAccountTokenFilterPolicy `
  -Value 1 -PropertyType DWORD -Force | Out-Null
Write-Host "  Admin tokens over SSH are now unfiltered."

# ---------- 3. Default SSH shell ----------
Step "Setting default SSH shell to PowerShell"
$pwshPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
New-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell `
  -Value $pwshPath -PropertyType String -Force | Out-Null
Write-Host "  SSH sessions land in PowerShell."

# ---------- 4. Kernel debug transport (serial only on this box) ----------
Step "Enabling kernel debug (/debug on)"
bcdedit /debug on | Out-Null

if ($PSBoundParameters.ContainsKey('SerialDebugPort')) {
  Step "Setting serial dbgsettings: debugport:$SerialDebugPort baudrate:$SerialBaud"
  bcdedit /dbgsettings serial debugport:$SerialDebugPort baudrate:$SerialBaud | Out-Null
} else {
  Write-Host "  -SerialDebugPort not supplied; leaving existing dbgsettings untouched."
  Write-Host "  Current dbgsettings:"
  bcdedit /dbgsettings | ForEach-Object { Write-Host "    $_" }
}

# ---------- 5. Recovery / crash behaviour ----------
Step "Disabling recovery loops + BSOD auto-restart"
bcdedit /set "{default}" recoveryenabled No        | Out-Null
bcdedit /set "{default}" bootstatuspolicy IgnoreAllFailures | Out-Null
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" -Name AutoReboot -Value 0
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" -Name CrashDumpEnabled -Value 3  # small minidump

# ---------- 5b. kd-friendly: stop the DPC watchdog from firing when we're broken in ----------
# When kd has the system, the kernel's wallclock can't track DPCs accurately;
# the DPC watchdog assertion ("This is NOT a break in update time / use gh!")
# then trips for purely kd-induced reasons and pollutes the kd log. These
# flags make the kernel less aggressive about timing assertions during debug.
Step "Quieting kd-related timing assertions"
bcdedit /set "{default}" disabledynamictick yes    | Out-Null
bcdedit /set "{default}" useplatformclock yes      | Out-Null
# DPC watchdog can also be hushed via reg; harmless if the kernel ignores it
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Kernel" `
  -Name DpcWatchdogPeriod -Value 0xFFFFFFFF -Type DWord -Force -ErrorAction SilentlyContinue
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Kernel" `
  -Name DpcWatchdogProfileOffset -Value 0xFFFFFFFF -Type DWord -Force -ErrorAction SilentlyContinue

# ---------- 6. Test-signing ----------
Step "Enabling test-signing"
bcdedit /set "{default}" testsigning on            | Out-Null

# ---------- 7. Drop dirs ----------
Step "Preparing C:\drv (driver landing pad)"
New-Item -ItemType Directory -Path "C:\drv" -Force | Out-Null

# ---------- 8. administrators_authorized_keys placeholder ----------
Step "Preparing administrators_authorized_keys"
$adminAuth = "$env:ProgramData\ssh\administrators_authorized_keys"
if (-not (Test-Path $adminAuth)) {
  New-Item -ItemType File -Path $adminAuth -Force | Out-Null
}
# Lock ACL: only SYSTEM + Administrators.
$acl = Get-Acl $adminAuth
$acl.SetAccessRuleProtection($true, $false)
$acl.Access | ForEach-Object { $acl.RemoveAccessRule($_) | Out-Null }
$acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
  "NT AUTHORITY\SYSTEM", "FullControl", "Allow")))
$acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
  "BUILTIN\Administrators", "FullControl", "Allow")))
Set-Acl -Path $adminAuth -AclObject $acl

# ---------- Summary ----------
Write-Host "`n================================================================" -ForegroundColor Green
Write-Host " SETUP COMPLETE - reboot the target to activate kdnet/testsigning." -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Hand these values back to the dev box (paste into Tools\target.psd1):"
Write-Host ""
$ip = (Get-NetIPAddress -AddressFamily IPv4 |
       Where-Object {$_.PrefixOrigin -eq 'Dhcp' -or $_.PrefixOrigin -eq 'Manual'} |
       Select-Object -First 1).IPAddress
Write-Host "  HostIp = '$ip'"
Write-Host "  (ComPort / ComBaud in target.psd1 are the DEV BOX side - the COM"
Write-Host "   port the kd.exe cable plugs into on your dev machine, e.g. COM3.)"
Write-Host ""
Write-Host "Then paste the dev box's SSH public key (id_ed25519.pub) into:"
Write-Host "  $adminAuth"
Write-Host ""
Write-Host "(Your account is in the Administrators group, so it MUST go in the"
Write-Host " admin file above, NOT in C:\Users\<you>\.ssh\authorized_keys.)"
