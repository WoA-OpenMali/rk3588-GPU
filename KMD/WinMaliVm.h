/*
 * WinMaliVm.h - VM (GPU virtual address space) handle table.
 *
 * A VM is a Mali AS slot + its root page table. Exposed to the UMD via
 * WinMaliEscape ops 1..4. This layer is fully wired (the header once said
 * "metadata-only / VmBind NOT YET WIRED" - that is stale):
 *
 *   VmCreate       allocates a VM id, picks/binds a Mali AS slot, builds the
 *                  L0 root page table (WinMaliMmuVmInit) and cross-maps the CSF
 *                  ring/sync/shader pages into the new AS
 *   VmDestroy      tears down the AS slot + page tables, releases the id
 *   VmGetState     returns Usable / Unusable
 *   VmBind         maps/unmaps a BO's pages into the VM's per-VM LPAE page
 *                  table at the UMD-chosen VA (WinMaliMmuVmMap/VmUnmap)
 *
 * The CSG executes against these per-VM AS slots. (dxgk's SetRootPageTable
 * path binds a separate AS keyed on hContext and is largely unexercised.)
 */

#pragma once

#include <ntddk.h>

struct _WINMALI_ADAPTER;
typedef struct _WINMALI_ADAPTER WINMALI_ADAPTER, *PWINMALI_ADAPTER;

#define WINMALI_VM_MAGIC    'mVmW'  /* 'WmVm' little-endian */

typedef enum _WINMALI_VM_RUN_STATE {
    WinMaliVmRun_Usable     = 0,
    WinMaliVmRun_Unusable   = 1,
} WINMALI_VM_RUN_STATE;

typedef struct _WINMALI_VM {
    LIST_ENTRY              Link;
    ULONG                   Magic;
    ULONG                   Id;
    ULONG                   Flags;
    WINMALI_VM_RUN_STATE    State;
    UINT64                  UserVaBase;     /* GPU VA base for user mappings */
    UINT64                  UserVaSize;     /* in bytes */
    LONG                    RefCount;
    /* Real per-VM PT - allocated by WinMaliMmuVmInit on VmCreate. */
    WINMALI_VM_PT           Pt;
    BOOLEAN                 PtInitialized;
    /* Bump allocator for KMD-owned GPU VAs inside this VM (tiler heap
       context + chunks). Starts just past the UMD user VA window
       (base+range) and grows up; the UMD maps its own BOs at the very top
       of the 48-bit space, so this mid-range window stays clear. */
    UINT64                  KmdVaNext;
    /* dxgk device (WINMALI_KMD_DEVICE*) whose escape created this VM.
       DestroyDevice runs down everything it owns - a UMD process that
       dies without VmDestroy must not leak the AS slot (there are only
       8; leaking 6 bricks VmCreate adapter-wide with -ENOMEM). */
    PVOID                   OwnerDevice;
} WINMALI_VM, *PWINMALI_VM;

typedef struct _WINMALI_VM_TABLE {
    KSPIN_LOCK              Lock;
    LIST_ENTRY              Head;
    LONG                    NextId;
    LONG                    Count;
} WINMALI_VM_TABLE, *PWINMALI_VM_TABLE;

VOID WinMaliVmTableInit(_Out_ PWINMALI_VM_TABLE Table);
VOID WinMaliVmTableTeardown(_Inout_ PWINMALI_ADAPTER Adapter,
                            _Inout_ PWINMALI_VM_TABLE Table);

/* Create a VM, returning the new id. UserVaRange == 0 means "KMD picks";
   we default to the same span the adapter publishes in DRIVERCAPS
   (InternalGpuVirtualAddressRange*). */
int WinMaliVmCreate(_Inout_ PWINMALI_ADAPTER Adapter,
                    _In_ ULONG Flags,
                    _In_ UINT64 UserVaRange,
                    _In_opt_ PVOID OwnerDevice,
                    _Out_ ULONG* OutId,
                    _Out_ UINT64* OutGrantedRange);

int WinMaliVmDestroy(_Inout_ PWINMALI_ADAPTER Adapter,
                     _In_ ULONG Id);

/* Destroy every VM owned by OwnerDevice (device rundown). Returns the
   number destroyed. */
ULONG WinMaliVmRundownOwner(_Inout_ PWINMALI_ADAPTER Adapter,
                            _In_ PVOID OwnerDevice);

int WinMaliVmGetState(_In_ PWINMALI_ADAPTER Adapter,
                      _In_ ULONG Id,
                      _Out_ WINMALI_VM_RUN_STATE* OutState);

/* Look up a VM by id and return it with refcount incremented. NULL if not
   found. Use WinMaliVmPut to release. */
PWINMALI_VM WinMaliVmGet(_In_ PWINMALI_ADAPTER Adapter, _In_ ULONG Id);

VOID WinMaliVmPut(_In_ PWINMALI_VM Vm);
