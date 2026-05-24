# rk3588-GPU

Open-source WDDM 2.0 driver for the Arm Mali-G610 MP4 "Odin" GPU on
the Rockchip RK3588.

This tree is the driver itself. The rest of the working folder holds
references and the mesa sources we build against:

- `..\mesa-modern\mesa\` — current mesa, built into all three user-mode
  binaries (D3D10/11 UMD, OpenGL ICD, Vulkan ICD) by
  `Tools\build-mesa.ps1`. Contains real panvk-v10 + native CSF support.
- `..\mesa\` — older mesa fork kept for historical reference; not built.
- `..\WddmReference\` — vendor drivers we cross-reference (Adreno's
  qcdx8380.inf is the WoA INF reference).
- `..\HardwareReference\` — RK3588 / Mali-G610 datasheets and the
  Linux panthor driver sources.

## Build

```powershell
.\Tools\build.ps1                                # Debug, auto platform, everything
.\Tools\build.ps1 -Configuration Release
.\Tools\build.ps1 -Platform ARM64                # The real deployment target
.\Tools\build.ps1 -SkipMesa                      # Just the .sys + INF
.\Tools\clean.ps1
```

First-time setup requires building the mesa dependencies (LLVM 18,
libclc, SPIRV-Tools) into `C:\mesa-deps\`
one-time `Tools\build-mesa-deps.ps1` invocation.

## Where stuff lives

| Path | Role |
|---|---|
| `WinMali-rk3588.sln` | Top-level Visual Studio solution |
| `KMD\` | Kernel-mode WDDM 2.0 miniport (`.sys`) + INF + Vulkan ICD manifests |
| `UMD\` | Hand-written D3D11 UMD — **retired**, kept for reference |
| `Mesa\` | MSBuild wrapper for the mesa user-mode binaries |
| `Shared\` | KMD↔UMD escape ABI header (`WinMaliEscape.h`) |
| `Tools\` | `env.ps1`, `build.ps1`, `build-mesa.ps1`, `build-mesa-deps.ps1`, `clean.ps1` |
| `docs\` | Layout, build, mesa integration, KMD author guide |

