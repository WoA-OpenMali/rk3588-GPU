/*++

Module Name:

    WinMaliKmd.h

Abstract:

    Internal header for the WinMali kernel-mode WDDM 2.0 miniport.

    Skeleton phase: only the per-adapter context, the adapter accessor,
    and the DDI prototypes that DriverEntry actually wires up. Nothing
    here touches hardware - all DDI entries are stubs returning
    STATUS_NOT_SUPPORTED.

    The header is C-only (dispmprt.h is C-only too). Wrap any future
    C++ consumers in extern "C".

--*/

#pragma once

#include <ntddk.h>
#include <dispmprt.h>
#include <ntstrsafe.h>

#include "..\Shared\WinMaliEscape.h"

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
    DXGK_START_INFO    DxgkStartInfo;

    BOOLEAN             Started;
} WINMALI_ADAPTER, *PWINMALI_ADAPTER;

/* Map a dxgk-supplied context (or hAdapter handle) back to our struct.
   Returns NULL on a bad-magic / NULL / mismatched-handle context so
   callers can fail with STATUS_INVALID_PARAMETER. */
PWINMALI_ADAPTER WinMaliAdapterFromContext(_In_opt_ const VOID* Context);
PWINMALI_ADAPTER WinMaliAdapterFromDxgkHandle(_In_opt_ const VOID* hAdapter);

/* ---------- DDI prototypes wired into DRIVER_INITIALIZATION_DATA ------- */

DRIVER_INITIALIZE                       DriverEntry;
DRIVER_UNLOAD                           WinMaliKmdUnload;

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
