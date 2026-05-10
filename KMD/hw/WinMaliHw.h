/*++

Module Name:

    WinMaliHw.h

Abstract:

    Tiny MMIO wrapper + Mali register offsets. Every function in this
    header is prefixed "WinMaliHw". Implementation is in WinMaliHw.c.

    For now the routines are stubs that do NOT touch hardware - they
    exist to pin down the surface area the rest of the driver calls
    into, so future MMIO work is a drop-in replacement of one .c file.

--*/

#pragma once

#include <ntddk.h>

// ---------------------------------------------------------------------------
// Mali Valhall register offsets (subset used at bring-up time).
// Full list is in panthor-linux6.1/drivers/gpu/drm/panthor/panthor_regs.h.
// ---------------------------------------------------------------------------

// GPU block (0x000-0x2FF). Offsets stay in sync with panthor_regs.h.
#define WINMALI_REG_GPU_ID            0x0000
#define WINMALI_REG_GPU_L2_FEATURES   0x0004
#define WINMALI_REG_GPU_CSF_ID        0x001C  // NB: panthor puts CSF_ID at 0x1C, not 0x08
#define WINMALI_REG_GPU_MMU_FEATURES  0x0014
#define WINMALI_REG_GPU_AS_PRESENT    0x0018
#define WINMALI_REG_GPU_IRQ_RAWSTAT   0x0020
#define WINMALI_REG_GPU_IRQ_CLEAR     0x0024
#define WINMALI_REG_GPU_IRQ_MASK      0x0028
#define WINMALI_REG_GPU_IRQ_STATUS    0x002C
#define WINMALI_REG_GPU_COMMAND       0x0030
#define WINMALI_REG_GPU_STATUS        0x0034
#define WINMALI_REG_GPU_FAULT_STATUS  0x003C
#define WINMALI_REG_GPU_FAULT_ADDR_LO 0x0040
#define WINMALI_REG_GPU_FAULT_ADDR_HI 0x0044
#define WINMALI_REG_GPU_REVID         0x0280

// Shader / L2 presence and power (panthor_regs.h).
#define WINMALI_REG_GPU_SHADER_PRESENT_LO  0x0100
#define WINMALI_REG_GPU_SHADER_PRESENT_HI  0x0104
#define WINMALI_REG_GPU_L2_PRESENT_LO      0x0120
#define WINMALI_REG_GPU_L2_PRESENT_HI      0x0124
#define WINMALI_REG_L2_READY_LO            0x0160
#define WINMALI_REG_L2_READY_HI            0x0164
#define WINMALI_REG_L2_PWRON_LO            0x01A0
#define WINMALI_REG_L2_PWRON_HI            0x01A4
#define WINMALI_REG_L2_PWROFF_LO           0x01E0
#define WINMALI_REG_L2_PWROFF_HI           0x01E4
#define WINMALI_REG_L2_PWRTRANS_LO         0x0220
#define WINMALI_REG_L2_PWRTRANS_HI         0x0224

// MCU block (0x700+). MCU_STATUS tells us whether the CSF firmware is alive.
// Values match panthor_regs.h / `MCU_STATUS_*`.
#define WINMALI_REG_MCU_CONTROL       0x0700
#define WINMALI_REG_MCU_STATUS        0x0704
#define WINMALI_MCU_CONTROL_DISABLE   0u
#define WINMALI_MCU_CONTROL_ENABLE    1u
#define WINMALI_MCU_CONTROL_AUTO      2u
#define WINMALI_MCU_STATUS_DISABLED   0u
#define WINMALI_MCU_STATUS_ENABLED    1u
#define WINMALI_MCU_STATUS_HALT       2u
#define WINMALI_MCU_STATUS_FATAL      3u

// Job control block (0x1000+).
#define WINMALI_REG_JOB_INT_RAWSTAT   0x1000
#define WINMALI_REG_JOB_INT_CLEAR     0x1004
#define WINMALI_REG_JOB_INT_MASK      0x1008
#define WINMALI_REG_JOB_INT_STAT      0x100C
#define WINMALI_JOB_INT_GLOBAL_IF     (1u << 31)

// CSF host doorbell #0 (global). See panthor_regs.h `CSF_DOORBELL`.
#define WINMALI_REG_CSF_DOORBELL(n)   (0x80000u + ((ULONG)(n)) * 0x10000u)

// MMU block (0x2000+). AS-specific registers: panthor_regs.h MMU_BASE / MMU_AS.
#define WINMALI_MMU_BASE                 0x2400u
#define WINMALI_MMU_AS_SHIFT             6u
#define WINMALI_REG_MMU_INT_RAWSTAT      0x2000
#define WINMALI_REG_MMU_INT_CLEAR        0x2004
#define WINMALI_REG_MMU_INT_MASK         0x2008
#define WINMALI_REG_MMU_INT_STAT         0x200C
#define WINMALI_REG_AS_FAULTSTATUS(as)   (WINMALI_MMU_BASE + (((ULONG)(as)) << WINMALI_MMU_AS_SHIFT) + 0x1Cu)
#define WINMALI_REG_AS_FAULTADDR_LO(as)  (WINMALI_MMU_BASE + (((ULONG)(as)) << WINMALI_MMU_AS_SHIFT) + 0x20u)
#define WINMALI_REG_AS_FAULTADDR_HI(as)  (WINMALI_MMU_BASE + (((ULONG)(as)) << WINMALI_MMU_AS_SHIFT) + 0x24u)

// GPU_ID bit-field decoders (matches panthor_regs.h). The raw register
// value is one 32-bit word; we split it into fields so higher-level code
// reads arch/prod cleanly.
#define WINMALI_GPU_ID_ARCH_MAJOR(x)  (((x) >> 28) & 0xFu)
#define WINMALI_GPU_ID_ARCH_MINOR(x)  (((x) >> 24) & 0xFu)
#define WINMALI_GPU_ID_ARCH_REV(x)    (((x) >> 20) & 0xFu)
#define WINMALI_GPU_ID_PROD_MAJOR(x)  (((x) >> 16) & 0xFu)
#define WINMALI_GPU_ID_VER_MAJOR(x)   (((x) >> 12) & 0xFu)
#define WINMALI_GPU_ID_VER_MINOR(x)   (((x) >>  4) & 0xFFu)
#define WINMALI_GPU_ID_VER_STATUS(x)  (((x) >>  0) & 0xFu)

// (arch_major, prod_major) product codes we recognise. Matches
// panthor_hw.c / GPU_PROD_ID_MAKE().
#define WINMALI_PROD_MALI_G610_ARCH   10u
#define WINMALI_PROD_MALI_G610_PROD   7u

// ---------------------------------------------------------------------------
// Per-adapter hardware context owned by WinMaliHw.c. Opaque to the rest
// of the driver.
// ---------------------------------------------------------------------------

typedef struct _WINMALI_HW {
    PVOID   RegsVa;       // NULL => not mapped yet
    ULONG   RegsSize;

    // Raw register snapshots taken in WinMaliHwProbeIdentity().
    ULONG   GpuId;        // 0x000
    ULONG   CsfId;        // 0x01C
    ULONG   RevId;        // 0x280
    ULONG   L2Features;   // 0x004
    ULONG   MmuFeatures;  // 0x014
    ULONG   AsPresent;    // 0x018

    // Decoded GPU_ID, precomputed for easy access (saves every caller
    // re-running the shift mask). Filled by WinMaliHwProbeIdentity().
    ULONG   ArchMajor;
    ULONG   ArchMinor;
    ULONG   ArchRev;
    ULONG   ProdMajor;
    ULONG   VerMajor;
    ULONG   VerMinor;
    ULONG   VerStatus;
} WINMALI_HW, *PWINMALI_HW;

// ---------------------------------------------------------------------------
// API surface
// ---------------------------------------------------------------------------

NTSTATUS WinMaliHwInitialize (_Out_ PWINMALI_HW Hw,
                              _In_  PVOID       MappedRegsVa,
                              _In_  ULONG       RegsSize);
VOID     WinMaliHwShutdown   (_Inout_ PWINMALI_HW Hw);

ULONG    WinMaliHwRead32     (_In_ const WINMALI_HW* Hw, _In_ ULONG Offset);
VOID     WinMaliHwWrite32    (_In_ const WINMALI_HW* Hw, _In_ ULONG Offset, _In_ ULONG Value);

// Reads and caches GPU_ID / CSF_ID / REVID etc. Safe to call even when
// Hw->RegsVa is NULL (returns zero defaults). On success the decoded
// fields (ArchMajor, ProdMajor, ...) are also populated.
NTSTATUS WinMaliHwProbeIdentity(_Inout_ PWINMALI_HW Hw);

// TRUE if GPU_ID decoded as (arch_major=10, prod_major=7) == Mali-G610.
BOOLEAN  WinMaliHwIsMaliG610  (_In_ const WINMALI_HW* Hw);

// Render a human-readable product name ("Mali-G610", "Mali-G710", "unknown")
// based on the decoded arch/prod fields. Returned string is a static
// literal, do not free.
PCSTR    WinMaliHwProductName (_In_ const WINMALI_HW* Hw);
