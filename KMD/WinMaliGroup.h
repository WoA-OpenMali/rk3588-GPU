/*
 * WinMaliGroup.h - CSF queue group handle table.
 *
 * A Group is a CSF queue group: an associated set of CS (command stream)
 * queues that share scheduling parameters and a VM. Mirrors panthor's
 * drm_panthor_group_create. Exposed to the UMD via WinMaliEscape ops 7..10.
 *
 * GroupCreate/Destroy/GetState track the group handle + queue parameters.
 * GroupSubmit is fully wired (WinMaliEscapeGroupSubmit_): it pulls the
 * WINMALI_QUEUE_SUBMIT entries, rebinds the CSG to the group's VM AS, and
 * CALLs each queue's CS stream GPU VA via WinMaliCsfSubmitGroupStream. VmBind
 * installs the LPAE PTEs into the VM's AS that those GPU VAs resolve against.
 */

#pragma once

#include <ntddk.h>
#include "..\Shared\WinMaliEscape.h"

struct _WINMALI_ADAPTER;
typedef struct _WINMALI_ADAPTER WINMALI_ADAPTER, *PWINMALI_ADAPTER;

#define WINMALI_GROUP_MAGIC     'rGmW'  /* 'WmGr' little-endian */
#define WINMALI_GROUP_MAX_QUEUES 16u

typedef struct _WINMALI_GROUP {
    LIST_ENTRY              Link;
    ULONG                   Magic;
    ULONG                   Handle;
    ULONG                   VmId;
    UINT8                   Priority;
    UINT8                   MaxComputeCores;
    UINT8                   MaxFragmentCores;
    UINT8                   MaxTilerCores;
    UINT64                  ComputeCoreMask;
    UINT64                  FragmentCoreMask;
    UINT64                  TilerCoreMask;
    ULONG                   QueuesCount;
    WINMALI_QUEUE_CREATE    Queues[WINMALI_GROUP_MAX_QUEUES];
    /* Updated by GroupGetState / future fault paths. */
    ULONG                   StateFlags;     /* WINMALI_GROUP_STATE_* */
    ULONG                   FatalQueueMask;
    LONG                    RefCount;
    /* dxgk device (WINMALI_KMD_DEVICE*) whose escape created this group;
       freed by DestroyDevice rundown if the UMD never destroys it. */
    PVOID                   OwnerDevice;
} WINMALI_GROUP, *PWINMALI_GROUP;

typedef struct _WINMALI_GROUP_TABLE {
    KSPIN_LOCK              Lock;
    LIST_ENTRY              Head;
    LONG                    NextHandle;
    LONG                    Count;
} WINMALI_GROUP_TABLE, *PWINMALI_GROUP_TABLE;

VOID WinMaliGroupTableInit(_Out_ PWINMALI_GROUP_TABLE Table);
VOID WinMaliGroupTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                               _Inout_ PWINMALI_GROUP_TABLE Table);

/* Create a group. Returns 0/-errno. *OutHandle is the new handle. */
int WinMaliGroupCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                       _In_ const WINMALI_GROUP_CREATE* Args,
                       _In_reads_(QueueCount) const WINMALI_QUEUE_CREATE* Queues,
                       _In_ ULONG QueueCount,
                       _In_opt_ PVOID OwnerDevice,
                       _Out_ ULONG* OutHandle);

int WinMaliGroupDestroy(_Inout_ PWINMALI_ADAPTER Adapter,
                        _In_ ULONG Handle);

/* Destroy every group owned by OwnerDevice (device rundown). Returns the
   number destroyed. */
ULONG WinMaliGroupRundownOwner(_Inout_ PWINMALI_ADAPTER Adapter,
                               _In_ PVOID OwnerDevice);

int WinMaliGroupGetState(_In_ PWINMALI_ADAPTER Adapter,
                         _In_ ULONG Handle,
                         _Out_ ULONG* OutStateFlags,
                         _Out_ ULONG* OutFatalQueueMask);

int WinMaliGroupMarkFatalQueue(_In_ PWINMALI_ADAPTER Adapter,
                               _In_ ULONG Handle,
                               _In_ ULONG QueueIndex);

/* Return the VM id a group is bound to (for resolving the CSG's target AS
   on submit). Returns 0/-errno. */
int WinMaliGroupGetVmId(_In_ PWINMALI_ADAPTER Adapter,
                        _In_ ULONG Handle,
                        _Out_ ULONG* OutVmId);
