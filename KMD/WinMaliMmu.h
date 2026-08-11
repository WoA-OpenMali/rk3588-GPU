#pragma once

#include <ntddk.h>

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
 * QUERYSEGMENT4 pass2); 32 MiB was still under the 64 MiB floor that
 * dxgmms2!VIDMM_GLOBAL::ReadCommitLimitInformation clamps
 * `MinimumSystemMemoryCommitLimit` to (decomp dxgmms2.sys.c:89350:
 * `if ((v7 << 20) <= 0x4000000) v1 = 0x4000000;`). When our reported
 * segment is below that kernel-wide floor, dxgkrnl boots the system
 * paging context, immediately tears it down, and PnP-stops the adapter
 * — matching the smoke-test → StopDevice cascade observed at runtime.
 * 128 MiB sits well above the floor while staying easy to satisfy as a
 * contiguous allocation on a 4-16 GiB rk3588 board.
 */
#define WINMALI_DMA_SEGMENT_BYTES (128UL * 1024UL * 1024UL)

#if WINMALI_VIDMM_PAGING_BUFFER_BYTES > WINMALI_MMU_SCRATCH_HEAP_BYTES
#error WINMALI_VIDMM_PAGING_BUFFER_BYTES must fit in WINMALI_MMU_SCRATCH_HEAP_BYTES
#endif

/**
 * Aperture-window page-table geometry. The aperture segment we publish in
 * QUERYSEGMENT3/4 spans WINMALI_SYSMEM_GPU_BASE .. +WINMALI_DMA_SEGMENT_BYTES
 * of GPU VA; every L3 table covering it (one per 2 MiB) is preallocated as
 * a single contiguous non-cached block at WinMaliMmuInit and wired into the
 * bring-up AS L2 up front. Mapping therefore never allocates and cannot
 * fail with a status outside dxgmms2's BuildPagingBuffer whitelist.
 * 128 MiB -> 64 tables -> 256 KiB.
 */
#define WINMALI_APERTURE_L3_COUNT   (WINMALI_DMA_SEGMENT_BYTES >> 21)
#define WINMALI_APERTURE_L3_BYTES   ((SIZE_T)WINMALI_APERTURE_L3_COUNT << 12)

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

// no longer needed?
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

// Install 4 KiB PTEs into the bring-up AS. Two GPU-VA windows resolve:
//   * the 2 MiB CSF bring-up window at WINMALI_MMU_TEST_GPU_VA (0x400000)
//     - CSF job submission (ring / sync / shader payloads);
//   * the aperture window at WINMALI_SYSMEM_GPU_BASE
//     (WINMALI_DMA_SEGMENT_BYTES wide, L3s preallocated at MmuInit)
//     - dxgk MAP_APERTURE_SEGMENT paging ops.
// A range must lie entirely within one window.
//
// Attrs is one of WINMALI_LPAE_L3_PAGE_ATTR_{RW_EX, RO_EX, RW_NX, RO_NX}
// — the caller picks based on what the GPU side will do with the buffer
// (e.g. kernel-queue ring/syncobj/iface = RW_NX; CSF stream payload that
// will be executed via a CALL instruction = RW_EX).
//
// MapGpuRange maps physically-contiguous pages starting at FirstPagePa;
// MapGpuPfnRange maps scattered frames from a PFN array (MDL) in one pass.
NTSTATUS WinMaliMmuMapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ PHYSICAL_ADDRESS   FirstPagePa,
    _In_ ULONG              PageCount,
    _In_ UINT64             Attrs);
NTSTATUS WinMaliMmuMapGpuPfnRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_reads_(PageCount) const PFN_NUMBER* Pfns,
    _In_ ULONG              PageCount,
    _In_ UINT64             Attrs);
NTSTATUS WinMaliMmuUnmapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ ULONG              PageCount);

/* WinMaliMmuEscapePing dropped - it used the BadDriver escape ABI.
   Diagnostic surface will be rebuilt against v1.0 WinMaliEscape.h later. */

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

//
// Return the Mali AS slot currently bound to `hContext` (via
// SetRootPageTable / BindContextRootPt), or WINMALI_AS_SLOT_MAX if the
// context has no dxgk-managed AS. Used by SubmitCommandVirtual to run a CS
// against the dxgk GpuMmu page tables (Phase 2) rather than the escape VM AS.
//
ULONG    WinMaliMmuGetContextAsSlot(_In_ PWINMALI_ADAPTER Adapter,
                                    _In_ HANDLE            hContext);

//
// Issue an AS_COMMAND FLUSH_MEM against every currently-bound *user*
// address space (AS slots >= 2; AS0=MCU and AS1=kernel are skipped). Used
// by DxgkDdiBuildPagingBuffer when handling DXGK_OPERATION_FLUSH_TLB - we
// don't yet track the dxgk-process-to-AS mapping, so an explicit flush
// targets every leased user AS. Safe (no-op) when no slots are bound.
//
NTSTATUS WinMaliMmuFlushUserAsSlots(_Inout_ PWINMALI_ADAPTER Adapter);

// ---------------------------------------------------------------------------
// Per-VM page-table walker for the UMD escape ABI.
//
// A WINMALI_VM_PT owns a freshly allocated L0 root page plus a list of
// intermediate (L1/L2/L3) pages allocated lazily on Map calls. Each PT
// page is a 4 KiB contiguous non-cached sysmem allocation.
//
// The walker handles the standard ARM-LPAE 4-level hierarchy used by Mali
// (9 bits per level + 12-bit page offset, AArch64 4K granule). For our
// 30-bit advertised VA range only L2 and L3 actually vary; L0 and L1
// reduce to a single entry each. The walker still allocates them so the
// hardware sees a fully formed PT chain.
// ---------------------------------------------------------------------------

typedef struct _WINMALI_VM_PT_PAGE {
    LIST_ENTRY          Link;
    PVOID               Va;
    PHYSICAL_ADDRESS    Pa;
} WINMALI_VM_PT_PAGE, *PWINMALI_VM_PT_PAGE;

typedef struct _WINMALI_VM_PT {
    /* L0 root - identifies the AS to the GPU. */
    PVOID               RootVa;
    PHYSICAL_ADDRESS    RootPa;
    /* All allocated pages (root + L1 + L2 + L3...) chained for teardown. */
    LIST_ENTRY          AllocatedPages;
    FAST_MUTEX          Lock;
    /* AS slot the VM is bound to (WINMALI_AS_SLOT_MAX = unbound). */
    ULONG               AsSlot;
} WINMALI_VM_PT, *PWINMALI_VM_PT;

// Initialize a fresh VM PT: allocates the L0 root, picks a free AS slot,
// programs the AS via AS_TRANSTAB. Returns STATUS_INSUFFICIENT_RESOURCES if
// no AS slot is available.
NTSTATUS WinMaliMmuVmInit(_Inout_ PWINMALI_ADAPTER Adapter,
                          _Out_ PWINMALI_VM_PT VmPt);

// Tear down a VM PT: disables the AS, frees all PT pages.
VOID     WinMaliMmuVmTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                              _Inout_ PWINMALI_VM_PT VmPt);

// Install LPAE PTEs for `PageCount` 4 KiB pages starting at `GpuVa`. The
// physical frames are taken from `Pfns[0..PageCount-1]`. Attrs is one of
// WINMALI_LPAE_L3_PAGE_ATTR_*. Walks/allocates intermediate PT pages as
// needed and flushes the AS TLB at the end.
NTSTATUS WinMaliMmuVmMap(_Inout_ PWINMALI_ADAPTER Adapter,
                        _Inout_ PWINMALI_VM_PT VmPt,
                        _In_ UINT64 GpuVa,
                        _In_reads_(PageCount) const PFN_NUMBER* Pfns,
                        _In_ ULONG PageCount,
                        _In_ UINT64 Attrs);

// Clear PTEs for `PageCount` pages starting at `GpuVa`. Intermediate PT
// pages are left allocated (no compaction). Flushes the AS TLB.
NTSTATUS WinMaliMmuVmUnmap(_Inout_ PWINMALI_ADAPTER Adapter,
                          _Inout_ PWINMALI_VM_PT VmPt,
                          _In_ UINT64 GpuVa,
                          _In_ ULONG PageCount);
