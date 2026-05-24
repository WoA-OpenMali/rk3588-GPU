<#
.SYNOPSIS
  Send a command string to a running kd-wrapper.py session.

.EXAMPLE
  .\kd-send.ps1 "bp WinMaliKmd!DxgkDdiStartDevice"
  .\kd-send.ps1 "g"
  .\kd-send.ps1 "kb; r; dt nt!_DRIVER_OBJECT @rcx"
#>
param(
  [Parameter(Mandatory=$true, Position=0, ValueFromRemainingArguments=$true)]
  [string[]]$Command,
  [int]$Port  # default from target.psd1
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Port) {
  $cfgPath = Join-Path $scriptDir "target.psd1"
  if (Test-Path $cfgPath) { $Port = (Import-PowerShellDataFile $cfgPath).KdCmdPort }
  if (-not $Port) { $Port = 5556 }
}

$text = ($Command -join " ")
$client = [System.Net.Sockets.TcpClient]::new()
$client.Connect("127.0.0.1", $Port)
$stream = $client.GetStream()
$bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
$stream.Write($bytes, 0, $bytes.Length)
$stream.Flush()
$stream.Close()
$client.Close()
Write-Host ">> $text" -ForegroundColor DarkGray
