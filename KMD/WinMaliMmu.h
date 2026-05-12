#pragma once

#include <ntddk.h>
#include "..\Shared\WinMaliCommon.h"

struct _WINMALI_ADAPTER;
typedef struct _WINMALI_ADAPTER WINMALI_ADAPTER, *PWINMALI_ADAPTER;

// Contiguous GPU heap for page tables + scratch (non-cached for CPU/GPU).
// Must be >= WINMALI_VIDMM_PAGING_BUFFER_BYTES: dxgmms2 validates paging pool
// fits in the segment we report in QUERYSEGMENT3/4.
/** Contiguous non-cached heap: page tables, firmware PT, MMU bring-up scratch, VIDMM carve-out. */
#define WINMALI_MMU_SCRATCH_HEAP_BYTES (256UL * 1024UL)

/** Paging buffer carved from segment id 1 (PagingBufferSegmentId). ROS uses PAGE_SIZE; a larger value increases VIDMM commit during DpiFdoStartAdapter. */
#define WINMALI_VIDMM_PAGING_BUFFER_BYTES (4096UL)

/**
 * Size of the contiguous PopulatedFromSystemMemory segment we publish as
 * segment id 1. dxgmms2 lays kernel DMA buffers (4 KiB each, one per
 * outstanding command submission) and small non-primary D3D allocations
 * into this region. 4 MiB proved too tight on some Win11 start paths
 * (DpiFdoStartAdapter rolling back with STATUS_NO_MEMORY immediately after
 * QUERYSEGMENT4 pass2). Use 16 MiB to give VIDMM headroom for early
 * paging/DMA buffer pools while keeping contiguous-allocation pressure low.
 */
#define WINMALI_DMA_SEGMENT_BYTES (32UL * 1024UL * 1024UL)

#if WINMALI_VIDMM_PAGING_BUFFER_BYTES > WINMALI_MMU_SCRATCH_HEAP_BYTES
#error WINMALI_VIDMM_PAGING_BUFFER_BYTES must fit in WINMALI_MMU_SCRATCH_HEAP_BYTES
#endif

// AS slot 0 is reserved for CSF MCU on Linux; use AS1 for kernel bring-up.
#define WINMALI_MMU_BRINGUP_AS      1u
#define WINMALI_MMU_MCU_AS          0u

//
// ARM-LPAE stage-1 L3 page descriptor (Mali MMU AArch64 mode).
//
// Bit field layout (8-byte little-endian descriptor):
//
//   bit 0/1     = 11   valid + page descriptor (vs. table)
//   bits 4:2    = 000  AttrIdx = 0  →  MAIR slot 0 (we program it to
//                                       WINMALI_AS_MEMATTR_BYTE_NC = 0x4C)
//   bit 5       = 0    NS                       (don't-care for Mali)
//   bit 6       = 1    AP[1] = EL0/GPU access   (must be 1 for the GPU)
//   bit 7       = AP[2]                          (0 = RW, 1 = RO)
//   bits 9:8    = 11   SH = Inner-Shareable    (harmless for NC memory)
//   bit 10      = 1    AF                       (required; PTW faults if 0)
//   bit 11      = 0    nG                       (don't-care for Mali)
//   bit 53      = PXN  (1 = privilege execute never)
//   bit 54      = UXN  (1 = unprivileged execute never)
//
// During early bring-up we used 0xff3 which accidentally set AP[2]=1 (RO)
// and AttrIdx=4 (an unprogrammed MAIR slot); that caused level-3 perm
// faults the first time the MCU wrote into its BSS. We then used a single
// 0x743 (RW + EX) for everything to unblock submission. The variants
// below let WinMaliFwBuildPtForSections_ derive the *correct* permission
// per FW section from the binary's CSF_FW_BINARY_IFACE_ENTRY_RD_{RD,WR,EX}
// bits — code sections become RO+EX, data RW+NX, the shared section
// RW+NX-coherent, etc.
//
// PA_MASK extracts the 4 KiB-aligned output address (bits[51:12]) from
// either an L3 page descriptor or an L0/L1/L2 table descriptor.
//
#define WINMALI_LPAE_PA_MASK              (0x0000fffffffff000ull)

// Common bits for every L3 page descriptor we ever write: valid+page,
// AttrIdx=0 (NC), NS=0, AP[1]=1 (GPU/EL0 accessible), SH=Inner-Shareable,
// AF=1, nG=0. PXN/UXN/AP[2] are layered on top per the variant below.
#define WINMALI_LPAE_L3_BASE              (0x743ull)

#define WINMALI_LPAE_L3_AP_RO             (1ull << 7)   // AP[2]=1 → read-only
#define WINMALI_LPAE_L3_PXN               (1ull << 53)  // privilege XN
#define WINMALI_LPAE_L3_UXN               (1ull << 54)  // user XN
#define WINMALI_LPAE_L3_NX                (WINMALI_LPAE_L3_PXN | WINMALI_LPAE_L3_UXN)

// Per-permission variants derived from the FW section flags.
#define WINMALI_LPAE_L3_PAGE_ATTR_RW_EX   (WINMALI_LPAE_L3_BASE)
#define WINMALI_LPAE_L3_PAGE_ATTR_RO_EX   (WINMALI_LPAE_L3_BASE | WINMALI_LPAE_L3_AP_RO)
#define WINMALI_LPAE_L3_PAGE_ATTR_RW_NX   (WINMALI_LPAE_L3_BASE | WINMALI_LPAE_L3_NX)
#define WINMALI_LPAE_L3_PAGE_ATTR_RO_NX   (WINMALI_LPAE_L3_BASE | WINMALI_LPAE_L3_AP_RO | WINMALI_LPAE_L3_NX)

// Legacy alias used by the (non-FW) Phase-2 kernel-queue mappings.
// Kernel-queue buffers (ring, syncobj, queue iface, suspend, protm,
// shader payload) hold pure data — never execute — so we tighten them to
// RW+NX. Naming kept "_RW" for source-level continuity with old call sites.
#define WINMALI_LPAE_L3_PAGE_ATTR_RW      (WINMALI_LPAE_L3_PAGE_ATTR_RW_NX)

NTSTATUS WinMaliMmuInit(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliMmuTeardown(_Inout_ PWINMALI_ADAPTER Adapter);

// Shared AS programming: scratch heap on AS1, MCU firmware VM on AS0.
VOID     WinMaliMmuGetDefaultAsParams(_In_ PWINMALI_ADAPTER Adapter,
                                      _Out_ UINT64* TransCfg,
                                      _Out_ UINT64* MemAttr);
NTSTATUS WinMaliMmuAsEnable(_Inout_ PWINMALI_ADAPTER Adapter,
                            _In_ ULONG           AsIndex,
                            _In_ UINT64          TranstabPa,
                            _In_ UINT64          TransCfg,
                            _In_ UINT64          MemAttr);
NTSTATUS WinMaliMmuAsDisable(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG AsIndex);

// Map 4 KiB pages into the Phase-2 bring-up AS1 page tables for GPU VA
// inside the 2 MiB window anchored at WINMALI_MMU_TEST_GPU_VA (0x400000).
// Used by CSF job submission (ring / sync / shader payloads).
//
// Attrs is one of WINMALI_LPAE_L3_PAGE_ATTR_{RW_EX, RO_EX, RW_NX, RO_NX}
// — the caller picks based on what the GPU side will do with the buffer
// (e.g. kernel-queue ring/syncobj/iface = RW_NX; CSF stream payload that
// will be executed via a CALL instruction = RW_EX).
NTSTATUS WinMaliMmuMapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ PHYSICAL_ADDRESS   FirstPagePa,
    _In_ ULONG              PageCount,
    _In_ UINT64             Attrs);
NTSTATUS WinMaliMmuUnmapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ ULONG              PageCount);

// Fills WINMALI_ESCAPE_MMU_OUT from live MMIO + CPU heap view (Passive).
NTSTATUS WinMaliMmuEscapePing(_In_ PWINMALI_ADAPTER Adapter,
                              _Out_ WINMALI_ESCAPE_MMU_OUT* Out);

//
// AS-slot allocator and dxgk-context -> Mali AS binding. Used by
// DxgkDdiCreateProcess / DxgkDdiSetRootPageTable / DxgkDdiDestroyProcess
// (see WinMaliDxgkInitFill.c).
//
// WinMaliMmuInitAsAllocator marks AS0 (CSF MCU) and AS1 (kernel bring-up)
// as in-use so dxgk handles never reuse them. Must be called once during
// AddDevice, before any QueryAdapterInfo / CreateProcess can race.
//
VOID     WinMaliMmuInitAsAllocator(_Inout_ PWINMALI_ADAPTER Adapter);

//
// Program (or reprogram) the GPU MMU AS slot bound to `hContext` so that
// `RootPtPa` becomes its live root page table. If `hContext` is already
// bound to an AS slot the same slot is reused (TRANSTAB rewritten +
// UPDATE issued). Otherwise a free AS is allocated. On success
// `*OutAs` receives the slot number (informational).
//
// RootPtPa == 0 is treated as "unbind"; the slot is disabled and freed.
//
NTSTATUS WinMaliMmuBindContextRootPt(_Inout_ PWINMALI_ADAPTER Adapter,
                                     _In_ HANDLE              hContext,
                                     _In_ UINT64              RootPtPa,
                                     _Out_opt_ PULONG         OutAs);

//
// Release the AS slot currently bound to `hContext` (no-op if none). Called
// from DxgkDdiDestroyContext / DxgkDdiDestroyProcess so we never leak AS
// slots across process exit.
//
NTSTATUS WinMaliMmuUnbindContext(_Inout_ PWINMALI_ADAPTER Adapter,
                                 _In_ HANDLE              hContext);
