<#
.SYNOPSIS
    Nuke the shared build\ tree and per-project Visual Studio caches.

.DESCRIPTION
    The single-folder build layout (build\ at the repo root) means the
    99% case is just: delete that folder. This script does that and
    also clears the .vs\ cache and per-project .user files so a stale
    user-properties drag doesn't surprise you on the next build.

    Examples:
        .\Tools\clean.ps1                # delete <repo>\build
        .\Tools\clean.ps1 -KeepMesa      # leave the meson build tree
        .\Tools\clean.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [switch]$KeepMesa,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"

function Remove-Target([string]$path, [string]$label) {
    if (-not (Test-Path $path)) {
        Write-Host "skip  ($label not present): $path" -ForegroundColor DarkGray
        return
    }
    if ($DryRun) {
        Write-Host "would remove ($label): $path" -ForegroundColor Yellow
        return
    }
    Write-Host "rm    ($label): $path" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $path
}

if ($KeepMesa -and (Test-Path $WinMaliBuildDir)) {
    Get-ChildItem $WinMaliBuildDir -Directory |
        Where-Object { $_.Name -ne 'mesa' } |
        ForEach-Object { Remove-Target $_.FullName "build subdir" }
} else {
    Remove-Target $WinMaliBuildDir "build/"
}

Remove-Target (Join-Path $WinMaliRoot ".vs") ".vs cache"

Get-ChildItem $WinMaliRoot -Recurse -File -Filter "*.user" -EA SilentlyContinue | ForEach-Object {
    Remove-Target $_.FullName ".user file"
}

Write-Host "Clean: done" -ForegroundColor Green
