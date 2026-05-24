/*
 * WinMaliGroup.c - CSF queue group lifecycle.
 *
 * Metadata-only until VmBind is wired and the existing CSF kernel-queue
 * submit path (WinMaliCsfSubmitStreamCall_) can be reached from UMD-supplied
 * GPU VAs. Once VmBind lands the next step is GroupSubmit:
 *
 *   for each WINMALI_QUEUE_SUBMIT in the args' tail:
 *     WinMaliCsfSubmitStreamCall_(adapter, fw,
 *                                 submit->StreamAddr,
 *                                 submit->StreamSize,
 *                                 0u, timeoutMs);
 *
 * Sync ops attached to each queue submit will need real SyncObj integration
 * (wait pre-submission, signal post-completion). That's a follow-on step.
 */

#include "WinMaliKmd.h"
#include "WinMaliGroup.h"

#define WMERR_OK          0
#define WMERR_EINVAL      -22
#define WMERR_ENOMEM      -12
#define WMERR_ENOENT      -2

VOID
WinMaliGroupTableInit(_Out_ PWINMALI_GROUP_TABLE Table)
{
    KeInitializeSpinLock(&Table->Lock);
    InitializeListHead(&Table->Head);
    Table->NextHandle = 1;
    Table->Count = 0;
}

static VOID
WinMaliGroupFree_(_Inout_ PWINMALI_GROUP Group)
{
    if (Group != NULL) {
        Group->Magic = 0;
        ExFreePoolWithTag(Group, WINMALI_POOL_TAG);
    }
}

VOID
WinMaliGroupTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                          _Inout_ PWINMALI_GROUP_TABLE Table)
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
        WinMaliGroupFree_(CONTAINING_RECORD(entry, WINMALI_GROUP, Link));
    }
}

int
WinMaliGroupCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                   _In_ const WINMALI_GROUP_CREATE* Args,
                   _In_reads_(QueueCount) const WINMALI_QUEUE_CREATE* Queues,
                   _In_ ULONG QueueCount,
                   _Out_ ULONG* OutHandle)
{
    PWINMALI_GROUP group;
    KIRQL oldIrql;

    *OutHandle = 0;
    if (Args == NULL || QueueCount == 0 || QueueCount > WINMALI_GROUP_MAX_QUEUES) {
        return WMERR_EINVAL;
    }
    if (Queues == NULL) {
        return WMERR_EINVAL;
    }
    /* TODO once VmTable exposes a lookup helper: confirm Args->VmId
       references a real VM. For now we accept any non-zero id. */
    if (Args->VmId == 0) {
        return WMERR_EINVAL;
    }

    group = (PWINMALI_GROUP)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*group),
                                                  WINMALI_POOL_TAG);
    if (group == NULL) {
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(group, sizeof(*group));
    group->Magic            = WINMALI_GROUP_MAGIC;
    group->VmId             = Args->VmId;
    group->Priority         = Args->Priority;
    group->MaxComputeCores  = Args->MaxComputeCores;
    group->MaxFragmentCores = Args->MaxFragmentCores;
    group->MaxTilerCores    = Args->MaxTilerCores;
    group->ComputeCoreMask  = Args->ComputeCoreMask;
    group->FragmentCoreMask = Args->FragmentCoreMask;
    group->TilerCoreMask    = Args->TilerCoreMask;
    group->QueuesCount      = QueueCount;
    RtlCopyMemory(group->Queues, Queues, QueueCount * sizeof(Queues[0]));
    group->RefCount         = 1;

    KeAcquireSpinLock(&Adapter->GroupTable.Lock, &oldIrql);
    group->Handle = (ULONG)(Adapter->GroupTable.NextHandle++);
    InsertTailList(&Adapter->GroupTable.Head, &group->Link);
    Adapter->GroupTable.Count++;
    KeReleaseSpinLock(&Adapter->GroupTable.Lock, oldIrql);

    *OutHandle = group->Handle;
    WINMALI_TRACE("GroupCreate: handle=%u vm=%u queues=%u prio=%u",
                  group->Handle, group->VmId, group->QueuesCount, group->Priority);
    return WMERR_OK;
}

static PWINMALI_GROUP
WinMaliGroupTakeByHandle_(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_GROUP found = NULL;

    KeAcquireSpinLock(&Adapter->GroupTable.Lock, &oldIrql);
    for (entry = Adapter->GroupTable.Head.Flink;
         entry != &Adapter->GroupTable.Head;
         entry = entry->Flink) {
        PWINMALI_GROUP g = CONTAINING_RECORD(entry, WINMALI_GROUP, Link);
        if (g->Handle == Handle) {
            RemoveEntryList(&g->Link);
            Adapter->GroupTable.Count--;
            found = g;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->GroupTable.Lock, oldIrql);
    return found;
}

int
WinMaliGroupDestroy(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    PWINMALI_GROUP group = WinMaliGroupTakeByHandle_(Adapter, Handle);
    if (group == NULL) {
        return WMERR_ENOENT;
    }
    WINMALI_TRACE("GroupDestroy: handle=%u", group->Handle);
    WinMaliGroupFree_(group);
    return WMERR_OK;
}

int
WinMaliGroupGetState(_In_ PWINMALI_ADAPTER Adapter,
                     _In_ ULONG Handle,
                     _Out_ ULONG* OutStateFlags,
                     _Out_ ULONG* OutFatalQueueMask)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    int err = WMERR_ENOENT;

    *OutStateFlags = 0;
    *OutFatalQueueMask = 0;
    KeAcquireSpinLock(&Adapter->GroupTable.Lock, &oldIrql);
    for (entry = Adapter->GroupTable.Head.Flink;
         entry != &Adapter->GroupTable.Head;
         entry = entry->Flink) {
        PWINMALI_GROUP g = CONTAINING_RECORD(entry, WINMALI_GROUP, Link);
        if (g->Handle == Handle) {
            *OutStateFlags    = g->StateFlags;
            *OutFatalQueueMask = g->FatalQueueMask;
            err = WMERR_OK;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->GroupTable.Lock, oldIrql);
    return err;
}
