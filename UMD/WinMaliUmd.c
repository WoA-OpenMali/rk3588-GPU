

#include "WinMaliUmd.h"
#include <stdio.h>

#define WINMALI_UMD_TRACE(fmt, ...)                                       \
    do {                                                                  \
        char _buf[256];                                                   \
        _snprintf_s(_buf, sizeof(_buf), _TRUNCATE,                        \
                    "[WinMaliUmd] " fmt "\n", __VA_ARGS__);              \
        OutputDebugStringA(_buf);                                         \
    } while (0)

// ---------------------------------------------------------------------------
// Adapter-level DDI stubs
// ---------------------------------------------------------------------------

SIZE_T APIENTRY
WinMaliUmdCalcPrivateDeviceSize(
    D3D10DDI_HADAPTER                             hAdapter,
    CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE*      pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    return sizeof(WINMALI_UMD_DEVICE);
}

HRESULT APIENTRY
WinMaliUmdCreateDevice(
    D3D10DDI_HADAPTER            hAdapter,
    D3D10DDIARG_CREATEDEVICE*    pArgs)
{
    PWINMALI_UMD_ADAPTER adapter = (PWINMALI_UMD_ADAPTER)hAdapter.pDrvPrivate;
    PWINMALI_UMD_DEVICE  device;

    if (adapter == NULL || adapter->Magic != WINMALI_UMD_ADAPTER_MAGIC) {
        return E_INVALIDARG;
    }
    if (pArgs == NULL || pArgs->hRTDevice.handle == 0) {
        return E_INVALIDARG;
    }

    device = (PWINMALI_UMD_DEVICE)pArgs->hDrvDevice.pDrvPrivate;
    if (device == NULL) {
        return E_OUTOFMEMORY;
    }
    device->Magic = WINMALI_UMD_DEVICE_MAGIC;
    device->Adapter = adapter;

    WINMALI_UMD_TRACE("CreateDevice: skeletal, most DDI entries are stubs");

    // Compute: raw Valhall submit is exercised via
    // Tools/winmali-compute-test (D3DKMTEscape). Wiring pfnDispatch +
    // D3D11DDI_DEVICEFUNCS to call the same escape is deferred until the
    // CSF submit path exists.
    return S_OK;
}

HRESULT APIENTRY
WinMaliUmdCloseAdapter(D3D10DDI_HADAPTER hAdapter)
{
    PWINMALI_UMD_ADAPTER adapter = (PWINMALI_UMD_ADAPTER)hAdapter.pDrvPrivate;
    if (adapter != NULL && adapter->Magic == WINMALI_UMD_ADAPTER_MAGIC) {
        adapter->Magic = 0;
        HeapFree(GetProcessHeap(), 0, adapter);
    }
    return S_OK;
}

HRESULT APIENTRY
WinMaliUmdGetSupportedVersions(
    D3D10DDI_HADAPTER   hAdapter,
    _Inout_ UINT32*     puEntries,
    _Out_writes_(*puEntries) UINT64* pSupportedDDIInterfaceVersions)
{
    static const UINT64 kVersions[] = {
        D3D11_0_DDI_INTERFACE_VERSION,
        D3D10_1_DDI_INTERFACE_VERSION,
        D3D10_0_DDI_INTERFACE_VERSION
    };
    const UINT32 need = (UINT32)(sizeof(kVersions) / sizeof(kVersions[0]));

    UNREFERENCED_PARAMETER(hAdapter);
    if (puEntries == NULL) return E_INVALIDARG;

    if (pSupportedDDIInterfaceVersions == NULL) {
        *puEntries = need;
        return S_OK;
    }
    if (*puEntries < need) {
        *puEntries = need;
        return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    for (UINT32 i = 0; i < need; ++i) {
        pSupportedDDIInterfaceVersions[i] = kVersions[i];
    }
    *puEntries = need;
    return S_OK;
}

HRESULT APIENTRY
WinMaliUmdGetCaps(
    D3D10DDI_HADAPTER                hAdapter,
    CONST D3D10_2DDIARG_GETCAPS*     pCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pCaps == NULL) return E_INVALIDARG;

    switch (pCaps->Type) {
    case D3D11DDICAPS_THREADING: {
        D3D11DDI_THREADING_CAPS* threadingCaps = (D3D11DDI_THREADING_CAPS*)pCaps->pData;
        if (threadingCaps == NULL || pCaps->DataSize < sizeof(*threadingCaps)) {
            return E_INVALIDARG;
        }
        threadingCaps->Caps = 0;  // no concurrent creates / command lists
        return S_OK;
    }
    case D3D11DDICAPS_SHADER: {
        D3D11DDI_SHADER_CAPS* shaderCaps = (D3D11DDI_SHADER_CAPS*)pCaps->pData;
        if (shaderCaps == NULL || pCaps->DataSize < sizeof(*shaderCaps)) {
            return E_INVALIDARG;
        }
        shaderCaps->Caps = 0;
        return S_OK;
    }
    default:
        return E_NOTIMPL;
    }
}

// ---------------------------------------------------------------------------
// Device-level stubs
// ---------------------------------------------------------------------------

HRESULT APIENTRY
WinMaliUmdDestroyDevice(D3D10DDI_HDEVICE hDevice)
{
    UNREFERENCED_PARAMETER(hDevice);
    return S_OK;
}

VOID APIENTRY
WinMaliUmdDefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice, UINT Slot,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemUP,
    UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Slot);
    UNREFERENCED_PARAMETER(pDstBox);
    UNREFERENCED_PARAMETER(pSysMemUP);
    UNREFERENCED_PARAMETER(RowPitch);
    UNREFERENCED_PARAMETER(DepthPitch);
    UNREFERENCED_PARAMETER(CopyFlags);
}

VOID APIENTRY
WinMaliUmdFlush(D3D10DDI_HDEVICE hDevice, UINT FlushFlags)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(FlushFlags);
}

VOID APIENTRY
WinMaliUmdCheckFormatSupport(
    D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, _Out_ UINT* pFormatSupport)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Format);
    if (pFormatSupport != NULL) *pFormatSupport = 0;
}

VOID APIENTRY
WinMaliUmdCheckMultisampleQualityLevels(
    D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT SampleCount,
    _Out_ UINT* pNumQualityLevels)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(SampleCount);
    if (pNumQualityLevels != NULL) *pNumQualityLevels = 0;
}

// ---------------------------------------------------------------------------
// OpenAdapter10_2 - the one and only exported entry point. D3D11
// LoadLibrary + GetProcAddress(WinMaliUmd.dll, "OpenAdapter10_2").
// ---------------------------------------------------------------------------

EXTERN_C HRESULT APIENTRY
OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenAdapter)
{
    PWINMALI_UMD_ADAPTER adapter;
    D3D10DDI_ADAPTERFUNCS* adapterFuncs;

    if (pOpenAdapter == NULL) return E_INVALIDARG;
    if (pOpenAdapter->pAdapterFuncs == NULL) return E_INVALIDARG;

    WINMALI_UMD_TRACE("OpenAdapter10_2 entered, requested ddi=%llu",
                      (unsigned long long)pOpenAdapter->Interface);

    adapter = (PWINMALI_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) return E_OUTOFMEMORY;

    adapter->Magic            = WINMALI_UMD_ADAPTER_MAGIC;
    adapter->RuntimeAdapter   = pOpenAdapter->hRTAdapter;
    adapter->AdapterCallbacks = *pOpenAdapter->pAdapterCallbacks;

    pOpenAdapter->hAdapter.pDrvPrivate = adapter;

    // Fill out the adapter-level function table the runtime uses.
    // The 10_2 variant already handles D3D10.x and D3D11.x negotiation.
    adapterFuncs = (D3D10DDI_ADAPTERFUNCS*)pOpenAdapter->pAdapterFuncs;
    RtlZeroMemory(adapterFuncs, sizeof(*adapterFuncs));
    adapterFuncs->pfnCalcPrivateDeviceSize = WinMaliUmdCalcPrivateDeviceSize;
    adapterFuncs->pfnCreateDevice          = WinMaliUmdCreateDevice;
    adapterFuncs->pfnCloseAdapter          = WinMaliUmdCloseAdapter;

    // Cap the reported interface to 11_0 even if the runtime asked for more.
    if (pOpenAdapter->Interface > WINMALI_UMD_DDI_VERSION) {
        pOpenAdapter->Interface = WINMALI_UMD_DDI_VERSION;
    }

    WINMALI_UMD_TRACE("OpenAdapter10_2 OK, capping interface at 0x%llx",
                      (unsigned long long)WINMALI_UMD_DDI_VERSION);
    return S_OK;
}

// ---------------------------------------------------------------------------
// Standard DLL entry
// ---------------------------------------------------------------------------

BOOL WINAPI
DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(lpvReserved);
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstance);
        break;
    }
    return TRUE;
}
