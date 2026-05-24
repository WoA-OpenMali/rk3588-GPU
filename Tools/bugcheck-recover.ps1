<#
.SYNOPSIS
  Pull recent minidumps from the target and run cdb !analyze on each.

.DESCRIPTION
  After a BSOD, the target writes a small minidump under
  C:\Windows\Minidump\*.dmp. This script:
    1. lists the N most recent dumps on the target
    2. SCPs any we don't already have locally to <repo>\build\dumps\
    3. runs cdb -z dump.dmp -c "!analyze -v; .ecxr; kb; q"
    4. writes the analysis next to the dump as <dump>.analysis.txt

.PARAMETER Last
  How many newest dumps to consider. Default 3.

.PARAMETER OnlyNew
  Don't re-analyse dumps we already have. Default $true.
#>
param(
  [int]$Last = 3,
  [switch]$ReAnalyze
)

$ErrorActionPreference = "Stop"
$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

$cfgPath = Join-Path $scriptDir "target.psd1"
if (-not (Test-Path $cfgPath)) { throw "Missing $cfgPath" }
$T = Import-PowerShellDataFile -Path $cfgPath
$endpoint = "$($T.User)@$($T.HostIp)"

$dumpDir = Join-Path $workspaceRoot "build\dumps"
New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null

# Remote: list newest N minidumps
$listScript = @"
Get-ChildItem C:\Windows\Minidump\*.dmp -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First $Last |
  ForEach-Object { `$_.FullName }
"@
$remoteDumps = (& ssh -o BatchMode=yes $endpoint $listScript) | Where-Object { $_ }
if (-not $remoteDumps) { Write-Host "No minidumps on target." -ForegroundColor Yellow; return }

foreach ($remote in $remoteDumps) {
  $name = Split-Path $remote -Leaf
  $local = Join-Path $dumpDir $name
  if (-not (Test-Path $local)) {
    Write-Host "scp $name" -ForegroundColor Cyan
    & scp -o BatchMode=yes "${endpoint}:$($remote -replace '\\','/')" $local
  } elseif (-not $ReAnalyze) {
    Write-Host "already have $name (skip)" -ForegroundColor DarkGray
    continue
  }

  $analysis = "$local.analysis.txt"
  Write-Host "cdb !analyze -v $name" -ForegroundColor Cyan
  $cdbArgs = @("-z", $local,
               "-y", $T.SymbolPath,
               "-c", "!analyze -v; .ecxr; kb; q")
  & $T.CdbExe @cdbArgs | Tee-Object -FilePath $analysis | Out-Host
}

Write-Host "`nAnalyses in $dumpDir" -ForegroundColor Green
