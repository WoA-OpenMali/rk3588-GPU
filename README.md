# rk3588-GPU

Open-source WDDM 2.0 driver for the Arm Mali-G610 MP4 "Odin" GPU on
the Rockchip RK3588.

This tree is the driver itself. The rest of the working folder holds
references and the mesa source we build against:

- `..\mesa\` — mesa source, built into the OpenGL ICD by
  `Tools\build-mesa.ps1`.
- `..\WddmReference\` — vendor drivers we cross-reference.
- `..\HardwareReference\` — RK3588 / Mali-G610 datasheets and prior
  Linux work.

## Build

```powershell
.\Tools\build.ps1                                # Debug, auto platform
.\Tools\build.ps1 -Configuration Release
.\Tools\build.ps1 -SkipMesa                      # Just the .sys + UMD
.\Tools\clean.ps1
```

See `docs\BUILD.md` for the full matrix.

## Where stuff lives

| | |
|---|---|
| `WinMali-rk3588.sln` | Top-level Visual Studio solution |
| `KMD\` | Kernel-mode WDDM 2.0 miniport (`.sys`) |
| `UMD\` | Hand-written D3D11 user-mode driver (`.dll`) |
| `Mesa\` | MSBuild wrapper for the mesa OpenGL ICD |
| `Shared\` | Headers shared between KMD and UMD |
| `Tools\` | `env.ps1`, `build.ps1`, `build-mesa.ps1`, `clean.ps1` |
| `docs\` | Layout, build, mesa integration, skeleton tour |

## Status

**Skeleton + mesa UMD/ICDs milestone** (2026-05-17):

- KMD: builds clean, packages, loads under PnP, does nothing.
- UMD/ICD: `WinMaliGL.dll` (OpenGL), `WinMaliUmd.dll` (D3D10/11), and
  `WinMaliVk.dll` (Vulkan placeholder) all build from mesa via
  `Tools\build-mesa.ps1`. INF registers all three.
- ABI: `Shared\WinMaliEscape.h` is the authoritative KMD↔UMD escape
  contract. KMD will be written to match it.

See `docs\KMD_IMPLEMENTATION.md` for the opcode-by-opcode guide to
writing the KMD against the `Shared\WinMaliEscape.h` ABI.
`docs\MESA_BUILD_STATUS.md` records the mesa-side bring-up.
`docs\NEXT_STEPS.md` covers the broader roadmap.
