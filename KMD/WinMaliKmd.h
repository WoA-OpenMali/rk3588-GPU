
#pragma once

#include <ntddk.h>
#include <dispmprt.h>
#include <ntstrsafe.h>

#include "..\Shared\WinMaliEscape.h"   /* defines WINMALI_POOL_TAG */
#include "hw\WinMaliHw.h"
#include "WinMaliMmu.h"
#include "WinMaliAlloc.h"
#include "WinMaliBo.h"
#include "WinMaliSync.h"
#include "WinMaliVm.h"
#include "WinMaliGroup.h"

/* Driver-wide trace shim. WPP isn't wired yet; route everything through
   DbgPrint so the kdnet/COM trace shows what's happening. */
#define WINMALI_TRACE(fmt, ...) DbgPrint("[WinMali] " fmt "\n", ##__VA_ARGS__)
#define WINMALI_WARN(fmt, ...)  DbgPrint("[WinMali] WARN " fmt "\n", ##__VA_ARGS__)
#define WINMALI_ERROR(fmt, ...) DbgPrint("[WinMali] ERR  " fmt "\n", ##__VA_ARGS__)

/*
 * Per-adapter context. dxgk hands MiniportDeviceContext back to us on
 * every DDI entry; the magic guards against the "callback fired before
 * AddDevice / after RemoveDevice" race we want to crash loudly on
 * instead of dereferencing uninitialised memory.
 */
typedef struct _WINMALI_ADAPTER {
    ULONG               Magic;
#define WINMALI_ADAPTER_MAGIC   'MniW'   /* 'WinM' little-endian */

    PDEVICE_OBJECT      PhysicalDeviceObject;

    /* Captured in StartDevice; cleared in StopDevice. The DxgkHandle is
       the back-reference dxgkrnl wants on every DxgkCb*. */
    HANDLE              DxgkHandle;
    DXGKRNL_INTERFACE   DxgkInterface;
    DXGK_START_INFO     DxgkStartInfo;
    DXGK_DEVICE_INFO    DeviceInfo;

    BOOLEAN             Started;

    /* Resources parsed from ACPI (DXGK device-info). */
    PHYSICAL_ADDRESS    GpuRegsPhys;
    ULONG               GpuRegsSize;
    PVOID               GpuRegsVa;
    BOOLEAN             GpuRegsMapped;

    ULONG               GpuIrqVector;
    KIRQL               GpuIrqLevel;
    KAFFINITY           GpuIrqAffinity;
    KINTERRUPT_MODE     GpuIrqMode;
    BOOLEAN             GpuIrqShareable;
    BOOLEAN             InterruptConnected;

    /* Embedded HW context. RegsVa is NULL until BringupHardware succeeds;
       all accessors in WinMaliHw.c guard against it. */
    WINMALI_HW          Hw;

    /* Bring-up state. Bits in WINMALI_ADAPTER_FLAG_* set as each stage
       completes; the diag-passed bit is for the future escape channel. */
    ULONG               AdapterFlags;

    /* Counters maintained by the ISR. */
    LONG64              InterruptsTotal;
    LONG64              InterruptsHandled;
    LONG64              InterruptsSpurious;

    /* Contiguous non-cached heap: page tables, FW page tables, MMU scratch. */
    PVOID               MmuScratchHeapVa;
    PHYSICAL_ADDRESS    MmuScratchHeapPhys;
    SIZE_T              MmuScratchHeapBytes;
    BOOLEAN             GpuMmuAsBound;
    ULONG               GpuMmuBringupAs;

    /* AS-slot allocator. AS0 is the CSF MCU; AS1 is the kernel bring-up
       address space. AS2..AS(N-1) are leased per dxgk context. */
    KSPIN_LOCK          AsSlotLock;
    ULONG               AsSlotInUseMask;     /* bit i set => AS i programmed */
    ULONG               AsSlotPresentMask;   /* mirror of GPU_AS_PRESENT */
#define WINMALI_AS_SLOT_MAX  16u
    struct {
        HANDLE  hContext;        /* dxgk-owned, opaque to us */
        UINT64  RootPtPa;        /* last programmed root-PT phys addr */
        BOOLEAN Bound;
    } AsBindings[WINMALI_AS_SLOT_MAX];

    /* CSF firmware image + MCU AS0 binding. */
    struct _WINMALI_FWCTX* FwCtx;
    BOOLEAN             FwMcuAsBound;
    BOOLEAN             GpuFwParkedForD3;

    /* Dedicated PopulatedFromSystemMemory segment for dxgk DMA buffers.
       Independent of MmuScratchHeap. Populated when QUERYSEGMENT3 starts
       publishing real segments; until then, fields are zero. */
    PVOID               DmaSegmentVa;
    PHYSICAL_ADDRESS    DmaSegmentPhys;
    SIZE_T              DmaSegmentBytes;

    /* Monotonic fences for submit / completion. Reset on TDR. */
    LONG64              GpuSubmittedFence;
    LONG64              GpuCompletedFence;
    LONG                NotifyDpcPending;

    /* Pending fence-completion queue: produced by WinMaliKmdSubmitCommand,
       drained by WinMaliKmdDpcRoutine which calls DxgkCbNotifyInterrupt.
       Synchronous "GPU work done immediately" path for paging submits we
       don't actually run on the GPU yet. */
#define WINMALI_FENCE_QUEUE_DEPTH  64u
    KSPIN_LOCK          FenceQueueLock;
    ULONG               FenceQueueHead;
    ULONG               FenceQueueTail;
    struct {
        UINT    SubmissionFenceId;
        UINT    NodeOrdinal;
        UINT    EngineOrdinal;
    } FenceQueue[WINMALI_FENCE_QUEUE_DEPTH];

    /* Set after a successful bring-up so RemoveDevice can tell a clean
       stop/remove cycle from a never-started one. */
    BOOLEAN             StartDeviceEverSucceeded;

    /* UMD escape ABI: BO handle table. Initialized in AddDevice, drained
       in RemoveDevice. See WinMaliBo.c. */
    WINMALI_BO_TABLE        BoTable;

    /* UMD escape ABI: SyncObj handle table. See WinMaliSync.c. */
    WINMALI_SYNCOBJ_TABLE   SyncObjTable;

    /* UMD escape ABI: VM handle table (metadata-only until VmBind wired). */
    WINMALI_VM_TABLE        VmTable;

    /* UMD escape ABI: CSF queue Group handle table (metadata-only;
       GroupSubmit blocked on VmBind). */
    WINMALI_GROUP_TABLE     GroupTable;
} WINMALI_ADAPTER, *PWINMALI_ADAPTER;

/* Bring-up state bits, used by the escape diagnostics channel + by
   bring-up code to gate features. */
#define WINMALI_ADAPTER_FLAG_MCU_ALIVE     0x00000001UL
#define WINMALI_ADAPTER_FLAG_MMU_READY     0x00000002UL
#define WINMALI_ADAPTER_FLAG_DIAG_PASSED   0x00000004UL
#define WINMALI_ADAPTER_FLAG_CSF_JOBS      0x00000008UL

/* Map a dxgk-supplied context (or hAdapter handle) back to our struct.
   Returns NULL on a bad-magic / NULL / mismatched-handle context so
   callers can fail with STATUS_INVALID_PARAMETER. */
PWINMALI_ADAPTER WinMaliAdapterFromContext(_In_opt_ const VOID* Context);
PWINMALI_ADAPTER WinMaliAdapterFromDxgkHandle(_In_opt_ const VOID* hAdapter);

/* ---------- DDI prototypes wired into DRIVER_INITIALIZATION_DATA ------- */

DRIVER_INITIALIZE                       DriverEntry;
DRIVER_UNLOAD                           WinMaliKmdUnload;
VOID APIENTRY                           WinMaliKmdDxgkUnload(VOID);

/* Kill-switch (early in DriverEntry) - returns Disabled=FALSE on any error
   so a registry/I/O glitch never bricks the driver. */
NTSTATUS WinMaliReadKillSwitch(_In_ PUNICODE_STRING RegistryPath,
                               _Out_ BOOLEAN* Disabled);

/* Auto-arm the kill-switch from inside DriverEntry. Used to prevent the
   second DriverEntry-after-teardown crash we hit on Win11 26100 dxgk - if
   the driver has been through one full bring-up + teardown cycle, dxgk's
   tracking lists are in an inconsistent state on retry. Arming the
   kill-switch makes the retry DriverEntry early-exit before touching
   anything. Manual recovery: unkill.ps1 -Reboot. */
NTSTATUS WinMaliArmKillSwitch(_In_ PUNICODE_STRING RegistryPath);

/* Resource parser + hardware bring-up. Defined in WinMaliBringup.c. */
NTSTATUS WinMaliParseResources(_Inout_ PWINMALI_ADAPTER Adapter);
NTSTATUS WinMaliBringupHardware(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliTeardownHardware(_Inout_ PWINMALI_ADAPTER Adapter);
NTSTATUS WinMaliConnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliDisconnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter);

/* MMU + firmware bring-up. Defined in WinMaliMmu.c / WinMaliFw.c. */
NTSTATUS WinMaliFwInit(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliFwTeardown(_Inout_ PWINMALI_ADAPTER Adapter);

/* Submit a single no-op CSF job to the kernel queue. Used as a self-test
   after FwInit succeeds - if this returns STATUS_SUCCESS we know the MCU
   is alive, the CS iface is programmed, and the queue ring works end-to-end. */
NTSTATUS WinMaliCsfSubmitNopJob(_Inout_ PWINMALI_ADAPTER Adapter);

/* 256 MiB sysmem-backed segment that VIDMM publishes as "segment 1". */
NTSTATUS WinMaliVidmmAllocateSegment(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliVidmmFreeSegment(_Inout_ PWINMALI_ADAPTER Adapter);
NTSTATUS APIENTRY WinMaliKmdGetNodeMetadata(IN_CONST_HANDLE hAdapter, UINT NodeOrdinalAndAdapterIndex, OUT_PDXGKARG_GETNODEMETADATA pGetNodeMetadata);
NTSTATUS APIENTRY WinMaliKmdCreateProcess(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_CREATEPROCESS pCreateProcess);
NTSTATUS APIENTRY WinMaliKmdDestroyProcess(IN_CONST_HANDLE hAdapter, IN_CONST_HANDLE hKmdProcess);
NTSTATUS APIENTRY WinMaliKmdCreateDevice(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_CREATEDEVICE pCreateDevice);
NTSTATUS APIENTRY WinMaliKmdDestroyDevice(IN_CONST_HANDLE hDevice);
NTSTATUS APIENTRY WinMaliKmdCreateContext(IN_CONST_HANDLE hDevice, INOUT_PDXGKARG_CREATECONTEXT pCreateContext);
NTSTATUS APIENTRY WinMaliKmdDestroyContext(IN_CONST_HANDLE hContext);
VOID     APIENTRY WinMaliKmdSetRootPageTable(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_SETROOTPAGETABLE pArgs);
SIZE_T   APIENTRY WinMaliKmdGetRootPageTableSize(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_GETROOTPAGETABLESIZE pArgs);
NTSTATUS APIENTRY WinMaliKmdQueryCurrentFence(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_QUERYCURRENTFENCE pArgs);
NTSTATUS APIENTRY WinMaliKmdControlInterrupt(IN_CONST_HANDLE hAdapter, IN_CONST_DXGK_INTERRUPT_TYPE InterruptType, IN_BOOLEAN EnableInterrupt);
NTSTATUS APIENTRY WinMaliKmdResetFromTimeout(IN_CONST_HANDLE hAdapter);
NTSTATUS APIENTRY WinMaliKmdRestartFromTimeout(IN_CONST_HANDLE hAdapter);
NTSTATUS APIENTRY WinMaliKmdCollectDbgInfo(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_COLLECTDBGINFO pArgs);
NTSTATUS APIENTRY WinMaliKmdBuildPagingBuffer(IN_CONST_HANDLE hAdapter, IN_PDXGKARG_BUILDPAGINGBUFFER pBpb);
NTSTATUS APIENTRY WinMaliKmdSubmitCommand(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmit);
NTSTATUS APIENTRY WinMaliKmdEscape(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_ESCAPE pEscape);

/* Segment id constants. dxgk segment ids are 1-based; id 1 is the
   sysmem-backed segment we allocate at StartDevice. */
#define WINMALI_SEGMENT_ID_SYSMEM   1u
#define WINMALI_SEGMENT_COUNT       1u

/* GPU-side base addresses we report in DXGK_SEGMENTDESCRIPTOR*. dxgk
   uses this as a hint for where this segment occupies the GPU VA
   space. Must be above PHYSICALADAPTERCAPS.MinimumAddress (= 0x10000)
   and avoid the firmware reservations < 0x04040000. */
#define WINMALI_SYSMEM_GPU_BASE     0x0000000010000000ULL  /* 256 MiB */

NTSTATUS APIENTRY WinMaliKmdAddDevice(
    _In_     CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Outptr_ PVOID*               MiniportDeviceContext);

NTSTATUS APIENTRY WinMaliKmdStartDevice(
    _In_  CONST PVOID            MiniportDeviceContext,
    _In_  PDXGK_START_INFO       DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE     DxgkInterface,
    _Out_ PULONG                 NumberOfVideoPresentSources,
    _Out_ PULONG                 NumberOfChildren);

NTSTATUS APIENTRY WinMaliKmdStopDevice(
    _In_ CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY WinMaliKmdRemoveDevice(
    _In_ CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY WinMaliKmdDispatchIoRequest(
    _In_ CONST PVOID                MiniportDeviceContext,
    _In_ ULONG                      VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET      VideoRequestPacket);

BOOLEAN APIENTRY WinMaliKmdInterruptRoutine(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG       MessageNumber);

VOID APIENTRY WinMaliKmdDpcRoutine(
    _In_ CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY WinMaliKmdExchangePreStartInfo(
    _In_    CONST PVOID            MiniportDeviceContext,
    _Inout_ PDXGK_PRE_START_INFO   PreStartInfo);

NTSTATUS APIENTRY WinMaliKmdQueryChildRelations(
    _In_                             CONST PVOID            MiniportDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_                             ULONG                  ChildRelationsSize);

NTSTATUS APIENTRY WinMaliKmdQueryChildStatus(
    _In_    CONST PVOID         MiniportDeviceContext,
    _In_    PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly);

NTSTATUS APIENTRY WinMaliKmdQueryDeviceDescriptor(
    _In_  CONST PVOID                  MiniportDeviceContext,
    _In_  ULONG                        ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR    DeviceDescriptor);

NTSTATUS APIENTRY WinMaliKmdSetPowerState(
    _In_ CONST PVOID         MiniportDeviceContext,
    _In_ ULONG               DeviceUid,
    _In_ DEVICE_POWER_STATE  DevicePowerState,
    _In_ POWER_ACTION        ActionType);

NTSTATUS APIENTRY WinMaliKmdNotifyAcpiEvent(
    _In_  CONST PVOID         MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE     EventType,
    _In_  ULONG               Event,
    _In_  PVOID               Argument,
    _Out_ PULONG              AcpiFlags);

VOID APIENTRY WinMaliKmdResetDevice(
    _In_ CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY WinMaliKmdQueryInterface(
    _In_ CONST PVOID    MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface);

VOID APIENTRY WinMaliKmdControlEtwLogging(
    _In_ BOOLEAN Enable,
    _In_ ULONG   Flags,
    _In_ UCHAR   Level);

NTSTATUS APIENTRY WinMaliKmdQueryAdapterInfo(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*    pQueryAdapterInfo);
