/*
 * PROJECT:     Mali G610 Render only display driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     TODO Functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include <driver.h>

NTSTATUS
NTAPI
G610StartDevice(_In_  VOID*              pDeviceContext,
                _In_  DXGK_START_INFO*   pDxgkStartInfo,
                _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                _Out_ ULONG*             pNumberOfViews,
                _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("--> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_ERROR, ("%s is UNIMPLEMENTED\n", __FUNCTION__));
    DbgBreakPoint();
    // UNIMPLEMENTED
    DbgPrint(TRACE_LEVEL_FATAL, ("<-- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
G610DdiGetScanLine(_In_    CONST HANDLE         hAdapter,
                   _Inout_ DXGKARG_GETSCANLINE* pGetScanLine)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetScanLine);

    DbgPrint(TRACE_LEVEL_ERROR, ("%s is UNIMPLEMENTED\n", __FUNCTION__));
    DbgBreakPoint();
    return STATUS_NO_MEMORY;
}