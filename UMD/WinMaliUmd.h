/*++

Module Name:

    WinMaliUmd.h

Abstract:

    Internal header for the hand-written D3D11 user-mode driver.

    Skeleton phase: the UMD owns a one-page adapter struct and an
    OpenAdapter10_2 that fills a D3D10_2DDI_ADAPTERFUNCS table whose
    every entry returns E_NOTIMPL (apart from CloseAdapter, which frees
    the adapter). This is enough to satisfy dxgkrnl's load contract;
    no D3D11 device will ever come up until we implement
    pfnCreateDevice.

    D3D11 user-mode DDI headers (d3d10umddi.h, d3d11_1.h) pull in some
    types from d3dkmddi.h that assume kernel-mode context. We include
    them in the order Mesa's d3d10umd frontend uses, which is the only
    order known to compile clean with the MSVC + WDK headers.

--*/

#pragma once

#include <windows.h>
#include <winternl.h>

/* Minor version negotiation: 2 is the lowest that exposes the
   D3D10_2 / D3D11 entry surface we care about. */
#define D3D10DDI_MINOR_HEADER_VERSION 2

#include <d3d10_1.h>
#include <d3d10umddi.h>

#include "..\Shared\WinMaliCommon.h"

typedef struct _WINMALI_UMD_ADAPTER {
    UINT32                    Magic;
#define WINMALI_UMD_ADAPTER_MAGIC   'AadM'    /* 'MdaA' little-endian */

    D3D10DDI_HRTADAPTER       RuntimeAdapter;
    D3DDDI_ADAPTERCALLBACKS   AdapterCallbacks;
} WINMALI_UMD_ADAPTER, *PWINMALI_UMD_ADAPTER;

EXTERN_C HRESULT APIENTRY
OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenAdapter);
