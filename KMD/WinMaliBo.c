/*
 * WinMaliBo.c - BO lifecycle implementation.
 *
 * Pages come from MmAllocatePagesForMdlEx (sysmem, non-contiguous, pinned).
 * The resulting MDL is what we track; the GPU MMU later maps individual
 * PFNs via VmBind/UPDATE_PAGE_TABLE. CPU mappings are produced on demand
 * via MmMapLockedPagesSpecifyCache(UserMode).
 *
 * Handle allocation is a monotonic counter - handles are never reused, so
 * a UMD that holds a stale handle gets a clean -ENOENT rather than
 * accidentally aliasing a fresh BO.
 */

#include "WinMaliKmd.h"
#include "WinMaliBo.h"

/* Local copy of Linux errnos (same values as WinMaliEscape.c). */
#define WMERR_OK          0
#define WMERR_EINVAL      -22
#define WMERR_ENOMEM      -12
#define WMERR_ENOENT      -2
#define WMERR_EBUSY       -16
#define WMERR_EACCES      -13

VOID
WinMaliBoTableInit(_Out_ PWINMALI_BO_TABLE Table)
{
    KeInitializeSpinLock(&Table->Lock);
    InitializeListHead(&Table->Head);
    Table->NextHandle = 1;     /* 0 is reserved for "invalid" */
    Table->BoCount = 0;
}

/* Free a BO's pages + structure. Caller must have removed it from the list
   already and dropped any user mapping. */
static VOID
WinMaliBoFree_(_Inout_ PWINMALI_BO Bo)
{
    if (Bo == NULL) {
        return;
    }
    if (Bo->UserVa != NULL && Bo->Mdl != NULL) {
        /* Only safe to unmap if we're still in the process that owns the
           mapping. If not, document the leak - the process is normally
           tearing down anyway (this only happens in adapter RemoveDevice
           or when the UMD failed to destroy its BO). */
        if (PsGetCurrentProcess() == Bo->UserVaProcess) {
            __try {
                MmUnmapLockedPages(Bo->UserVa, Bo->Mdl);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                WINMALI_WARN("BoFree: MmUnmapLockedPages raised 0x%08x for handle %u",
                             GetExceptionCode(), Bo->Handle);
            }
        } else {
            WINMALI_WARN("BoFree: handle %u UserVa=%p owned by foreign process; leaking mapping",
                         Bo->Handle, Bo->UserVa);
        }
        Bo->UserVa = NULL;
        Bo->UserVaProcess = NULL;
    }
    if (Bo->Mdl != NULL) {
        if (Bo->PagesImported) {
            /* Hand-built PFN MDL over a dxgk allocation's pages: drop any
               system mapping we created, free the MDL shell, leave the
               pages alone (VidMm owns them). */
            if (Bo->Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) {
                MmUnmapLockedPages(Bo->Mdl->MappedSystemVa, Bo->Mdl);
            }
            ExFreePoolWithTag(Bo->Mdl, WINMALI_POOL_TAG);
        } else {
            MmFreePagesFromMdl(Bo->Mdl);
            ExFreePool(Bo->Mdl);
        }
        Bo->Mdl = NULL;
    }
    Bo->Magic = 0;
    ExFreePoolWithTag(Bo, WINMALI_POOL_TAG);
}

VOID
WinMaliBoTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                       _Inout_ PWINMALI_BO_TABLE Table)
{
    KIRQL oldIrql;
    LIST_ENTRY freeHead;
    PLIST_ENTRY entry;

    UNREFERENCED_PARAMETER(Adapter);
    InitializeListHead(&freeHead);

    /* Steal the whole list under the lock, then free outside. */
    KeAcquireSpinLock(&Table->Lock, &oldIrql);
    while (!IsListEmpty(&Table->Head)) {
        entry = RemoveHeadList(&Table->Head);
        InsertTailList(&freeHead, entry);
    }
    Table->BoCount = 0;
    KeReleaseSpinLock(&Table->Lock, oldIrql);

    while (!IsListEmpty(&freeHead)) {
        entry = RemoveHeadList(&freeHead);
        PWINMALI_BO bo = CONTAINING_RECORD(entry, WINMALI_BO, Link);
        WinMaliBoFree_(bo);
    }
}

int
WinMaliBoCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                _In_ SIZE_T Size,
                _In_ ULONG Flags,
                _In_ ULONG ExclusiveVmId,
                _In_opt_ PVOID OwnerDevice,
                _Out_ ULONG* OutHandle)
{
    PWINMALI_BO bo;
    PHYSICAL_ADDRESS low, high, skip;
    KIRQL oldIrql;
    SIZE_T pageAligned;

    *OutHandle = 0;
    if (Size == 0) {
        return WMERR_EINVAL;
    }
    pageAligned = (Size + PAGE_SIZE - 1) & ~((SIZE_T)PAGE_SIZE - 1);

    bo = (PWINMALI_BO)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*bo),
                                            WINMALI_POOL_TAG);
    if (bo == NULL) {
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(bo, sizeof(*bo));
    bo->Magic = WINMALI_BO_MAGIC;
    bo->Size = pageAligned;
    bo->Flags = Flags;
    bo->ExclusiveVmId = ExclusiveVmId;
    bo->RefCount = 1;
    bo->OwnerDevice = OwnerDevice;
    KeInitializeMutex(&bo->Lock, 0);

    low.QuadPart  = 0;
    high.QuadPart = MAXLONGLONG;
    skip.QuadPart = 0;
    /* MUST be MmCached to MATCH the GPU MMU mapping, which is cacheable
       (VmBind pte attrs 0x743). A v46 experiment mapped these WriteCombined
       (Normal-NC) to "fix coherency" - it REGRESSED readback to all black,
       because a CPU non-cacheable + GPU cacheable alias on the same physical
       pages is architecturally undefined on ARM64. Cached GL clear->readback
       worked (v22: 51,102,153,255), proving the cached path is coherent
       enough here; the D3D-triangle black is a separate copy/coverage issue,
       not CPU/GPU cache coherency. Keep both sides cached. */
    bo->Mdl = MmAllocatePagesForMdlEx(low, high, skip, pageAligned,
                                      MmCached,
                                      MM_ALLOCATE_FULLY_REQUIRED);
    if (bo->Mdl == NULL ||
        MmGetMdlByteCount(bo->Mdl) < pageAligned) {
        if (bo->Mdl != NULL) {
            MmFreePagesFromMdl(bo->Mdl);
            ExFreePool(bo->Mdl);
        }
        ExFreePoolWithTag(bo, WINMALI_POOL_TAG);
        return WMERR_ENOMEM;
    }

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    bo->Handle = (ULONG)(Adapter->BoTable.NextHandle++);
    InsertTailList(&Adapter->BoTable.Head, &bo->Link);
    Adapter->BoTable.BoCount++;
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);

    *OutHandle = bo->Handle;
    WINMALI_TRACE("BoCreate: handle=%u size=%llu flags=0x%x vm=%u",
                  bo->Handle, (ULONGLONG)bo->Size, bo->Flags, bo->ExclusiveVmId);
    return WMERR_OK;
}

int
WinMaliBoCreateFromPfns(_Inout_ PWINMALI_ADAPTER Adapter,
                        _In_reads_(PageCount) const PFN_NUMBER* Pfns,
                        _In_ ULONG PageCount,
                        _In_ ULONG Flags,
                        _In_opt_ PVOID OwnerDevice,
                        _Out_ ULONG* OutHandle)
{
    PWINMALI_BO bo;
    PMDL        mdl;
    SIZE_T      mdlBytes;
    KIRQL       oldIrql;

    *OutHandle = 0;
    if (Pfns == NULL || PageCount == 0) {
        return WMERR_EINVAL;
    }

    bo = (PWINMALI_BO)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*bo),
                                            WINMALI_POOL_TAG);
    if (bo == NULL) {
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(bo, sizeof(*bo));
    bo->Magic = WINMALI_BO_MAGIC;
    bo->Size = (SIZE_T)PageCount << PAGE_SHIFT;
    bo->Flags = Flags;
    bo->ExclusiveVmId = 0;   /* shareable - the caller maps it via VmBind */
    bo->RefCount = 1;
    bo->OwnerDevice = OwnerDevice;
    bo->PagesImported = TRUE;
    KeInitializeMutex(&bo->Lock, 0);

    /* Hand-built MDL describing the imported frames. MmInitializeMdl with a
       NULL base VA gives ByteOffset 0 and the right page count; marking
       MDL_PAGES_LOCKED makes MmMapLockedPages* / MmGetSystemAddressForMdlSafe
       treat it like any pinned-pages MDL (same shape MmAllocatePagesForMdlEx
       produces). The pages themselves are pinned by the UMD's LockCb on the
       source allocation, NOT by this MDL. */
    mdlBytes = sizeof(MDL) + (SIZE_T)PageCount * sizeof(PFN_NUMBER);
    mdl = (PMDL)ExAllocatePoolWithTag(NonPagedPoolNx, mdlBytes,
                                      WINMALI_POOL_TAG);
    if (mdl == NULL) {
        ExFreePoolWithTag(bo, WINMALI_POOL_TAG);
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(mdl, mdlBytes);
    MmInitializeMdl(mdl, NULL, (SIZE_T)PageCount << PAGE_SHIFT);
    RtlCopyMemory(MmGetMdlPfnArray(mdl), Pfns,
                  (SIZE_T)PageCount * sizeof(PFN_NUMBER));
    mdl->MdlFlags |= MDL_PAGES_LOCKED;
    bo->Mdl = mdl;

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    bo->Handle = (ULONG)(Adapter->BoTable.NextHandle++);
    InsertTailList(&Adapter->BoTable.Head, &bo->Link);
    Adapter->BoTable.BoCount++;
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);

    *OutHandle = bo->Handle;
    WINMALI_TRACE("BoCreateFromPfns: handle=%u pages=%u flags=0x%x",
                  bo->Handle, PageCount, bo->Flags);
    return WMERR_OK;
}

/* Find a BO by handle and pop it from the list under the lock. */
static PWINMALI_BO
WinMaliBoTakeByHandle_(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_BO found = NULL;

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    for (entry = Adapter->BoTable.Head.Flink;
         entry != &Adapter->BoTable.Head;
         entry = entry->Flink) {
        PWINMALI_BO bo = CONTAINING_RECORD(entry, WINMALI_BO, Link);
        if (bo->Handle == Handle) {
            RemoveEntryList(&bo->Link);
            Adapter->BoTable.BoCount--;
            found = bo;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);
    return found;
}

int
WinMaliBoDestroy(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    PWINMALI_BO bo = WinMaliBoTakeByHandle_(Adapter, Handle);
    if (bo == NULL) {
        return WMERR_ENOENT;
    }
    WINMALI_TRACE("BoDestroy: handle=%u size=%llu",
                  bo->Handle, (ULONGLONG)bo->Size);
    WinMaliBoFree_(bo);
    return WMERR_OK;
}

ULONG
WinMaliBoRundownOwner(_Inout_ PWINMALI_ADAPTER Adapter, _In_ PVOID OwnerDevice)
{
    KIRQL oldIrql;
    LIST_ENTRY freeHead;
    PLIST_ENTRY entry, next;
    ULONG freed = 0;

    if (OwnerDevice == NULL) {
        return 0;
    }
    InitializeListHead(&freeHead);

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    for (entry = Adapter->BoTable.Head.Flink;
         entry != &Adapter->BoTable.Head;
         entry = next) {
        PWINMALI_BO bo = CONTAINING_RECORD(entry, WINMALI_BO, Link);
        next = entry->Flink;
        if (bo->OwnerDevice == OwnerDevice) {
            RemoveEntryList(&bo->Link);
            Adapter->BoTable.BoCount--;
            InsertTailList(&freeHead, &bo->Link);
        }
    }
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);

    while (!IsListEmpty(&freeHead)) {
        PWINMALI_BO bo = CONTAINING_RECORD(RemoveHeadList(&freeHead),
                                           WINMALI_BO, Link);
        WINMALI_TRACE("BoRundown: handle=%u size=%llu (owner device died "
                      "without BoDestroy)", bo->Handle, (ULONGLONG)bo->Size);
        WinMaliBoFree_(bo);
        ++freed;
    }
    return freed;
}

PWINMALI_BO
WinMaliBoGet(_In_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_BO found = NULL;

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    for (entry = Adapter->BoTable.Head.Flink;
         entry != &Adapter->BoTable.Head;
         entry = entry->Flink) {
        PWINMALI_BO bo = CONTAINING_RECORD(entry, WINMALI_BO, Link);
        if (bo->Handle == Handle) {
            InterlockedIncrement(&bo->RefCount);
            found = bo;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);
    return found;
}

VOID
WinMaliBoPut(_In_ PWINMALI_BO Bo)
{
    if (Bo != NULL) {
        InterlockedDecrement(&Bo->RefCount);
        /* Note: we don't auto-free at 0 here. BoDestroy is the explicit
           release path. Refcount is informational for now (used by VmBind
           to mark a BO as live-in-GPU). */
    }
}

int
WinMaliBoMapCpu(_Inout_ PWINMALI_ADAPTER Adapter,
                _In_ ULONG Handle,
                _In_ ULONG Prot,
                _Out_ UINT64* OutUserVa)
{
    PWINMALI_BO bo;
    PVOID va;
    LOCK_OPERATION op;
    int err = WMERR_OK;

    *OutUserVa = 0;
    bo = WinMaliBoGet(Adapter, Handle);
    if (bo == NULL) {
        return WMERR_ENOENT;
    }

    KeWaitForSingleObject(&bo->Lock, Executive, KernelMode, FALSE, NULL);

    if (bo->UserVa != NULL) {
        /* Already mapped in some process. We don't support remapping
           into different processes - the design assumes one UMD per BO. */
        if (bo->UserVaProcess == PsGetCurrentProcess()) {
            *OutUserVa = (UINT64)(ULONG_PTR)bo->UserVa;
            err = WMERR_OK;
        } else {
            err = WMERR_EBUSY;
        }
        goto out;
    }

    op = (Prot & 2) ? IoModifyAccess : IoReadAccess;
    /* MmCached to match both the page allocation and the GPU MMU mapping
       (see WinMaliBoCreate: a WriteCombined CPU alias vs the GPU's cacheable
       mapping is undefined on ARM64 and turned readback black). */
    __try {
        va = MmMapLockedPagesSpecifyCache(bo->Mdl,
                                          UserMode,
                                          MmCached,
                                          NULL,    /* OS picks address */
                                          FALSE,
                                          NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WINMALI_WARN("BoMapCpu: MmMapLockedPagesSpecifyCache raised 0x%08x",
                     GetExceptionCode());
        va = NULL;
    }

    if (va == NULL) {
        err = WMERR_ENOMEM;
        goto out;
    }
    bo->UserVa = va;
    bo->UserVaProcess = PsGetCurrentProcess();
    *OutUserVa = (UINT64)(ULONG_PTR)va;
    UNREFERENCED_PARAMETER(op);
    WINMALI_TRACE("BoMapCpu: handle=%u prot=0x%x imported=%u -> userVa=0x%llx",
                  Handle, Prot, (ULONG)bo->PagesImported, *OutUserVa);

out:
    KeReleaseMutex(&bo->Lock, FALSE);
    WinMaliBoPut(bo);
    UNREFERENCED_PARAMETER(Adapter);
    return err;
}

VOID
WinMaliBoFlushVm(_Inout_ PWINMALI_ADAPTER Adapter,
                 _In_ ULONG VmId,
                 _In_ BOOLEAN Clean)
{
    KIRQL       oldIrql;
    PLIST_ENTRY entry;
    /* KeFlushIoBuffers ReadOperation: TRUE = device->memory (invalidate CPU
       cache so CPU sees GPU writes); FALSE = memory->device (clean CPU cache
       to DRAM so GPU sees CPU writes). MDL-based, so it works regardless of
       the (non-cacheable) GPU mapping attribute - alias-safe. */
    BOOLEAN readOp = Clean ? FALSE : TRUE;

    KeAcquireSpinLock(&Adapter->BoTable.Lock, &oldIrql);
    for (entry = Adapter->BoTable.Head.Flink;
         entry != &Adapter->BoTable.Head;
         entry = entry->Flink) {
        PWINMALI_BO bo = CONTAINING_RECORD(entry, WINMALI_BO, Link);
        if (bo->Mdl != NULL &&
            (bo->ExclusiveVmId == VmId || bo->ExclusiveVmId == 0)) {
            KeFlushIoBuffers(bo->Mdl, readOp, TRUE);
        }
    }
    KeReleaseSpinLock(&Adapter->BoTable.Lock, oldIrql);
}

int
WinMaliBoSetLabel(_Inout_ PWINMALI_ADAPTER Adapter,
                  _In_ ULONG Handle,
                  _In_reads_(LabelLen) const char* Label,
                  _In_ ULONG LabelLen)
{
    PWINMALI_BO bo = WinMaliBoGet(Adapter, Handle);
    ULONG copy;
    if (bo == NULL) {
        return WMERR_ENOENT;
    }
    copy = (LabelLen < sizeof(bo->Label) - 1) ? LabelLen : (ULONG)sizeof(bo->Label) - 1;
    RtlZeroMemory(bo->Label, sizeof(bo->Label));
    if (copy > 0 && Label != NULL) {
        RtlCopyMemory(bo->Label, Label, copy);
    }
    WinMaliBoPut(bo);
    return WMERR_OK;
}
