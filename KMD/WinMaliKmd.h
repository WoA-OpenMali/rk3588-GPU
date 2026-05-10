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
// DRIVER_INITIALIZATION_DATA.Version must equal DXGKDDI_INTERFACE_VERSION
// from the WDK headers. On recent Windows builds, DxgkInitialize can fail
// with STATUS_REVISION_MISMATCH if required slots in the expanded init
// structure are NULL; WinMaliDxgkPatchInitializationData (WinMaliDxgkInitFill.c)
// assigns the full WDDM 3.2 callback table.

#include <ntstrsafe.h>

#include "..\Shared\WinMaliCommon.h"
#include "WinMaliTrace.h"       // WINMALI_TRACE / _WARN / _ERROR / _ENTER
#include "hw\WinMaliHw.h"       // WINMALI_HW, register offsets

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

    // D3: firmware parked (FwTeardown); D0 re-runs FwInit.
    BOOLEAN           GpuFwParkedForD3;

    /* VOP2 */
    RK3588_DISP_GOP_FB Gop;
    D3DKMDT_VIDPN_PRESENT_PATH        ActivePath;
    UINT PrimaryConnector;
    BOOLEAN                           HasActivePath;
      BOOLEAN                           SourceVisible;
} WINMALI_ADAPTER, *PWINMALI_ADAPTER;

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