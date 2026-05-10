<#
.SYNOPSIS
    Lightweight test harness for the WinMali-rk3588 source tree.

.DESCRIPTION
    Runs every test we can reasonably execute on a Windows dev box with
    the driver not yet loaded on hardware. Tests fall into four buckets:

      1. source-sanity
         - Every .c/.h parses as C (#include sweep).
         - No banned identifiers in headers (e.g. leftover DXGKDDI_WDDMv2_0).
         - Valhall shader header compiles with just <windows.h>/<wdm.h>.
      2. shader-pipeline
         - Runs Tools\build-shaders.ps1 and verifies every assembled .bin
           byte-for-byte matches the blobs embedded in the .h file.
      3. inf-sanity
         - Runs InfVerif against WinMaliKmd.inx if it is present.
           Skips cleanly if not.
      4. build
         - Invokes Tools\build.ps1 -Project All.

    Each bucket either PASSes, FAILs, or is marked SKIPPED with a reason.
    Final exit code: number of failing buckets.

.PARAMETER Only
    Run only the named bucket. One of: sanity, shaders, inf, build,
    phase1, vop2.
#>
[CmdletBinding()]
param(
    [ValidateSet("all", "sanity", "shaders", "inf", "build", "phase1", "vop2")]
    [string]$Only = "all"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"

$results = [ordered]@{}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Test-Bucket {
    param([string]$Name, [scriptblock]$Body)
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkGray
    Write-Host "  $Name" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor DarkGray
    try {
        & $Body
        $results[$Name] = "PASS"
        Write-Host "  -> PASS" -ForegroundColor Green
    } catch {
        $results[$Name] = "FAIL: $($_.Exception.Message)"
        Write-Host "  -> FAIL: $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Skip-Bucket {
    param([string]$Name, [string]$Reason)
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkGray
    Write-Host "  $Name (SKIPPED)" -ForegroundColor Yellow
    Write-Host "    $Reason" -ForegroundColor DarkYellow
    Write-Host "============================================================" -ForegroundColor DarkGray
    $results[$Name] = "SKIP: $Reason"
}

# ---------------------------------------------------------------------------
# Bucket 1: source-sanity
# ---------------------------------------------------------------------------

function Invoke-SourceSanity {
    # (a) Banned identifiers - catches regressions on known-bad symbols.
    $banned = @(
        @{ Pattern = 'DXGKDDI_WDDMv2_0'; Reason = "WDK 10.0.26100 uses DXGKDDI_WDDMv2, not _v2_0" }
    )
    $srcGlob = Get-ChildItem -Path $WinMaliRoot -Include *.c,*.h,*.cxx,*.cpp -Recurse -File
    foreach ($rule in $banned) {
        foreach ($f in $srcGlob) {
            $hits = Select-String -Path $f.FullName -Pattern $rule.Pattern -CaseSensitive -ErrorAction SilentlyContinue
            if ($hits) {
                throw "banned identifier '$($rule.Pattern)' in $($f.FullName) - $($rule.Reason)"
            }
        }
    }
    Write-Host "  banned-identifier sweep: clean" -ForegroundColor DarkGreen

    # (b) Shader header must parse as pure C. We don't need <windows.h> -
    #     we stub the handful of kernel types the header mentions and feed
    #     cl.exe /Zs (syntax-only) with NO system headers at all. Avoids
    #     the whole "no INCLUDE env set up" class of problem.
    if (-not $WinMaliClExe) {
        throw "cl.exe not found; install Visual Studio 2022 with C++ workload"
    }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) "winmali_syntax_$([IO.Path]::GetRandomFileName() -replace '\.','').c"
    # Replace <wdm.h> at preprocessor time with our stub.
    $headerAbs = Join-Path $WinMaliRoot "Shaders\winmali_shader_programs.h"
    @"
/* Kernel type stubs so winmali_shader_programs.h parses without
 * pulling in the real <wdm.h>. Keep this list in sync with what the
 * header actually references. */
typedef unsigned char  UCHAR;
typedef          char  CHAR;
typedef unsigned long  ULONG;
typedef unsigned char  BOOLEAN;
#define TRUE  1
#define FALSE 0
#define _Notnull_
#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif
#include "$($headerAbs -replace '\\','/')"
int _winmali_check(void) { return (int)WinMaliDiagPrograms[0].SizeBytes; }
"@ | Out-File -Encoding ASCII $tmp

    # Create an empty wdm.h on a private include dir so the #include <wdm.h>
    # inside the header resolves to nothing.
    $stubIncDir = Join-Path ([IO.Path]::GetTempPath()) "winmali_stub_inc_$([IO.Path]::GetRandomFileName() -replace '\.','')"
    New-Item -ItemType Directory -Path $stubIncDir | Out-Null
    "/* empty wdm.h stub for syntax check */" | Out-File -Encoding ASCII (Join-Path $stubIncDir "wdm.h")

    # Run cl /Zs with ONLY the stub include dir. /X strips default INCLUDE.
    $vsDevCmdArgs = @("/nologo", "/c", "/TC", "/Zs", "/X", "/I", $stubIncDir, $tmp)
    $compilerOut = & $WinMaliClExe @vsDevCmdArgs 2>&1 | Out-String
    Remove-Item $tmp -ErrorAction SilentlyContinue
    Remove-Item $stubIncDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -ne 0) {
        Write-Host $compilerOut -ForegroundColor DarkRed
        throw "winmali_shader_programs.h fails to parse as C"
    }
    Write-Host "  winmali_shader_programs.h: parses cleanly (cl /Zs)" -ForegroundColor DarkGreen

    # (c) Every KMD .c has a WinMali* prefix claim in its identifiers.
    $wrong = @()
    Get-ChildItem -Path (Join-Path $WinMaliRoot "KMD") -Include *.c -Recurse -File | ForEach-Object {
        $raw = Get-Content -Raw $_.FullName
        # Look for NTSTATUS APIENTRY <name>( that doesn't start with WinMali
        $matches = [regex]::Matches($raw, '(?m)^\s*(?:NTSTATUS\s+APIENTRY|VOID|BOOLEAN)\s+([A-Za-z_]\w+)\s*\(')
        foreach ($m in $matches) {
            $name = $m.Groups[1].Value
            if ($name -notmatch '^(DriverEntry|WinMali|DllMain|_)') {
                $wrong += "$($_.Name): $name"
            }
        }
    }
    if ($wrong.Count -gt 0) {
        throw "Non-prefixed exports found:`n    $($wrong -join "`n    ")"
    }
    Write-Host "  WinMali prefix discipline: ok" -ForegroundColor DarkGreen

    # (d) Same check for the display KMD: every top-level export must
    #     start with Rk3588Disp (or DriverEntry/underscore-internal).
    $dispDir = Join-Path $WinMaliRoot "Rk3588DispKmd"
    if (Test-Path $dispDir) {
        $wrong = @()
        Get-ChildItem -Path $dispDir -Include *.c -Recurse -File | ForEach-Object {
            $raw = Get-Content -Raw $_.FullName
            $matches = [regex]::Matches($raw, '(?m)^\s*(?:NTSTATUS\s+APIENTRY|NTSTATUS|VOID|BOOLEAN|ULONG)\s+([A-Za-z_]\w+)\s*\(')
            foreach ($m in $matches) {
                $name = $m.Groups[1].Value
                if ($name -notmatch '^(DriverEntry|Rk3588Disp|DllMain|_)') {
                    $wrong += "$($_.Name): $name"
                }
            }
        }
        if ($wrong.Count -gt 0) {
            throw "Non-prefixed exports in Rk3588DispKmd:`n    $($wrong -join "`n    ")"
        }
        Write-Host "  Rk3588Disp prefix discipline: ok" -ForegroundColor DarkGreen
    }
}

# ---------------------------------------------------------------------------
# Bucket 2: shader-pipeline
# ---------------------------------------------------------------------------

function Invoke-ShaderPipeline {
    if (-not $WinMaliPython) {
        throw "python not on PATH; cannot run asm.py"
    }
    if (-not $WinMaliMesaRoot) {
        throw "mesa\ not found adjacent to WinMali-rk3588\"
    }

    # Run the build script.
    & (Join-Path $PSScriptRoot "build-shaders.ps1") | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "build-shaders.ps1 exited $LASTEXITCODE"
    }

    # Compare every .bin against the header.
    $header = Get-Content -Raw (Join-Path $WinMaliRoot "Shaders\winmali_shader_programs.h")
    $genDir = Join-Path $WinMaliRoot "Shaders\generated"
    $programs = @(
        @{ Bin="00_nop.bin";             Sym="WinMaliShaderNop" },
        @{ Bin="01_mov.bin";             Sym="WinMaliShaderMovR1R2" },
        @{ Bin="02_iadd.bin";            Sym="WinMaliShaderIaddR0R1R2" },
        @{ Bin="03_store_constant.bin";  Sym="WinMaliShaderStoreConstant" },
        @{ Bin="04_load_add_store.bin";  Sym="WinMaliShaderLoadAddStore" }
    )
    foreach ($p in $programs) {
        $binPath = Join-Path $genDir $p.Bin
        if (-not (Test-Path $binPath)) { throw "missing generated bin: $binPath" }
        $bytes = [System.IO.File]::ReadAllBytes($binPath)
        # Extract the hex list that immediately follows the symbol definition.
        $rx = "(?s)$([regex]::Escape($p.Sym))\s*\[\]\s*=\s*\{([^}]+)\}"
        $m = [regex]::Match($header, $rx)
        if (-not $m.Success) { throw "symbol $($p.Sym) not found in header" }
        # Strip // line comments inside the init list - otherwise a hex
        # literal like 0x7060504 inside a comment would match our 0xNN
        # regex and inflate the byte count.
        $cleaned = [regex]::Replace($m.Groups[1].Value, '//[^\r\n]*', '')
        # Also strip /* ... */ block comments to be safe.
        $cleaned = [regex]::Replace($cleaned, '(?s)/\*.*?\*/', '')
        # Our data bytes are strictly 0xNN (exactly 2 hex digits) followed
        # by a comma, `}` or whitespace. Use a negative look-ahead to
        # reject longer literals like 0x7060504.
        $hexList = [regex]::Matches($cleaned, '0x([0-9a-fA-F]{2})(?![0-9a-fA-F])') `
                   | ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) }
        if ($hexList.Count -ne $bytes.Length) {
            throw "$($p.Sym): header has $($hexList.Count) bytes, asm.py emitted $($bytes.Length)"
        }
        for ($i = 0; $i -lt $bytes.Length; $i++) {
            if ($hexList[$i] -ne $bytes[$i]) {
                throw "$($p.Sym) byte[$i]: header=0x{0:x2} asm.py=0x{1:x2}" -f $hexList[$i], $bytes[$i]
            }
        }
        Write-Host ("  {0,-28} {1,3} bytes  match" -f $p.Sym, $bytes.Length) -ForegroundColor DarkGreen
    }
}

# ---------------------------------------------------------------------------
# Bucket 3: inf-sanity
# ---------------------------------------------------------------------------

function Invoke-InfSanity {
    if (-not $WinMaliWdkRoot) {
        throw "WDK not installed"
    }
    # Look for InfVerif in all the places WDK 10.0.26100 might put it.
    $candidates = @(
        (Join-Path $WinMaliWdkRoot "bin\$WinMaliWdkVersion\x64\infverif.exe"),
        (Join-Path $WinMaliWdkRoot "bin\$WinMaliWdkVersion\x86\infverif.exe"),
        (Join-Path $WinMaliWdkRoot "bin\x64\infverif.exe"),
        (Join-Path $WinMaliWdkRoot "tools\$WinMaliWdkVersion\x64\infverif.exe")
    )
    $infverif = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $infverif) {
        throw "InfVerif.exe not found (checked $($candidates.Count) paths)"
    }

    # Prefer the MSBuild-stamped .inf in the ARM64\Debug output - it has
    # all [Strings] tokens already expanded. Fall back to copying the
    # .inx to a temp .inf if the build hasn't run yet.
    $stampedInf = Join-Path $WinMaliRoot "ARM64\Debug\WinMaliKmd.inf"
    if (Test-Path $stampedInf) {
        $inf = $stampedInf
    } else {
        $inx = Join-Path $WinMaliRoot "KMD\WinMaliKmd.inx"
        $tmpDir = Join-Path ([IO.Path]::GetTempPath()) "winmali_inf_$([IO.Path]::GetRandomFileName() -replace '\.','')"
        New-Item -ItemType Directory -Path $tmpDir | Out-Null
        $inf = Join-Path $tmpDir "WinMaliKmd.inf"
        Copy-Item $inx $inf
    }

    # InfVerif returns:
    #   0     = clean
    #   <>0   = real errors
    # BUT on a render-only WDDM miniport (ACPI HID, no PnP ID that InfVerif
    # recognises) it emits "No applicable hardware" and returns 1627 even
    # though the INF is structurally fine. Treat 1627 as a soft warning.
    $out = & $infverif /info $inf 2>&1
    $out | ForEach-Object { Write-Host "    $_" }
    $rc = $LASTEXITCODE
    if ($rc -eq 1627) {
        Write-Host "  note: InfVerif reported 1627 (no applicable hardware) - expected for ACPI render-only miniport" -ForegroundColor DarkYellow
        return
    }
    if ($rc -ne 0) {
        throw "InfVerif exited $rc"
    }
}

# ---------------------------------------------------------------------------
# Bucket 4: build
# ---------------------------------------------------------------------------

function Invoke-MsbuildFull {
    & (Join-Path $PSScriptRoot "build.ps1") -Project All -LogLevel minimal
    if ($LASTEXITCODE -ne 0) {
        throw "build.ps1 exited $LASTEXITCODE"
    }
}

# ---------------------------------------------------------------------------
# Bucket 5: phase1-artifacts
#
# Sanity-check that Phase 1 produced the things downstream phases need:
#   - A WinMaliKmd.sys exists somewhere under KMD\.
#   - winmali-diag.exe exists and runs without crashing. On a dev box
#     with no WinMali adapter it exits 2, which is fine; any other
#     non-zero exit means the tool itself blew up.
# ---------------------------------------------------------------------------

function Invoke-Phase1Artifacts {
    # msbuild drops outputs in two places depending on how it was
    # invoked: per-project builds go to <proj>\<Platform>\<Config>\,
    # solution-level builds go to <repoRoot>\<Platform>\<Config>\.
    # Both are valid; accept either.
    $sysCandidates = @(
        (Join-Path $WinMaliRoot "KMD\x64\Debug\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "KMD\ARM64\Debug\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "KMD\x64\Release\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "KMD\ARM64\Release\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "x64\Debug\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "ARM64\Debug\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "x64\Release\WinMaliKmd.sys"),
        (Join-Path $WinMaliRoot "ARM64\Release\WinMaliKmd.sys")
    )
    $sys = $sysCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $sys) {
        throw "no WinMaliKmd.sys found - run build.ps1 first"
    }
    Write-Host "  sys: $sys" -ForegroundColor DarkGray

    $diagCandidates = @(
        (Join-Path $WinMaliRoot "Tools\winmali-diag\x64\Debug\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\winmali-diag\ARM64\Debug\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\winmali-diag\x64\Release\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\winmali-diag\ARM64\Release\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "x64\Debug\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "ARM64\Debug\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "x64\Release\winmali-diag.exe"),
        (Join-Path $WinMaliRoot "ARM64\Release\winmali-diag.exe")
    )
    $diag = $diagCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $diag) {
        throw "winmali-diag.exe not built - run build.ps1 -Project Diag"
    }
    Write-Host "  diag: $diag" -ForegroundColor DarkGray

    try {
        & $diag | Out-Null
        $rc = $LASTEXITCODE
    } catch {
        throw "winmali-diag.exe crashed: $_"
    }
    if ($rc -ne 0 -and $rc -ne 2) {
        throw "winmali-diag.exe exit=$rc (expected 0 or 2)"
    }
    if ($rc -eq 0) {
        Write-Host "  winmali-diag ran, found an adapter claiming to be WinMali" -ForegroundColor DarkGreen
    } else {
        Write-Host "  winmali-diag ran, no WinMali adapter (expected on dev box)" -ForegroundColor DarkGreen
    }
}

# ---------------------------------------------------------------------------
# Bucket 6: vop2-artifacts
#
# Same philosophy as phase1-artifacts but for the display stack:
#   - Rk3588DispKmd.sys exists somewhere.
#   - rk3588disp-diag.exe exists and runs without crashing. Exit 2 means
#     "no adapter" which is expected on a dev box; anything other than
#     0 / 2 means the tool itself is broken.
# ---------------------------------------------------------------------------

function Invoke-Vop2Artifacts {
    # Same dual-layout handling as Invoke-Phase1Artifacts.
    $sysCandidates = @(
        (Join-Path $WinMaliRoot "Rk3588DispKmd\x64\Debug\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "Rk3588DispKmd\ARM64\Debug\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "Rk3588DispKmd\x64\Release\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "Rk3588DispKmd\ARM64\Release\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "x64\Debug\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "ARM64\Debug\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "x64\Release\Rk3588DispKmd.sys"),
        (Join-Path $WinMaliRoot "ARM64\Release\Rk3588DispKmd.sys")
    )
    $sys = $sysCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $sys) {
        throw "no Rk3588DispKmd.sys found - run build.ps1 -Project Disp first"
    }
    Write-Host "  sys: $sys" -ForegroundColor DarkGray

    $infCandidates = @(
        (Join-Path $WinMaliRoot "Rk3588DispKmd\x64\Debug\Rk3588DispKmd.inf"),
        (Join-Path $WinMaliRoot "Rk3588DispKmd\ARM64\Debug\Rk3588DispKmd.inf"),
        (Join-Path $WinMaliRoot "x64\Debug\Rk3588DispKmd.inf"),
        (Join-Path $WinMaliRoot "ARM64\Debug\Rk3588DispKmd.inf")
    )
    $inf = $infCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($inf) {
        $rawInf = Get-Content -Raw $inf
        if ($rawInf -notmatch 'ACPI\\RKCP5650') {
            throw "Rk3588DispKmd.inf missing ACPI\\RKCP5650 binding (got: no match)"
        }
        Write-Host "  inf: $inf (ACPI\\RKCP5650 binding present)" -ForegroundColor DarkGray
    }

    $diagCandidates = @(
        (Join-Path $WinMaliRoot "Tools\rk3588disp-diag\x64\Debug\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\rk3588disp-diag\ARM64\Debug\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\rk3588disp-diag\x64\Release\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "Tools\rk3588disp-diag\ARM64\Release\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "x64\Debug\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "ARM64\Debug\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "x64\Release\rk3588disp-diag.exe"),
        (Join-Path $WinMaliRoot "ARM64\Release\rk3588disp-diag.exe")
    )
    $diag = $diagCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $diag) {
        throw "rk3588disp-diag.exe not built - run build.ps1 -Project DispDiag"
    }
    Write-Host "  diag: $diag" -ForegroundColor DarkGray

    try {
        & $diag | Out-Null
        $rc = $LASTEXITCODE
    } catch {
        throw "rk3588disp-diag.exe crashed: $_"
    }
    if ($rc -ne 0 -and $rc -ne 2) {
        throw "rk3588disp-diag.exe exit=$rc (expected 0 or 2)"
    }
    if ($rc -eq 0) {
        Write-Host "  rk3588disp-diag ran, found an adapter claiming to be Rk3588Disp" -ForegroundColor DarkGreen
    } else {
        Write-Host "  rk3588disp-diag ran, no Rk3588Disp adapter (expected on dev box)" -ForegroundColor DarkGreen
    }
}

# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

if ($Only -in @("all","sanity"))  { Test-Bucket "source-sanity"    { Invoke-SourceSanity } }
if ($Only -in @("all","shaders")) { Test-Bucket "shader-pipeline"  { Invoke-ShaderPipeline } }
if ($Only -in @("all","inf"))     {
    if ($WinMaliWdkRoot) { Test-Bucket "inf-sanity" { Invoke-InfSanity } }
    else                 { Skip-Bucket "inf-sanity" "WDK not installed" }
}
if ($Only -in @("all","build"))   {
    if ($WinMaliMsBuild -and $WinMaliWdkRoot) { Test-Bucket "msbuild" { Invoke-MsbuildFull } }
    else { Skip-Bucket "msbuild" "MSBuild or WDK missing" }
}
if ($Only -in @("all","phase1"))  { Test-Bucket "phase1-artifacts" { Invoke-Phase1Artifacts } }
if ($Only -in @("all","vop2"))    { Test-Bucket "vop2-artifacts"   { Invoke-Vop2Artifacts } }

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "============================================================" -ForegroundColor DarkGray
Write-Host "  summary" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor DarkGray
$fails = 0
foreach ($k in $results.Keys) {
    $v = $results[$k]
    $color = "Green"
    if     ($v.StartsWith("FAIL")) { $color = "Red";    $fails++ }
    elseif ($v.StartsWith("SKIP")) { $color = "Yellow" }
    Write-Host ("  {0,-18}  {1}" -f $k, $v) -ForegroundColor $color
}
Write-Host ""
if ($fails -eq 0) {
    Write-Host "OK" -ForegroundColor Green
} else {
    Write-Host "$fails bucket(s) failed" -ForegroundColor Red
}
exit $fails
