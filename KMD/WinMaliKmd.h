/*++

Module Name:

    WinMaliKmd.h

Abstract:

    Internal header for the WinMali kernel-mode WDDM miniport driver.
    All public symbols in this driver are prefixed with WinMaliKmd or
    WinMali (for hw/diag helpers).

--*/

#pragma once

#include <ntddk.h>

// dispmprt.h is C-only; it's already in an extern "C" in the WDK for C++,
// but we include it defensively.
#ifdef __cplusplus
extern "C" {
#endif
#include <dispmprt.h>
#ifdef __cplusplus
}
#endif

//
// DRIVER_INITIALIZATION_DATA.Version is pinned in WinMaliDxgkInitFill.c to
// DXGKDDI_INTERFACE_VERSION_WDDM2_4 so it matches DRIVERCAPS /
// WDDMDEVICECAPS / INF WDDMVersion. The WDK default DXGKDDI_INTERFACE_VERSION
// is WDDM 3.2; using that while reporting WDDM 2.4 caps made dxgk abort
// adapter init after GetNodeMetadata.
//
#include <ntstrsafe.h>

#include "..\Shared\WinMaliCommon.h"
#include "WinMaliTrace.h"       // WINMALI_TRACE / _WARN / _ERROR / _ENTER
#include "hw\WinMaliHw.h"       // WINMALI_HW, register offsets
#include "vop2\WinMaliVop2.h"   // WINMALI_VOP2 - VOP2 MMIO + state snapshot

struct _WINMALI_FWCTX;

// ---------------------------------------------------------------------------
// Per-adapter context
// ---------------------------------------------------------------------------

typedef struct _RK3588_DISP_GOP_FB {
    PHYSICAL_ADDRESS   PhysBase;
    ULONG              Width;
    ULONG              Height;
    ULONG              Pitch;             // bytes per scanline
    ULONG              Bpp;               // 32 on every known RK3588 UEFI
    D3DDDIFORMAT       ColorFormat;       // usually D3DDDIFMT_X8R8G8B8
    ULONG              RefreshHz;         // our best guess, usually 60
    BOOLEAN            Valid;

    // Lazy MmMapIoSpaceEx of PhysBase, populated by SystemDisplayEnable.
    // SystemDisplayWrite memcpys into this VA from the dxgkrnl-supplied
    // source buffer (BSOD path / boot UI handoff). NonCached so it's safe
    // from a post-bugcheck context where the cache hierarchy is suspect.
    PVOID              SystemDisplayVa;
    SIZE_T             SystemDisplayBytes;
} RK3588_DISP_GOP_FB, *PRK3588_DISP_GOP_FB;


typedef struct _WINMALI_ADAPTER {
    ULONG Magic;                         // = 'MniW' to catch bad casts
#define WINMALI_ADAPTER_CONTEXT_MAGIC    'MniW'

    // Back-reference handed to us by DXGK in StartDevice. We need it to
    // call Dxgk callbacks (Dxgkrnl routes everything through this handle).
    HANDLE            DxgkHandle;

    // Saved DXGK interfaces captured in DxgkDdiStartDevice.
    DXGKRNL_INTERFACE DxgkInterface;
    DXGK_START_INFO   DxgkStartInfo;
    DXGK_DEVICE_INFO  DeviceInfo;

    // PDO, captured in AddDevice. Lets us call IoGetDeviceProperty etc.
    PDEVICE_OBJECT    PhysicalDeviceObject;

    // Translated MMIO descriptor (CmResourceTypeMemory) from DXGK. We
    // keep both the physical base/size and the mapped VA so hw/ can use
    // either. GpuRegsVa is NULL until MmMapIoSpaceEx succeeds.
    PHYSICAL_ADDRESS  GpuRegsPhys;
    ULONG             GpuRegsSize;
    PVOID             GpuRegsVa;
    BOOLEAN           GpuRegsMapped;

    // First CmResourceTypeInterrupt partial from DXGK (for logging / diag).
    // ACPI may expose one combined Interrupt() or several partials (job /
    // mmu / gpu). WDDM does not use these fields to register the ISR;
    // Dxgkrnl binds DxgkDdiInterruptRoutine from the PDO resource list.
    ULONG             GpuIrqVector;
    KIRQL             GpuIrqLevel;
    KAFFINITY         GpuIrqAffinity;
    KINTERRUPT_MODE   GpuIrqMode;
    BOOLEAN           GpuIrqShareable;
    BOOLEAN           InterruptConnected;

    // Counters - maintained by the ISR, read by the escape path. Use
    // InterlockedIncrement64 on write.
    LONG64            InterruptsTotal;
    LONG64            InterruptsHandled;
    LONG64            InterruptsSpurious;

    // Hardware layer (defined in hw/WinMaliHw.h). Embedded, not a pointer,
    // so we don't need a second pool allocation.
    WINMALI_HW        Hw;

    // Mirror of WINMALI_ADAPTER_FLAG_* bits - set as bring-up progresses
    // so the escape channel can report what milestones fired.
    ULONG             AdapterFlags;

    // Contiguous non-cached heap: page tables, firmware page tables, MMU scratch.
    PVOID             MmuScratchHeapVa;
    PHYSICAL_ADDRESS  MmuScratchHeapPhys;
    SIZE_T            MmuScratchHeapBytes;
    BOOLEAN           GpuMmuAsBound;
    ULONG             GpuMmuBringupAs;

    // CSF firmware image + MCU address space (MMU AS0).
    struct _WINMALI_FWCTX* FwCtx;
    BOOLEAN           FwMcuAsBound;

    // Initialization state.
    BOOLEAN           StartedDevice;
    // Set when StartDevice returns success; not cleared by StopDevice so
    // RemoveDevice can tell a normal stop/remove from a never-started remove.
    BOOLEAN           StartDeviceEverSucceeded;

    // Monotonic fences (escape path and future submit). Reset on TDR/recovery.
    LONG64            GpuSubmittedFence;
    LONG64            GpuCompletedFence;

    // SubmitCommand / PreemptCommand fake-complete via DxgkCbNotifyInterrupt
    // and then queue a DPC. NotifyDpcPending is a single-bit "we owe dxgk a
    // NotifyDpc call" flag, set by the submit path and cleared by the DPC.
    // Reset on TDR/recovery (no in-flight commands across reset).
    LONG              NotifyDpcPending;

    // D3: firmware parked (FwTeardown); D0 re-runs FwInit.
    BOOLEAN           GpuFwParkedForD3;

    //
    // Mali MMU AS-slot allocator. The MMU exposes up to 16 address-space
    // slots (`GPU_AS_PRESENT` bitmap, 0xFF for G610). The CSF firmware lives
    // permanently in AS0 (`WINMALI_MMU_MCU_AS`) and our static kernel bring-up
    // VM is bound to AS1 (`WINMALI_MMU_BRINGUP_AS`). CreateProcess /
    // SetRootPageTable hand out AS2..AS(N-1) on demand and return them via
    // DestroyProcess. AsSlotLock is taken at DISPATCH_LEVEL by all paths so
    // SetRootPageTable can be safely called from any IRQL dxgk uses.
    //
    KSPIN_LOCK         AsSlotLock;
    ULONG              AsSlotInUseMask;     // bit i set => AS i programmed
    ULONG              AsSlotPresentMask;   // mirror of `GPU_AS_PRESENT`

    //
    // Process / context -> AS-slot bindings. SetRootPageTable is keyed by
    // dxgk's per-context handle (hContext), not by hKmdProcess, so we keep a
    // small flat table indexed by AS slot. Each slot remembers which dxgk
    // handle programmed it and what root-PT physical address is live, so
    // re-bind requests with the same hContext rewrite the same AS and we
    // never accidentally leak an AS slot.
    //
#define WINMALI_AS_SLOT_MAX            16u
    struct {
        HANDLE  hContext;       // dxgk-owned, opaque to us
        UINT64  RootPtPa;       // last programmed root-PT phys addr
        BOOLEAN Bound;
    } AsBindings[WINMALI_AS_SLOT_MAX];

    //
    // KMD process list. CreateProcess allocates a WINMALI_KMD_PROCESS and
    // hands its address back as DXGKARG_CREATEPROCESS.hKmdProcess; we keep a
    // doubly-linked list so RemoveDevice can clean up any process state that
    // dxgk leaked across a surprise teardown. ProcessListLock guards both
    // ProcessList and ActiveProcessCount; taken at DISPATCH_LEVEL.
    //
    KSPIN_LOCK         ProcessListLock;
    LIST_ENTRY         ProcessList;
    ULONG              ActiveProcessCount;

    /* VOP2 */
    RK3588_DISP_GOP_FB Gop;
    D3DKMDT_VIDPN_PRESENT_PATH        ActivePath;
    UINT PrimaryConnector;
    BOOLEAN                           HasActivePath;
    BOOLEAN                           SourceVisible;

    //
    // VOP2 hardware sub-block (RK3588 video output processor v2). Populated
    // by WinMaliVop2Initialize during WinMaliBringupHardware; valid for the
    // life of the adapter once Vop2.Initialized==TRUE. Phase 2a is probe-
    // only (read VP timings so we know what UEFI handed off); phase 2b
    // adds reset / clock / IRQ; phase 2c+ takes over scan-out so we can
    // publish a real sysmem-backed primary segment to dxgk.
    //
    WINMALI_VOP2                      Vop2;

    //
    // Route B segment publication: a CPU-visible aperture segment for
    // general D3D allocations and a tiny local-memory segment backed by
    // the captured GOP framebuffer for the DWM primary. The aperture
    // page table tracks pages that VIDMM has installed via
    // MAP_APERTURE_SEGMENT in DxgkDdiBuildPagingBuffer; we don't program
    // any GPU MMU off of it yet, but VIDMM still validates the calls.
    //
    PPFN_NUMBER       AperturePageTable;   // size = AperturePageCount
    SIZE_T            AperturePageCount;   // pages in the aperture seg
    SIZE_T            ApertureSegmentBytes;// = AperturePageCount * PAGE_SIZE

    //
    // Dedicated PopulatedFromSystemMemory segment backing kernel DMA buffers
    // and other non-primary D3D allocations. dxgmms2 in GpuMmu mode expects
    // CreateContext's DmaBufferSegmentSet to name a real segment dxgk owns;
    // setting it to 0 ("anywhere in sysmem") AVed dxgmms2!AddDmaBufferToPool
    // on Win11 26100, so we publish a separate contiguous WC sysmem block
    // and reference it as segment id 1 / DmaBufferSegmentSet bit 0.
    //
    // The block is independent of MmuScratchHeap (which is reserved for
    // page tables + bring-up scratch we own) so dxgk can freely lay DMA
    // buffers and small allocations across this whole region.
    //
    PVOID             DmaSegmentVa;
    PHYSICAL_ADDRESS  DmaSegmentPhys;
    SIZE_T            DmaSegmentBytes;

    //
    // Phase 2d: contiguous cached sysmem block published as dxgk segment id
    // WINMALI_GOP_SEGMENT_ID (2) — CpuVisible | PopulatedFromSystemMemory |
    // DirectFlip | LocalBudgetGroup. Filled at StartDevice from the GOP
    // framebuffer copy, then VOP2 Esmart YRGB_MST points here instead of
    // BIOS-reserved 0xEDA00000. Freed in WinMaliVop2TeardownScanoutSegment.
    //
    PVOID             ScanoutSegmentVa;
    PHYSICAL_ADDRESS  ScanoutSegmentPhys;
    SIZE_T            ScanoutSegmentBytes;

    // CPU-visible mapping of the GOP framebuffer for the post-display /
    // SetVidPnSourceAddress fallback path. Lazy-initialised the first
    // time we need it.
    PVOID             GopRuntimeVa;
    SIZE_T            GopRuntimeBytes;
} WINMALI_ADAPTER, *PWINMALI_ADAPTER;

//
// Route B segment id constants. dxgk segment ids are 1-based; segment 0
// is reserved as the "unused" slot in DXGK_SEGMENTPREFERENCE. Bit positions
// in SupportedReadSegmentSet / SupportedWriteSegmentSet are zero-based:
// segment i corresponds to bit (i - 1).
//
// Segment 1 was historically a CpuVisible Aperture window for sysmem. After
// the WDDM 2.4 GpuMmu rewrite (see WinMaliBuildSegmentList_ comments) it now
// names a PopulatedFromSystemMemory block, not an aperture - kept under the
// APERTURE-flavoured name so the QUERYSEGMENT path, allocation routing, and
// MAP/UNMAP_APERTURE_SEGMENT fallbacks all keep referring to a stable id.
// The macro just means "segment id 1, our sysmem-backed segment".
#define WINMALI_APERTURE_SEGMENT_ID      1u
#define WINMALI_SYSMEM_SEGMENT_ID        WINMALI_APERTURE_SEGMENT_ID
#define WINMALI_GOP_SEGMENT_ID           2u
#define WINMALI_SEGMENT_COUNT            2u

//
// GPU-side base addresses we report to dxgk in DXGK_SEGMENTDESCRIPTOR*.
//
// PHYSICALADAPTERCAPS publishes MinimumAddress=0x10000 and the firmware lives
// at fixed GPU VAs in [0x00000000..0x04040000). VIDMM uses the segment's
// BaseAddress as a hint for where the segment occupies the GPU VA space, so
// we must keep both segments above the FW reservations and above
// MinimumAddress. dxgkrnl validates this and silently StopDevices the adapter
// when BaseAddress is 0 or below MinimumAddress.
//
// Aperture sits at 0x10000000 (256 MB) for 256 MB of room; GOP local sits at
// 0xC0000000 (3 GB), matching the convention used by roskmd / other shipping
// WDDM samples.
//
#define WINMALI_APERTURE_GPU_BASE        0x10000000ull
#define WINMALI_GOP_GPU_BASE             0x30000000ull

//
// Legacy "fake but plausible" CPU translated address for the CpuVisible
// aperture: we don't expose a real linear CPU window for the aperture
// (each page has its own kernel VA via the MDL the runtime maps), but
// CpuVisible=1 requires SOMETHING here. Shipping WDDM samples use this
// magic value so dxgk never tries to touch it directly.
//
#define WINMALI_APERTURE_CPU_FAKE_ADDR   0xFFFFFFFE00000000ull

// 64 MB aperture. We previously sized this at 256 MB but
// DRIVERCAPS.ApertureSegmentCommitLimit is 64 MB; dxgkrnl validates that
// the aperture segment Size cannot exceed the announced commit limit and
// silently StopDevices us right after QUERYSEGMENT4 pass-2 when it does.
//
// 64 MB is also more than enough for early bring-up - DWM's redirection
// surfaces for a single 1080p primary fit in well under 32 MB. If we ever
// need more, we MUST raise both this value AND
// DRIVERCAPS.ApertureSegmentCommitLimit together.
// surfaces, D3D11 textures, swap-chain back buffers and shader heaps
// without rejecting CreateAllocation. dxgk only ever fills the pages
// it actually needs (demand-paged), so the page-table allocation cost
// scales with usage, not with the announced size.
#define WINMALI_APERTURE_SEGMENT_PAGES   16384u
#define WINMALI_APERTURE_SEGMENT_BYTES   ((SIZE_T)WINMALI_APERTURE_SEGMENT_PAGES * 0x1000u)

C_ASSERT(WINMALI_APERTURE_SEGMENT_BYTES == (64u * 1024u * 1024u));

//
// Per-process driver state. Returned via DXGKARG_CREATEPROCESS.hKmdProcess
// and freed in DxgkDdiDestroyProcess. The CreateProcess path inserts it on
// adapter->ProcessList; DestroyProcess removes it. Magic catches stale /
// wrong-type handles that some test harnesses pass on accident.
//
#define WINMALI_KMD_PROCESS_MAGIC      'PrcW'

typedef struct _WINMALI_KMD_PROCESS {
    ULONG               Magic;             // = WINMALI_KMD_PROCESS_MAGIC
    ULONG               Flags;             // mirror of DXGK_CREATEPROCESSFLAGS
    HANDLE              hDxgkProcess;      // hDxgkProcess from CreateProcess
    PWINMALI_ADAPTER    Adapter;           // back-ref (never NULL)
    LIST_ENTRY          AdapterLink;       // adapter->ProcessList
} WINMALI_KMD_PROCESS, *PWINMALI_KMD_PROCESS;

//
// Per-context driver state. Returned via DXGKARG_CREATECONTEXT.hContext (out)
// and freed in DxgkDdiDestroyContext. Mirrors render-only-sample's RosKmContext
// but tied to our adapter/process layout. SetRootPageTable uses the same
// pointer to look up the Mali AS slot owned by this context, so the address
// MUST stay stable for the entire context lifetime.
//
#define WINMALI_KMD_CONTEXT_MAGIC      'CtxW'

//
// DmaBufferSize for "render" contexts. dxgk allocates a DMA buffer of this
// size for every command submission. PAGE_SIZE matches render-only-sample
// (ROSD_COMMAND_BUFFER_SIZE) and is plenty for the NOP-style command streams
// we currently emit; bump when real Valhall command streams need more.
//
#define WINMALI_KMD_DMA_BUFFER_SIZE              PAGE_SIZE
#define WINMALI_KMD_ALLOCATION_LIST_SIZE         64u
#define WINMALI_KMD_PATCH_LOCATION_LIST_SIZE     128u

typedef struct _WINMALI_KMD_CONTEXT {
    ULONG               Magic;             // = WINMALI_KMD_CONTEXT_MAGIC
    PWINMALI_ADAPTER    Adapter;           // back-ref (never NULL)
    HANDLE              hRtContext;        // dxgk runtime handle (in from
                                            //  DXGKARG_CREATECONTEXT.hContext)
    UINT                NodeOrdinal;       // 0 = our only 3D node
    UINT                EngineAffinity;
    UINT                Flags;             // mirror of DXGK_CREATECONTEXTFLAGS
    UINT64              SubmittedFence;    // monotonic; reset on TDR
    UINT64              CompletedFence;
    LONG                PendingDmaCount;   // submitted but not yet NotifyDpc'd
} WINMALI_KMD_CONTEXT, *PWINMALI_KMD_CONTEXT;

//
// Per-resource driver state. dxgk creates a "resource" whenever
// DXGK_CREATEALLOCATIONFLAGS.Resource is set; the resource is an opaque
// grouping (typically a swapchain or a mip-chain) whose allocations live or
// die together. We mirror that with a small list head; the actual allocation
// structs are stored on AllocationListHead so RemoveDevice can defensively
// reap leaked allocations on surprise teardown.
//
#define WINMALI_KMD_RESOURCE_MAGIC     'ReoW'

typedef struct _WINMALI_KMD_RESOURCE {
    ULONG               Magic;             // = WINMALI_KMD_RESOURCE_MAGIC
    PWINMALI_ADAPTER    Adapter;           // back-ref (never NULL)
    ULONG               NumAllocations;    // count of live allocations
    ULONG               Flags;
    LIST_ENTRY          AllocationListHead;// WINMALI_KMD_ALLOCATION::ResourceLink
} WINMALI_KMD_RESOURCE, *PWINMALI_KMD_RESOURCE;

//
// Per-allocation driver state. Lifetime spans CreateAllocation ->
// DestroyAllocation; open/close on a per-device handle is currently a
// no-op (we hand the same pointer back as hDeviceSpecificAllocation).
//
// Magic catches stale or wrong-type handles that test harnesses sometimes
// pass; OwnerResource (NULL for stand-alone allocations) is followed by
// DestroyAllocation when the runtime asks to destroy the whole resource.
//
#define WINMALI_KMD_ALLOC_MAGIC        'AolW'

typedef struct _WINMALI_KMD_ALLOCATION {
    ULONG               Magic;             // = WINMALI_KMD_ALLOC_MAGIC
    PWINMALI_ADAPTER    Adapter;           // back-ref (never NULL)
    SIZE_T              Size;              // bytes (page aligned)
    ULONG               Alignment;
    ULONG               Flags;             // WINMALI_ALLOC_FLAG_*
    ULONG               Format;            // D3DDDIFORMAT
    ULONG               Width;
    ULONG               Height;
    ULONG               Pitch;
    ULONG               BytesPerPixel;
    ULONG               RefreshNumerator;
    ULONG               RefreshDenominator;
    ULONG               MultisampleMethod;
    ULONG               Rotation;
    // dxgk segment id (1-based) we asked VIDMM to land this allocation
    // in. Used by SetVidPnSourceAddress to tell whether the primary is
    // already on the GOP framebuffer (no copy) or needs the aperture
    // memcpy fallback.
    ULONG               PreferredSegmentId;
    PWINMALI_KMD_RESOURCE OwnerResource;   // NULL for stand-alone allocs
    LIST_ENTRY          ResourceLink;      // links into OwnerResource list
} WINMALI_KMD_ALLOCATION, *PWINMALI_KMD_ALLOCATION;

// ---------------------------------------------------------------------------
// DXGKDDI entry points exported by this driver.
// All are stubs in the skeletal build - they log and return a plausible
// value. Fill them in as bring-up progresses.
// ---------------------------------------------------------------------------

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_UNLOAD    WinMaliKmdDriverUnload;

// Validate an opaque MiniportDeviceContext / hAdapter cookie back to our
// adapter type, returning NULL on a stale or wrong-tag pointer. Callers must
// null-check the result before dereferencing.
PWINMALI_ADAPTER WinMaliAdapterFromContext(_In_ const VOID* Context);

// Same adapter as above, but keyed by what dxgk passes as hAdapter on most
// DDIs: DXGKRNL_INTERFACE::DeviceHandle after StartDevice. QueryAdapterInfo
// sometimes passes the miniport context pointer instead; this helper accepts
// either (singleton adapter).
PWINMALI_ADAPTER WinMaliAdapterFromDxgkHandle(_In_opt_ const VOID* hAdapter);

// Device lifecycle
// NOTE: AddDevice takes IN_CONST_PDEVICE_OBJECT which is typedef'd as
// "CONST PDEVICE_OBJECT" (pointer-is-const, object is not), not
// "const DEVICE_OBJECT*". Getting this wrong gives a C4113 parameter-
// list mismatch when assigning into DRIVER_INITIALIZATION_DATA.
NTSTATUS APIENTRY WinMaliKmdAddDevice    (_In_ CONST PDEVICE_OBJECT      PhysicalDeviceObject,
                                          _Outptr_ PVOID*                MiniportDeviceContext);
NTSTATUS APIENTRY WinMaliKmdStartDevice  (_In_  const PVOID              MiniportDeviceContext,
                                          _In_  PDXGK_START_INFO         DxgkStartInfo,
                                          _In_  PDXGKRNL_INTERFACE       DxgkInterface,
                                          _Out_ PULONG                   NumberOfVideoPresentSources,
                                          _Out_ PULONG                   NumberOfChildren);
NTSTATUS APIENTRY WinMaliKmdStopDevice   (_In_ const PVOID               MiniportDeviceContext);
NTSTATUS APIENTRY WinMaliKmdRemoveDevice (_In_ const PVOID               MiniportDeviceContext);
VOID              WinMaliKmdDdiUnload    (VOID);

// Queries
NTSTATUS APIENTRY WinMaliKmdQueryAdapterInfo   (_In_ const HANDLE                      hAdapter,
                                                _In_ const DXGKARG_QUERYADAPTERINFO*   pQueryAdapterInfo);
NTSTATUS APIENTRY WinMaliKmdGetNodeMetadata    (_In_ const HANDLE                      hAdapter,
                                                _In_ UINT                               NodeOrdinalAndAdapterIndex,
                                                _Out_ DXGKARG_GETNODEMETADATA*          pGetNodeMetadata);
NTSTATUS APIENTRY WinMaliKmdQueryChildRelations(_In_ const PVOID                       MiniportDeviceContext,
                                                _Inout_ PDXGK_CHILD_DESCRIPTOR         ChildRelations,
                                                _In_ ULONG                             ChildRelationsSize);
NTSTATUS APIENTRY WinMaliKmdQueryChildStatus   (_In_ const PVOID                       MiniportDeviceContext,
                                                _In_ PDXGK_CHILD_STATUS                ChildStatus,
                                                _In_ BOOLEAN                           NonDestructiveOnly);
NTSTATUS APIENTRY WinMaliKmdQueryDeviceDescriptor(_In_ const PVOID                     MiniportDeviceContext,
                                                  _In_ ULONG                           ChildUid,
                                                  _Inout_ PDXGK_DEVICE_DESCRIPTOR      DeviceDescriptor);
NTSTATUS APIENTRY WinMaliKmdQueryInterface     (_In_ const PVOID                       MiniportDeviceContext,
                                                _In_ PQUERY_INTERFACE                  QueryInterface);

// Power
NTSTATUS APIENTRY WinMaliKmdSetPowerState (_In_ const PVOID                   MiniportDeviceContext,
                                           _In_ ULONG                         DeviceUid,
                                           _In_ DEVICE_POWER_STATE            DevicePowerState,
                                           _In_ POWER_ACTION                  ActionType);
NTSTATUS APIENTRY WinMaliKmdNotifyAcpiEvent(_In_ const PVOID                  MiniportDeviceContext,
                                            _In_ DXGK_EVENT_TYPE              EventType,
                                            _In_ ULONG                        Event,
                                            _In_ PVOID                        Argument,
                                            _Out_ PULONG                      AcpiFlags);
VOID              WinMaliKmdResetDevice   (_In_ const PVOID                   MiniportDeviceContext);

// Interrupt
BOOLEAN           WinMaliKmdInterruptRoutine(_In_ const PVOID                 MiniportDeviceContext,
                                             _In_ ULONG                       MessageNumber);
VOID              WinMaliKmdDpcRoutine      (_In_ const PVOID                 MiniportDeviceContext);
NTSTATUS APIENTRY WinMaliKmdControlInterrupt(_In_ const HANDLE                hAdapter,
                                             _In_ const DXGK_INTERRUPT_TYPE   InterruptType,
                                             _In_ BOOLEAN                     Enable);

// Paging, command submission, context, allocation - all stubs in skeletal.
NTSTATUS APIENTRY WinMaliKmdCreateDevice     (_In_ const HANDLE                      hAdapter,
                                              _Inout_ DXGKARG_CREATEDEVICE*          pCreateDevice);
NTSTATUS APIENTRY WinMaliKmdDestroyDevice    (_In_ const HANDLE                      hDevice);
NTSTATUS APIENTRY WinMaliKmdCreateAllocation (_In_ const HANDLE                      hAdapter,
                                              _Inout_ DXGKARG_CREATEALLOCATION*      pCreateAllocation);
NTSTATUS APIENTRY WinMaliKmdDestroyAllocation(_In_ const HANDLE                      hAdapter,
                                              _In_ const DXGKARG_DESTROYALLOCATION*  pDestroyAllocation);
NTSTATUS APIENTRY WinMaliKmdOpenAllocation   (_In_ const HANDLE                      hDevice,
                                              _In_ const DXGKARG_OPENALLOCATION*     pOpenAllocation);
NTSTATUS APIENTRY WinMaliKmdCloseAllocation  (_In_ const HANDLE                      hDevice,
                                              _In_ const DXGKARG_CLOSEALLOCATION*    pCloseAllocation);

// Command / paging / fence — non-NULL stubs required for DxgkInitialize on
// modern Windows even before real GPU scheduling exists.
NTSTATUS APIENTRY WinMaliKmdDescribeAllocation(
    _In_ const HANDLE                              hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION*            pDescribeAllocation);
NTSTATUS APIENTRY WinMaliKmdGetStandardAllocationDriverData(
    _In_ const HANDLE                              hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pGetStandardAllocationDriverData);
NTSTATUS APIENTRY WinMaliKmdPatch(
    _In_ const HANDLE                    hAdapter,
    _In_ const DXGKARG_PATCH*            pPatch);
NTSTATUS APIENTRY WinMaliKmdSubmitCommand(
    _In_ const HANDLE                        hAdapter,
    _In_ const DXGKARG_SUBMITCOMMAND*       pSubmitCommand);
NTSTATUS APIENTRY WinMaliKmdPreemptCommand(
    _In_ const HANDLE                        hAdapter,
    _In_ const DXGKARG_PREEMPTCOMMAND*      pPreemptCommand);
NTSTATUS APIENTRY WinMaliKmdBuildPagingBuffer(
    _In_ const HANDLE                        hAdapter,
    _In_ DXGKARG_BUILDPAGINGBUFFER*          pBuildPagingBuffer);
NTSTATUS APIENTRY WinMaliKmdQueryCurrentFence(
    _In_ const HANDLE                        hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE*       pCurrentFence);
NTSTATUS APIENTRY WinMaliKmdCancelCommand(
    _In_ const HANDLE                        hAdapter,
    _In_ const DXGKARG_CANCELCOMMAND*        pCancelCommand);
NTSTATUS APIENTRY WinMaliKmdResetFromTimeout(_In_ const HANDLE hAdapter);
NTSTATUS APIENTRY WinMaliKmdRestartFromTimeout(_In_ const HANDLE hAdapter);
NTSTATUS APIENTRY WinMaliKmdRender(
    _In_ const HANDLE              hContext,
    _Inout_ DXGKARG_RENDER*         pRender);

// Presentation / VidPN - we are a render-only adapter for now, so these
// return STATUS_NOT_IMPLEMENTED; a separate DOD (VOP2DOD) owns display.
NTSTATUS APIENTRY WinMaliKmdIsSupportedVidPn (_In_ const HANDLE                      hAdapter,
                                              _Inout_ DXGKARG_ISSUPPORTEDVIDPN*      pIsSupportedVidPn);
NTSTATUS APIENTRY WinMaliKmdRecommendFunctionalVidPn(_In_ const HANDLE               hAdapter,
                                              _In_ const DXGKARG_RECOMMENDFUNCTIONALVIDPN* pRecommendFunctionalVidPn);
NTSTATUS APIENTRY WinMaliKmdEnumVidPnCofuncModality(_In_ const HANDLE                hAdapter,
                                              _In_ const DXGKARG_ENUMVIDPNCOFUNCMODALITY* pEnumCofuncModality);
NTSTATUS APIENTRY WinMaliKmdSetVidPnSourceVisibility(_In_ const HANDLE               hAdapter,
                                              _In_ const DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);
NTSTATUS APIENTRY WinMaliKmdCommitVidPn      (_In_ const HANDLE                      hAdapter,
                                              _In_ const DXGKARG_COMMITVIDPN*        pCommitVidPn);
NTSTATUS APIENTRY WinMaliKmdUpdateActiveVidPnPresentPath(_In_ const HANDLE            hAdapter,
                                              _In_ const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* pUpdateActive);
NTSTATUS APIENTRY WinMaliKmdRecommendMonitorModes(_In_ const HANDLE                  hAdapter,
                                              _In_ const DXGKARG_RECOMMENDMONITORMODES* pRecommendMonitorModes);

NTSTATUS APIENTRY WinMaliKmdStopDeviceAndReleasePostDisplayOwnership(
    _In_ const PVOID                     MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION      DisplayInfo);

// Escape channel - used by the host diag tool. See Shared\WinMaliCommon.h
// for the opcode/payload definitions.
NTSTATUS APIENTRY WinMaliKmdEscape(
    _In_ const HANDLE                    hAdapter,
    _In_ const DXGKARG_ESCAPE*           pEscape);


// Parse the translated resource list DXGK hands us in StartDevice, fill in
// GpuRegsPhys/GpuRegsSize and GpuIrq* fields on the adapter. Returns
// STATUS_SUCCESS on a valid layout, STATUS_DEVICE_CONFIGURATION_ERROR
// if the list doesn't match what gpu.asl declared.
NTSTATUS WinMaliParseResources(_Inout_ PWINMALI_ADAPTER Adapter);

// Map MMIO, probe GPU_ID, validate Mali-G610 product code. Safe to call
// on hardware we do not recognise - it will log + return an error but
// not crash.
NTSTATUS WinMaliBringupHardware(_Inout_ PWINMALI_ADAPTER Adapter);

// Tear down anything WinMaliBringupHardware set up. Safe to call even
// if bring-up failed partway through.
VOID     WinMaliTeardownHardware(_Inout_ PWINMALI_ADAPTER Adapter);

//
// Optional bring-up smoke test (deploy / lab builds): maps the GOP
// framebuffer after DxgkCbAcquirePostDisplayOwnership, draws a visible
// square into scan-out RAM, then re-pulses YRGB_MST + CFG_DONE. Call
// once from StartDevice after Rk3588DispCaptureGopFb.
//
VOID     WinMaliVop2SmokeVisualDraw(_Inout_ PWINMALI_ADAPTER Adapter);

NTSTATUS WinMaliVop2SetupSysmemScanout(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliVop2TeardownScanoutSegment(_Inout_ PWINMALI_ADAPTER Adapter);

// Connect the shared GPU interrupt (GSIV 124/125/126) via the DXGK
// callback surface. No-op if the IRQ was already connected.
NTSTATUS WinMaliConnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliDisconnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter);

NTSTATUS WinMaliFwInit(_Inout_ PWINMALI_ADAPTER Adapter);
VOID     WinMaliFwTeardown(_Inout_ PWINMALI_ADAPTER Adapter);

// CSF: submit NOP stream via bootstrapped kernel queue (Passive). Used by Escape.
// Requires WINMALI_ADAPTER_FLAG_CSF_JOBS and firmware up.
NTSTATUS WinMaliCsfSubmitNopJob(_Inout_ PWINMALI_ADAPTER Adapter);




/* VOP2 / GOP */
VOID
Rk3588DispCaptureGopFb(_Inout_ PWINMALI_ADAPTER Adapter);

VOID
Rk3588DispReleaseGopFb(_Inout_ PWINMALI_ADAPTER Adapter);

NTSTATUS APIENTRY
Rk3588DispEnumVidPnCofuncModality(
    _In_ const HANDLE                              hAdapter,
    _In_ const DXGKARG_ENUMVIDPNCOFUNCMODALITY*    pEnumCofuncModality);

NTSTATUS APIENTRY
Rk3588DispRecommendFunctionalVidPn(
    _In_ const HANDLE                                     hAdapter,
    _In_ const DXGKARG_RECOMMENDFUNCTIONALVIDPN*          pRecommendFunctionalVidPn);


NTSTATUS APIENTRY
Rk3588DispCommitVidPn(
    _In_ const HANDLE                  hAdapter,
    _In_ const DXGKARG_COMMITVIDPN*    pCommitVidPn);
NTSTATUS APIENTRY
Rk3588DispUpdateActiveVidPnPresentPath(
    _In_ const HANDLE                                 hAdapter,
    _In_ const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH*  pUpdateActive);

NTSTATUS APIENTRY
Rk3588DispSetVidPnSourceVisibility(
    _In_ const HANDLE                             hAdapter,
    _In_ const DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility);

NTSTATUS APIENTRY
Rk3588DispRecommendMonitorModes(
    _In_ const HANDLE                           hAdapter,
    _In_ const DXGKARG_RECOMMENDMONITORMODES*   pRecommendMonitorModes);

NTSTATUS APIENTRY Rk3588DispSetVidPnSourceAddress(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS     pSetVidPnSourceAddress);