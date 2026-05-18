<#
.SYNOPSIS
    Build the WinMali mesa-modern build-time dependencies that have no
    Windows packages (LLVM dev libs + libclc + SPIRV-LLVM-Translator +
    SPIRV-Tools). Installs everything into $InstallPrefix.

.DESCRIPTION
    Mesa-modern's panfrost gallium driver and panvk-v10 Vulkan driver
    both rely on compile-time SPIR-V kernels emitted by `mesa_clc`,
    which in turn needs LLVM dev libs + libclc + SPIRV-LLVM-Translator.
    None of these ship as Windows packages.

    Modeled on mesa's .gitlab-ci/windows/mesa_deps_build.ps1 but version-
    matched to our pre-installed LLVM 18.1.8 binary (so the bitcode
    libclc emits is loadable by mesa at runtime with the same LLVM the
    clang-cl 18 we're already using is based on).

    Steps:
      1. Clone llvmorg-18.1.8 source + SPIRV-LLVM-Translator into deps/.
      2. cmake + ninja build LLVM + Clang + SPIRV-LLVM-Translator into
         $InstallPrefix.
      3. cmake + ninja build libclc using the freshly-installed LLVM
         tools into $InstallPrefix.
      4. Clone + build SPIRV-Tools into $InstallPrefix.

    Idempotent: skips any step whose install-side marker already exists.

.PARAMETER InstallPrefix
    Where to install the produced libs/headers/tools. Defaults to
    C:\mesa-deps.

.PARAMETER WorkDir
    Where to clone/build. Defaults to <InstallPrefix>\src.

.PARAMETER LlvmVersion
    LLVM source tag. Defaults to llvmorg-18.1.8 (matches our binary
    clang-cl install).

.PARAMETER SpirvTranslatorBranch
    SPIRV-LLVM-Translator branch. Defaults to llvm_release_180.

.PARAMETER SpirvToolsTag
    SPIRV-Tools tag. Defaults to vulkan-sdk-1.3.290.0 (a stable point
    in the SPIRV-Headers/Tools release matrix that builds against
    LLVM 18).

.PARAMETER Force
    Wipe deps/ and rebuild from scratch.
#>
[CmdletBinding()]
param(
    [string]$InstallPrefix         = "C:\mesa-deps",
    [string]$WorkDir               = "",
    [string]$LlvmVersion           = "llvmorg-18.1.8",
    [string]$SpirvTranslatorBranch = "llvm_release_180",
    [string]$SpirvToolsTag         = "vulkan-sdk-1.4.350.0",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\env.ps1"

if (-not $WorkDir) { $WorkDir = Join-Path $InstallPrefix "src" }

# The dep build (LLVM 18.1.8 + Clang) needs a newer MSVC than VS 2017
# ships - LLVM 18's CheckCompilerVersion requires MSVC >= 14.27, and
# LLVM source uses std::isdigit (added to the STL in VS 2019). VS 2022
# Build Tools is installed side-by-side at C:\BuildTools for this
# purpose; the WinMali driver itself still builds against VS 2017 +
# WDK 1809.
$bt = "C:\BuildTools\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $bt)) {
    Write-Host "FATAL: VS 2022 Build Tools not found at C:\BuildTools\" -ForegroundColor Red
    Write-Host "Run vs_BuildTools.exe with --installPath C:\BuildTools first." -ForegroundColor Yellow
    exit 1
}
Write-Host "Entering VS 2022 Build Tools dev env (C:\BuildTools)..." -ForegroundColor DarkCyan
$marker = "__MESA_DEPS_VSDEVCMD_DONE__"
$output = & cmd.exe /c "`"$bt`" -arch=x64 -host_arch=x64 -no_logo >nul && echo $marker&& set"
$seen = $false
foreach ($line in $output) {
    if (-not $seen) {
        if ($line.Trim() -eq $marker) { $seen = $true }
        continue
    }
    if ($line -match '^([^=]+)=(.*)$') {
        $name = $matches[1]
        $val  = $matches[2]
        if ($name -in @('PROCESSOR_ARCHITECTURE','PROCESSOR_IDENTIFIER',
                        'PROCESSOR_LEVEL','PROCESSOR_REVISION','PROMPT','=ExitCode')) {
            continue
        }
        Set-Item -Path "Env:$name" -Value $val -ErrorAction SilentlyContinue
    }
}
if (-not $env:VSINSTALLDIR -or -not $env:VSINSTALLDIR.StartsWith("C:\BuildTools")) {
    Write-Host "FATAL: VS 2022 Build Tools dev env did not load (VSINSTALLDIR=$env:VSINSTALLDIR)" -ForegroundColor Red
    exit 2
}

# Tools we need on PATH for the build. CMake from the portable zip,
# ninja from the pip install, git from Git for Windows.
$cmakeBin = (Get-ChildItem "C:\tools\cmake-*\bin\cmake.exe" -EA SilentlyContinue |
             Select-Object -First 1).FullName
if (-not $cmakeBin) {
    Write-Host "FATAL: cmake not found under C:\tools\cmake-*\bin\" -ForegroundColor Red
    exit 2
}
$cmakeDir = Split-Path $cmakeBin -Parent
if ($env:PATH -notlike "*$cmakeDir*") { $env:PATH = "$cmakeDir;$env:PATH" }

if (-not $WinMaliNinja) {
    Write-Host "FATAL: ninja not discovered by env.ps1" -ForegroundColor Red
    exit 3
}
$ninjaDir = Split-Path $WinMaliNinja -Parent
if ($env:PATH -notlike "*$ninjaDir*") { $env:PATH = "$ninjaDir;$env:PATH" }

if (-not (Get-Command git -EA SilentlyContinue)) {
    Write-Host "FATAL: git not on PATH (install Git for Windows)" -ForegroundColor Red
    exit 4
}

# Force a TLS 1.2/1.3 minimum so GitHub clones don't fall over.
[Net.ServicePointManager]::SecurityProtocol =
   [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13

if ($Force -and (Test-Path $WorkDir)) {
    Write-Host "Force: removing $WorkDir" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $WorkDir
}

New-Item -ItemType Directory -Force -Path $InstallPrefix | Out-Null
New-Item -ItemType Directory -Force -Path $WorkDir       | Out-Null

# ----------------------------------------------------------------------
# Step 1 - LLVM + Clang + SPIRV-LLVM-Translator
# ----------------------------------------------------------------------

$llvmInstalledMarker = Join-Path $InstallPrefix "bin\llvm-as.exe"
if (Test-Path $llvmInstalledMarker) {
    Write-Host "== LLVM + Clang already installed; skipping ==" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "== Step 1: LLVM + Clang $LlvmVersion ==" -ForegroundColor Cyan

    $llvmSrc = Join-Path $WorkDir "llvm-project"
    if (-not (Test-Path $llvmSrc)) {
        Write-Host "Cloning llvm-project @ $LlvmVersion..." -ForegroundColor DarkCyan
        & git clone -b $LlvmVersion --depth=1 https://github.com/llvm/llvm-project $llvmSrc
        if ($LASTEXITCODE -ne 0) { Write-Host "git clone llvm-project failed" -ForegroundColor Red; exit 11 }
    }

    $translatorSrc = Join-Path $llvmSrc "llvm\projects\SPIRV-LLVM-Translator"
    if (-not (Test-Path $translatorSrc)) {
        Write-Host "Cloning SPIRV-LLVM-Translator @ $SpirvTranslatorBranch..." -ForegroundColor DarkCyan
        & git clone -b $SpirvTranslatorBranch --depth=1 https://github.com/KhronosGroup/SPIRV-LLVM-Translator $translatorSrc
        if ($LASTEXITCODE -ne 0) { Write-Host "git clone translator failed" -ForegroundColor Red; exit 12 }
    }

    $llvmBuild = Join-Path $WorkDir "llvm-build"
    New-Item -ItemType Directory -Force -Path $llvmBuild | Out-Null
    # Build LLVM with VS 2022 Build Tools' cl.exe (auto-picked from
    # the env we entered). That's MSVC 14.44 which satisfies LLVM 18's
    # >= 14.27 requirement and has the std::isdigit STL entry it
    # expects. No -fms-compatibility-version dance needed.
    Push-Location $llvmBuild
    try {
        Write-Host "Configuring LLVM (VS 2022 cl.exe, MultiThreadedDLL)..." -ForegroundColor DarkCyan
        & cmake (Join-Path $llvmSrc "llvm") `
            -GNinja `
            -DCMAKE_BUILD_TYPE=Release `
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
            -DCMAKE_INSTALL_PREFIX="$InstallPrefix" `
            -DCMAKE_C_FLAGS="/utf-8" `
            -DCMAKE_CXX_FLAGS="/utf-8" `
            -DLLVM_ENABLE_PROJECTS="clang" `
            -DLLVM_TARGETS_TO_BUILD="AArch64;X86" `
            -DLLVM_OPTIMIZED_TABLEGEN=TRUE `
            -DLLVM_ENABLE_ASSERTIONS=FALSE `
            -DLLVM_INCLUDE_UTILS=OFF `
            -DLLVM_INCLUDE_RUNTIMES=OFF `
            -DLLVM_INCLUDE_TESTS=OFF `
            -DLLVM_INCLUDE_EXAMPLES=OFF `
            -DLLVM_INCLUDE_GO_TESTS=OFF `
            -DLLVM_INCLUDE_BENCHMARKS=OFF `
            -DLLVM_BUILD_LLVM_C_DYLIB=OFF `
            -DLLVM_ENABLE_DIA_SDK=OFF `
            -DCLANG_BUILD_TOOLS=ON `
            -DLLVM_SPIRV_INCLUDE_TESTS=OFF `
            -DLLVM_ENABLE_ZLIB=OFF `
            -Wno-dev
        if ($LASTEXITCODE -ne 0) { Write-Host "cmake llvm failed" -ForegroundColor Red; throw "llvm cmake failure" }

        Write-Host "Compiling LLVM (this is the big one)..." -ForegroundColor DarkCyan
        & ninja install
        if ($LASTEXITCODE -ne 0) { Write-Host "ninja llvm install failed" -ForegroundColor Red; throw "llvm ninja failure" }
    } finally {
        Pop-Location
    }
}

# ----------------------------------------------------------------------
# Step 2 - libclc
# ----------------------------------------------------------------------

$libclcInstalledMarker = Get-ChildItem -Recurse -ErrorAction SilentlyContinue `
                            (Join-Path $InstallPrefix "share\clc") -Filter "*.bc" |
                        Select-Object -First 1
if ($libclcInstalledMarker) {
    Write-Host "== libclc already installed; skipping ==" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "== Step 2: libclc ==" -ForegroundColor Cyan

    $llvmSrc = Join-Path $WorkDir "llvm-project"
    if (-not (Test-Path (Join-Path $llvmSrc "libclc"))) {
        Write-Host "FATAL: $llvmSrc\libclc missing (LLVM step needed first)" -ForegroundColor Red
        exit 21
    }

    $libclcBuild = Join-Path $WorkDir "libclc-build"
    if (Test-Path $libclcBuild) {
        Remove-Item -Recurse -Force $libclcBuild
    }
    New-Item -ItemType Directory -Force -Path $libclcBuild | Out-Null
    Push-Location $libclcBuild
    try {
        Write-Host "Configuring libclc..." -ForegroundColor DarkCyan
        # mesa only consumes the spirv-mesa3d- bitcode targets.
        & cmake (Join-Path $llvmSrc "libclc") `
            -GNinja `
            -DCMAKE_BUILD_TYPE=Release `
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
            -DCMAKE_INSTALL_PREFIX="$InstallPrefix" `
            -DLIBCLC_TARGETS_TO_BUILD="spirv-mesa3d-;spirv64-mesa3d-"
        if ($LASTEXITCODE -ne 0) { Write-Host "cmake libclc failed" -ForegroundColor Red; exit 22 }

        Write-Host "Compiling libclc..." -ForegroundColor DarkCyan
        & ninja install
        if ($LASTEXITCODE -ne 0) { Write-Host "ninja libclc install failed" -ForegroundColor Red; exit 23 }
    } finally {
        Pop-Location
    }
}

# ----------------------------------------------------------------------
# Step 3 - SPIRV-Tools
# ----------------------------------------------------------------------

$spvInstalledMarker = Join-Path $InstallPrefix "lib\SPIRV-Tools.lib"
if (Test-Path $spvInstalledMarker) {
    Write-Host "== SPIRV-Tools already installed; skipping ==" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "== Step 3: SPIRV-Tools $SpirvToolsTag ==" -ForegroundColor Cyan

    $spvSrc = Join-Path $WorkDir "SPIRV-Tools"
    if (-not (Test-Path $spvSrc)) {
        & git clone -b $SpirvToolsTag --depth=1 https://github.com/KhronosGroup/SPIRV-Tools $spvSrc
        if ($LASTEXITCODE -ne 0) { Write-Host "git clone SPIRV-Tools failed" -ForegroundColor Red; exit 31 }
        & git clone -b $SpirvToolsTag --depth=1 https://github.com/KhronosGroup/SPIRV-Headers (Join-Path $spvSrc "external\SPIRV-Headers")
        if ($LASTEXITCODE -ne 0) { Write-Host "git clone SPIRV-Headers failed" -ForegroundColor Red; exit 32 }
    }

    $spvBuild = Join-Path $WorkDir "spv-build"
    if (Test-Path $spvBuild) { Remove-Item -Recurse -Force $spvBuild }
    New-Item -ItemType Directory -Force -Path $spvBuild | Out-Null
    Push-Location $spvBuild
    try {
        Write-Host "Configuring SPIRV-Tools..." -ForegroundColor DarkCyan
        & cmake $spvSrc `
            -GNinja `
            -DCMAKE_BUILD_TYPE=Release `
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
            -DCMAKE_INSTALL_PREFIX="$InstallPrefix" `
            -DSPIRV_WERROR=OFF
        if ($LASTEXITCODE -ne 0) { Write-Host "cmake SPIRV-Tools failed" -ForegroundColor Red; exit 33 }

        Write-Host "Compiling SPIRV-Tools..." -ForegroundColor DarkCyan
        & ninja install
        if ($LASTEXITCODE -ne 0) { Write-Host "ninja SPIRV-Tools install failed" -ForegroundColor Red; exit 34 }
    } finally {
        Pop-Location
    }
}

Write-Host ""
Write-Host "== All deps installed to $InstallPrefix ==" -ForegroundColor Green
