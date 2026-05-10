<#
.SYNOPSIS
    Assembles every .va file under Shaders/sources/ into Valhall bytes and
    regenerates the C header used by the KMD (winmali_shader_programs.h).

.DESCRIPTION
    Requires:
      - Python 3 on PATH
      - Mesa checkout at ..\..\mesa (relative to this script) containing
        src/panfrost/compiler/bifrost/valhall/asm.py

    Output:
      - Shaders/generated/<name>.bin   (raw bytes per .va file)
      - A console dump of bytes you can paste into winmali_shader_programs.h

    This script does NOT overwrite winmali_shader_programs.h - the C header
    contains human-written comments and expected-output metadata that a
    generator can't know. Use it to refresh bytes when editing a .va.

.EXAMPLE
    PS> .\build-shaders.ps1

.NOTES
    This is an offline, developer-only tool. Neither the KMD nor the UMD
    depends on Python or Mesa at build or runtime.
#>

$ErrorActionPreference = "Stop"

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot    = Resolve-Path (Join-Path $scriptDir "..\..")
$sourcesDir  = Join-Path $scriptDir "..\Shaders\sources"
$outDir      = Join-Path $scriptDir "..\Shaders\generated"
$asm         = Join-Path $repoRoot "mesa\src\panfrost\compiler\bifrost\valhall\asm.py"

if (-not (Test-Path $asm)) {
    throw "Valhall assembler not found at $asm. Is Mesa cloned at $repoRoot\mesa?"
}

New-Item -Path $outDir -ItemType Directory -Force | Out-Null

$sources = Get-ChildItem -Path $sourcesDir -Filter *.va | Sort-Object Name
if ($sources.Count -eq 0) {
    Write-Warning "No .va sources under $sourcesDir"
    return
}

Write-Host "Assembling $($sources.Count) shader sources..." -ForegroundColor Cyan
Write-Host ""

foreach ($src in $sources) {
    $outBin = Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($src.Name) + ".bin")
    & python $asm $src.FullName $outBin
    if ($LASTEXITCODE -ne 0) {
        throw "asm.py failed on $($src.Name)"
    }

    $bytes = [System.IO.File]::ReadAllBytes($outBin)
    $hex   = ($bytes | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", "

    Write-Host ("  {0,-28} -> {1,3} bytes  ({2} instr)" -f $src.Name, $bytes.Length, ($bytes.Length / 8)) -ForegroundColor Green
    Write-Host "      $hex"
    Write-Host ""
}

Write-Host "Done. Paste the above bytes into Shaders\winmali_shader_programs.h if any .va changed." -ForegroundColor Cyan
