#pragma once

// ---------------------------------------------------------------------------
// Driver identity
// ---------------------------------------------------------------------------

#define WINMALI_DRIVER_NAME_A    "WinMali"
#define WINMALI_DRIVER_NAME_W    L"WinMali"
#define WINMALI_POOL_TAG         'MniW'   // "WinM" in little-endian

// Bump when changing bring-up-critical paths (VIDMM / QueryAdapterInfo) so
// kernel logs prove the board is running the intended binary.
#define WINMALI_KMD_CAP_STAMP    20260510u

// Targeted platform: RK3588 / Mali-G610 Valhall (arch v10).
#define WINMALI_TARGET_GPU_NAME  "Mali-G610"
#define WINMALI_TARGET_SOC_NAME  "Rockchip RK3588"

// ACPI hardware IDs, as published in edk2-rk3588's ACPI tables:
//   Silicon/Rockchip/RK3588/AcpiTables/gpu.asl   => ARMH5655 (Mali-G610)
//   Silicon/Rockchip/RK3588/AcpiTables/vop2.asl  => RKCP5650 (VOP2 display)
#define WINMALI_ACPI_HID_GPU     "ACPI\\ARMH5655"
#define WINMALI_ACPI_HID_VOP2    "ACPI\\RKCP5650"

// ---------------------------------------------------------------------------
// Hardware base addresses (for diagnostic logging / sanity checks only - real
// addresses come from the DXGK resource list at runtime).
// ---------------------------------------------------------------------------

#define WINMALI_GPU_MMIO_BASE    0xFB000000ULL
#define WINMALI_GPU_MMIO_SIZE    0x00200000UL   // 2 MiB

// GIC SPI numbers (GSIVs) from edk2 gpu.asl: job=124, mmu=125, gpu=126.
#define WINMALI_GSIV_JOB         124
#define WINMALI_GSIV_MMU         125
#define WINMALI_GSIV_GPU         126

// ---------------------------------------------------------------------------
// UMD <-> KMD private data (DXGKARG_QUERYADAPTERINFO type = UMDRIVERPRIVATE)
// ---------------------------------------------------------------------------

// Magic sent back by the KMD so the UMD can sanity-check it really did
// open a WinMali adapter (and not someone else's driver claiming to).
// "WinMG610" in ASCII, big-endian readable.
#define WINMALI_ADAPTER_MAGIC    0x57696E4D47363130ULL

typedef struct _WINMALI_ADAPTER_INFO {
    unsigned long long Magic;              // = WINMALI_ADAPTER_MAGIC
    unsigned long      KmdMajorVersion;    // = 0
    unsigned long      KmdMinorVersion;    // = 1
    unsigned long      WddmVersion;        // echo of DXGK_DRIVERCAPS.WDDMVersion
    unsigned long      GpuId;              // GPU_ID register
    unsigned long      CsfId;              // GPU_CSF_ID register
    unsigned long      GpuRevId;           // GPU_REVID
    unsigned long      Flags;              // WINMALI_ADAPTER_FLAG_*
} WINMALI_ADAPTER_INFO;

#define WINMALI_ADAPTER_FLAG_MCU_ALIVE     0x00000001UL
#define WINMALI_ADAPTER_FLAG_MMU_READY     0x00000002UL
#define WINMALI_ADAPTER_FLAG_DIAG_PASSED   0x00000004UL
/** CSF kernel queue + CSG0/CS0 programmed (Panthor-style bootstrap complete). */
#define WINMALI_ADAPTER_FLAG_CSF_JOBS      0x00000008UL

// ---------------------------------------------------------------------------
// Host-side diagnostics channel (D3DKMT Escape).
//
// We carry these over DxgkDdiEscape(D3DKMT_ESCAPEFLAGS.DeviceEscape=FALSE),
// so the host tool (winmali-diag.exe) can dump GPU state without needing
// a fully-working D3D device. Escape payloads are versioned by
// Opcode + Version so old tools don't silently get garbage.
// ---------------------------------------------------------------------------

// "WnMe" little-endian magic placed at the front of every escape header so
// the KMD can bail out quickly on wrong-driver traffic.
#define WINMALI_ESCAPE_MAGIC     0x656D6E57UL

typedef enum _WINMALI_ESCAPE_OPCODE {
    WinMaliEscapeOp_Invalid            = 0,
    WinMaliEscapeOp_GetDiagnostics     = 1,   // fills WINMALI_ESCAPE_DIAG_OUT
    WinMaliEscapeOp_GetMmioSnapshot    = 2,   // fills WINMALI_ESCAPE_MMIO_OUT
    /** Mesa pan_kmod / Gallium: packed Linux `struct drm_panfrost_submit` + inline
     *  BO handle table (UMD-built buffer). Do **not** use opcode 2 for this —
     *  opcode 2 is reserved for GetMmioSnapshot. Layout: see
     *  `WINMALI_PANFROST_SUBMIT_*` below and mesaport `WINMALI_MESA_INTEGRATION.md` §5.2.
     */
    WinMaliEscapeOp_PanfrostSubmit     = 3,
    /** MMU AS bind + page table self-check; fills WINMALI_ESCAPE_MMU_OUT. */
    WinMaliEscapeOp_PingMmu            = 4,
    /** Raw Valhall blob harness for bring-up (see WINMALI_ESCAPE_RAW_SHADER). */
    WinMaliEscapeOp_SubmitRawShader    = 5,
    WinMaliEscapeOp_Max
} WINMALI_ESCAPE_OPCODE;

typedef struct _WINMALI_ESCAPE_HEADER {
    unsigned long Magic;         // = WINMALI_ESCAPE_MAGIC
    unsigned long Opcode;        // WINMALI_ESCAPE_OPCODE
    unsigned long Version;       // currently 1
    unsigned long Reserved;
} WINMALI_ESCAPE_HEADER;

#define WINMALI_ESCAPE_VERSION   1

// ---------------------------------------------------------------------------
// WinMaliEscapeOp_PanfrostSubmit (3) — input buffer from Mesa `wm_adapter_panfrost_submit`
//
// Linux DRM uses a userspace pointer for bo_handles; the WinMali escape carries
// one contiguous `pPrivateDriverData` blob. Field layout of the submit copy
// matches `struct drm_panfrost_submit` in Linux `panfrost_drm.h` (40 bytes):
//   __u64 jc; __u64 in_syncs; __u32 in_sync_count; __u32 out_sync;
//   __u64 bo_handles; __u32 bo_handle_count; __u32 requirements;
// In this escape, bo_handles is a BYTE OFFSET from the start of pPrivateDriverData
// to the first uint32_t BO handle (8-byte aligned from buffer start), not a VA.
// Minimum size = that offset + bo_handle_count * sizeof(uint32_t).
// ---------------------------------------------------------------------------

/** Size of the embedded `drm_panfrost_submit` copy (no trailing padding). */
#define WINMALI_PANFROST_SUBMIT_STRUCT_BYTES 40u

#define WINMALI_PANFROST_SUBMIT_BO_HANDLE_COUNT_OFFSET \
    (sizeof(WINMALI_ESCAPE_HEADER) + 32u)

#define WINMALI_PANFROST_SUBMIT_HANDLE_TABLE_OFFSET() \
    (((unsigned int)sizeof(WINMALI_ESCAPE_HEADER) + WINMALI_PANFROST_SUBMIT_STRUCT_BYTES + 7u) & ~7u)

#define WINMALI_PANFROST_SUBMIT_REQUIRED_BYTES(_bo_handle_count) \
    (WINMALI_PANFROST_SUBMIT_HANDLE_TABLE_OFFSET() + (unsigned int)(_bo_handle_count) * 4u)

// Op_GetDiagnostics: everything the host tool needs to decide "is the KMD
// bound, did it talk to the chip, is the IRQ counting up".
typedef struct _WINMALI_ESCAPE_DIAG_OUT {
    WINMALI_ESCAPE_HEADER  Header;

    unsigned long          KmdMajorVersion;
    unsigned long          KmdMinorVersion;
    unsigned long          WddmVersion;

    // Mirror of WINMALI_ADAPTER_FLAG_*
    unsigned long          AdapterFlags;

    // Physical resources the KMD was handed by DXGK.
    unsigned long long     MmioPhysBase;
    unsigned long long     MmioPhysSize;
    unsigned long          MmioMapped;          // 1 if MmMapIoSpace succeeded
    unsigned long          InterruptConnected;  // 1 if IoConnectInterruptEx ok

    // Raw GPU registers (0 until MMIO is mapped).
    unsigned long          GpuId;               // raw value of reg 0x000
    unsigned long          CsfId;               // raw value of reg 0x01C
    unsigned long          GpuRevId;            // raw value of reg 0x280
    unsigned long          L2Features;          // raw value of reg 0x004
    unsigned long          MmuFeatures;         // raw value of reg 0x014
    unsigned long          AsPresent;           // raw value of reg 0x018

    // Decoded GPU_ID (see panthor_regs.h GPU_ARCH_MAJOR / GPU_PROD_MAJOR).
    unsigned long          ArchMajor;           // expect 10 for Mali-G610
    unsigned long          ArchMinor;
    unsigned long          ArchRev;
    unsigned long          ProdMajor;           // expect 7  for Mali-G610
    unsigned long          VerMajor;
    unsigned long          VerMinor;
    unsigned long          VerStatus;

    // Counters (useful to confirm IRQ wiring is live).
    unsigned long long     InterruptsTotal;
    unsigned long long     InterruptsHandled;
    unsigned long long     InterruptsSpurious;
} WINMALI_ESCAPE_DIAG_OUT;

// Op_GetMmioSnapshot: a small fixed set of register reads, driver-filtered.
// We do NOT expose arbitrary register read/write from user-mode - that
// would be a privilege escalation even on an ACPI render device.
typedef struct _WINMALI_ESCAPE_MMIO_OUT {
    WINMALI_ESCAPE_HEADER  Header;

    unsigned long          GpuStatus;        // 0x034
    unsigned long          GpuFaultStatus;   // 0x03C
    unsigned long          GpuFaultAddrLo;   // 0x040
    unsigned long          GpuFaultAddrHi;   // 0x044
    unsigned long          GpuIntRawStat;    // 0x020
    unsigned long          GpuIntStat;       // 0x02C
    unsigned long          McuStatus;        // 0x704
    unsigned long          JobIntStat;       // 0x100C
    unsigned long          MmuIntStat;       // 0x200C
} WINMALI_ESCAPE_MMIO_OUT;

// Op_PingMmu: MMU bring-up snapshot (no user input beyond header).
typedef struct _WINMALI_ESCAPE_MMU_OUT {
    WINMALI_ESCAPE_HEADER  Header;

    unsigned long          NtStatus;          // NTSTATUS value
    unsigned long          MmuReady;          // 1 if WINMALI_ADAPTER_FLAG_MMU_READY
    unsigned long          AsIndex;
    unsigned long          AsStatus;          // raw AS_STATUS(as)
    unsigned long          TranstabLo;
    unsigned long          TranstabHi;
    unsigned long long     RootTablePhys;
    unsigned long long     MappedGpuVa;       // VA chosen for test mapping
    unsigned long long     DataPhys;
    unsigned long          PatternExpected;   // 0xA5A5A5A5
    unsigned long          PatternActual;     // first ULONG at data page (CPU)
} WINMALI_ESCAPE_MMU_OUT;

// Op_SubmitRawShader (5) — compute bring-up over D3DKMTEscape.
// Layout: fixed WINMALI_ESCAPE_RAW_SHADER header, then ShaderByteCount bytes
// of raw Valhall (4-byte aligned; pad caller buffer). KMD reads InValue and
// optional shader bytes; writes OutValue, IoStatus, fence snapshots.
// Real GPU submission is gated on WINMALI_ADAPTER_FLAG_MCU_ALIVE; until
// then callers may set WINMALI_RAW_SHADER_FLAG_CPU_SIMULATE for a kernel-side
// in+1 → out CPU simulate path (no CSF queue).
#define WINMALI_SHADER_BLOB_MAGIC            0xBA10BEEFul
#define WINMALI_RAW_SHADER_FLAG_CPU_SIMULATE 0x00000001ul
#define WINMALI_RAW_SHADER_MAX_BYTES         65536ul

typedef struct _WINMALI_ESCAPE_RAW_SHADER {
    WINMALI_ESCAPE_HEADER  Header;
    unsigned long          BlobMagic;         // = WINMALI_SHADER_BLOB_MAGIC
    unsigned long          ShaderByteCount;
    unsigned long          Flags;             // WINMALI_RAW_SHADER_FLAG_*
    unsigned long          InValue;
    unsigned long          OutValue;          // filled by KMD
    unsigned long          IoStatus;          // NTSTATUS as ULONG
    unsigned long          McuAlive;          // 0 or 1
    unsigned long long     SubmittedFence;
    unsigned long long     CompletedFence;
    unsigned long          Reserved;
} WINMALI_ESCAPE_RAW_SHADER;

#define WINMALI_ESCAPE_RAW_SHADER_BASE_BYTES ((unsigned int)sizeof(WINMALI_ESCAPE_RAW_SHADER))

#define WINMALI_ESCAPE_RAW_SHADER_TOTAL_BYTES(_shaderByteCount) \
    (WINMALI_ESCAPE_RAW_SHADER_BASE_BYTES + (unsigned int)(_shaderByteCount))
