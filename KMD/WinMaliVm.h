/*
 * WinMaliVm.h - VM (GPU virtual address space) handle table.
 *
 * A VM is a Mali AS slot + its root page table. Exposed to the UMD via
 * WinMaliEscape ops 1..4. Currently this layer is metadata-only:
 *
 *   VmCreate       allocates a VM id and tracks UserVaRange / state
 *   VmDestroy      releases the id
 *   VmGetState     returns Usable / Unusable
 *   VmBind         NOT YET WIRED - needs WinMaliMmu.c to accept a per-VM
 *                  AS slot parameter (currently MapGpuRange hardcodes AS1)
 *
 * Once VmBind is wired, this struct grows AsSlot + RootPtMdl + RootPtPa
 * fields. For now the UMD can allocate / track VMs but GPU access to BO
 * pages still goes through dxgk's SetRootPageTable path.
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
                    _Out_ ULONG* OutId,
                    _Out_ UINT64* OutGrantedRange);

int WinMaliVmDestroy(_Inout_ PWINMALI_ADAPTER Adapter,
                     _In_ ULONG Id);

int WinMaliVmGetState(_In_ PWINMALI_ADAPTER Adapter,
                      _In_ ULONG Id,
                      _Out_ WINMALI_VM_RUN_STATE* OutState);

/* Look up a VM by id and return it with refcount incremented. NULL if not
   found. Use WinMaliVmPut to release. */
PWINMALI_VM WinMaliVmGet(_In_ PWINMALI_ADAPTER Adapter, _In_ ULONG Id);

VOID WinMaliVmPut(_In_ PWINMALI_VM Vm);
