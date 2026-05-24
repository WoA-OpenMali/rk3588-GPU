/*
 * WinMaliVm.c - VM handle table implementation.
 *
 * Metadata-only for now. VmBind isn't wired; this layer exists so the UMD
 * can allocate / track its VM handles and run its own bookkeeping.
 * Hooking VmBind into Mali MMU PTE writes is a separate change that
 * extends WinMaliMmuMapGpuRange to accept an AS-slot parameter (currently
 * hardcoded to AS1 = the bring-up kernel AS).
 */

#include "WinMaliKmd.h"
#include "WinMaliVm.h"

#define WMERR_OK          0
#define WMERR_EINVAL      -22
#define WMERR_ENOMEM      -12
#define WMERR_ENOENT      -2

/* Default user VA span when UMD passes 0 in VmCreate.UserVaRange. Must
   match what we publish in DRIVERCAPS.InternalGpuVirtualAddressRange* -
   that span is 0x10000 .. 0x3FFFFFFF (30-bit). The reserved low region
   (firmware sections under 0x04040000) is excluded by anchoring the user
   base above WINMALI_SYSMEM_GPU_BASE. */
#define WINMALI_VM_DEFAULT_USER_VA_BASE     WINMALI_SYSMEM_GPU_BASE
#define WINMALI_VM_DEFAULT_USER_VA_RANGE    (0x30000000ULL)  /* 768 MiB */

VOID
WinMaliVmTableInit(_Out_ PWINMALI_VM_TABLE Table)
{
    KeInitializeSpinLock(&Table->Lock);
    InitializeListHead(&Table->Head);
    Table->NextId = 1;
    Table->Count = 0;
}

static VOID
WinMaliVmFree_(_Inout_ PWINMALI_ADAPTER Adapter, _Inout_ PWINMALI_VM Vm)
{
    if (Vm != NULL) {
        if (Vm->PtInitialized) {
            WinMaliMmuVmTeardown(Adapter, &Vm->Pt);
            Vm->PtInitialized = FALSE;
        }
        Vm->Magic = 0;
        ExFreePoolWithTag(Vm, WINMALI_POOL_TAG);
    }
}

VOID
WinMaliVmTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                       _Inout_ PWINMALI_VM_TABLE Table)
{
    KIRQL oldIrql;
    LIST_ENTRY freeHead;
    PLIST_ENTRY entry;

    UNREFERENCED_PARAMETER(Adapter);
    InitializeListHead(&freeHead);

    KeAcquireSpinLock(&Table->Lock, &oldIrql);
    while (!IsListEmpty(&Table->Head)) {
        entry = RemoveHeadList(&Table->Head);
        InsertTailList(&freeHead, entry);
    }
    Table->Count = 0;
    KeReleaseSpinLock(&Table->Lock, oldIrql);

    while (!IsListEmpty(&freeHead)) {
        entry = RemoveHeadList(&freeHead);
        WinMaliVmFree_(Adapter, CONTAINING_RECORD(entry, WINMALI_VM, Link));
    }
}

int
WinMaliVmCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                _In_ ULONG Flags,
                _In_ UINT64 UserVaRange,
                _Out_ ULONG* OutId,
                _Out_ UINT64* OutGrantedRange)
{
    PWINMALI_VM vm;
    KIRQL oldIrql;
    UINT64 granted;

    *OutId = 0;
    *OutGrantedRange = 0;

    granted = (UserVaRange != 0) ? UserVaRange : WINMALI_VM_DEFAULT_USER_VA_RANGE;
    /* Cap to the span we advertise. */
    if (granted > WINMALI_VM_DEFAULT_USER_VA_RANGE) {
        granted = WINMALI_VM_DEFAULT_USER_VA_RANGE;
    }

    vm = (PWINMALI_VM)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*vm),
                                            WINMALI_POOL_TAG);
    if (vm == NULL) {
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(vm, sizeof(*vm));
    vm->Magic       = WINMALI_VM_MAGIC;
    vm->Flags       = Flags;
    vm->State       = WinMaliVmRun_Usable;
    vm->UserVaBase  = WINMALI_VM_DEFAULT_USER_VA_BASE;
    vm->UserVaSize  = granted;
    vm->RefCount    = 1;

    {
        NTSTATUS st = WinMaliMmuVmInit(Adapter, &vm->Pt);
        if (!NT_SUCCESS(st)) {
            ExFreePoolWithTag(vm, WINMALI_POOL_TAG);
            WINMALI_WARN("VmCreate: WinMaliMmuVmInit failed 0x%08x", st);
            return (st == STATUS_INSUFFICIENT_RESOURCES) ? WMERR_ENOMEM
                                                         : WMERR_EINVAL;
        }
        vm->PtInitialized = TRUE;
    }

    KeAcquireSpinLock(&Adapter->VmTable.Lock, &oldIrql);
    vm->Id = (ULONG)(Adapter->VmTable.NextId++);
    InsertTailList(&Adapter->VmTable.Head, &vm->Link);
    Adapter->VmTable.Count++;
    KeReleaseSpinLock(&Adapter->VmTable.Lock, oldIrql);

    *OutId = vm->Id;
    *OutGrantedRange = granted;
    WINMALI_TRACE("VmCreate: id=%u flags=0x%x base=0x%llx range=0x%llx",
                  vm->Id, vm->Flags, (ULONGLONG)vm->UserVaBase,
                  (ULONGLONG)vm->UserVaSize);
    return WMERR_OK;
}

static PWINMALI_VM
WinMaliVmTakeById_(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Id)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_VM found = NULL;

    KeAcquireSpinLock(&Adapter->VmTable.Lock, &oldIrql);
    for (entry = Adapter->VmTable.Head.Flink;
         entry != &Adapter->VmTable.Head;
         entry = entry->Flink) {
        PWINMALI_VM vm = CONTAINING_RECORD(entry, WINMALI_VM, Link);
        if (vm->Id == Id) {
            RemoveEntryList(&vm->Link);
            Adapter->VmTable.Count--;
            found = vm;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->VmTable.Lock, oldIrql);
    return found;
}

int
WinMaliVmDestroy(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Id)
{
    PWINMALI_VM vm = WinMaliVmTakeById_(Adapter, Id);
    if (vm == NULL) {
        return WMERR_ENOENT;
    }
    WINMALI_TRACE("VmDestroy: id=%u", vm->Id);
    WinMaliVmFree_(Adapter, vm);
    return WMERR_OK;
}

PWINMALI_VM
WinMaliVmGet(_In_ PWINMALI_ADAPTER Adapter, _In_ ULONG Id)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_VM found = NULL;

    KeAcquireSpinLock(&Adapter->VmTable.Lock, &oldIrql);
    for (entry = Adapter->VmTable.Head.Flink;
         entry != &Adapter->VmTable.Head;
         entry = entry->Flink) {
        PWINMALI_VM vm = CONTAINING_RECORD(entry, WINMALI_VM, Link);
        if (vm->Id == Id) {
            InterlockedIncrement(&vm->RefCount);
            found = vm;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->VmTable.Lock, oldIrql);
    return found;
}

VOID
WinMaliVmPut(_In_ PWINMALI_VM Vm)
{
    if (Vm != NULL) {
        InterlockedDecrement(&Vm->RefCount);
    }
}

int
WinMaliVmGetState(_In_ PWINMALI_ADAPTER Adapter,
                  _In_ ULONG Id,
                  _Out_ WINMALI_VM_RUN_STATE* OutState)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    int err = WMERR_ENOENT;

    *OutState = WinMaliVmRun_Unusable;
    KeAcquireSpinLock(&Adapter->VmTable.Lock, &oldIrql);
    for (entry = Adapter->VmTable.Head.Flink;
         entry != &Adapter->VmTable.Head;
         entry = entry->Flink) {
        PWINMALI_VM vm = CONTAINING_RECORD(entry, WINMALI_VM, Link);
        if (vm->Id == Id) {
            *OutState = vm->State;
            err = WMERR_OK;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->VmTable.Lock, oldIrql);
    return err;
}
