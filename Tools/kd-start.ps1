<#
.SYNOPSIS
  Launch kd.exe wrapped by kd-wrapper.py. Run this once per debug session.

.DESCRIPTION
  Reads endpoint info from target.psd1. kd.exe attaches to the dev-box
  COM port (defined in target.psd1's ComPort/ComBaud) and talks to the
  target's UART. Once running, send commands with Tools\kd-send.ps1.

.PARAMETER ComPort
  Override the COM port from target.psd1. Default: target.psd1 ComPort.

.PARAMETER ComBaud
  Override the baud rate from target.psd1. Default: target.psd1 ComBaud.

.PARAMETER Log
  Session log path. Default: build\kd\kd-<timestamp>.log

.EXAMPLE
  .\kd-start.ps1
  .\kd-start.ps1 -ComPort COM3 -ComBaud 115200
#>
param(
  [string]$ComPort,
  [int]$ComBaud,
  [string]$Log
)

$ErrorActionPreference = "Stop"
$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

$cfgPath = Join-Path $scriptDir "target.psd1"
if (-not (Test-Path $cfgPath)) { throw "Missing $cfgPath - see target.psd1.example" }
$T = Import-PowerShellDataFile -Path $cfgPath

if (-not $ComPort) { $ComPort = $T.ComPort }
if (-not $ComBaud) { $ComBaud = $T.ComBaud }
if (-not $ComPort) { throw "ComPort not set (target.psd1 or -ComPort)" }

if (-not $Log) {
  $logDir = Join-Path $workspaceRoot "build\kd"
  New-Item -ItemType Directory -Force -Path $logDir | Out-Null
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $Log = Join-Path $logDir "kd-$stamp.log"
}

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $python) { throw "python (or py launcher) not on PATH" }

$wrapper = Join-Path $scriptDir "kd-wrapper.py"

$argList = @($wrapper,
  "--mode",         "com",
  "--com-port",     $ComPort,
  "--com-baud",     $ComBaud,
  "--kd",           $T.KdExe,
  "--symbol-path",  $T.SymbolPath,
  "--cmd-port",     $T.KdCmdPort,
  "--log",          $Log)

Write-Host "Starting kd on $ComPort @ $ComBaud. Logs -> $Log" -ForegroundColor Cyan
Write-Host "Send commands from another shell with Tools\kd-send.ps1 ""...""." -ForegroundColor Cyan
& $python @argList
