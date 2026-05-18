/*++

Module Name:

    WinMaliDxgkInitFill.c

Abstract:

    Fills DRIVER_INITIALIZATION_DATA for DxgkInitialize.

    Version pinning: we publish DXGKDDI_INTERFACE_VERSION_WDDM2_0
    here. dxgk validates that every wired callback exists for the
    version we advertise, so when (and only when) you wire a new DDI
    that's part of a later DDI revision, bump this constant and the
    matching WDDMVersion / DRIVERCAPS reporting in lock-step. Keeping
    the WDK-default DXGKDDI_INTERFACE_VERSION (currently WDDM 3.2)
    while reporting WDDM 2.0 caps will make dxgk reject the adapter
    after GetNodeMetadata.

--*/

#include "WinMaliKmd.h"
#include "WinMaliDxgkInitFill.h"

VOID
WinMaliDxgkPatchInitializationData(_Out_ DRIVER_INITIALIZATION_DATA* init)
{
    RtlZeroMemory(init, sizeof(*init));

    init->Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;

    /* Lifecycle */
    init->DxgkDdiAddDevice              = WinMaliKmdAddDevice;
    init->DxgkDdiStartDevice            = WinMaliKmdStartDevice;
    init->DxgkDdiStopDevice             = WinMaliKmdStopDevice;
    init->DxgkDdiRemoveDevice           = WinMaliKmdRemoveDevice;
    init->DxgkDdiDispatchIoRequest      = WinMaliKmdDispatchIoRequest;
    init->DxgkDdiInterruptRoutine       = WinMaliKmdInterruptRoutine;
    init->DxgkDdiDpcRoutine             = WinMaliKmdDpcRoutine;

    /* Enumeration (render-only: zero children, zero VidPN sources) */
    init->DxgkDdiQueryChildRelations    = WinMaliKmdQueryChildRelations;
    init->DxgkDdiQueryChildStatus       = WinMaliKmdQueryChildStatus;
    init->DxgkDdiQueryDeviceDescriptor  = WinMaliKmdQueryDeviceDescriptor;

    /* Power / ACPI / reset / iface */
    init->DxgkDdiSetPowerState          = WinMaliKmdSetPowerState;
    init->DxgkDdiNotifyAcpiEvent        = WinMaliKmdNotifyAcpiEvent;
    init->DxgkDdiResetDevice            = WinMaliKmdResetDevice;
    init->DxgkDdiQueryInterface         = WinMaliKmdQueryInterface;

    /* ETW + adapter info */
    init->DxgkDdiControlEtwLogging      = WinMaliKmdControlEtwLogging;
    init->DxgkDdiQueryAdapterInfo       = WinMaliKmdQueryAdapterInfo;
}
