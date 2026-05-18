/*++

Module Name:

    WinMaliDxgkInitFill.h

Abstract:

    Wires the DRIVER_INITIALIZATION_DATA struct that DxgkInitialize
    consumes. Split out from WinMaliDriver.c so the version-pinning
    rationale is documented in one place.

--*/

#pragma once

#include <ntddk.h>
#include <dispmprt.h>

VOID WinMaliDxgkPatchInitializationData(_Out_ DRIVER_INITIALIZATION_DATA* init);
