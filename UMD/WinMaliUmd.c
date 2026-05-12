

#include "WinMaliUmd.h"

/* Must match d3dumddi.h D3DDDICB_QUERYADAPTERINFO — we avoid including d3dumddi.h
 * here because it pulls D3D9 types in an order that breaks plain C + d3d10umddi. */
typedef struct WINMALI_D3DDDICB_QUERYADAPTERINFO {
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
} WINMALI_D3DDDICB_QUERYADAPTERINFO;

typedef HRESULT (APIENTRY *WINMALI_PFND3DDDI_QUERYADAPTERINFOCB)(
    HANDLE hAdapter, WINMALI_D3DDDICB_QUERYADAPTERINFO *pQueryAdapterInfo);

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

static void WinMaliUmdPatchDeviceFuncs(D3D11DDI_DEVICEFUNCS *df);

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
    if (pArgs == NULL || pArgs->hRTDevice.handle == NULL) {
        return E_INVALIDARG;
    }
    if (pArgs->Interface != D3D11_0_DDI_INTERFACE_VERSION) {
        return E_NOINTERFACE;
    }
    if (pArgs->p11DeviceFuncs == NULL) {
        return E_INVALIDARG;
    }
    if (adapter->KmdInfo.Magic != WINMALI_ADAPTER_MAGIC) {
        WINMALI_UMD_TRACE("CreateDevice: missing/invalid KMD UMDRIVERPRIVATE (magic=0x%llx)",
                          (unsigned long long)adapter->KmdInfo.Magic);
        return E_FAIL;
    }

    device = (PWINMALI_UMD_DEVICE)pArgs->hDrvDevice.pDrvPrivate;
    if (device == NULL) {
        return E_INVALIDARG;
    }
    memset(device, 0, sizeof(*device));
    device->Magic = WINMALI_UMD_DEVICE_MAGIC;
    device->Adapter = adapter;
    device->RuntimeDevice = pArgs->hRTDevice;

    WinMaliUmdFillD3d11DeviceFuncs(pArgs->p11DeviceFuncs);
    WinMaliUmdPatchDeviceFuncs(pArgs->p11DeviceFuncs);

    WINMALI_UMD_TRACE("CreateDevice: D3D11_0 device funcs installed (stubs + trace hooks)");

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
        //
        // Route B: still no concurrent creates and no driver-built command
        // lists, but the runtime treats Caps=0 as "no threading at all"
        // which blocks D3D11 device creation when DWM is the caller.
        // We grant the lowest meaningful tier, which lets the runtime know
        // there is no driver-side parallelism but it can still call us.
        //
        threadingCaps->Caps = 0;
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
    case D3D11DDICAPS_3DPIPELINESUPPORT: {
        D3D11DDI_3DPIPELINESUPPORT_CAPS* pipeCaps =
            (D3D11DDI_3DPIPELINESUPPORT_CAPS*)pCaps->pData;
        if (pipeCaps == NULL || pCaps->DataSize < sizeof(*pipeCaps)) {
            return E_INVALIDARG;
        }
        pipeCaps->Caps = 0;
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
    PWINMALI_UMD_DEVICE device = (PWINMALI_UMD_DEVICE)hDevice.pDrvPrivate;

    WINMALI_UMD_TRACE("%s hDevice.pDrvPrivate=%p",
                        __FUNCTION__,
                        hDevice.pDrvPrivate);
    if (device != NULL && device->Magic == WINMALI_UMD_DEVICE_MAGIC) {
        device->Magic = 0;
    }
    return S_OK;
}

VOID APIENTRY
WinMaliUmdDefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemUP,
    UINT RowPitch,
    UINT DepthPitch)
{
    WINMALI_UMD_TRACE("%s Subresource=%u RowPitch=%u DepthPitch=%u pSysMemUP=%p",
                        __FUNCTION__,
                        Subresource,
                        RowPitch,
                        DepthPitch,
                        pSysMemUP);
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hResource);
    UNREFERENCED_PARAMETER(pDstBox);
    UNREFERENCED_PARAMETER(pSysMemUP);
    UNREFERENCED_PARAMETER(RowPitch);
    UNREFERENCED_PARAMETER(DepthPitch);
}

VOID APIENTRY
WinMaliUmdFlush(D3D10DDI_HDEVICE hDevice)
{
    WINMALI_UMD_TRACE("%s", __FUNCTION__);
    UNREFERENCED_PARAMETER(hDevice);
}

VOID APIENTRY
WinMaliUmdCheckFormatSupport(
    D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, _Out_ UINT* pFormatSupport)
{
    UNREFERENCED_PARAMETER(hDevice);
    if (pFormatSupport == NULL) {
        return;
    }

    //
    // Route B: report a baseline "passthrough" capability set for the
    // formats DWM and the redirection bitmap path care about. Returning 0
    // for every format makes the D3D11 runtime conclude the adapter is
    // unusable, which in turn causes IDXGIFactory::EnumAdapters to skip us
    // - and DWM logs "D3D11: Removing Device" right after. We don't have
    // texture sampling or blending in HW yet, so we only claim RENDERTARGET
    // and (when available) the WDDM 3.0 displayable + scan-out bits.
    //
    UINT bits = 0;
    switch (Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        bits = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
#if defined(D3D11_1DDI_FORMAT_SUPPORT_DISPLAY)
        bits |= D3D11_1DDI_FORMAT_SUPPORT_DISPLAY;
#endif
#if defined(D3DWDDM3_0DDI_FORMAT_SUPPORT_DISPLAYABLE)
        bits |= D3DWDDM3_0DDI_FORMAT_SUPPORT_DISPLAYABLE;
#endif
        break;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
        bits = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
        break;
    default:
        bits = 0;
        break;
    }
    *pFormatSupport = bits;
    WINMALI_UMD_TRACE("%s Format=%u -> 0x%x", __FUNCTION__, (unsigned)Format, bits);
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

static void WinMaliUmdPatchDeviceFuncs(D3D11DDI_DEVICEFUNCS *df)
{
    if (df == NULL) {
        return;
    }
    df->pfnDestroyDevice = WinMaliUmdDestroyDevice;
    df->pfnDefaultConstantBufferUpdateSubresourceUP =
        WinMaliUmdDefaultConstantBufferUpdateSubresourceUP;
    df->pfnFlush = WinMaliUmdFlush;
    df->pfnCheckFormatSupport = WinMaliUmdCheckFormatSupport;
    df->pfnCheckMultisampleQualityLevels = WinMaliUmdCheckMultisampleQualityLevels;
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
    memset(&adapter->KmdInfo, 0, sizeof(adapter->KmdInfo));

    if (pOpenAdapter->pAdapterCallbacks == NULL
        || pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfoCb == NULL) {
        WINMALI_UMD_TRACE("OpenAdapter: missing pfnQueryAdapterInfoCb");
        HeapFree(GetProcessHeap(), 0, adapter);
        return E_FAIL;
    } else {
        WINMALI_D3DDDICB_QUERYADAPTERINFO qai;
        HRESULT qhr;
        WINMALI_PFND3DDDI_QUERYADAPTERINFOCB pfnQai =
            (WINMALI_PFND3DDDI_QUERYADAPTERINFOCB)
                pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfoCb;

        memset(&qai, 0, sizeof(qai));
        qai.pPrivateDriverData = &adapter->KmdInfo;
        qai.PrivateDriverDataSize = sizeof(adapter->KmdInfo);
        qhr = pfnQai((HANDLE)pOpenAdapter->hRTAdapter.handle, &qai);
        if (FAILED(qhr)) {
            WINMALI_UMD_TRACE("OpenAdapter: pfnQueryAdapterInfoCb hr=0x%08lx",
                              (unsigned long)qhr);
            HeapFree(GetProcessHeap(), 0, adapter);
            return qhr;
        }
        if (adapter->KmdInfo.Magic != WINMALI_ADAPTER_MAGIC) {
            WINMALI_UMD_TRACE(
                "OpenAdapter: not a WinMali KMD (adapter private magic 0x%llx)",
                (unsigned long long)adapter->KmdInfo.Magic);
            HeapFree(GetProcessHeap(), 0, adapter);
            return E_FAIL;
        }
    }

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
