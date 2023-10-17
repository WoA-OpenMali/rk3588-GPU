#pragma once

#include <dispmprt.h>
#include "utils.h"
#include "chipsets.h"


/* kernel mode DDIs: */

NTSTATUS
G610AddDevice(_In_ DEVICE_OBJECT* pPhysicalDeviceObject,
              _Outptr_ PVOID*  ppDeviceContext);

NTSTATUS
G610StartDevice(_In_  VOID*              pDeviceContext,
                _In_  DXGK_START_INFO*   pDxgkStartInfo,
                _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                _Out_ ULONG*             pNumberOfViews,
                _Out_ ULONG*             pNumberOfChildren);

NTSTATUS
APIENTRY
G610DdiGetScanLine(_In_    CONST HANDLE         hAdapter,
                   _Inout_ DXGKARG_GETSCANLINE* pGetScanLine);
