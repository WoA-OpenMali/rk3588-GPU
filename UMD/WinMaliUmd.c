

#include "WinMaliUmd.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// User-mode trace: OutputDebugString is delivered to debuggers that listen
// for it (user-mode WinDbg on the loader, kernel WinDbg with the right
// client/output settings, DebugView global capture, etc.). We also append
// to %%TEMP%%\WinMaliUmd.log so traces exist on disk without any debugger.
static void WinMaliUmdTraceV(const char *fmt, va_list ap)
{
    char msg[400];
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);

    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[WinMaliUmd] %s", msg);

    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    wchar_t tempDir[MAX_PATH];
    DWORD nDir = GetTempPathW(MAX_PATH, tempDir);
    if (nDir == 0 || nDir >= MAX_PATH) {
        return;
    }

    wchar_t path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sWinMaliUmd.log", tempDir);

    HANDLE h = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    char fileLine[528];
    _snprintf_s(fileLine, sizeof(fileLine), _TRUNCATE, "%s\r\n", line);
    DWORD n = (DWORD)strlen(fileLine);
    DWORD written = 0;
    WriteFile(h, fileLine, n, &written, NULL);
    CloseHandle(h);
}

static void WinMaliUmdTraceImpl(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    WinMaliUmdTraceV(fmt, ap);
    va_end(ap);
}

#define WINMALI_UMD_TRACE(fmt, ...) WinMaliUmdTraceImpl((fmt), ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Adapter-level DDI stubs
// ---------------------------------------------------------------------------

SIZE_T APIENTRY
WinMaliUmdCalcPrivateDeviceSize(
    D3D10DDI_HADAPTER                             hAdapter,
    CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE*      pArgs)
{
    WINMALI_UMD_TRACE("%s hAdapter.pDrvPrivate=%p pArgs=%p Interface=0x%x",
                        __FUNCTION__,
                        hAdapter.pDrvPrivate,
                        pArgs,
                        pArgs ? pArgs->Interface : 0);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    return sizeof(WINMALI_UMD_DEVICE);
}

HRESULT APIENTRY
WinMaliUmdCreateDevice(
    D3D10DDI_HADAPTER            hAdapter,
    D3D10DDIARG_CREATEDEVICE*    pArgs)
{
    WINMALI_UMD_TRACE("%s hAdapter.pDrvPrivate=%p pArgs=%p",
                        __FUNCTION__,
                        hAdapter.pDrvPrivate,
                        pArgs);

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
    WINMALI_UMD_TRACE("%s hAdapter.pDrvPrivate=%p",
                        __FUNCTION__,
                        hAdapter.pDrvPrivate);
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
    WINMALI_UMD_TRACE("%s puEntries=%p *puEntries=%u pSupportedDDIInterfaceVersions=%p",
                        __FUNCTION__,
                        puEntries,
                        puEntries ? *puEntries : 0,
                        pSupportedDDIInterfaceVersions);
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
    if (pCaps == NULL) return E_INVALIDARG;

    WINMALI_UMD_TRACE("%s Type=%u DataSize=%u pData=%p hAdapter.pDrvPrivate=%p",
                        __FUNCTION__,
                        (unsigned)pCaps->Type,
                        (unsigned)pCaps->DataSize,
                        pCaps->pData,
                        hAdapter.pDrvPrivate);

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
        if (pCaps->pData == NULL) {
            return E_INVALIDARG;
        }
        RtlZeroMemory(pCaps->pData, pCaps->DataSize);
        return S_OK;
    }
}

// ---------------------------------------------------------------------------
// Device-level stubs
// ---------------------------------------------------------------------------

HRESULT APIENTRY
WinMaliUmdDestroyDevice(D3D10DDI_HDEVICE hDevice)
{
    WINMALI_UMD_TRACE("%s hDevice.pDrvPrivate=%p",
                        __FUNCTION__,
                        hDevice.pDrvPrivate);
    return S_OK;
}

VOID APIENTRY
WinMaliUmdDefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice, UINT Slot,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemUP,
    UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    WINMALI_UMD_TRACE("%s Slot=%u CopyFlags=0x%x RowPitch=%u DepthPitch=%u pSysMemUP=%p",
                        __FUNCTION__,
                        Slot,
                        CopyFlags,
                        RowPitch,
                        DepthPitch,
                        pSysMemUP);
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
    WINMALI_UMD_TRACE("%s FlushFlags=0x%x", __FUNCTION__, FlushFlags);
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(FlushFlags);
}

VOID APIENTRY
WinMaliUmdCheckFormatSupport(
    D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, _Out_ UINT* pFormatSupport)
{
    WINMALI_UMD_TRACE("%s Format=%u", __FUNCTION__, (unsigned)Format);
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Format);
    if (pFormatSupport != NULL) *pFormatSupport = 0;
}

VOID APIENTRY
WinMaliUmdCheckMultisampleQualityLevels(
    D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT SampleCount,
    _Out_ UINT* pNumQualityLevels)
{
    WINMALI_UMD_TRACE("%s Format=%u SampleCount=%u",
                        __FUNCTION__,
                        (unsigned)Format,
                        SampleCount);
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
    D3D10_2DDI_ADAPTERFUNCS* adapterFuncs;

    if (pOpenAdapter == NULL) return E_INVALIDARG;
    if (pOpenAdapter->pAdapterFuncs == NULL) return E_INVALIDARG;

    WINMALI_UMD_TRACE("%s hRTAdapter=%p Version=%u Interface=0x%llx pAdapterFuncs=%p pAdapterCallbacks=%p",
                        __FUNCTION__,
                        pOpenAdapter->hRTAdapter.handle,
                        pOpenAdapter->Version,
                        (unsigned long long)pOpenAdapter->Interface,
                        pOpenAdapter->pAdapterFuncs,
                        pOpenAdapter->pAdapterCallbacks);

    adapter = (PWINMALI_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) return E_OUTOFMEMORY;

    adapter->Magic            = WINMALI_UMD_ADAPTER_MAGIC;
    adapter->RuntimeAdapter   = pOpenAdapter->hRTAdapter;
    adapter->AdapterCallbacks = *pOpenAdapter->pAdapterCallbacks;

    pOpenAdapter->hAdapter.pDrvPrivate = adapter;

    // pAdapterFuncs and pAdapterFuncs_2 are the same pointer (union). For
    // OpenAdapter10_2 the runtime expects D3D10_2DDI_ADAPTERFUNCS, which adds
    // pfnGetSupportedVersions and pfnGetCaps; leaving those NULL breaks D3D11
    // bring-up right after KMD StartDevice (dxgk tears the stack down).
    adapterFuncs = (D3D10_2DDI_ADAPTERFUNCS*)pOpenAdapter->pAdapterFuncs;
    RtlZeroMemory(adapterFuncs, sizeof(*adapterFuncs));
    adapterFuncs->pfnCalcPrivateDeviceSize = WinMaliUmdCalcPrivateDeviceSize;
    adapterFuncs->pfnCreateDevice          = WinMaliUmdCreateDevice;
    adapterFuncs->pfnCloseAdapter          = WinMaliUmdCloseAdapter;
    adapterFuncs->pfnGetSupportedVersions  = WinMaliUmdGetSupportedVersions;
    adapterFuncs->pfnGetCaps               = WinMaliUmdGetCaps;

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
    UNREFERENCED_PARAMETER(lpvReserved);
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        WINMALI_UMD_TRACE("%s DLL_PROCESS_ATTACH hInstance=%p",
                            __FUNCTION__,
                            hInstance);
        DisableThreadLibraryCalls(hInstance);
        break;
    case DLL_PROCESS_DETACH:
        WINMALI_UMD_TRACE("%s DLL_PROCESS_DETACH hInstance=%p",
                            __FUNCTION__,
                            hInstance);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    default:
        break;
    }
    return TRUE;
}
