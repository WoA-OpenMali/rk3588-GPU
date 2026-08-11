/*
 * WinMaliSync.c - SyncObj implementation. KEVENT-backed.
 *
 * Binary mode:    KEVENT (NotificationEvent) + TimelineValue stays 0/1
 * Timeline mode:  KEVENT pulsed on each Signal + TimelineValue = current point
 *
 * Wait semantics:
 *   - Single binary handle: KeWaitForSingleObject on the KEVENT
 *   - Single timeline handle: re-check counter on each pulse
 *   - Multi-handle WaitAll/Any: KeWaitForMultipleObjects with KEVENT array
 *
 * Multi-handle TIMELINE waits aren't implemented yet (would need per-handle
 * re-evaluation loops). Single-handle timeline is enough for the basic
 * GroupSubmit fence model.
 */

#include "WinMaliKmd.h"
#include "WinMaliSync.h"

#define WMERR_OK          0
#define WMERR_EINVAL      -22
#define WMERR_ENOMEM      -12
#define WMERR_ENOENT      -2
#define WMERR_ETIMEDOUT   -110

VOID
WinMaliSyncObjTableInit(_Out_ PWINMALI_SYNCOBJ_TABLE Table)
{
    KeInitializeSpinLock(&Table->Lock);
    InitializeListHead(&Table->Head);
    Table->NextHandle = 1;
    Table->Count = 0;
}

static VOID
WinMaliSyncObjFree_(_Inout_ PWINMALI_SYNCOBJ So)
{
    if (So != NULL) {
        So->Magic = 0;
        ExFreePoolWithTag(So, WINMALI_POOL_TAG);
    }
}

VOID
WinMaliSyncObjTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                            _Inout_ PWINMALI_SYNCOBJ_TABLE Table)
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
        WinMaliSyncObjFree_(CONTAINING_RECORD(entry, WINMALI_SYNCOBJ, Link));
    }
}

int
WinMaliSyncObjCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                     _In_ ULONG Flags,
                     _In_ ULONG InitialState,
                     _In_opt_ PVOID OwnerDevice,
                     _Out_ ULONG* OutHandle)
{
    PWINMALI_SYNCOBJ so;
    KIRQL oldIrql;

    *OutHandle = 0;
    so = (PWINMALI_SYNCOBJ)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*so),
                                                 WINMALI_POOL_TAG);
    if (so == NULL) {
        return WMERR_ENOMEM;
    }
    RtlZeroMemory(so, sizeof(*so));
    so->Magic         = WINMALI_SYNCOBJ_MAGIC;
    so->IsTimeline    = (Flags & WINMALI_SYNC_OBJ_FLAG_TIMELINE) ? TRUE : FALSE;
    so->TimelineValue = (InitialState != 0) ? 1 : 0;
    so->RefCount      = 1;
    so->OwnerDevice   = OwnerDevice;
    KeInitializeSpinLock(&so->Lock);
    KeInitializeEvent(&so->Event, NotificationEvent,
                      (InitialState != 0) ? TRUE : FALSE);

    KeAcquireSpinLock(&Adapter->SyncObjTable.Lock, &oldIrql);
    so->Handle = (ULONG)(Adapter->SyncObjTable.NextHandle++);
    InsertTailList(&Adapter->SyncObjTable.Head, &so->Link);
    Adapter->SyncObjTable.Count++;
    KeReleaseSpinLock(&Adapter->SyncObjTable.Lock, oldIrql);

    *OutHandle = so->Handle;
    WINMALI_TRACE("SyncObjCreate: handle=%u timeline=%u initial=%u",
                  so->Handle, so->IsTimeline, InitialState);
    return WMERR_OK;
}

static PWINMALI_SYNCOBJ
WinMaliSyncObjTakeByHandle_(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_SYNCOBJ found = NULL;

    KeAcquireSpinLock(&Adapter->SyncObjTable.Lock, &oldIrql);
    for (entry = Adapter->SyncObjTable.Head.Flink;
         entry != &Adapter->SyncObjTable.Head;
         entry = entry->Flink) {
        PWINMALI_SYNCOBJ so = CONTAINING_RECORD(entry, WINMALI_SYNCOBJ, Link);
        if (so->Handle == Handle) {
            RemoveEntryList(&so->Link);
            Adapter->SyncObjTable.Count--;
            found = so;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->SyncObjTable.Lock, oldIrql);
    return found;
}

static PWINMALI_SYNCOBJ
WinMaliSyncObjGet_(_In_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PWINMALI_SYNCOBJ found = NULL;

    KeAcquireSpinLock(&Adapter->SyncObjTable.Lock, &oldIrql);
    for (entry = Adapter->SyncObjTable.Head.Flink;
         entry != &Adapter->SyncObjTable.Head;
         entry = entry->Flink) {
        PWINMALI_SYNCOBJ so = CONTAINING_RECORD(entry, WINMALI_SYNCOBJ, Link);
        if (so->Handle == Handle) {
            InterlockedIncrement(&so->RefCount);
            found = so;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->SyncObjTable.Lock, oldIrql);
    return found;
}

ULONG
WinMaliSyncObjRundownOwner(_Inout_ PWINMALI_ADAPTER Adapter,
                           _In_ PVOID OwnerDevice)
{
    KIRQL oldIrql;
    LIST_ENTRY freeHead;
    PLIST_ENTRY entry, next;
    ULONG freed = 0;

    if (OwnerDevice == NULL) {
        return 0;
    }
    InitializeListHead(&freeHead);

    KeAcquireSpinLock(&Adapter->SyncObjTable.Lock, &oldIrql);
    for (entry = Adapter->SyncObjTable.Head.Flink;
         entry != &Adapter->SyncObjTable.Head;
         entry = next) {
        PWINMALI_SYNCOBJ so = CONTAINING_RECORD(entry, WINMALI_SYNCOBJ, Link);
        next = entry->Flink;
        if (so->OwnerDevice == OwnerDevice) {
            RemoveEntryList(&so->Link);
            Adapter->SyncObjTable.Count--;
            InsertTailList(&freeHead, &so->Link);
        }
    }
    KeReleaseSpinLock(&Adapter->SyncObjTable.Lock, oldIrql);

    while (!IsListEmpty(&freeHead)) {
        PWINMALI_SYNCOBJ so = CONTAINING_RECORD(RemoveHeadList(&freeHead),
                                                WINMALI_SYNCOBJ, Link);
        WINMALI_TRACE("SyncObjRundown: handle=%u (owner device died without "
                      "SyncObjDestroy)", so->Handle);
        WinMaliSyncObjFree_(so);
        ++freed;
    }
    return freed;
}

int
WinMaliSyncObjDestroy(_Inout_ PWINMALI_ADAPTER Adapter, _In_ ULONG Handle)
{
    PWINMALI_SYNCOBJ so = WinMaliSyncObjTakeByHandle_(Adapter, Handle);
    if (so == NULL) {
        return WMERR_ENOENT;
    }
    WINMALI_TRACE("SyncObjDestroy: handle=%u", so->Handle);
    WinMaliSyncObjFree_(so);
    return WMERR_OK;
}

int
WinMaliSyncObjSignal(_Inout_ PWINMALI_ADAPTER Adapter,
                     _In_ ULONG Handle,
                     _In_ UINT64 Point)
{
    PWINMALI_SYNCOBJ so = WinMaliSyncObjGet_(Adapter, Handle);
    KIRQL oldIrql;
    if (so == NULL) {
        return WMERR_ENOENT;
    }
    KeAcquireSpinLock(&so->Lock, &oldIrql);
    if (so->IsTimeline) {
        if (Point > so->TimelineValue) {
            so->TimelineValue = Point;
        }
    } else {
        so->TimelineValue = 1;
    }
    KeReleaseSpinLock(&so->Lock, oldIrql);
    /* PulseEvent so any waiters re-check; for binary that's just "signal".
       NotificationEvent SetEvent semantics make it stay signalled until
       reset; we use SetEvent + the waiter re-checks on its own. */
    KeSetEvent(&so->Event, IO_NO_INCREMENT, FALSE);
    InterlockedDecrement(&so->RefCount);
    return WMERR_OK;
}

/* Translate panthor's "0 = poll, <0 = forever, >0 = ns" into a KeWait
   compatible LARGE_INTEGER. Returns NULL for "wait forever". */
static PLARGE_INTEGER
WinMaliSyncObjMakeTimeout_(_Inout_ LARGE_INTEGER* Storage,
                           _In_ INT64 TimeoutNs)
{
    if (TimeoutNs == 0) {
        Storage->QuadPart = 0;          /* poll */
        return Storage;
    }
    if (TimeoutNs < 0) {
        return NULL;                    /* infinite */
    }
    /* 100-ns units, relative (negative). */
    Storage->QuadPart = -(TimeoutNs / 100);
    if (Storage->QuadPart == 0) {
        Storage->QuadPart = -1;         /* at least 100ns */
    }
    return Storage;
}

int
WinMaliSyncObjWait(_Inout_ PWINMALI_ADAPTER Adapter,
                   _In_ ULONG Count,
                   _In_reads_(Count) const ULONG* Handles,
                   _In_reads_opt_(Count) const UINT64* Points,
                   _In_ BOOLEAN WaitAll,
                   _In_ INT64 TimeoutNs)
{
    PWINMALI_SYNCOBJ singleSo;
    LARGE_INTEGER timeout;
    PLARGE_INTEGER pTimeout;
    NTSTATUS st;
    int err = WMERR_OK;

    if (Count == 0 || Handles == NULL) {
        return WMERR_EINVAL;
    }
    pTimeout = WinMaliSyncObjMakeTimeout_(&timeout, TimeoutNs);

    if (Count == 1) {
        singleSo = WinMaliSyncObjGet_(Adapter, Handles[0]);
        if (singleSo == NULL) {
            return WMERR_ENOENT;
        }

        if (singleSo->IsTimeline) {
            UINT64 target = (Points != NULL) ? Points[0] : 0;
            /* Re-check loop: each pulse may not bring us to the target. */
            for (;;) {
                KIRQL oldIrql;
                BOOLEAN reached;
                KeAcquireSpinLock(&singleSo->Lock, &oldIrql);
                reached = (singleSo->TimelineValue >= target);
                KeReleaseSpinLock(&singleSo->Lock, oldIrql);
                if (reached) {
                    err = WMERR_OK;
                    break;
                }
                /* NotificationEvent stays signalled - reset before wait so
                   we only wake on a fresh signal. */
                KeClearEvent(&singleSo->Event);
                /* Re-check after clear to avoid lost-wakeup. */
                KeAcquireSpinLock(&singleSo->Lock, &oldIrql);
                reached = (singleSo->TimelineValue >= target);
                KeReleaseSpinLock(&singleSo->Lock, oldIrql);
                if (reached) {
                    err = WMERR_OK;
                    break;
                }
                st = KeWaitForSingleObject(&singleSo->Event, Executive,
                                           KernelMode, FALSE, pTimeout);
                if (st == STATUS_TIMEOUT) {
                    err = WMERR_ETIMEDOUT;
                    break;
                }
                if (!NT_SUCCESS(st)) {
                    err = WMERR_EINVAL;
                    break;
                }
                /* Loop and re-check counter. */
            }
        } else {
            st = KeWaitForSingleObject(&singleSo->Event, Executive,
                                       KernelMode, FALSE, pTimeout);
            err = (st == STATUS_TIMEOUT) ? WMERR_ETIMEDOUT
                : (NT_SUCCESS(st)        ? WMERR_OK : WMERR_EINVAL);
        }
        InterlockedDecrement(&singleSo->RefCount);
        return err;
    }

    /* Multi-handle path: binary-only for now. Each handle's KEVENT joins
       the wait array. */
    {
        PVOID  objects[MAXIMUM_WAIT_OBJECTS];
        PWINMALI_SYNCOBJ refs[MAXIMUM_WAIT_OBJECTS];
        ULONG i;
        if (Count > MAXIMUM_WAIT_OBJECTS) {
            return WMERR_EINVAL;
        }
        for (i = 0; i < Count; ++i) {
            refs[i] = WinMaliSyncObjGet_(Adapter, Handles[i]);
            if (refs[i] == NULL) {
                while (i > 0) {
                    InterlockedDecrement(&refs[--i]->RefCount);
                }
                return WMERR_ENOENT;
            }
            if (refs[i]->IsTimeline) {
                /* Multi-handle timeline wait isn't implemented. */
                while (i + 1 > 0) {
                    InterlockedDecrement(&refs[i--]->RefCount);
                    if (i == (ULONG)-1) {
                        break;
                    }
                }
                return WMERR_EINVAL;
            }
            objects[i] = &refs[i]->Event;
        }
        st = KeWaitForMultipleObjects(Count, objects,
                                      WaitAll ? WaitAll : WaitAny,
                                      Executive, KernelMode, FALSE,
                                      pTimeout, NULL);
        for (i = 0; i < Count; ++i) {
            InterlockedDecrement(&refs[i]->RefCount);
        }
        if (st == STATUS_TIMEOUT) {
            return WMERR_ETIMEDOUT;
        }
        if (!NT_SUCCESS(st)) {
            return WMERR_EINVAL;
        }
        return WMERR_OK;
    }
}
