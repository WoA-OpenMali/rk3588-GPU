/*
 * WinMaliVidmm.c - dxgk-facing memory segment management.
 *
 * Mali is tile-based, no dedicated VRAM. Everything lives in sysmem. We
 * still need to publish a memory segment to dxgk so VIDMM can route
 * allocations (DMA buffers, page tables, D3D resources). At StartDevice
 * we allocate a contiguous 256 MiB sysmem block and call it "segment 1
 * (sysmem)". QUERYSEGMENT3/4 hand its description out to dxgk.
 *
 * 256 MiB is large enough for early D3D11 workloads on 1080p (DMA
 * buffers @ 4 KiB each, swap-chain back buffers, shader heaps) while
 * being achievable as a single contiguous block on the RK3588's
 * 4..16 GiB main DRAM. If MmAllocateContiguousMemorySpecifyCache fails
 * (fragmented sysmem after a long uptime), we shrink and retry.
 */

#include "WinMaliKmd.h"

#define WINMALI_VIDMM_SEGMENT_BYTES_DEFAULT  (256UL * 1024UL * 1024UL)
#define WINMALI_VIDMM_SEGMENT_BYTES_MIN      (64UL  * 1024UL * 1024UL)

NTSTATUS
WinMaliVidmmAllocateSegment(_Inout_ PWINMALI_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS lowest, highest, skip;
    SIZE_T           tryBytes;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->DmaSegmentVa != NULL) {
        return STATUS_SUCCESS;  /* idempotent */
    }

    lowest.QuadPart  = 0;
    highest.QuadPart = (LONGLONG)-1;   /* anywhere in physical memory */
    skip.QuadPart    = 0;

    /* Try 256 MiB first; on failure shrink in halves down to 64 MiB so a
       fragmented system still gets us a usable pool. */
    for (tryBytes = WINMALI_VIDMM_SEGMENT_BYTES_DEFAULT;
         tryBytes >= WINMALI_VIDMM_SEGMENT_BYTES_MIN;
         tryBytes >>= 1)
    {
        PVOID va = MmAllocateContiguousMemorySpecifyCache(
            tryBytes, lowest, highest, skip, MmCached);
        if (va != NULL) {
            Adapter->DmaSegmentVa    = va;
            Adapter->DmaSegmentPhys  = MmGetPhysicalAddress(va);
            Adapter->DmaSegmentBytes = tryBytes;
            RtlZeroMemory(va, tryBytes);
            WINMALI_TRACE(
                "VIDMM segment: va=%p phys=0x%llx size=0x%Ix (%lu MiB)",
                va,
                (ULONGLONG)Adapter->DmaSegmentPhys.QuadPart,
                tryBytes,
                (ULONG)(tryBytes >> 20));
            return STATUS_SUCCESS;
        }
        WINMALI_WARN(
            "MmAllocateContiguousMemorySpecifyCache(%lu MiB) failed, halving...",
            (ULONG)(tryBytes >> 20));
    }

    WINMALI_ERROR("Failed to allocate any sysmem segment - dxgk will not see a VRAM pool");
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
WinMaliVidmmFreeSegment(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) return;
    if (Adapter->DmaSegmentVa != NULL) {
        MmFreeContiguousMemory(Adapter->DmaSegmentVa);
        WINMALI_TRACE("VIDMM segment freed (size 0x%Ix)", Adapter->DmaSegmentBytes);
        Adapter->DmaSegmentVa          = NULL;
        Adapter->DmaSegmentPhys.QuadPart = 0;
        Adapter->DmaSegmentBytes       = 0;
    }
}
