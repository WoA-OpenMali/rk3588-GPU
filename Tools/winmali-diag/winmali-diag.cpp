#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct Options {
    std::wstring adapterSubstring;
    bool testAllAdapters = false;
    bool includeSoftware = false;
    bool extended = true;
    bool dumpUmdLog = true;
    UINT flushCount = 4;
};

static std::wstring ToLowerCopy(const std::wstring& value)
{
    std::wstring lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
    return lowered;
}

static bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty()) {
        return true;
    }

    return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::wstring::npos;
}

static void PrintUsage()
{
    std::wcout
        << L"winmali-diag [--adapter-substr <text>] [--all] [--include-software] [--basic] [--no-umd-log] [--flush-count <n>]\n"
        << L"  --adapter-substr <text>  Only probe adapters whose description contains <text>.\n"
        << L"  --all                    Probe every matching adapter instead of only the best candidate.\n"
        << L"  --include-software       Include software/RDP adapters in probing.\n"
        << L"  --basic                  Stop after device creation, caps, and flush tests.\n"
        << L"  --no-umd-log             Skip tailing %TEMP%\\WinMaliUmd.log at the end.\n"
        << L"  --flush-count <n>        Number of immediate-context flushes to issue (default 4).\n";
}

static bool ParseUint(const wchar_t* text, UINT* value)
{
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || parsed > UINT_MAX) {
        return false;
    }
    *value = static_cast<UINT>(parsed);
    return true;
}

static bool ParseArgs(int argc, wchar_t** argv, Options* options)
{
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index];
        if (arg == L"--adapter-substr") {
            if (index + 1 >= argc) {
                std::wcerr << L"Missing value for --adapter-substr\n";
                return false;
            }
            options->adapterSubstring = argv[++index];
        } else if (arg == L"--all") {
            options->testAllAdapters = true;
        } else if (arg == L"--include-software") {
            options->includeSoftware = true;
        } else if (arg == L"--basic") {
            options->extended = false;
        } else if (arg == L"--no-umd-log") {
            options->dumpUmdLog = false;
        } else if (arg == L"--flush-count") {
            if (index + 1 >= argc) {
                std::wcerr << L"Missing value for --flush-count\n";
                return false;
            }
            if (!ParseUint(argv[++index], &options->flushCount)) {
                std::wcerr << L"Invalid --flush-count value\n";
                return false;
            }
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            PrintUsage();
            ExitProcess(0);
        } else {
            std::wcerr << L"Unknown argument: " << arg << L"\n";
            return false;
        }
    }

    return true;
}

static std::wstring HrToString(HRESULT hr)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    DWORD chars = FormatMessageW(
        flags,
        nullptr,
        static_cast<DWORD>(hr),
        langId,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstringstream stream;
    stream << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << static_cast<unsigned long>(hr);
    if (chars != 0 && buffer != nullptr) {
        while (chars != 0 && (buffer[chars - 1] == L'\r' || buffer[chars - 1] == L'\n')) {
            buffer[--chars] = L'\0';
        }
        stream << L" (" << buffer << L")";
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return stream.str();
}

static std::wstring GetTempPathString()
{
    wchar_t tempPath[MAX_PATH] = {};
    DWORD chars = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
    if (chars == 0 || chars >= std::size(tempPath)) {
        return L".";
    }
    return std::wstring(tempPath, chars);
}

static void DumpUmdLogTail()
{
    const std::wstring path = GetTempPathString() + L"WinMaliUmd.log";
    std::wifstream input(path);
    input.imbue(std::locale(""));
    if (!input.is_open()) {
        std::wcout << L"UMD log: not found at " << path << L"\n";
        return;
    }

    std::vector<std::wstring> lines;
    std::wstring line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    std::wcout << L"UMD log tail: " << path << L"\n";
    if (lines.empty()) {
        std::wcout << L"  <empty>\n";
        return;
    }

    const size_t start = (lines.size() > 20) ? (lines.size() - 20) : 0;
    for (size_t index = start; index < lines.size(); ++index) {
        std::wcout << L"  " << lines[index] << L"\n";
    }
}

static void PrintAdapterDesc(UINT index, const DXGI_ADAPTER_DESC1& desc)
{
    std::wcout
        << L"[" << index << L"] " << desc.Description
        << L" vendor=0x" << std::hex << std::setw(4) << std::setfill(L'0') << desc.VendorId
        << L" device=0x" << std::setw(4) << desc.DeviceId
        << std::dec
        << L" flags=0x" << std::hex << desc.Flags << std::dec
        << L" dedicatedVideoMB=" << (desc.DedicatedVideoMemory / (1024ull * 1024ull))
        << L" sharedSystemMB=" << (desc.SharedSystemMemory / (1024ull * 1024ull))
        << L"\n";
}

static bool AdapterLooksLikeWinMali(const DXGI_ADAPTER_DESC1& desc)
{
    const std::wstring description(desc.Description);
    return ContainsInsensitive(description, L"mali")
        || ContainsInsensitive(description, L"winmali");
}

static HRESULT ProbeDeviceCaps(ID3D11Device* device)
{
    static const DXGI_FORMAT formats[] = {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R16G16_UNORM,
    };
    static const UINT sampleCounts[] = {1, 2, 4, 8};

    D3D11_FEATURE_DATA_THREADING threading = {};
    HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    std::wcout << L"  CheckFeatureSupport(Threading): " << HrToString(hr);
    if (SUCCEEDED(hr)) {
        std::wcout << L" driverConcurrentCreates=" << threading.DriverConcurrentCreates
                   << L" driverCommandLists=" << threading.DriverCommandLists;
    }
    std::wcout << L"\n";

    for (DXGI_FORMAT format : formats) {
        UINT support = 0;
        hr = device->CheckFormatSupport(format, &support);
        std::wcout << L"  CheckFormatSupport(" << static_cast<unsigned>(format) << L"): "
                   << HrToString(hr);
        if (SUCCEEDED(hr)) {
            std::wcout << L" bits=0x" << std::hex << support << std::dec;
        }
        std::wcout << L"\n";
    }

    for (UINT sampleCount : sampleCounts) {
        UINT qualityLevels = 0;
        hr = device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, sampleCount, &qualityLevels);
        std::wcout << L"  CheckMultisampleQualityLevels(B8G8R8A8, " << sampleCount << L"): "
                   << HrToString(hr);
        if (SUCCEEDED(hr)) {
            std::wcout << L" qualityLevels=" << qualityLevels;
        }
        std::wcout << L"\n";
    }

    return S_OK;
}

static HRESULT ProbeExtendedPath(ID3D11Device* device, ID3D11DeviceContext* context)
{
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = 64;
    textureDesc.Height = 64;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &texture);
    std::wcout << L"  CreateTexture2D: " << HrToString(hr) << L"\n";
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = device->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
    std::wcout << L"  CreateRenderTargetView: " << HrToString(hr) << L"\n";
    if (FAILED(hr)) {
        return hr;
    }

    const FLOAT colors[3][4] = {
        {0.85f, 0.10f, 0.10f, 1.0f},
        {0.10f, 0.85f, 0.10f, 1.0f},
        {0.10f, 0.10f, 0.85f, 1.0f},
    };

    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    for (const auto& color : colors) {
        context->ClearRenderTargetView(rtv.Get(), color);
    }
    context->Flush();
    std::wcout << L"  Extended path: OMSetRenderTargets + 3x ClearRenderTargetView + Flush issued\n";

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = 4096;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    unsigned char initData[4096] = {};
    for (size_t index = 0; index < std::size(initData); ++index) {
        initData[index] = static_cast<unsigned char>(index & 0xFFu);
    }

    D3D11_SUBRESOURCE_DATA subresource = {};
    subresource.pSysMem = initData;
    subresource.SysMemPitch = sizeof(initData);

    ComPtr<ID3D11Buffer> buffer;
    hr = device->CreateBuffer(&bufferDesc, &subresource, &buffer);
    std::wcout << L"  CreateBuffer: " << HrToString(hr) << L"\n";
    if (FAILED(hr)) {
        return hr;
    }

    const UINT stride = 16;
    const UINT offset = 0;
    ID3D11Buffer* vertexBuffer = buffer.Get();
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->Flush();
    std::wcout << L"  Extended path: IASetVertexBuffers + Flush issued\n";
    return S_OK;
}

static HRESULT ProbeAdapter(IDXGIAdapter1* adapter, const DXGI_ADAPTER_DESC1& desc, const Options& options)
{
    static const D3D_FEATURE_LEVEL requestedFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    std::wcout << L"Probing adapter: " << desc.Description << L"\n";

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_9_1;

    const UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        createFlags,
        requestedFeatureLevels,
        static_cast<UINT>(std::size(requestedFeatureLevels)),
        D3D11_SDK_VERSION,
        &device,
        &createdFeatureLevel,
        &context);

    std::wcout << L"  D3D11CreateDevice: " << HrToString(hr) << L"\n";
    if (FAILED(hr)) {
        return hr;
    }

    std::wcout << L"  FeatureLevel: 0x" << std::hex << static_cast<unsigned>(createdFeatureLevel) << std::dec << L"\n";

    hr = ProbeDeviceCaps(device.Get());
    if (FAILED(hr)) {
        return hr;
    }

    for (UINT flushIndex = 0; flushIndex < options.flushCount; ++flushIndex) {
        context->Flush();
    }
    std::wcout << L"  ImmediateContext::Flush x" << options.flushCount << L" completed\n";

    if (options.extended) {
        hr = ProbeExtendedPath(device.Get(), context.Get());
        if (FAILED(hr)) {
            std::wcout << L"  Extended path stopped at " << HrToString(hr) << L"\n";
        }
    }

    return S_OK;
}

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage();
        return 2;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::wcerr << L"CoInitializeEx failed: " << HrToString(hr) << L"\n";
        return 1;
    }

    bool sawMatchingAdapter = false;
    bool anyProbeSucceeded = false;

    ComPtr<IDXGIFactory6> factory6;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory6));
    if (FAILED(hr)) {
        std::wcerr << L"CreateDXGIFactory1(IDXGIFactory6) failed: " << HrToString(hr) << L"\n";
        CoUninitialize();
        return 1;
    }

    std::vector<ComPtr<IDXGIAdapter1>> selectedAdapters;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory6->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            std::wcerr << L"EnumAdapterByGpuPreference failed at index " << index << L": "
                       << HrToString(hr) << L"\n";
            break;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        hr = adapter->GetDesc1(&desc);
        if (FAILED(hr)) {
            std::wcerr << L"GetDesc1 failed for adapter " << index << L": " << HrToString(hr) << L"\n";
            continue;
        }

        PrintAdapterDesc(index, desc);

        const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const bool matchesSubstring = ContainsInsensitive(desc.Description, options.adapterSubstring);
        if (!matchesSubstring) {
            continue;
        }
        if (isSoftware && !options.includeSoftware) {
            continue;
        }

        sawMatchingAdapter = true;
        if (options.testAllAdapters) {
            selectedAdapters.push_back(adapter);
            continue;
        }

        if (AdapterLooksLikeWinMali(desc)) {
            selectedAdapters.clear();
            selectedAdapters.push_back(adapter);
            break;
        }
        if (selectedAdapters.empty()) {
            selectedAdapters.push_back(adapter);
        }
    }

    if (!sawMatchingAdapter) {
        std::wcerr << L"No adapters matched the current filter.\n";
        if (options.dumpUmdLog) {
            DumpUmdLogTail();
        }
        CoUninitialize();
        return 3;
    }

    for (const auto& adapter : selectedAdapters) {
        DXGI_ADAPTER_DESC1 desc = {};
        hr = adapter->GetDesc1(&desc);
        if (FAILED(hr)) {
            std::wcerr << L"GetDesc1 failed during probe: " << HrToString(hr) << L"\n";
            continue;
        }

        hr = ProbeAdapter(adapter.Get(), desc, options);
        if (SUCCEEDED(hr)) {
            anyProbeSucceeded = true;
        } else {
            std::wcerr << L"Probe failed for adapter " << desc.Description << L": " << HrToString(hr) << L"\n";
        }
    }

    if (options.dumpUmdLog) {
        DumpUmdLogTail();
    }

    CoUninitialize();
    return anyProbeSucceeded ? 0 : 4;
}
