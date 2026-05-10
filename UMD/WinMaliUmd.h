/*++

Module Name:

    WinMaliUmd.h

Abstract:

    Internal header for the WinMali user-mode D3D11 display driver.
    Exports a single OS-visible entry point (OpenAdapter10_2) and a
    minimal DDI function table (all stubs).

--*/

#pragma once

// D3D11 user-mode DDI headers pull in <d3dkmddi.h> which is written
// assuming kernel-mode context (NTSTATUS, IRQL annotations, PDEVICE_OBJECT
// etc.). For a user-mode UMD we provide the missing type aliases *before*
// including the DDI headers. This mirrors the pattern used by Mesa's
// d3d10umd frontend (mesa\include\winddk\winddk_compat.h).
#include <windows.h>

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

// Pick a minor header version supported by the installed WDK; D3D10.1 is
// version 2, D3D11 adds 3+.
#define D3D10DDI_MINOR_HEADER_VERSION 2

// Include d3d10_1.h to avoid duplicate-symbol link errors from the
// const-variable feature-level constants in d3d10umddi.h. (Same
// workaround as Mesa's DriverIncludes.h.)
#include <d3d10_1.h>
#include <d3d10umddi.h>

#include "..\Shared\WinMaliCommon.h"

// ---------------------------------------------------------------------------
// Feature-level cap this UMD advertises. Callbacks that report caps should
// clamp to this and never advertise anything higher than what the KMD can
// actually support.
// ---------------------------------------------------------------------------

#define WINMALI_UMD_MAX_FEATURE_LEVEL   D3D10_DDI_FEATURE_LEVEL_11_0

// Runtime interface version the UMD talks. D3D11 UMDs negotiate
// D3D11_1_DDI_INTERFACE_VERSION family; we pick the minimum that gives
// us FL 11_0 without forcing 11_1/11_2 features we don't implement.
#define WINMALI_UMD_DDI_VERSION         D3D11_0_DDI_INTERFACE_VERSION

// ---------------------------------------------------------------------------
// Per-adapter and per-device state the UMD owns.
// ---------------------------------------------------------------------------

typedef struct _WINMALI_UMD_ADAPTER {
    UINT32 Magic;
#define WINMALI_UMD_ADAPTER_MAGIC   'AadM' // "MdaA"

    D3D10DDI_HRTADAPTER       RuntimeAdapter;
    D3DDDI_ADAPTERCALLBACKS   AdapterCallbacks;

    // Echoed from the KMD via QueryAdapterInfo(UMDRIVERPRIVATE).
    WINMALI_ADAPTER_INFO      KmdInfo;
} WINMALI_UMD_ADAPTER, *PWINMALI_UMD_ADAPTER;

typedef struct _WINMALI_UMD_DEVICE {
    UINT32 Magic;
#define WINMALI_UMD_DEVICE_MAGIC   'DdeM'  // "MdeD"

    D3D10DDI_HDEVICE          RuntimeDevice;
    PWINMALI_UMD_ADAPTER      Adapter;
} WINMALI_UMD_DEVICE, *PWINMALI_UMD_DEVICE;

// ---------------------------------------------------------------------------
// Exported entry point (see WinMaliUmd.def)
// ---------------------------------------------------------------------------

EXTERN_C HRESULT APIENTRY
OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenAdapter);

// ---------------------------------------------------------------------------
// Adapter-level DDI stubs (WinMali-prefixed; OpenAdapter10_2 wires them up)
// ---------------------------------------------------------------------------

SIZE_T APIENTRY  WinMaliUmdCalcPrivateDeviceSize    (D3D10DDI_HADAPTER hAdapter, CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE* pArgs);
HRESULT APIENTRY WinMaliUmdCreateDevice             (D3D10DDI_HADAPTER hAdapter, D3D10DDIARG_CREATEDEVICE* pArgs);
HRESULT APIENTRY WinMaliUmdCloseAdapter             (D3D10DDI_HADAPTER hAdapter);
HRESULT APIENTRY WinMaliUmdGetSupportedVersions     (D3D10DDI_HADAPTER hAdapter, _Inout_ UINT32* puEntries, _Out_writes_(*puEntries) UINT64* pSupportedDDIInterfaceVersions);
HRESULT APIENTRY WinMaliUmdGetCaps                  (D3D10DDI_HADAPTER hAdapter, CONST D3D10_2DDIARG_GETCAPS* pCaps);

// ---------------------------------------------------------------------------
// Device-level DDI stubs (just enough to not crash the runtime)
// ---------------------------------------------------------------------------

HRESULT APIENTRY WinMaliUmdDestroyDevice            (D3D10DDI_HDEVICE hDevice);
VOID    APIENTRY WinMaliUmdDefaultConstantBufferUpdateSubresourceUP(D3D10DDI_HDEVICE hDevice, UINT Slot, _In_opt_ CONST D3D10_DDI_BOX* pDstBox, _In_ CONST VOID* pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags);
VOID    APIENTRY WinMaliUmdFlush                    (D3D10DDI_HDEVICE hDevice, UINT FlushFlags);
VOID    APIENTRY WinMaliUmdCheckFormatSupport       (D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, _Out_ UINT* pFormatSupport);
VOID    APIENTRY WinMaliUmdCheckMultisampleQualityLevels(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT SampleCount, _Out_ UINT* pNumQualityLevels);
