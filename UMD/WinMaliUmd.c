/*
 * PROJECT:     WinMaliUMD
 * LICENSE:
 * PURPOSE:     Implementations of the d3d entrypoint stub? - None Mesa.
 * COPYRIGHT:   Justin Miller <justin.miller@reactos.org>
 */

#include "WinMaliUmd.h"

#include <stdio.h>

/* ---------------------------------------------------------------------- */
/* Trace                                                                   */
/* ---------------------------------------------------------------------- */

static void WinMaliUmdTrace(const char* fmt, ...)
{
    char    line[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA("[WinMaliUmd] ");
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
}

/* ---------------------------------------------------------------------- */
/* Adapter-level DDI stubs                                                 */
/* ---------------------------------------------------------------------- */

static SIZE_T APIENTRY
WinMaliUmdCalcPrivateDeviceSize(
    D3D10DDI_HADAPTER                          hAdapter,
    CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE*   pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    /* Return 0: until CreateDevice does anything, the runtime can't
       allocate space for a private device anyway. */
    return 0;
}

static HRESULT APIENTRY
WinMaliUmdCreateDevice(
    D3D10DDI_HADAPTER             hAdapter,
    D3D10DDIARG_CREATEDEVICE*     pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    WinMaliUmdTrace("CreateDevice: skeleton returns E_NOTIMPL");
    return E_NOTIMPL;
}

static HRESULT APIENTRY
WinMaliUmdCloseAdapter(D3D10DDI_HADAPTER hAdapter)
{
    PWINMALI_UMD_ADAPTER adapter = (PWINMALI_UMD_ADAPTER)hAdapter.pDrvPrivate;
    if (adapter == NULL || adapter->Magic != WINMALI_UMD_ADAPTER_MAGIC) {
        return E_INVALIDARG;
    }
    WinMaliUmdTrace("CloseAdapter ctx=%p", adapter);
    adapter->Magic = 0;
    HeapFree(GetProcessHeap(), 0, adapter);
    return S_OK;
}

static HRESULT APIENTRY
WinMaliUmdGetSupportedVersions(
    D3D10DDI_HADAPTER                          hAdapter,
    _Inout_ UINT32*                            puEntries,
    _Out_writes_(*puEntries) UINT64*           pSupportedDDIInterfaceVersions)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (puEntries == NULL) {
        return E_INVALIDARG;
    }
    if (pSupportedDDIInterfaceVersions == NULL) {
        /* Probing call: report capacity (1, since we only claim
           D3D11_0_DDI_INTERFACE_VERSION). */
        *puEntries = 1;
        return S_OK;
    }
    if (*puEntries < 1) {
        *puEntries = 1;
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    pSupportedDDIInterfaceVersions[0] = D3D11_0_DDI_INTERFACE_VERSION;
    *puEntries = 1;
    return S_OK;
}

static HRESULT APIENTRY
WinMaliUmdGetCaps(
    D3D10DDI_HADAPTER                hAdapter,
    CONST D3D10_2DDIARG_GETCAPS*     pCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCaps);
    /* No caps published in skeleton; returning NOT_IMPL is correct,
       the runtime treats it as "ask the KMD". */
    return E_NOTIMPL;
}

/* ---------------------------------------------------------------------- */
/* OpenAdapter10_2 - the .def-exported entry point                         */
/* ---------------------------------------------------------------------- */

EXTERN_C HRESULT APIENTRY
OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenAdapter)
{
    PWINMALI_UMD_ADAPTER     adapter;
    D3D10_2DDI_ADAPTERFUNCS* funcs;

    if (pOpenAdapter == NULL || pOpenAdapter->pAdapterFuncs_2 == NULL) {
        return E_INVALIDARG;
    }

    adapter = (PWINMALI_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) {
        return E_OUTOFMEMORY;
    }

    adapter->Magic            = WINMALI_UMD_ADAPTER_MAGIC;
    adapter->RuntimeAdapter   = pOpenAdapter->hRTAdapter;
    adapter->AdapterCallbacks = *pOpenAdapter->pAdapterCallbacks;

    pOpenAdapter->hAdapter.pDrvPrivate = adapter;
    pOpenAdapter->Interface            = D3D11_0_DDI_INTERFACE_VERSION;
    pOpenAdapter->Version              = pOpenAdapter->Version; /* echo */

    funcs                          = pOpenAdapter->pAdapterFuncs_2;
    funcs->pfnCalcPrivateDeviceSize = WinMaliUmdCalcPrivateDeviceSize;
    funcs->pfnCreateDevice          = WinMaliUmdCreateDevice;
    funcs->pfnCloseAdapter          = WinMaliUmdCloseAdapter;
    funcs->pfnGetSupportedVersions  = WinMaliUmdGetSupportedVersions;
    funcs->pfnGetCaps               = WinMaliUmdGetCaps;

    WinMaliUmdTrace("OpenAdapter10_2 ctx=%p iface=0x%x",
                    adapter, pOpenAdapter->Interface);
    return S_OK;
}

/* DllMain - DLL_PROCESS_ATTACH is the only thing the runtime hits
   before OpenAdapter10_2. DisableThreadLibraryCalls is the standard
   "don't bother me with thread up/down" optimization. */
BOOL WINAPI
DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}
