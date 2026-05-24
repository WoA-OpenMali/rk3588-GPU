
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
