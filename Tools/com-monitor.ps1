<#
.SYNOPSIS
  Read-only COM monitor. For watching the target's UART when kdnet isn't
  ready yet (very early boot, pre-DxgkInitialize) or when kd itself is
  silent. NOT a debugger - just a tail.

.EXAMPLE
  .\com-monitor.ps1
  .\com-monitor.ps1 -Port COM5 -Baud 115200 -Log boot-log.txt
#>
param(
  [string]$Port,
  [int]$Baud,
  [string]$Log
)

$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

$cfgPath = Join-Path $scriptDir "target.psd1"
if (Test-Path $cfgPath) {
  $T = Import-PowerShellDataFile -Path $cfgPath
  if (-not $Port) { $Port = $T.ComPort }
  if (-not $Baud) { $Baud = $T.ComBaud }
}
if (-not $Port) { $Port = "COM5" }
if (-not $Baud) { $Baud = 115200 }

if (-not $Log) {
  $logDir = Join-Path $workspaceRoot "build\kd"
  New-Item -ItemType Directory -Force -Path $logDir | Out-Null
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $Log = Join-Path $logDir "com-$stamp.log"
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$sp.NewLine = "`n"
$sp.ReadTimeout = 200
$sp.Open()
Write-Host "Reading $Port @ $Baud -> $Log (Ctrl-C to stop)" -ForegroundColor Cyan
try {
  while ($true) {
    try {
      $line = $sp.ReadLine()
      $stamped = "[{0:HH:mm:ss.fff}] {1}" -f (Get-Date), $line.TrimEnd()
      Write-Host $stamped
      Add-Content -Path $Log -Value $stamped
    } catch [System.TimeoutException] { continue }
  }
} finally {
  $sp.Close()
  $sp.Dispose()
}
