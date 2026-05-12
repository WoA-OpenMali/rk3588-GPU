/*++
    contiguous heap + ARM LPAE-style page tables + Mali AS
    programming. Sequences follow Linux Panthor
    `panthor_mmu_as_enable` / `panthor_mmu_as_disable` /
    `panthor_vm_active` (transcfg + memattr + TRANSTAB + UPDATE).

    Register layout: `panthor-linux6.1/.../panthor_regs.h` (MMU_BASE,
    MMU_AS, AS_COMMAND_*, AS_TRANSCFG_*).

    Firmware (CSF) gap — important:

    On Linux, `panthor_device` probe order is `panthor_gpu_init` ->
    `panthor_mmu_init` -> **`panthor_fw_init`** -> `panthor_sched_init`
    (`panthor_device.c`). That last step loads **`mali_csffw.bin`** into
    the CSF MCU (`panthor_fw.c`). Firmware load is `WinMaliFwInit` after MMU init.

    Programming AS TRANSTAB here can still be useful for MMIO-level bring-up,
    but it is **not** full Linux parity: jobs, global doorbells, and the
    MCU VM all assume `panthor_fw_init` has run. `WinMaliMmuInit` logs
    `MCU_STATUS` and warns when the CSF image is not **ENABLED**.
--*/

#include "WinMaliKmd.h"
#include "WinMaliMmu.h"

// VA used for the first fixed mapping (4 MiB): L2 index == 2, L3 index 0.
#define WINMALI_MMU_TEST_GPU_VA   0x0000000000400000ULL

// MMU AS block: WINMALI_MMU_BASE / WINMALI_MMU_AS_SHIFT in hw/WinMaliHw.h.

#define WINMALI_AS_TRANSTAB_LO(as)   (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x00u)
#define WINMALI_AS_TRANSTAB_HI(as)   (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x04u)
#define WINMALI_AS_MEMATTR_LO(as)    (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x08u)
#define WINMALI_AS_MEMATTR_HI(as)    (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x0Cu)
#define WINMALI_AS_COMMAND(as)       (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x18u)
#define WINMALI_AS_STATUS(as)        (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x28u)
#define WINMALI_AS_TRANSCFG_LO(as)   (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x30u)
#define WINMALI_AS_TRANSCFG_HI(as)   (WINMALI_MMU_BASE + ((as) << WINMALI_MMU_AS_SHIFT) + 0x34u)

#define WINMALI_AS_COMMAND_NOP        0u
#define WINMALI_AS_COMMAND_UPDATE     1u
#define WINMALI_AS_COMMAND_FLUSH_MEM  5u

#define WINMALI_AS_STATUS_AS_ACTIVE   (1u << 0)

#define WINMALI_AS_TRANSCFG_ADRMODE_UNMAPPED       (1ull << 0)
#define WINMALI_AS_TRANSCFG_ADRMODE_AARCH64_4K     (6ull << 0)
#define WINMALI_AS_TRANSCFG_INA_BITS(x)            (((UINT64)(x)) << 6)
#define WINMALI_AS_TRANSCFG_PTW_MEMATTR_NC         (1ull << 24)
#define WINMALI_AS_TRANSCFG_PTW_MEMATTR_WB         (2ull << 24)
#define WINMALI_AS_TRANSCFG_PTW_SH_NS              (0ull << 28)
#define WINMALI_AS_TRANSCFG_PTW_SH_OS              (2ull << 28)
#define WINMALI_AS_TRANSCFG_PTW_SH_IS              (3ull << 28)
#define WINMALI_AS_TRANSCFG_PTW_RA                 (1ull << 30)

// AS_MEMATTR: one byte per MAIR index — NC + Midgard inner + explicit alloc (Linux panthor path).
#define WINMALI_AS_MEMATTR_BYTE_NC   (0x4Cu)

static UINT64
WinMaliMmuMakeL3PagePte_(_In_ UINT64 physPa, _In_ UINT64 attrs)
{
    // See WINMALI_LPAE_L3_PAGE_ATTR_* variants in WinMaliMmu.h for the
    // meaning of each bit. Caller chooses RW/RO + EX/NX based on how the
    // GPU side will use this buffer.
    return (physPa & WINMALI_LPAE_PA_MASK) | attrs;
}

static NTSTATUS
WinMaliMmuWaitAsIdle_(_In_ const WINMALI_HW* Hw, _In_ ULONG asNr)
{
    ULONG spins;
    for (spins = 0; spins < 500000u; ++spins) {
        ULONG st = WinMaliHwRead32(Hw, WINMALI_AS_STATUS(asNr));
        if ((st & WINMALI_AS_STATUS_AS_ACTIVE) == 0) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1);
    }
    WINMALI_WARN(
        "MMU AS%u AS_ACTIVE stuck (AS_STATUS=0x%08x)",
        asNr,
        WinMaliHwRead32(Hw, WINMALI_AS_STATUS(asNr)));
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
WinMaliMmuAsWriteCommand_(_In_ const WINMALI_HW* Hw, _In_ ULONG asNr, _In_ ULONG command)
{
    NTSTATUS status = WinMaliMmuWaitAsIdle_(Hw, asNr);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WinMaliHwWrite32(Hw, WINMALI_AS_COMMAND(asNr), command);
    return WinMaliMmuWaitAsIdle_(Hw, asNr);
}

static NTSTATUS
WinMaliMmuAsFlushMem_(_In_ const WINMALI_HW* Hw, _In_ ULONG asNr)
{
    return WinMaliMmuAsWriteCommand_(Hw, asNr, WINMALI_AS_COMMAND_FLUSH_MEM);
}

NTSTATUS
WinMaliMmuAsEnable(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ ULONG             asNr,
    _In_ UINT64            transtabPa,
    _In_ UINT64            transcfg,
    _In_ UINT64            memattr)
{
    NTSTATUS status;

    status = WinMaliMmuAsFlushMem_(&Adapter->Hw, asNr);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSTAB_LO(asNr), (ULONG)(transtabPa & 0xffffffffull));
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSTAB_HI(asNr), (ULONG)(transtabPa >> 32));

    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_MEMATTR_LO(asNr), (ULONG)(memattr & 0xffffffffull));
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_MEMATTR_HI(asNr), (ULONG)(memattr >> 32));

    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSCFG_LO(asNr), (ULONG)(transcfg & 0xffffffffull));
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSCFG_HI(asNr), (ULONG)(transcfg >> 32));

    status = WinMaliMmuAsWriteCommand_(&Adapter->Hw, asNr, WINMALI_AS_COMMAND_UPDATE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WINMALI_TRACE(
        "MMU AS%u enabled: TTBR=0x%016llx transcfg_lo=0x%08x memattr_lo=0x%08x",
        asNr,
        transtabPa,
        WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_TRANSCFG_LO(asNr)),
        WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_MEMATTR_LO(asNr)));
    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliMmuAsDisable(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG asNr)
{
    NTSTATUS status;

    if (!Adapter->GpuRegsMapped) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = WinMaliMmuAsFlushMem_(&Adapter->Hw, asNr);
    if (!NT_SUCCESS(status)) {
        WINMALI_WARN("MMU AS%u FLUSH_MEM before disable failed 0x%08x", asNr, status);
    }

    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSTAB_LO(asNr), 0);
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSTAB_HI(asNr), 0);
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_MEMATTR_LO(asNr), 0);
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_MEMATTR_HI(asNr), 0);
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSCFG_LO(asNr), (ULONG)WINMALI_AS_TRANSCFG_ADRMODE_UNMAPPED);
    WinMaliHwWrite32(&Adapter->Hw, WINMALI_AS_TRANSCFG_HI(asNr), 0);

    status = WinMaliMmuAsWriteCommand_(&Adapter->Hw, asNr, WINMALI_AS_COMMAND_UPDATE);
    if (!NT_SUCCESS(status)) {
        WINMALI_WARN("MMU AS%u disable UPDATE failed 0x%08x", asNr, status);
    }
    return status;
}

VOID
WinMaliMmuTeardown(_Inout_ PWINMALI_ADAPTER Adapter)
{
    KIRQL irql;
    ULONG i;
    ULONG releasedMask = 0;

    if (Adapter == NULL) {
        return;
    }

    //
    // Release any per-context AS slots dxgk left programmed. In normal
    // teardown DxgkDdiDestroyContext / SetRootPageTable(Address=0) cleans
    // these out before we get here; on surprise-removal we may still see
    // bound AS slots whose root-PT pages are about to vanish from system
    // memory. Drop TRANSTAB/UPDATE on each so the GPU never page-walks
    // through stale physical addresses.
    //
    KeAcquireSpinLock(&Adapter->AsSlotLock, &irql);
    for (i = 0; i < WINMALI_AS_SLOT_MAX; ++i) {
        if (!Adapter->AsBindings[i].Bound) {
            continue;
        }
        if (i == WINMALI_MMU_MCU_AS || i == WINMALI_MMU_BRINGUP_AS) {
            // These two slots are unbound by their own owners below.
            continue;
        }
        Adapter->AsBindings[i].Bound    = FALSE;
        Adapter->AsBindings[i].hContext = NULL;
        Adapter->AsBindings[i].RootPtPa = 0;
        Adapter->AsSlotInUseMask &= ~(1u << i);
        releasedMask |= (1u << i);
    }
    KeReleaseSpinLock(&Adapter->AsSlotLock, irql);

    if (releasedMask != 0 && Adapter->GpuRegsMapped) {
        for (i = 0; i < WINMALI_AS_SLOT_MAX; ++i) {
            if ((releasedMask & (1u << i)) != 0u) {
                (VOID)WinMaliMmuAsDisable(Adapter, i);
            }
        }
        WINMALI_WARN("MMU teardown: forced-disable AS mask=0x%x (leaked context binds)",
                     releasedMask);
    }

    if (Adapter->GpuMmuAsBound && Adapter->GpuRegsMapped) {
        (VOID)WinMaliMmuAsDisable(Adapter, Adapter->GpuMmuBringupAs);
    }
    Adapter->GpuMmuAsBound = FALSE;
    Adapter->AdapterFlags &= (ULONG)~WINMALI_ADAPTER_FLAG_MMU_READY;

    if (Adapter->MmuScratchHeapVa != NULL) {
        MmFreeContiguousMemory(Adapter->MmuScratchHeapVa);
        WINMALI_TRACE("MMU scratch heap freed va=%p phys=0x%llx",
                      Adapter->MmuScratchHeapVa,
                      (ULONGLONG)Adapter->MmuScratchHeapPhys.QuadPart);
        Adapter->MmuScratchHeapVa     = NULL;
        Adapter->MmuScratchHeapPhys.QuadPart = 0;
        Adapter->MmuScratchHeapBytes  = 0;
    }
}

VOID
WinMaliMmuGetDefaultAsParams(_In_ PWINMALI_ADAPTER Adapter,
                             _Out_ UINT64* TransCfg,
                             _Out_ UINT64* MemAttr)
{
    ULONG              vaBits;
    UINT64             transcfg;
    UINT64             memattr;

    if (TransCfg != NULL) {
        *TransCfg = 0;
    }
    if (MemAttr != NULL) {
        *MemAttr = 0;
    }
    if (Adapter == NULL || TransCfg == NULL || MemAttr == NULL) {
        return;
    }

    vaBits = Adapter->Hw.MmuFeatures & 0xFFu;
    if (vaBits < 32u || vaBits > 48u) {
        vaBits = 48u;
    }

    memattr = 0;
    {
        UINT64 b = (UINT64)WINMALI_AS_MEMATTR_BYTE_NC;
        ULONG  i;
        for (i = 0; i < 8; ++i) {
            memattr |= (b << (8 * i));
        }
    }

    // Page tables and FW section backing memory are allocated with
    // MmNonCached on the CPU side, so the GPU's page-table walker must
    // read them non-cacheable too — otherwise we hit the ARMv8 mismatched-
    // memory-type case (CPU NC vs. GPU WB-OS) and the GPU sees stale
    // (zero) PTEs even though our DRAM stores have completed. Once we
    // back PTs / FW sections with cacheable memory + explicit cache
    // maintenance, this can flip back to PTW_MEMATTR_WB | PTW_SH_OS.
    transcfg = WINMALI_AS_TRANSCFG_PTW_MEMATTR_NC
             | WINMALI_AS_TRANSCFG_ADRMODE_AARCH64_4K
             | WINMALI_AS_TRANSCFG_INA_BITS(55u - vaBits)
             | WINMALI_AS_TRANSCFG_PTW_SH_NS;

    *TransCfg = transcfg;
    *MemAttr  = memattr;
}

NTSTATUS
WinMaliMmuInit(_Inout_ PWINMALI_ADAPTER Adapter)
{
    NTSTATUS           status;
    PHYSICAL_ADDRESS   low;
    PHYSICAL_ADDRESS   high;
    PVOID              va;
    PUCHAR             base;
    UINT64*            l0;
    UINT64*            l1;
    UINT64*            l2;
    UINT64*            l3;
    UINT64             rootPa;
    UINT64             l1pa;
    UINT64             l2pa;
    UINT64             l3pa;
    UINT64             dataPa;
    UINT64             transcfg;
    UINT64             memattr;
    ULONG              vaBits;
    const ULONG        asNr = WINMALI_MMU_BRINGUP_AS;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Adapter->GpuRegsMapped) {
        return STATUS_DEVICE_NOT_READY;
    }

    {
        const ULONG mcu = WinMaliHwRead32(&Adapter->Hw, WINMALI_REG_MCU_STATUS);
        WINMALI_TRACE(
            "MMU init: MCU_STATUS=%lu (WinMaliFwInit follows; 1 means CSF alive)",
            mcu);
    }

    WinMaliMmuTeardown(Adapter);

    low.QuadPart  = 0;
    high.QuadPart = (ULONG64)-1LL;

    va = MmAllocateContiguousMemorySpecifyCache(
        WINMALI_MMU_SCRATCH_HEAP_BYTES,
        low,
        high,
        low,
        MmNonCached);
    if (va == NULL) {
        WINMALI_ERROR("MMU scratch heap: MmAllocateContiguousMemorySpecifyCache(%u) failed",
                      (ULONG)WINMALI_MMU_SCRATCH_HEAP_BYTES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Adapter->MmuScratchHeapVa     = va;
    Adapter->MmuScratchHeapPhys   = MmGetPhysicalAddress(va);
    Adapter->MmuScratchHeapBytes  = WINMALI_MMU_SCRATCH_HEAP_BYTES;

    RtlFillMemory(va, WINMALI_MMU_SCRATCH_HEAP_BYTES, 0);

    base   = (PUCHAR)va;
    rootPa = Adapter->MmuScratchHeapPhys.QuadPart;
    l1pa   = rootPa + 4096ull;
    l2pa   = rootPa + 8192ull;
    l3pa   = rootPa + 12288ull;
    dataPa = rootPa + 16384ull;

    l0 = (UINT64*)base;
    l1 = (UINT64*)(base + 4096);
    l2 = (UINT64*)(base + 8192);
    l3 = (UINT64*)(base + 12288);

    *(PULONG)(base + 16384) = 0xA5A5A5A5UL;

    l0[0] = l1pa | 3ull;
    l1[0] = l2pa | 3ull;
    l2[2] = l3pa | 3ull; // VA 0x400000 → L2 index 2
    // Phase-2 heap data page is a CPU/GPU sanity-check sentinel only —
    // the GPU never executes from here, so RW + NX is the right choice.
    l3[0] = WinMaliMmuMakeL3PagePte_(dataPa, WINMALI_LPAE_L3_PAGE_ATTR_RW_NX);

    KeMemoryBarrier();

    vaBits = Adapter->Hw.MmuFeatures & 0xFFu;
    if (vaBits < 32u || vaBits > 48u) {
        vaBits = 48u;
    }

    WinMaliMmuGetDefaultAsParams(Adapter, &transcfg, &memattr);

    (VOID)WinMaliMmuAsDisable(Adapter, asNr);

    status = WinMaliMmuAsEnable(Adapter, asNr, rootPa, transcfg, memattr);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("WinMaliMmuAsEnable failed 0x%08x", status);
        WinMaliMmuTeardown(Adapter);
        return status;
    }

    {
        UINT64 readBack =
            (UINT64)WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_TRANSTAB_LO(asNr))
            | ((UINT64)WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_TRANSTAB_HI(asNr)) << 32);
        if (readBack != rootPa) {
            WINMALI_WARN("MMU AS%u TRANSTAB readback 0x%016llx != expected 0x%016llx",
                         asNr,
                         readBack,
                         rootPa);
        }
    }

    Adapter->GpuMmuAsBound   = TRUE;
    Adapter->GpuMmuBringupAs = asNr;
    Adapter->AdapterFlags |= WINMALI_ADAPTER_FLAG_MMU_READY;

    WINMALI_TRACE(
        "MMU: scratch heap_va=%p heap_phys=0x%llx L3_pte=0x%016llx gpu_va=0x%llx data_pa=0x%llx va_bits=%u",
        va,
        (ULONGLONG)rootPa,
        l3[0],
        (ULONGLONG)WINMALI_MMU_TEST_GPU_VA,
        (ULONGLONG)dataPa,
        vaBits);

    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliMmuEscapePing(_In_ PWINMALI_ADAPTER Adapter, _Out_ WINMALI_ESCAPE_MMU_OUT* Out)
{
    NTSTATUS st = STATUS_SUCCESS;

    if (Adapter == NULL || Out == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Out, sizeof(*Out));
    Out->Header.Magic   = WINMALI_ESCAPE_MAGIC;
    Out->Header.Opcode  = WinMaliEscapeOp_PingMmu;
    Out->Header.Version = WINMALI_ESCAPE_VERSION;

    Out->AsIndex           = Adapter->GpuMmuBringupAs;
    Out->MappedGpuVa       = WINMALI_MMU_TEST_GPU_VA;
    Out->PatternExpected   = 0xA5A5A5A5UL;
    Out->MmuReady          = (Adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_MMU_READY) ? 1UL : 0UL;
    Out->RootTablePhys     = Adapter->MmuScratchHeapPhys.QuadPart;
    Out->DataPhys          = 0;
    Out->PatternActual     = 0;
    Out->TranstabLo        = 0;
    Out->TranstabHi        = 0;
    Out->AsStatus          = 0;

    if (Adapter->MmuScratchHeapVa == NULL) {
        st = STATUS_DEVICE_NOT_READY;
    } else {
        Out->DataPhys = Adapter->MmuScratchHeapPhys.QuadPart + 16384ull;
        Out->PatternActual = *(volatile ULONG*)((PUCHAR)Adapter->MmuScratchHeapVa + 16384);
    }

    if (Adapter->GpuRegsMapped && Adapter->GpuMmuAsBound) {
        ULONG as = Adapter->GpuMmuBringupAs;
        Out->TranstabLo = WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_TRANSTAB_LO(as));
        Out->TranstabHi = WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_TRANSTAB_HI(as));
        Out->AsStatus   = WinMaliHwRead32(&Adapter->Hw, WINMALI_AS_STATUS(as));
    }

    Out->NtStatus = (ULONG)st;
    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliMmuMapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ PHYSICAL_ADDRESS   FirstPagePa,
    _In_ ULONG              PageCount,
    _In_ UINT64             Attrs)
{
    PUINT64 l3;
    ULONG   i;
    UINT64  regionLo = WINMALI_MMU_TEST_GPU_VA;
    UINT64  regionHi = WINMALI_MMU_TEST_GPU_VA + (2ull * 1024ull * 1024ull);

    if (Adapter == NULL || Adapter->MmuScratchHeapVa == NULL || !Adapter->GpuMmuAsBound) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (PageCount == 0u || PageCount > 512u) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((GpuVaStart & 0xfffull) != 0ull || (FirstPagePa.QuadPart & 0xfffull) != 0ull) {
        return STATUS_INVALID_PARAMETER;
    }
    if (GpuVaStart < regionLo || GpuVaStart >= regionHi) {
        return STATUS_INVALID_PARAMETER;
    }
    if (GpuVaStart + (UINT64)PageCount * 4096ull > regionHi) {
        return STATUS_INVALID_PARAMETER;
    }

    l3 = (PUINT64)((PUCHAR)Adapter->MmuScratchHeapVa + 12288u);
    for (i = 0; i < PageCount; ++i) {
        UINT64 va  = GpuVaStart + (UINT64)i * 4096ull;
        UINT64 idx = (va - regionLo) >> 12;
        if (idx >= 512ull) {
            return STATUS_INVALID_PARAMETER;
        }
        l3[(ULONG)idx] = WinMaliMmuMakeL3PagePte_(FirstPagePa.QuadPart + (UINT64)i * 4096ull, Attrs);
        KeMemoryBarrier();
    }

    (VOID)WinMaliMmuAsFlushMem_(&Adapter->Hw, Adapter->GpuMmuBringupAs);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// AS-slot allocator (CreateProcess / SetRootPageTable / DestroyProcess).
//
// AS0 = CSF MCU, AS1 = kernel bring-up. AS2..AS(N-1) are dynamic.
// `AsSlotInUseMask` is the source of truth; `AsSlotPresentMask` mirrors the
// HW `GPU_AS_PRESENT` register. Both are populated by
// WinMaliMmuInitAsAllocator at AddDevice time so this allocator works even
// before WinMaliMmuInit has bound the bring-up AS.
// ---------------------------------------------------------------------------

VOID
WinMaliMmuInitAsAllocator(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) {
        return;
    }

    KeInitializeSpinLock(&Adapter->AsSlotLock);
    Adapter->AsSlotPresentMask = (Adapter->Hw.AsPresent != 0)
                                     ? Adapter->Hw.AsPresent
                                     : 0xFFu;
    if ((Adapter->AsSlotPresentMask & ((1u << WINMALI_MMU_MCU_AS)
                                       | (1u << WINMALI_MMU_BRINGUP_AS))) !=
        ((1u << WINMALI_MMU_MCU_AS) | (1u << WINMALI_MMU_BRINGUP_AS)))
    {
        Adapter->AsSlotPresentMask |= (1u << WINMALI_MMU_MCU_AS)
                                    | (1u << WINMALI_MMU_BRINGUP_AS);
    }
    Adapter->AsSlotInUseMask = (1u << WINMALI_MMU_MCU_AS)
                             | (1u << WINMALI_MMU_BRINGUP_AS);
    RtlZeroMemory(Adapter->AsBindings, sizeof(Adapter->AsBindings));
}

static ULONG
WinMaliMmuFindAsForContext_(_In_ PWINMALI_ADAPTER Adapter, _In_ HANDLE hContext)
{
    ULONG i;
    for (i = 0; i < WINMALI_AS_SLOT_MAX; ++i) {
        if (Adapter->AsBindings[i].Bound
            && Adapter->AsBindings[i].hContext == hContext)
        {
            return i;
        }
    }
    return WINMALI_AS_SLOT_MAX;
}

static ULONG
WinMaliMmuPickFreeAs_(_In_ PWINMALI_ADAPTER Adapter)
{
    ULONG i;
    for (i = 0; i < WINMALI_AS_SLOT_MAX; ++i) {
        if ((Adapter->AsSlotPresentMask & (1u << i)) == 0u) {
            continue;
        }
        if ((Adapter->AsSlotInUseMask & (1u << i)) != 0u) {
            continue;
        }
        return i;
    }
    return WINMALI_AS_SLOT_MAX;
}

NTSTATUS
WinMaliMmuBindContextRootPt(_Inout_ PWINMALI_ADAPTER Adapter,
                            _In_ HANDLE              hContext,
                            _In_ UINT64              RootPtPa,
                            _Out_opt_ PULONG         OutAs)
{
    KIRQL    irql;
    ULONG    as;
    NTSTATUS status;
    UINT64   transcfg = 0;
    UINT64   memattr  = 0;
    BOOLEAN  reused;

    if (OutAs != NULL) {
        *OutAs = WINMALI_AS_SLOT_MAX;
    }
    if (Adapter == NULL || !Adapter->GpuRegsMapped) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (RootPtPa == 0ULL) {
        return WinMaliMmuUnbindContext(Adapter, hContext);
    }
    if ((RootPtPa & 0xFFFull) != 0ULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->AsSlotLock, &irql);
    as = WinMaliMmuFindAsForContext_(Adapter, hContext);
    reused = (as < WINMALI_AS_SLOT_MAX);
    if (!reused) {
        as = WinMaliMmuPickFreeAs_(Adapter);
        if (as >= WINMALI_AS_SLOT_MAX) {
            KeReleaseSpinLock(&Adapter->AsSlotLock, irql);
            WINMALI_WARN(
                "BindContextRootPt: no free AS slot (inUse=0x%x present=0x%x hCtx=%p)",
                Adapter->AsSlotInUseMask,
                Adapter->AsSlotPresentMask,
                hContext);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Adapter->AsSlotInUseMask |= (1u << as);
    }
    Adapter->AsBindings[as].hContext = hContext;
    Adapter->AsBindings[as].RootPtPa = RootPtPa;
    Adapter->AsBindings[as].Bound    = TRUE;
    KeReleaseSpinLock(&Adapter->AsSlotLock, irql);

    WinMaliMmuGetDefaultAsParams(Adapter, &transcfg, &memattr);

    status = WinMaliMmuAsEnable(Adapter, as, RootPtPa, transcfg, memattr);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&Adapter->AsSlotLock, &irql);
        Adapter->AsBindings[as].Bound    = FALSE;
        Adapter->AsBindings[as].hContext = NULL;
        Adapter->AsBindings[as].RootPtPa = 0;
        if (!reused) {
            Adapter->AsSlotInUseMask &= ~(1u << as);
        }
        KeReleaseSpinLock(&Adapter->AsSlotLock, irql);
        WINMALI_WARN("BindContextRootPt: AsEnable AS%u failed 0x%08x", as, status);
        return status;
    }

    if (OutAs != NULL) {
        *OutAs = as;
    }
    WINMALI_TRACE(
        "BindContextRootPt: hCtx=%p root=0x%llx -> AS%u (%s)",
        hContext,
        (ULONGLONG)RootPtPa,
        as,
        reused ? "rebind" : "new");
    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliMmuUnbindContext(_Inout_ PWINMALI_ADAPTER Adapter, _In_ HANDLE hContext)
{
    KIRQL  irql;
    ULONG  as;
    UINT64 root;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->AsSlotLock, &irql);
    as = WinMaliMmuFindAsForContext_(Adapter, hContext);
    if (as >= WINMALI_AS_SLOT_MAX) {
        KeReleaseSpinLock(&Adapter->AsSlotLock, irql);
        return STATUS_SUCCESS;
    }
    root = Adapter->AsBindings[as].RootPtPa;
    Adapter->AsBindings[as].Bound    = FALSE;
    Adapter->AsBindings[as].hContext = NULL;
    Adapter->AsBindings[as].RootPtPa = 0;
    Adapter->AsSlotInUseMask &= ~(1u << as);
    KeReleaseSpinLock(&Adapter->AsSlotLock, irql);

    if (Adapter->GpuRegsMapped) {
        (VOID)WinMaliMmuAsDisable(Adapter, as);
    }
    WINMALI_TRACE(
        "UnbindContext: hCtx=%p AS%u root=0x%llx released",
        hContext, as, (ULONGLONG)root);
    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliMmuUnmapGpuRange(
    _Inout_ PWINMALI_ADAPTER Adapter,
    _In_ UINT64             GpuVaStart,
    _In_ ULONG              PageCount)
{
    PUINT64 l3;
    ULONG   i;
    UINT64  regionLo = WINMALI_MMU_TEST_GPU_VA;
    UINT64  regionHi = WINMALI_MMU_TEST_GPU_VA + (2ull * 1024ull * 1024ull);

    if (Adapter == NULL || Adapter->MmuScratchHeapVa == NULL || !Adapter->GpuMmuAsBound) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (PageCount == 0u || PageCount > 512u) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((GpuVaStart & 0xfffull) != 0ull) {
        return STATUS_INVALID_PARAMETER;
    }
    if (GpuVaStart < regionLo || GpuVaStart >= regionHi) {
        return STATUS_INVALID_PARAMETER;
    }
    if (GpuVaStart + (UINT64)PageCount * 4096ull > regionHi) {
        return STATUS_INVALID_PARAMETER;
    }

    l3 = (PUINT64)((PUCHAR)Adapter->MmuScratchHeapVa + 12288u);
    for (i = 0; i < PageCount; ++i) {
        UINT64 va  = GpuVaStart + (UINT64)i * 4096ull;
        UINT64 idx = (va - regionLo) >> 12;
        if (idx >= 512ull) {
            return STATUS_INVALID_PARAMETER;
        }
        l3[(ULONG)idx] = 0;
        KeMemoryBarrier();
    }

    (VOID)WinMaliMmuAsFlushMem_(&Adapter->Hw, Adapter->GpuMmuBringupAs);
    return STATUS_SUCCESS;
}
