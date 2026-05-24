/*
 * WinMaliSync.h - SyncObj handle table + dispatch surface.
 *
 * SyncObjs are u32 handles exposed via WinMaliEscape ops 0x82..0x84. Two
 * flavours mirror drmSyncobj:
 *
 *   Binary (default): one signalled/unsignalled bit, backed by KEVENT.
 *                     Wait succeeds when signalled; signal sets the bit.
 *
 *   Timeline (FLAG_TIMELINE): a u64 counter that monotonically increases.
 *                     Wait succeeds when counter >= requested point.
 *                     Backed by KEVENT + counter under a spinlock; signaler
 *                     advances the counter and pulses the event so waiters
 *                     re-check.
 *
 * Handle table is per-adapter, mirroring WinMaliBo.c (LIST_ENTRY + KSPIN_LOCK
 * + monotonic counter). Refcount lets future code (queue submit) hold a
 * reference while a wait/signal is outstanding.
 */

#pragma once

#include <ntddk.h>

struct _WINMALI_ADAPTER;
typedef struct _WINMALI_ADAPTER WINMALI_ADAPTER, *PWINMALI_ADAPTER;

#define WINMALI_SYNCOBJ_MAGIC   'oSmW'  /* 'WmSo' little-endian */

typedef struct _WINMALI_SYNCOBJ {
    LIST_ENTRY          Link;
    ULONG               Magic;
    ULONG               Handle;
    BOOLEAN             IsTimeline;     /* TRUE for timeline-mode handles */
    KEVENT              Event;          /* NotificationEvent so all waiters wake */
    KSPIN_LOCK          Lock;           /* protects TimelineValue */
    UINT64              TimelineValue;  /* monotonic; binary uses 0/1 */
    LONG                RefCount;
} WINMALI_SYNCOBJ, *PWINMALI_SYNCOBJ;

typedef struct _WINMALI_SYNCOBJ_TABLE {
    KSPIN_LOCK          Lock;
    LIST_ENTRY          Head;
    LONG                NextHandle;
    LONG                Count;
} WINMALI_SYNCOBJ_TABLE, *PWINMALI_SYNCOBJ_TABLE;

VOID WinMaliSyncObjTableInit(_Out_ PWINMALI_SYNCOBJ_TABLE Table);
VOID WinMaliSyncObjTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                                 _Inout_ PWINMALI_SYNCOBJ_TABLE Table);

/* Returns 0/-errno. *OutHandle is set on success. */
int WinMaliSyncObjCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                         _In_ ULONG Flags,
                         _In_ ULONG InitialState,
                         _Out_ ULONG* OutHandle);

int WinMaliSyncObjDestroy(_Inout_ PWINMALI_ADAPTER Adapter,
                          _In_ ULONG Handle);

/* Signal one handle to the given timeline point. For binary handles,
   any non-zero Point is treated as "signal". Returns 0/-errno. */
int WinMaliSyncObjSignal(_Inout_ PWINMALI_ADAPTER Adapter,
                         _In_ ULONG Handle,
                         _In_ UINT64 Point);

/* Wait on an array of handles with optional timeline points.
   Handles[i] is the handle, Points[i] is the timeline value to wait for
   (ignored for binary; 0 on binary means "any signal").
   WaitAll => all must reach; otherwise any one suffices.
   TimeoutNs == 0 polls, < 0 waits forever, > 0 waits up to TimeoutNs.
   Returns 0/-errno. */
int WinMaliSyncObjWait(_Inout_ PWINMALI_ADAPTER Adapter,
                       _In_ ULONG Count,
                       _In_reads_(Count) const ULONG* Handles,
                       _In_reads_opt_(Count) const UINT64* Points,
                       _In_ BOOLEAN WaitAll,
                       _In_ INT64 TimeoutNs);
