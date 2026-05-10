#include "WinMaliKmd.h"
#include "WinMaliDxgkInitFill.h"
#include "WinMaliMmu.h"
#include "winmali_shader_programs.h"

// Minor version bumps let the UMD refuse to talk to a mismatched KMD.
#define WINMALI_KMD_MAJOR   0
#define WINMALI_KMD_MINOR   1

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// RK3588 has exactly one Mali-G610 adapter, so a singleton is safe. If a
// future SoC with more than one Mali adapter wires into this driver,
// switch to a linked list keyed by DxgkHandle.
static PWINMALI_ADAPTER g_WinMaliAdapter = NULL;

PWINMALI_ADAPTER
WinMaliAdapterFromContext(_In_ const VOID* Context)
{
    PWINMALI_ADAPTER adapter = (PWINMALI_ADAPTER)Context;
    if (adapter == NULL || adapter->Magic != WINMALI_ADAPTER_CONTEXT_MAGIC) {
        return NULL;
    }
    return adapter;
}

// RtlGetVersion + unconditional DbgPrint so bring-up logs still show OS
// build / DDI / init-table size even if ETW or trace macros are filtered.
static VOID
WinMaliDiagPrintOsAndDxgk_(_In_ ULONG ddiVersion, _In_ ULONG initBytes)
{
    RTL_OSVERSIONINFOW ver;

    ver.dwOSVersionInfoSize = sizeof(ver);
    if (!NT_SUCCESS(RtlGetVersion(&ver))) {
        DbgPrint("[WinMali] pre-Dxgk RtlGetVersion failed ddi=0x%lx init=%lu\n",
                 ddiVersion, initBytes);
        return;
    }

    DbgPrint("[WinMali] pre-Dxgk os=%lu.%lu build=%lu ddi=0x%lx init=%lu\n",
             ver.dwMajorVersion,
             ver.dwMinorVersion,
             ver.dwBuildNumber,
             ddiVersion,
             initBytes);
}

// Resolve the per-adapter context from what dxgkrnl passes as hAdapter.
//
// On most DDIs, hAdapter is DXGKRNL_INTERFACE::DeviceHandle (saved as
// adapter->DxgkHandle in StartDevice). On DxgkDdiQueryAdapterInfo (and a
// few other paths), the OS passes the miniport device context pointer
// instead — the same value as *MiniportDeviceContext from AddDevice.
// Both must match for a singleton adapter.
//
PWINMALI_ADAPTER
WinMaliAdapterFromDxgkHandle(_In_opt_ const VOID* hAdapter)
{
    PWINMALI_ADAPTER adapter = g_WinMaliAdapter;
    if (adapter == NULL || hAdapter == NULL) {
        return NULL;
    }
    if (adapter->DxgkHandle == (HANDLE)hAdapter) {
        return adapter;
    }
    if ((PVOID)adapter == (PVOID)hAdapter) {
        return adapter;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// DriverEntry - the only symbol the loader looks up by name.
// ---------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    DRIVER_INITIALIZATION_DATA init;

    status = WinMaliTraceRegister();
    if (!NT_SUCCESS(status)) {
        // WinMaliTraceRegister already logged via DbgPrintEx in this case.
        return status;
    }

    WINMALI_TRACE(
        "DriverEntry (major=%u minor=%u cap_stamp=%u)",
        WINMALI_KMD_MAJOR,
        WINMALI_KMD_MINOR,
        WINMALI_KMD_CAP_STAMP);

    RtlZeroMemory(&init, sizeof(init));
    WinMaliDxgkPatchInitializationData(&init);

    WinMaliDiagPrintOsAndDxgk_(init.Version, (ULONG)sizeof(init));

    WINMALI_TRACE(
        "DxgkInitialize (DDI version 0x%x, DRIVER_INITIALIZATION_DATA %u bytes)",
        init.Version,
        (ULONG)sizeof(init));

    status = DxgkInitialize(DriverObject, RegistryPath, &init);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("DxgkInitialize failed: 0x%08x", status);
        WinMaliTraceUnregister();
        return status;
    }

    WINMALI_TRACE("DriverEntry OK");
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Device lifecycle
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
WinMaliKmdAddDevice(
    _In_     CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Outptr_ PVOID*               MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter;

    WINMALI_ENTER();

    if (MiniportDeviceContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    adapter = (PWINMALI_ADAPTER)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*adapter), WINMALI_POOL_TAG);
    if (adapter == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(adapter, sizeof(*adapter));
    adapter->Magic                = WINMALI_ADAPTER_CONTEXT_MAGIC;
    adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    *MiniportDeviceContext        = adapter;

    // Publish the singleton so handle-based callbacks (Escape,
    // CreateDevice) can find the miniport context from an opaque
    // DXGK adapter handle.
    g_WinMaliAdapter = adapter;

    WINMALI_TRACE("AddDevice OK (ctx=%p pdo=%p)", adapter, PhysicalDeviceObject);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdStartDevice(
    _In_  const PVOID            MiniportDeviceContext,
    _In_  PDXGK_START_INFO       DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE     DxgkInterface,
    _Out_ PULONG                 NumberOfVideoPresentSources,
    _Out_ PULONG                 NumberOfChildren)
{
    NTSTATUS         status;
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);

    WINMALI_ENTER();
    WINMALI_TRACE("StartDevice PnP (ctx=%p)", MiniportDeviceContext);

    if (adapter == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    adapter->DxgkStartInfo = *DxgkStartInfo;
    adapter->DxgkInterface = *DxgkInterface;
    adapter->DxgkHandle    = DxgkInterface->DeviceHandle;

    // Hardware start: parse resources, map MMIO, probe GPU_ID. DXGK
    // connects the interrupt from DRIVER_INITIALIZATION_DATA.
    //
    // Any failure here returns an error so DXGK correctly fails
    // IRP_MN_START_DEVICE and puts us back in the "driver failed to
    // start" error state in Device Manager - much easier to debug than
    // a silently-loaded-but-broken driver.
    status = WinMaliParseResources(adapter);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("WinMaliParseResources failed 0x%08x", status);
        return status;
    }

    status = WinMaliBringupHardware(adapter);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("WinMaliBringupHardware failed 0x%08x", status);
        // Continue: escape still reports useful info about what
        // we _did_ parse. But flag it as not-diag-passed.
        adapter->AdapterFlags &= ~WINMALI_ADAPTER_FLAG_DIAG_PASSED;
    } else {
        adapter->AdapterFlags |= WINMALI_ADAPTER_FLAG_DIAG_PASSED;
    }

    // Linux: panthor_mmu_init -> panthor_fw_init. MMU scratch heap + AS1,
    // then WinMaliFwInit (firmware + AS0 + MCU) after IRQ connect.
    if (NT_SUCCESS(status) && adapter->GpuRegsMapped) {
        NTSTATUS mmuStatus = WinMaliMmuInit(adapter);
        if (!NT_SUCCESS(mmuStatus)) {
            WINMALI_WARN("WinMaliMmuInit failed 0x%08x (MMU scratch / AS bind not active)", mmuStatus);
        }
    }

    status = WinMaliConnectInterrupt(adapter);
    if (!NT_SUCCESS(status)) {
        WINMALI_WARN("WinMaliConnectInterrupt failed 0x%08x", status);
    }

    // FwInit polls for MCU boot; it does not depend on ConnectInterrupt success.
    if (adapter->GpuRegsMapped) {
        NTSTATUS fwStatus = WinMaliFwInit(adapter);
        if (!NT_SUCCESS(fwStatus)) {
            WINMALI_WARN("WinMaliFwInit failed 0x%08x (CSF firmware not active)", fwStatus);
        }
    }

    adapter->StartedDevice             = TRUE;
    adapter->StartDeviceEverSucceeded  = TRUE;
    //
    // PrimaryConnector is currently a fixed UID we use as both the
    // ChildUid in QueryChildRelations and the VidPnTargetId in our one
    // recommended path. UID 0 is fine; using 1 confused some dxgkrnl
    // versions because the source/target IDs are also zero-based. The
    // VOP2 connector ID (e.g. Vop2DispConnHdmi0=0) maps to this once we
    // wire vop2connectors.c.
    //
    adapter->PrimaryConnector = 0;
    Rk3588DispCaptureGopFb(adapter);

    //
    // Sources: one VidPN source (the desktop). We always claim 1 so the
    // OS can build a "blank desktop" topology even if GOP capture failed.
    // Children: only advertise the connector child if GOP capture worked,
    // otherwise dxgkrnl will probe a child that has no monitor mode and
    // unload us. With no children we behave as a pure render adapter.
    //
    if (NumberOfVideoPresentSources != NULL) {
        *NumberOfVideoPresentSources = 1;
    }
    if (NumberOfChildren != NULL) {
        *NumberOfChildren = adapter->Gop.Valid ? 1u : 0u;
    }

    WINMALI_TRACE("StartDevice OK: sources=1 children=%u gop_valid=%u "
                  "mmio_mapped=%u irq_ok=%u",
                  adapter->Gop.Valid ? 1u : 0u,
                  (ULONG)adapter->Gop.Valid,
                  adapter->GpuRegsMapped,
                  adapter->InterruptConnected);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdStopDevice(_In_ const PVOID MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    WINMALI_ENTER();
    if (adapter != NULL) {
        Rk3588DispReleaseGopFb(adapter);
        WinMaliDisconnectInterrupt(adapter);
        WinMaliFwTeardown(adapter);
        adapter->GpuFwParkedForD3 = FALSE;
        WinMaliMmuTeardown(adapter);
        WinMaliTeardownHardware(adapter);
        adapter->StartedDevice = FALSE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdRemoveDevice(_In_ const PVOID MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    WINMALI_ENTER();
    if (adapter != NULL) {
        if (!adapter->StartDeviceEverSucceeded) {
            WINMALI_WARN(
                "RemoveDevice: StartDevice never reached success (ctx=%p) -- "
                "dxgk/PnP failed before or during start",
                adapter);
        }
        // Defensive - StopDevice should already have run, but be
        // idempotent so surprise removal still unmaps MMIO.
        Rk3588DispReleaseGopFb(adapter);
        WinMaliDisconnectInterrupt(adapter);
        WinMaliFwTeardown(adapter);
        adapter->GpuFwParkedForD3 = FALSE;
        WinMaliMmuTeardown(adapter);
        WinMaliTeardownHardware(adapter);
        if (g_WinMaliAdapter == adapter) {
            g_WinMaliAdapter = NULL;
        }
        adapter->Magic = 0;
        ExFreePoolWithTag(adapter, WINMALI_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

VOID
WinMaliKmdDdiUnload(VOID)
{
    WINMALI_TRACE("DdiUnload");
    WinMaliTraceUnregister();
}

VOID
WinMaliKmdDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    WINMALI_TRACE("DriverUnload");
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
WinMaliKmdQueryAdapterInfo(
    _In_ const HANDLE                    hAdapter,
    _In_ const DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    WINMALI_ENTER();

    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    WINMALI_TRACE("QueryAdapterInfo type=%d size=%u",
                  pQueryAdapterInfo->Type,
                  pQueryAdapterInfo->OutputDataSize);

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS* caps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;
        if (caps == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
            return STATUS_INVALID_PARAMETER;
        }
        RtlZeroMemory(caps, sizeof(*caps));

        //
        // Full-graphics caps. Two things matter most for "Win11 will leave
        // me loaded as the primary adapter":
        //   1. SupportNonVGA + SectionBackedPrimary together tell dxgkrnl
        //      we participate in the modern (WDDM 2.x section-backed)
        //      primary-surface model. Without these, dxgkrnl assumes
        //      we're a legacy adapter that can't host the desktop.
        //   2. Non-zero MaxAllocationListSlotId + ApertureSegmentCommitLimit
        //      flag us as having allocatable GPU memory.
        // The render-only sample I cribbed earlier had these zeroed which is
        // exactly what was provoking dxgkrnl to fall back to MS Basic Display.
        //
        caps->WDDMVersion = DXGKDDI_WDDMv2_4;
        caps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;

        caps->SchedulingCaps.VSyncPowerSaveAware = 1;
        caps->SchedulingCaps.MultiEngineAware    = 1;
        caps->SchedulingCaps.PreemptionAware     = 1;

        //
        // SectionBackedPrimary: we expose primary surfaces as section objects.
        // PagingNode=0: node 0 also handles paging (we only have one node).
        // IoMmuSupported=0: we don't go through the system IOMMU; the GPU has
        // its own MMU which we'll plumb later.
        //
        caps->MemoryManagementCaps.SectionBackedPrimary = 1;
        caps->MemoryManagementCaps.PagingNode           = 0;
        caps->MemoryManagementCaps.IoMmuSupported       = 0;

        caps->MaxAllocationListSlotId       = 7;
        caps->ApertureSegmentCommitLimit    = 64 * 1024 * 1024;

        caps->MaxQueuedFlipOnVSync           = 1;
        caps->FlipCaps.FlipOnVSyncMmIo       = 1;
        caps->FlipCaps.FlipOnVSyncWithNoWait = 0;
        caps->FlipCaps.FlipIndependent       = 0;

        //
        // SupportNonVGA tells dxgkrnl "I am NOT a VGA-compatible adapter",
        // i.e. don't try to talk to me through legacy 0x3D4 VGA registers.
        // SupportDirectFlip enables modern flip-model presentation (no copy).
        // SupportSmoothRotation: false until we wire the rotation path.
        //
        caps->SupportNonVGA          = TRUE;
        caps->SupportSmoothRotation  = FALSE;
        caps->SupportDirectFlip      = 1;

        // One 3-D node is the usual minimum for a render adapter. Zero
        // nodes plus a stub GetNodeMetadata can prevent dxgk from ever
        // calling DxgkDdiStartDevice on recent builds.
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;

        WINMALI_TRACE("DRIVERCAPS: WDDM=0x%x slots=%u apertureLimit=%llu "
                      "maxFlips=%u nodes=%u nonvga=1 sectionBacked=1 directFlip=1",
                      caps->WDDMVersion,
                      caps->MaxAllocationListSlotId,
                      (ULONGLONG)caps->ApertureSegmentCommitLimit,
                      caps->MaxQueuedFlipOnVSync,
                      caps->GpuEngineTopology.NbAsymetricProcessingNodes);
        return STATUS_SUCCESS;
    }

    //
    // DISPLAY_DRIVERCAPS_EXTENSION: queried only on full-graphics drivers.
    // Returning NOT_SUPPORTED here previously made dxgkrnl re-classify us
    // as not-a-display-adapter and fall back to MS Basic Display. The
    // bare minimum is to advertise virtual-mode support so the OS knows it
    // can request modes that don't exactly match a monitor EDID entry.
    //
    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
        DXGK_DISPLAY_DRIVERCAPS_EXTENSION* dext;
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(*dext))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        dext = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(dext, sizeof(*dext));
        dext->VirtualModeSupport = 1;
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    // Queried after AddDevice, before StartDevice (same window as DRIVERCAPS).
    case DXGKQAITYPE_WDDMDEVICECAPS: {
        DXGK_WDDMDEVICECAPS* wddmDev = (DXGK_WDDMDEVICECAPS*)pQueryAdapterInfo->pOutputData;

        if (wddmDev == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*wddmDev)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(wddmDev, sizeof(*wddmDev));
        wddmDev->WDDMVersion = DXGKDDI_WDDMv2_4;  // keep in sync with DRIVERCAPS / UMDRIVERPRIVATE
        return STATUS_SUCCESS;
    }
#endif

    case DXGKQAITYPE_PHYSICALADAPTERCAPS: {
        //
        // Microsoft Learn (DXGK_PHYSICALADAPTERCAPS): DxgkPhysicalAdapterHandle
        // is the value passed as DXGKRNL_INTERFACE::DeviceHandle from
        // DxgkDdiStartDevice — we store it in adapter->DxgkHandle. Do not use
        // the miniport context pointer here even if it aliases the same
        // address on some builds; always use DeviceHandle after StartDevice.
        //
        // pInputData can be DXGK_QUERYPHYSICALADAPTERCAPSIN (physical adapter
        // index in an LDA chain). Single-GPU: only index 0 is valid.
        //
        DXGK_PHYSICALADAPTERCAPS* phys = (DXGK_PHYSICALADAPTERCAPS*)pQueryAdapterInfo->pOutputData;
        PWINMALI_ADAPTER          adapter;
        const DXGK_QUERYPHYSICALADAPTERCAPSIN* physIn;

        if (phys == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*phys)) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        if (pQueryAdapterInfo->pInputData != NULL
            && pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYPHYSICALADAPTERCAPSIN))
        {
            physIn = (const DXGK_QUERYPHYSICALADAPTERCAPSIN*)pQueryAdapterInfo->pInputData;
            if (physIn->PhysicalAdapterIndex != 0) {
                WINMALI_WARN(
                    "PHYSICALADAPTERCAPS: unsupported PhysicalAdapterIndex=%u (single GPU)",
                    physIn->PhysicalAdapterIndex);
                return STATUS_INVALID_PARAMETER;
            }
        }

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        if (adapter == NULL || adapter->DxgkHandle == NULL) {
            WINMALI_WARN(
                "PHYSICALADAPTERCAPS: StartDevice has not set DxgkHandle (adapter=%p hAdapter=%p)",
                adapter,
                hAdapter);
            return STATUS_DEVICE_NOT_READY;
        }

        RtlZeroMemory(phys, sizeof(*phys));
        phys->NumExecutionNodes        = 1;
        phys->PagingNodeIndex          = 0;
        phys->DxgkPhysicalAdapterHandle = adapter->DxgkHandle;
        //
        // GpuMmuSupported is REQUIRED whenever DRIVERCAPS reports
        // SectionBackedPrimary or anything beyond the WDDM 1.x aperture
        // memory model. Without it, dxgmms2!ReadPhysicalAdapterConfiguration
        // crashes at +0xf4 because VIDMM_PHYSICAL_ADAPTER::Initialize takes
        // a "no MMU declared, modern caps" branch where its root-PT pointer
        // is still NULL when a follow-up helper tries to dereference it.
        //
        // We do have a real GPU MMU (the Mali AS array). The matching DDIs
        // are wired in WinMaliDxgkInitFill.c:
        //   - SetRootPageTable     : VOID, no-op stub (safe even if dxgk calls)
        //   - GetRootPageTableSize : returns PAGE_SIZE so dxgk allocates a
        //                            minimal root-PT backing object on 26100+
        //                            (returning 0 tripped VIDMM init paths).
        //   - {Map,Unmap}CpuHostAperture : NOT_SUPPORTED (we don't expose a
        //                            CPU-visible GPU aperture yet).
        //
        phys->Flags.Value              = 0;
        phys->Flags.GpuMmuSupported    = 1;

        WINMALI_TRACE(
            "PHYSICALADAPTERCAPS: stamp=%u hAdapter=%p DeviceHandle=%p ctx=%p flags=0x%x (GpuMmu=1)",
            WINMALI_KMD_CAP_STAMP,
            hAdapter,
            phys->DxgkPhysicalAdapterHandle,
            adapter,
            phys->Flags.Value);
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    //
    // ADAPTERPERFDATA_CAPS / GPUVERSION are queried by dxgkrnl on every
    // recent build right after PHYSICALADAPTERCAPS. They MUST succeed:
    // returning STATUS_NOT_SUPPORTED here previously caused dxgkrnl to
    // proceed with garbage caps and crash deeper in VIDMM as it tried to
    // read perf/clock thresholds for our adapter. Both structs are pure
    // diagnostic / telemetry surfaces - zero is a valid "unknown" report.
    //
    case DXGKQAITYPE_ADAPTERPERFDATA_CAPS: {
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize == 0)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        // dxgkrnl asks for whatever its internal struct size is (28 on the
        // current 26100 build); zero the entire buffer, leaving every metric
        // at "unknown / not reported".
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        WINMALI_TRACE("ADAPTERPERFDATA_CAPS: zeroed %u bytes",
                      pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUVERSION: {
        DXGK_GPUVERSION* ver = (DXGK_GPUVERSION*)pQueryAdapterInfo->pOutputData;

        if (ver == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*ver)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(ver, sizeof(*ver));
        // Diagnostic strings only. Sized DXGK_MAX_GPUVERSION_NAME_LENGTH
        // (32 WCHARs incl. NUL). Avoid wcscpy_s here to keep WDK compat
        // simple; a truncated copy is acceptable per docs.
        (void)RtlStringCbCopyW(ver->BiosVersion,
                               sizeof(ver->BiosVersion),
                               L"WinMali UEFI");
        (void)RtlStringCbCopyW(ver->GpuArchitecture,
                               sizeof(ver->GpuArchitecture),
                               L"Mali-G610 (Valhall)");
        WINMALI_TRACE("GPUVERSION: arch='Mali-G610 (Valhall)' bios='WinMali UEFI'");
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    case DXGKQAITYPE_PHYSICAL_MEMORY_CAPS: {
        DXGK_PHYSICAL_MEMORY_CAPS* pmem = (DXGK_PHYSICAL_MEMORY_CAPS*)pQueryAdapterInfo->pOutputData;

        if (pmem == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*pmem)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pmem, sizeof(*pmem));
        pmem->HighestVisibleAddress.QuadPart = (ULONG64)-1;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_IOMMU_CAPS: {
        DXGK_IOMMU_CAPS* iommu = (DXGK_IOMMU_CAPS*)pQueryAdapterInfo->pOutputData;

        if (iommu == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*iommu)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(iommu, sizeof(*iommu));
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    case DXGKQAITYPE_HARDWARERESERVEDRANGES:
    case DXGKQAITYPE_HARDWARERESERVEDRANGES2: {
        DXGK_HARDWARERESERVEDRANGES* res = (DXGK_HARDWARERESERVEDRANGES*)pQueryAdapterInfo->pOutputData;

        if (res == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*res)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(res, sizeof(*res));
        res->NumRanges        = 0;
        res->pPhysicalRanges  = NULL;
        return STATUS_SUCCESS;
    }
#endif

    case DXGKQAITYPE_64BITONLYCAPS: {
        DXGK_64_BIT_ONLY_CAPS* b64 = (DXGK_64_BIT_ONLY_CAPS*)pQueryAdapterInfo->pOutputData;

        if (b64 == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*b64)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(b64, sizeof(*b64));
        b64->SupportsOnly64Bit = 1;
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    //
    // With PHYSICALADAPTERCAPS.GpuMmuSupported, dxgk expects these before VIDMM
    // walks segments. Missing NOT_SUPPORTED can strand init before QUERYSEGMENT*.
    //
    case DXGKQAITYPE_GPUMMUCAPS: {
        const DXGK_QUERYGPUMMUCAPSIN* in;
        DXGK_GPUMMUCAPS*              caps;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYGPUMMUCAPSIN))
        {
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYGPUMMUCAPSIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_GPUMMUCAPS))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        caps = (DXGK_GPUMMUCAPS*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported   = 1;
        caps->NoExecuteMemorySupported  = 1;
        caps->ZeroInPteSupported        = 1;
        caps->PageTableUpdateMode       = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        caps->VirtualAddressBitCount    = 48;
        caps->LeafPageTableSizeFor64KPagesInBytes = (UINT)PAGE_SIZE;
        caps->PageTableLevelCount       = 4;
        WINMALI_TRACE("GPUMMUCAPS: VA_bits=48 levels=4 update=CPU_VIRTUAL seg0");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN* in;
        DXGK_PAGE_TABLE_LEVEL_DESC*           desc;
        const UINT                            levels = 4;
        const UINT                            idxBits = 9;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYPAGETABLELEVELDESCIN))
        {
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYPAGETABLELEVELDESCIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            return STATUS_INVALID_PARAMETER;
        }
        if (in->LevelIndex >= levels) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_PAGE_TABLE_LEVEL_DESC))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        desc = (DXGK_PAGE_TABLE_LEVEL_DESC*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(desc, sizeof(*desc));
        desc->PageTableIndexBitCount           = idxBits;
        desc->PageTableSegmentId               = 0;
        desc->PagingProcessPageTableSegmentId  = 0;
        desc->PageTableSizeInBytes             = (UINT)PAGE_SIZE;
        desc->PageTableAlignmentInBytes        = 0;
        WINMALI_TRACE(
            "PAGETABLELEVELDESC: level=%u idxBits=%u size=%u seg=0",
            (ULONG)in->LevelIndex,
            idxBits,
            (ULONG)PAGE_SIZE);
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    case DXGKQAITYPE_QUERYSEGMENT3: {
        DXGK_QUERYSEGMENTOUT3*     qo;
        DXGK_SEGMENTDESCRIPTOR3*   seg;
        PWINMALI_ADAPTER           adapter;

        if (pQueryAdapterInfo->OutputDataSize
            < sizeof(DXGK_QUERYSEGMENTOUT3) + sizeof(DXGK_SEGMENTDESCRIPTOR3)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        qo = (DXGK_QUERYSEGMENTOUT3*)pQueryAdapterInfo->pOutputData;
        if (qo == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        seg = qo->pSegmentDescriptor;
        if (seg == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

        RtlZeroMemory(qo, sizeof(*qo));
        qo->pSegmentDescriptor = seg;
        RtlZeroMemory(seg, sizeof(*seg));

        seg->Flags.PopulatedFromSystemMemory = 1;
        seg->Flags.CpuVisible                = 1;
        seg->Flags.CacheCoherent             = 0;
        seg->Flags.NonLocalBudgetGroup       = 1;
        if (adapter != NULL && adapter->MmuScratchHeapVa != NULL) {
            seg->CpuTranslatedAddress = adapter->MmuScratchHeapPhys;
            seg->Size                 = adapter->MmuScratchHeapBytes;
            seg->BaseAddress          = adapter->MmuScratchHeapPhys;
        } else {
            seg->CpuTranslatedAddress.QuadPart = 0;
            seg->Size                          = 0;
            seg->BaseAddress.QuadPart          = 0;
        }
        seg->NbOfBanks              = 0;
        seg->pBankRangeTable        = NULL;
        seg->CommitLimit            = 0;
        seg->SystemMemoryEndAddress = 0;
        seg->Reserved               = 0;

        qo->NbSegment                   = 1;
        qo->PagingBufferSegmentId       = 0;
        qo->PagingBufferSize            = WINMALI_VIDMM_PAGING_BUFFER_BYTES;
        qo->PagingBufferPrivateDataSize = 0;

        WINMALI_TRACE(
            "QUERYSEGMENT3: segment0 size=0x%llx cpu_phys=0x%llx",
            (ULONGLONG)seg->Size,
            (ULONGLONG)seg->CpuTranslatedAddress.QuadPart);
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    //
    // WDDM 2.x VIDMM uses QUERYSEGMENT4 (not only QUERYSEGMENT3). Returning
    // NOT_SUPPORTED here makes dxgkrnl tear the adapter down (StopDevice).
    //
    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4*   qo;
        DXGK_SEGMENTDESCRIPTOR4* seg;
        PWINMALI_ADAPTER         adapter;
        BYTE*                    pOutBase;
        BYTE*                    pOutEnd;
        UINT                     nbSeg;
        SIZE_T                   stride;
        SIZE_T                   needTotal;

        if (pQueryAdapterInfo->pInputData != NULL
            && pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYSEGMENTIN4))
        {
            const DXGK_QUERYSEGMENTIN4* sin =
                (const DXGK_QUERYSEGMENTIN4*)pQueryAdapterInfo->pInputData;
            if (sin->PhysicalAdapterIndex != 0) {
                WINMALI_WARN(
                    "QUERYSEGMENT4: unsupported PhysicalAdapterIndex=%u",
                    sin->PhysicalAdapterIndex);
                return STATUS_INVALID_PARAMETER;
            }
        }

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4)
            || pQueryAdapterInfo->pOutputData == NULL)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo       = (DXGK_QUERYSEGMENTOUT4*)pQueryAdapterInfo->pOutputData;
        stride   = sizeof(DXGK_SEGMENTDESCRIPTOR4);
        pOutBase = (BYTE*)pQueryAdapterInfo->pOutputData;
        pOutEnd  = pOutBase + pQueryAdapterInfo->OutputDataSize;

        //
        // MSDN DXGK_QUERYSEGMENTOUT4: first call has NbSegment == 0. The driver
        // must return SUCCESS and set only NbSegment — do not read or write any
        // other member (including RtlZeroMemory of the struct). Violating this
        // caused dxgkrnl to repeat 40-byte calls and then fault.
        //
        if (qo->NbSegment == 0) {
            qo->NbSegment = 1;
            WINMALI_TRACE(
                "QUERYSEGMENT4: pass1 count-only nb=1 (out=%u hdr=%u)",
                pQueryAdapterInfo->OutputDataSize,
                (ULONG)sizeof(DXGK_QUERYSEGMENTOUT4));
            return STATUS_SUCCESS;
        }

        nbSeg = qo->NbSegment;
        if (nbSeg != 1) {
            WINMALI_WARN("QUERYSEGMENT4: pass2 unsupported nbSeg=%u", nbSeg);
            return STATUS_INVALID_PARAMETER;
        }

        needTotal = sizeof(DXGK_QUERYSEGMENTOUT4) + (SIZE_T)nbSeg * stride;

        //
        // Pass 2 layouts:
        //   (A) Contiguous: pSegmentDescriptor == header + sizeof(DXGK_QUERYSEGMENTOUT4).
        //       When OutputDataSize is header-only (40), that address equals pOutEnd;
        //       it is still the first byte of the descriptor in the same allocation.
        //   (B) In-buffer but not at pOutEnd: psd in (pOutBase, pOutEnd) with room.
        //   (C) Separate dxgkrnl buffer: psd not in [pOutBase, pOutEnd).
        //
        {
            BYTE* const pAfterHdr = pOutBase + sizeof(DXGK_QUERYSEGMENTOUT4);

            if (qo->pSegmentDescriptor != NULL) {
                BYTE* psd = (BYTE*)qo->pSegmentDescriptor;

                if (psd == pAfterHdr) {
                    seg = (DXGK_SEGMENTDESCRIPTOR4*)psd;
                } else if (psd >= pOutBase && psd < pOutEnd) {
                    if (psd < pAfterHdr) {
                        WINMALI_WARN(
                            "QUERYSEGMENT4: pass2 pSegmentDescriptor overlaps header");
                        return STATUS_INVALID_PARAMETER;
                    }
                    if (psd + (SIZE_T)nbSeg * stride > pOutEnd) {
                        WINMALI_TRACE(
                            "QUERYSEGMENT4: pass2 in-buf psd overflow out=%u",
                            pQueryAdapterInfo->OutputDataSize);
                        return STATUS_BUFFER_TOO_SMALL;
                    }
                    seg = (DXGK_SEGMENTDESCRIPTOR4*)psd;
                } else {
                    seg = (DXGK_SEGMENTDESCRIPTOR4*)psd;
                }
            } else if (pQueryAdapterInfo->OutputDataSize >= needTotal) {
                seg = (DXGK_SEGMENTDESCRIPTOR4*)pAfterHdr;
            } else {
                WINMALI_TRACE(
                    "QUERYSEGMENT4: pass2 no psd and out=%u < need=%llu",
                    pQueryAdapterInfo->OutputDataSize,
                    (ULONGLONG)needTotal);
                return STATUS_BUFFER_TOO_SMALL;
            }
        }

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

        RtlZeroMemory(seg, sizeof(*seg));

        seg->Flags.Value                     = 0;
        seg->Flags.PopulatedFromSystemMemory = 1;
        seg->Flags.CpuVisible                = 1;
        seg->Flags.CacheCoherent             = 0;
        seg->Flags.SupportsCpuHostAperture   = 0;
        seg->Flags.NonLocalBudgetGroup       = 1;

        seg->CommitLimit            = 0;
        seg->SystemMemoryEndAddress = 0;
        if (adapter != NULL && adapter->MmuScratchHeapVa != NULL) {
            seg->CpuTranslatedAddress = adapter->MmuScratchHeapPhys;
            seg->Size                 = adapter->MmuScratchHeapBytes;
            //
            // d3dkmddi.h: BaseAddress is the GPU logical base for the segment.
            // For this UMA carve-out, use the same PA as the CPU-visible base.
            //
            seg->BaseAddress          = adapter->MmuScratchHeapPhys;
        } else {
            seg->CpuTranslatedAddress.QuadPart = 0;
            seg->Size                          = 0;
            seg->BaseAddress.QuadPart          = 0;
        }
        seg->NumInvalidMemoryRanges = 0;
        seg->VprRangeStartOffset    = 0;
        seg->VprRangeSize           = 0;
        seg->VprAlignment           = 0;
        seg->NumVprSupported        = 0;
        seg->VprReserveSize         = 0;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
        seg->NumUEFIFrameBufferRanges = 0;
#endif

        qo->pSegmentDescriptor          = (BYTE*)seg;
        qo->SegmentDescriptorStride     = stride;
        qo->NbSegment                   = nbSeg;
        //
        // DXGK_QUERYSEGMENTOUT3: PagingBufferSegmentId==0 allocates the paging
        // buffer as a contiguous WC block (not from a segment). The paging
        // segment must be an *aperture* segment; ours is not, and we do not
        // implement aperture map/unmap in BuildPagingBuffer yet — using segment
        // index 1 here makes dxgmms2!InitializePhysicalAdapterSegments AV.
        //
        qo->PagingBufferSegmentId       = 0;
        qo->PagingBufferSize            = WINMALI_VIDMM_PAGING_BUFFER_BYTES;
        qo->PagingBufferPrivateDataSize = 0;

        WINMALI_TRACE(
            "QUERYSEGMENT4: pass2 OK out=%u psd=%p seg0 size=0x%llx base=0x%llx cpu_phys=0x%llx in_out=%u paging_seg=%u",
            pQueryAdapterInfo->OutputDataSize,
            (void*)qo->pSegmentDescriptor,
            (ULONGLONG)seg->Size,
            (ULONGLONG)seg->BaseAddress.QuadPart,
            (ULONGLONG)seg->CpuTranslatedAddress.QuadPart,
            (ULONG)((BYTE*)seg >= pOutBase && (BYTE*)seg < pOutEnd),
            qo->PagingBufferSegmentId);
        return STATUS_SUCCESS;
    }
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    //
    // WDDM 3.2+ (e.g. Windows 11 24H2 / 26200): dxgkrnl may use these instead
    // of (or in addition to) QUERYSEGMENT3/4. Returning NOT_SUPPORTED here
    // fails VIDMM init; the stack stops the device right after GetNodeMetadata
    // and never loads the UMD — with no obvious "segment" line in older logs.
    //
    case DXGKQAITYPE_QUERYSEGMENTCOUNT: {
        const DXGK_QUERYSEGMENTCOUNTIN* in;
        DXGK_QUERYSEGMENTCOUNTOUT*      out;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYSEGMENTCOUNTIN)) {
            WINMALI_WARN("QUERYSEGMENTCOUNT: invalid input");
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYSEGMENTCOUNTIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            WINMALI_WARN(
                "QUERYSEGMENTCOUNT: PhysicalAdapterIndex=%u",
                in->PhysicalAdapterIndex);
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTCOUNTOUT)) {
            WINMALI_WARN("QUERYSEGMENTCOUNT: output too small");
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYSEGMENTCOUNTOUT*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(*out));
        out->SegmentCount = 1;
        WINMALI_TRACE("QUERYSEGMENTCOUNT: SegmentCount=1");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT5: {
        const DXGK_QUERYSEGMENTIN5* in;
        DXGK_QUERYSEGMENTOUT5*       qo;
        DXGK_SEGMENTDESCRIPTOR5*     seg;
        PWINMALI_ADAPTER             adapter;
        SIZE_T                       need;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYSEGMENTIN5)) {
            WINMALI_WARN("QUERYSEGMENT5: invalid input");
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYSEGMENTIN5*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            WINMALI_WARN("QUERYSEGMENT5: PhysicalAdapterIndex=%u", in->PhysicalAdapterIndex);
            return STATUS_INVALID_PARAMETER;
        }
        need = sizeof(DXGK_QUERYSEGMENTOUT5) + sizeof(DXGK_SEGMENTDESCRIPTOR5);
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < need) {
            WINMALI_WARN(
                "QUERYSEGMENT5: need %llu bytes have %u",
                (ULONGLONG)need,
                pQueryAdapterInfo->OutputDataSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo = (DXGK_QUERYSEGMENTOUT5*)pQueryAdapterInfo->pOutputData;
        seg = qo->SegmentDescriptors;
        if (seg == NULL) {
            seg = (DXGK_SEGMENTDESCRIPTOR5*)((BYTE*)qo + sizeof(DXGK_QUERYSEGMENTOUT5));
        }
        if ((BYTE*)seg + sizeof(DXGK_SEGMENTDESCRIPTOR5)
            > (BYTE*)pQueryAdapterInfo->pOutputData + pQueryAdapterInfo->OutputDataSize) {
            WINMALI_WARN("QUERYSEGMENT5: descriptor past buffer end");
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(qo, sizeof(*qo));
        qo->SegmentDescriptors = seg;

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        RtlZeroMemory(seg, sizeof(*seg));
        seg->SegmentType = DXGK_SEGMENTTYPE_SYSMEM;
        seg->Flags.Value                     = 0;
        seg->Flags.PopulatedFromSystemMemory = 1;
        seg->Flags.CpuVisible                = 1;
        seg->Flags.NonLocalBudgetGroup       = 1;
        seg->SlabSize                        = DXGK_PAGESIZE_4KB;
        if (adapter != NULL && adapter->MmuScratchHeapVa != NULL) {
            seg->BaseAddress          = adapter->MmuScratchHeapPhys;
            seg->Size                 = adapter->MmuScratchHeapBytes;
            seg->CpuTranslatedAddress = adapter->MmuScratchHeapPhys;
        } else {
            seg->BaseAddress.QuadPart          = 0;
            seg->Size                          = 0;
            seg->CpuTranslatedAddress.QuadPart   = 0;
        }
        seg->SystemMemoryEndAddress     = 0;
        seg->VprRangeStartOffset        = 0;
        seg->VprRangeSize               = 0;
        seg->VprAlignment               = 0;
        seg->NumInvalidMemoryRanges     = 0;
        seg->NumVprSupported            = 0;
        seg->VprReserveSize             = 0;
        seg->NumUEFIFrameBufferRanges   = 0;

        WINMALI_TRACE(
            "QUERYSEGMENT5: seg0 size=0x%llx base=0x%llx cpu_phys=0x%llx",
            (ULONGLONG)seg->Size,
            (ULONGLONG)seg->BaseAddress.QuadPart,
            (ULONGLONG)seg->CpuTranslatedAddress.QuadPart);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYMMUCOUNT: {
        const DXGK_QUERYMMUCOUNTIN* in;
        DXGK_QUERYMMUCOUNTOUT*       out;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYMMUCOUNTIN)) {
            WINMALI_WARN("QUERYMMUCOUNT: invalid input");
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYMMUCOUNTIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            WINMALI_WARN("QUERYMMUCOUNT: PhysicalAdapterIndex=%u", in->PhysicalAdapterIndex);
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYMMUCOUNTOUT)) {
            WINMALI_WARN("QUERYMMUCOUNT: output too small");
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYMMUCOUNTOUT*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(*out));
        out->MmuCount = 1;
        WINMALI_TRACE("QUERYMMUCOUNT: MmuCount=1");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYMMUS: {
        const DXGK_QUERYMMUSIN* in;
        DXGK_QUERYMMUSOUT*       qo;
        DXGK_MMUDESCRIPTOR*      mmu;
        SIZE_T                   need;

        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYMMUSIN)) {
            WINMALI_WARN("QUERYMMUS: invalid input");
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYMMUSIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            WINMALI_WARN("QUERYMMUS: PhysicalAdapterIndex=%u", in->PhysicalAdapterIndex);
            return STATUS_INVALID_PARAMETER;
        }
        need = sizeof(DXGK_QUERYMMUSOUT) + sizeof(DXGK_MMUDESCRIPTOR);
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < need) {
            WINMALI_WARN(
                "QUERYMMUS: need %llu bytes have %u",
                (ULONGLONG)need,
                pQueryAdapterInfo->OutputDataSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo = (DXGK_QUERYMMUSOUT*)pQueryAdapterInfo->pOutputData;
        mmu = qo->MmuDescriptors;
        if (mmu == NULL) {
            mmu = (DXGK_MMUDESCRIPTOR*)((BYTE*)qo + sizeof(DXGK_QUERYMMUSOUT));
        }
        if ((BYTE*)mmu + sizeof(DXGK_MMUDESCRIPTOR)
            > (BYTE*)pQueryAdapterInfo->pOutputData + pQueryAdapterInfo->OutputDataSize) {
            WINMALI_WARN("QUERYMMUS: descriptor past buffer end");
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(qo, sizeof(*qo));
        qo->MmuDescriptors = mmu;
        qo->DisplayMmuId   = 0;

        RtlZeroMemory(mmu, sizeof(*mmu));
        mmu->Size = (1ULL << 48) - 1;

        WINMALI_TRACE("QUERYMMUS: DisplayMmuId=0 mmuSize=0x%llx", (ULONGLONG)mmu->Size);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGINGPROCESSGPUVASIZE: {
        const UINT* inPa;
        UINT*       outMb;

        //
        // WDDM 3.2 (allocation notification): UINT in = physical adapter index;
        // UINT out = paging-process GPU VA size in megabytes. Zero means the OS
        // picks the size (recommended when there is no local vRAM segment).
        //
        if (pQueryAdapterInfo->pInputData == NULL
            || pQueryAdapterInfo->InputDataSize < sizeof(UINT)) {
            WINMALI_WARN("PAGINGPROCESSGPUVASIZE: invalid input");
            return STATUS_INVALID_PARAMETER;
        }
        inPa = (const UINT*)pQueryAdapterInfo->pInputData;
        if (*inPa != 0) {
            WINMALI_WARN("PAGINGPROCESSGPUVASIZE: PhysicalAdapterIndex=%u", *inPa);
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            WINMALI_WARN("PAGINGPROCESSGPUVASIZE: output too small");
            return STATUS_BUFFER_TOO_SMALL;
        }
        outMb = (UINT*)pQueryAdapterInfo->pOutputData;
        *outMb = 0;
        WINMALI_TRACE("PAGINGPROCESSGPUVASIZE: 0 MB (OS default)");
        return STATUS_SUCCESS;
    }
#endif // DXGKDDI_INTERFACE_VERSION_WDDM3_2

    case DXGKQAITYPE_UMDRIVERPRIVATE: {
        WINMALI_ADAPTER_INFO* info = (WINMALI_ADAPTER_INFO*)pQueryAdapterInfo->pOutputData;
        if (info == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*info)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(info, sizeof(*info));
        info->Magic           = WINMALI_ADAPTER_MAGIC;
        info->KmdMajorVersion = WINMALI_KMD_MAJOR;
        info->KmdMinorVersion = WINMALI_KMD_MINOR;
        info->WddmVersion     = DXGKDDI_WDDMv2_4;
        info->GpuId           = 0;  // filled in once MMIO bring-up lands
        info->CsfId           = 0;
        info->GpuRevId        = 0;
        info->Flags           = 0;
        return STATUS_SUCCESS;
    }

    default:
        WINMALI_WARN(
            "QueryAdapterInfo NOT_SUPPORTED type=%lu size=%u",
            (ULONG)pQueryAdapterInfo->Type,
            pQueryAdapterInfo->OutputDataSize);
        return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS APIENTRY
WinMaliKmdGetNodeMetadata(
    _In_ const HANDLE           hAdapter,
    _In_ UINT                    NodeOrdinalAndAdapterIndex,
    _Out_ DXGKARG_GETNODEMETADATA* pGetNodeMetadata)
{
    WINMALI_ENTER();
    UINT nodeOrdinal;
    UINT physicalAdapterIndex;

    if (hAdapter == NULL || pGetNodeMetadata == NULL) {
        WINMALI_WARN("GetNodeMetadata: invalid parameter");
        return STATUS_INVALID_PARAMETER;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    nodeOrdinal          = DXGKNODEMETADATA_GETNODEORDINAL(NodeOrdinalAndAdapterIndex);
    physicalAdapterIndex = DXGKNODEMETADATA_GETPHYSICALADAPTERINDEX(NodeOrdinalAndAdapterIndex);
#else
    nodeOrdinal          = NodeOrdinalAndAdapterIndex;
    physicalAdapterIndex = 0;
#endif

    if (physicalAdapterIndex != 0 || nodeOrdinal != 0) {
        WINMALI_WARN(
            "GetNodeMetadata: unsupported node phys=%u ordinal=%u",
            physicalAdapterIndex,
            nodeOrdinal);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    (VOID)RtlStringCbCopyW(
        pGetNodeMetadata->FriendlyName,
        sizeof(pGetNodeMetadata->FriendlyName),
        L"WinMali");
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    //
    // Learn: "KMD sets the bits for every feature that the specified GPU node
    // supports." Our 3D node participates in dxgk context scheduling (see
    // DRIVERCAPS.SchedulingCaps); advertise at least that much.
    //
    pGetNodeMetadata->Flags.ContextSchedulingSupported = 1;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    //
    // Must match PHYSICALADAPTERCAPS.Flags.GpuMmuSupported and
    // DRIVERCAPS.MemoryManagementCaps.IoMmuSupported. Reporting GPU MMU at
    // the adapter but FALSE here leaves dxgk with contradictory per-node
    // metadata during VIDMM / segment setup.
    //
    pGetNodeMetadata->GpuMmuSupported = TRUE;
    pGetNodeMetadata->IoMmuSupported    = FALSE;
#endif
    WINMALI_TRACE("GetNodeMetadata: node0 GpuMmu=1 IoMmu=0");
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdQueryChildRelations(
    _In_    const PVOID             MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR  ChildRelations,
    _In_    ULONG                   ChildRelationsSize)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    ULONG            slots;
    ULONG            primaryUid;

    WINMALI_ENTER();

    if (adapter == NULL || ChildRelations == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // dxgkrnl always allocates ChildRelations with one extra "sentinel"
    // entry that we leave zeroed (terminator), so we get N+1 slots for
    // N children. We currently expose exactly one child: the primary
    // GOP-backed connector (HDMI0 by default; whatever Rk3588DispCaptureGopFb
    // resolves PrimaryConnector to). All other VOP2 connectors will be
    // added as we wire each one up.
    //
    RtlZeroMemory(ChildRelations, ChildRelationsSize);
    slots = ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR);
    if (slots < 2) {
        WINMALI_WARN("QueryChildRelations: dxgkrnl gave us %u slots (need >= 2)", slots);
        return STATUS_BUFFER_TOO_SMALL;
    }

    primaryUid = (ULONG)adapter->PrimaryConnector;

    ChildRelations[0].ChildDeviceType                                            = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology     = D3DKMDT_VOT_HDMI;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes       = FALSE;
    //
    // HpdAwarenessAlwaysConnected tells dxgkrnl: "I never fire QueryConnectionChange
    // for this child, the monitor is always present." That's correct for the GOP
    // path because the firmware already did detect+train and we have no HPD IRQ
    // wired yet. Once vop2connectors.c grows real HPD plumbing, switch to
    // HpdAwarenessInterruptible and start firing DxgkCbIndicateChildStatus.
    //
    ChildRelations[0].ChildCapabilities.HpdAwareness                             = HpdAwarenessAlwaysConnected;
    ChildRelations[0].AcpiUid                                                    = 0;
    ChildRelations[0].ChildUid                                                   = primaryUid;

    WINMALI_TRACE("QueryChildRelations: 1 child, uid=%u type=VideoOutput tech=HDMI hpd=AlwaysConnected (slots=%u)",
                  primaryUid, slots);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdQueryChildStatus(
    _In_ const PVOID            MiniportDeviceContext,
    _In_ PDXGK_CHILD_STATUS     ChildStatus,
    _In_ BOOLEAN                NonDestructiveOnly)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);

    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    WINMALI_ENTER();

    if (adapter == NULL || ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildStatus->ChildUid != (ULONG)adapter->PrimaryConnector) {
        // We currently expose exactly one child; any other UID is bogus.
        WINMALI_WARN("QueryChildStatus: unknown ChildUid=%u (primary=%u)",
                     ChildStatus->ChildUid, adapter->PrimaryConnector);
        return STATUS_INVALID_PARAMETER;
    }

    switch (ChildStatus->Type) {
    case StatusConnection:
        //
        // We're HpdAwarenessAlwaysConnected: report the monitor as plugged in
        // unconditionally. NonDestructiveOnly is a hint that we shouldn't do an
        // active probe (e.g. HPD pulse) - we don't probe at all yet, so safe.
        //
        ChildStatus->HotPlug.Connected = TRUE;
        WINMALI_TRACE("QueryChildStatus: uid=%u Connection=Connected (NonDestructiveOnly=%u)",
                      ChildStatus->ChildUid, (ULONG)NonDestructiveOnly);
        return STATUS_SUCCESS;

    case StatusRotation:
        ChildStatus->Rotation.Angle = 0;
        return STATUS_SUCCESS;

    default:
        WINMALI_TRACE("QueryChildStatus: uid=%u type=%d not supported",
                      ChildStatus->ChildUid, (int)ChildStatus->Type);
        return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS APIENTRY
WinMaliKmdQueryDeviceDescriptor(
    _In_    const PVOID             MiniportDeviceContext,
    _In_    ULONG                   ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    WINMALI_ENTER();
    //
    // No DDC/EDID on this bring-up path; OS uses synthetic modes (MSNIL*).
    // Log at WARN so a silent STATUS_MONITOR_NO_DESCRIPTOR is never mistaken
    // for "no callback ran".
    //
    WINMALI_WARN(
        "QueryDeviceDescriptor: STATUS_MONITOR_NO_DESCRIPTOR (ChildUid=%lu)",
        ChildUid);
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

// Same values as DEFINE_GUID in dispmprt.h; local constants avoid INITGUID
// linkage for optional QUERY_INTERFACE probes.
static CONST GUID kWinMaliGuidI2c = {0x2564AA4F, 0xDDDB, 0x4495, {0xB4, 0x97, 0x6A, 0xD4, 0xA8, 0x41, 0x63, 0xD7}};
static CONST GUID kWinMaliGuidOpm  = {0xBF4672DE, 0x6B4E, 0x4BE4, {0xA3, 0x25, 0x68, 0xA9, 0x1E, 0xA4, 0x9C, 0x09}};
static CONST GUID kWinMaliGuidOpm2 = {0x7F098726, 0x2EBB, 0x4FF3, {0xA2, 0x7F, 0x10, 0x46, 0xB9, 0x5D, 0xC5, 0x17}};
static CONST GUID kWinMaliGuidOpm2Jtp = {0xE929EEA4, 0xB9F1, 0x407B, {0xAA, 0xB9, 0xAB, 0x08, 0xBB, 0x44, 0xFB, 0xF4}};
static CONST GUID kWinMaliGuidOpm3 = {0x693a2cb1, 0x8c8d, 0x4ab6, {0x95, 0x55, 0x4b, 0x85, 0xef, 0x2c, 0x7c, 0x6b}};
static CONST GUID kWinMaliGuidMiracast = {0xaf03f190, 0x22af, 0x48cb, {0x94, 0xbb, 0xb7, 0x8e, 0x76, 0xa2, 0x51, 0x07}};
static CONST GUID kWinMaliGuidDisplayMux2 = {0x086467FB, 0xDDDF, 0x4C19, {0x97, 0xD5, 0xC4, 0x1D, 0x76, 0x72, 0x21, 0xC8}};
static CONST GUID kWinMaliGuidWddmFeature = {0x94bb3993, 0xc6c3, 0x4da7, {0x89, 0x49, 0xa1, 0x13, 0x82, 0x32, 0xe7, 0x59}};

static BOOLEAN
WinMaliKmdGuidEq_(_In_ CONST GUID* a, _In_ CONST GUID* b)
{
    return RtlCompareMemory(a, b, sizeof(GUID)) == sizeof(GUID);
}

// Human-readable tag for optional QUERY_INTERFACE GUIDs (dispmprt.h).
// dxgkrnl probes these for most adapters; a render-only KMD returns
// STATUS_NOT_SUPPORTED unless the feature is implemented.
static PCSTR
WinMaliKmdQueryInterfaceTag_(_In_opt_ CONST GUID* g)
{
    if (g == NULL) {
        return "null";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidI2c)) {
        return "I2C";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidOpm)) {
        return "OPM";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidOpm2)) {
        return "OPM_2";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidOpm2Jtp)) {
        return "OPM_2_JTP";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidOpm3)) {
        return "OPM_3";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidMiracast)) {
        return "MIRACAST_DISPLAY";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidDisplayMux2)) {
        return "DISPLAYMUX_2";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidWddmFeature)) {
        return "WDDM_FEATURE";
    }
    return "unknown";
}

NTSTATUS APIENTRY
WinMaliKmdQueryInterface(
    _In_ const PVOID       MiniportDeviceContext,
    _In_ PQUERY_INTERFACE  QueryInterface)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (QueryInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    // dxgkrnl probes many optional KMD extension interfaces in a row; each
    // call uses a different InterfaceType GUID. Log at INFO so lines are
    // distinct without enabling VERBOSE.
    if (QueryInterface->InterfaceType != NULL) {
        CONST GUID* const g = QueryInterface->InterfaceType;
        WINMALI_TRACE(
            "QueryInterface %s guid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x "
            "ver=%u size=%u uid=%lu",
            WinMaliKmdQueryInterfaceTag_(g),
            g->Data1,
            g->Data2,
            g->Data3,
            g->Data4[0],
            g->Data4[1],
            g->Data4[2],
            g->Data4[3],
            g->Data4[4],
            g->Data4[5],
            g->Data4[6],
            g->Data4[7],
            (ULONG)QueryInterface->Version,
            (ULONG)QueryInterface->Size,
            (ULONG)QueryInterface->DeviceUid);
    } else {
        WINMALI_TRACE("QueryInterface (null InterfaceType) ver=%u size=%u uid=%lu",
                      (ULONG)QueryInterface->Version,
                      (ULONG)QueryInterface->Size,
                      (ULONG)QueryInterface->DeviceUid);
    }

    return STATUS_NOT_SUPPORTED;
}

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
WinMaliKmdSetPowerState(
    _In_ const PVOID              MiniportDeviceContext,
    _In_ ULONG                    DeviceUid,
    _In_ DEVICE_POWER_STATE       DevicePowerState,
    _In_ POWER_ACTION             ActionType)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);

    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    if (adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    WINMALI_TRACE("SetPowerState: state=%d (D0=%d D3=%d)",
                  (int)DevicePowerState,
                  (int)PowerDeviceD0,
                  (int)PowerDeviceD3);

    if (DevicePowerState == PowerDeviceD0) {
        if (adapter->GpuFwParkedForD3 && adapter->GpuRegsMapped) {
            NTSTATUS st = WinMaliFwInit(adapter);

            if (NT_SUCCESS(st)) {
                adapter->GpuFwParkedForD3 = FALSE;
                WINMALI_TRACE("SetPowerState D0: CSF re-init OK");
            } else {
                WINMALI_WARN("SetPowerState D0: WinMaliFwInit failed 0x%08x", st);
            }
        }
        return STATUS_SUCCESS;
    }

    if (DevicePowerState == PowerDeviceD3) {
        WinMaliFwTeardown(adapter);
        adapter->GpuFwParkedForD3 = TRUE;
        WINMALI_TRACE("SetPowerState D3: CSF parked (FwTeardown)");
        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdNotifyAcpiEvent(
    _In_  const PVOID      MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE  EventType,
    _In_  ULONG            Event,
    _In_  PVOID            Argument,
    _Out_ PULONG           AcpiFlags)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);
    if (AcpiFlags != NULL) *AcpiFlags = 0;
    return STATUS_SUCCESS;
}

VOID
WinMaliKmdResetDevice(_In_ const PVOID MiniportDeviceContext)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    WINMALI_TRACE("ResetDevice (stub)");
}

// ---------------------------------------------------------------------------
// Interrupts.
//
// ISR: sample GPU/JOB/MMU INT_STAT, clear latched bits, bump counters.
// With per-block MASK at reset (all masked), this rarely runs until
// firmware unmasks sources; never claim a shared line without status.
// ---------------------------------------------------------------------------

BOOLEAN
WinMaliKmdInterruptRoutine(_In_ const PVOID MiniportDeviceContext, _In_ ULONG MessageNumber)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    ULONG  gpuStat  = 0;
    ULONG  jobStat  = 0;
    ULONG  mmuStat  = 0;
    BOOLEAN handled = FALSE;

    UNREFERENCED_PARAMETER(MessageNumber);

    if (adapter == NULL) {
        return FALSE;
    }

    InterlockedIncrement64(&adapter->InterruptsTotal);

    if (!adapter->GpuRegsMapped) {
        // MMIO not mapped yet - we can't tell whether the line is ours,
        // so defer to the next handler in the shared chain.
        InterlockedIncrement64(&adapter->InterruptsSpurious);
        return FALSE;
    }

    gpuStat = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_IRQ_STATUS);
    jobStat = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_JOB_INT_STAT);
    mmuStat = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_MMU_INT_STAT);

    if (gpuStat != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_GPU_IRQ_CLEAR, gpuStat);
        handled = TRUE;
    }
    if (jobStat != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_JOB_INT_CLEAR, jobStat);
        handled = TRUE;
    }
    if (mmuStat != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_MMU_INT_CLEAR, mmuStat);
        handled = TRUE;
    }

    if (handled) {
        InterlockedIncrement64(&adapter->InterruptsHandled);
        // Queue DPC for logging / future deferral once CSF IRQs are unmasked.
        if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL
         && adapter->DxgkHandle != NULL) {
            (VOID)adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
        }
    } else {
        InterlockedIncrement64(&adapter->InterruptsSpurious);
    }

    return handled;
}

VOID
WinMaliKmdDpcRoutine(_In_ const PVOID MiniportDeviceContext)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    if (adapter == NULL) return;

    // Read-only snapshot at DPC level. We print the first few events
    // loudly so bring-up can spot them in DbgView, then throttle.
    if (adapter->InterruptsHandled <= 8) {
        WINMALI_TRACE("DPC: handled=%lld total=%lld spurious=%lld",
                      adapter->InterruptsHandled,
                      adapter->InterruptsTotal,
                      adapter->InterruptsSpurious);
    }
}

NTSTATUS APIENTRY
WinMaliKmdControlInterrupt(
    _In_ const HANDLE               hAdapter,
    _In_ const DXGK_INTERRUPT_TYPE  InterruptType,
    _In_ BOOLEAN                    Enable)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    // In DXGKv2 this is called to toggle the CRTC_VSYNC interrupt for
    // flip notifications, which a render-only miniport never implements.
    // Return STATUS_NOT_IMPLEMENTED for recognised-but-unsupported types
    // so the scheduler knows not to wait on us.
    WINMALI_TRACE("ControlInterrupt: type=%d enable=%u", InterruptType, Enable);
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// Render pipeline — stubs until CSF / paging are real. Several of these
// pointers must be non-NULL for DxgkInitialize on current Windows builds.
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
WinMaliKmdCreateDevice(_In_ const HANDLE hAdapter, _Inout_ DXGKARG_CREATEDEVICE* p)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromDxgkHandle(hAdapter);
    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (adapter == NULL) {
        WINMALI_WARN(
            "CreateDevice: no adapter for hAdapter=%p (saved DxgkHandle=%p g_WinMaliAdapter=%p)",
            hAdapter,
            g_WinMaliAdapter != NULL ? g_WinMaliAdapter->DxgkHandle : NULL,
            g_WinMaliAdapter);
        return STATUS_INVALID_PARAMETER;
    }
    // Per-device handle: use adapter pointer so Escape resolves hDevice.
    // Full per-context queues and fences come with the real submit path.
    p->hDevice = (HANDLE)adapter;
    WINMALI_TRACE("CreateDevice: handing back adapter %p as hDevice (dxgk=%p)",
                  adapter, hAdapter);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdDestroyDevice(_In_ const HANDLE hDevice)
{
    UNREFERENCED_PARAMETER(hDevice);
    WINMALI_ENTER();
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdCreateAllocation(_In_ const HANDLE hAdapter, _Inout_ DXGKARG_CREATEALLOCATION* p)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

    WINMALI_ENTER();
    WINMALI_TRACE(
        "CreateAllocation: MmuScratchHeap=%p (VIDMM CreateAllocation not wired yet)",
        (adapter != NULL) ? adapter->MmuScratchHeapVa : NULL);
    UNREFERENCED_PARAMETER(p);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdDestroyAllocation(_In_ const HANDLE hAdapter, _In_ const DXGKARG_DESTROYALLOCATION* p)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(p);
    WINMALI_ENTER();
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdOpenAllocation(_In_ const HANDLE hDevice, _In_ const DXGKARG_OPENALLOCATION* p)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(p);
    WINMALI_ENTER();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdCloseAllocation(_In_ const HANDLE hDevice, _In_ const DXGKARG_CLOSEALLOCATION* p)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(p);
    WINMALI_ENTER();
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdDescribeAllocation(
    _In_ const HANDLE hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pDescribeAllocation);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdGetStandardAllocationDriverData(
    _In_ const HANDLE hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pGetStandardAllocationDriverData)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetStandardAllocationDriverData);
    WINMALI_WARN("GetStandardAllocationDriverData: NOT_SUPPORTED (stub)");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY
WinMaliKmdPatch(_In_ const HANDLE hAdapter, _In_ const DXGKARG_PATCH* pPatch)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdSubmitCommand(
    _In_ const HANDLE hAdapter,
    _In_ const DXGKARG_SUBMITCOMMAND* pSubmitCommand)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSubmitCommand);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdPreemptCommand(
    _In_ const HANDLE hAdapter,
    _In_ const DXGKARG_PREEMPTCOMMAND* pPreemptCommand)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    // No GPU preemption path yet; success avoids wedging the scheduler.
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdBuildPagingBuffer(
    _In_ const HANDLE hAdapter,
    _In_ DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pBuildPagingBuffer);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
WinMaliKmdQueryCurrentFence(
    _In_ const HANDLE hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE* pCurrentFence)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

    if (pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (adapter != NULL) {
        // Some WDKs typedef CurrentFence as UINT; others use UINT64 — avoid C4242.
        UINT64 fence64 = (UINT64)adapter->GpuCompletedFence;
        RtlCopyMemory(&pCurrentFence->CurrentFence, &fence64, sizeof(pCurrentFence->CurrentFence));
    } else {
        UINT64 z = 0ull;
        RtlCopyMemory(&pCurrentFence->CurrentFence, &z, sizeof(pCurrentFence->CurrentFence));
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdCancelCommand(
    _In_ const HANDLE hAdapter,
    _In_ const DXGKARG_CANCELCOMMAND* pCancelCommand)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
WinMaliGpuResetRecovery_(_Inout_ PWINMALI_ADAPTER Adapter)
{
    NTSTATUS st;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    InterlockedExchange64(&Adapter->GpuSubmittedFence, 0);
    InterlockedExchange64(&Adapter->GpuCompletedFence, 0);

    WinMaliFwTeardown(Adapter);

    if (!Adapter->GpuRegsMapped) {
        WINMALI_WARN("GpuResetRecovery: MMIO not mapped");
        return STATUS_DEVICE_NOT_READY;
    }

    st = WinMaliFwInit(Adapter);
    if (NT_SUCCESS(st)) {
        Adapter->GpuFwParkedForD3 = FALSE;
        WINMALI_TRACE("GpuResetRecovery: WinMaliFwInit OK");
    } else {
        WINMALI_WARN("GpuResetRecovery: WinMaliFwInit failed 0x%08x", st);
    }

    return st;
}

NTSTATUS APIENTRY
WinMaliKmdResetFromTimeout(_In_ const HANDLE hAdapter)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

    WINMALI_WARN("ResetFromTimeout: tearing down CSF + re-init firmware");
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    return WinMaliGpuResetRecovery_(adapter);
}

NTSTATUS APIENTRY
WinMaliKmdRestartFromTimeout(_In_ const HANDLE hAdapter)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

    WINMALI_TRACE("RestartFromTimeout: same recovery as reset");
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    return WinMaliGpuResetRecovery_(adapter);
}

NTSTATUS APIENTRY
WinMaliKmdRender(_In_ const HANDLE hContext, _Inout_ DXGKARG_RENDER* pRender)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    return STATUS_NOT_IMPLEMENTED;
}


NTSTATUS APIENTRY
WinMaliKmdIsSupportedVidPn(_In_ const HANDLE hAdapter, _Inout_ DXGKARG_ISSUPPORTEDVIDPN* p)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // The "blank desktop" VidPn (hDesiredVidPn == NULL) must be supported
    // by every WDDM driver - it represents the OS's "all monitors off"
    // mode. Returning FALSE here is what makes dxgkrnl decide we cannot
    // honor any topology and stop the device immediately after Start.
    //
    if (p->hDesiredVidPn == 0) {
        p->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    //
    // For real topologies: we currently support exactly one path (Source 0
    // -> PrimaryConnector at the GOP-captured mode). Rather than walk the
    // VidPn here (Source/Target enumeration, mode-set inspection, etc.) we
    // accept any topology and let CommitVidPn / EnumVidPnCofuncModality do
    // the real validation - they already handle the "unrecognized target"
    // case by warning + STATUS_SUCCESS. This is the same pragmatic strategy
    // used by ROSKMD's render-only sample.
    //
    UNREFERENCED_PARAMETER(a);
    p->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdRecommendFunctionalVidPn(_In_ const HANDLE hAdapter, _In_ const DXGKARG_RECOMMENDFUNCTIONALVIDPN* p)
{
    return Rk3588DispRecommendFunctionalVidPn(hAdapter, p);
}

NTSTATUS APIENTRY
WinMaliKmdEnumVidPnCofuncModality(_In_ const HANDLE hAdapter, _In_ const DXGKARG_ENUMVIDPNCOFUNCMODALITY* p)
{
    return Rk3588DispEnumVidPnCofuncModality(hAdapter, p);
}


NTSTATUS APIENTRY
WinMaliKmdSetVidPnSourceVisibility(_In_ const HANDLE hAdapter, _In_ const DXGKARG_SETVIDPNSOURCEVISIBILITY* p)
{
    return Rk3588DispSetVidPnSourceVisibility(hAdapter, p);
}

NTSTATUS APIENTRY
WinMaliKmdCommitVidPn(_In_ const HANDLE hAdapter, _In_ const DXGKARG_COMMITVIDPN* p)
{
    return Rk3588DispCommitVidPn(hAdapter, p);
}

NTSTATUS APIENTRY
WinMaliKmdUpdateActiveVidPnPresentPath(_In_ const HANDLE hAdapter, _In_ const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* p)
{
    return Rk3588DispUpdateActiveVidPnPresentPath(hAdapter, p);
}

NTSTATUS APIENTRY
WinMaliKmdRecommendMonitorModes(_In_ const HANDLE hAdapter, _In_ const DXGKARG_RECOMMENDMONITORMODES* p)
{
    return Rk3588DispRecommendMonitorModes(hAdapter, p);
}

NTSTATUS APIENTRY
WinMaliKmdStopDeviceAndReleasePostDisplayOwnership(
    _In_  const PVOID                     MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION       DisplayInfo)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);

    WINMALI_ENTER();
    if (a == NULL || DisplayInfo == NULL) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
    if (a->Gop.Valid) {
        DisplayInfo->Width         = a->Gop.Width;
        DisplayInfo->Height        = a->Gop.Height;
        DisplayInfo->Pitch         = a->Gop.Pitch;
        DisplayInfo->ColorFormat   = a->Gop.ColorFormat;
        DisplayInfo->PhysicAddress = a->Gop.PhysBase;
        DisplayInfo->TargetId      = (D3DDDI_VIDEO_PRESENT_TARGET_ID)a->PrimaryConnector;
        DisplayInfo->AcpiId        = a->PrimaryConnector;
    }
    
    return STATUS_SUCCESS;

}

// ---------------------------------------------------------------------------
// Escape channel (host-side diagnostics).
//
// The escape path is a tiny RPC-over-D3DKMT that lets user-mode tools
// query the driver without needing a working D3D device.
//
// Security model: the caller is a user-mode process (winmali-diag.exe).
// We MUST NOT trust input sizes or offsets; every opcode is a fixed-size
// struct and we reject anything else. We also NEVER expose arbitrary
// register reads to user-mode - only a filtered, driver-author-approved
// set.
// ---------------------------------------------------------------------------

static NTSTATUS
WinMaliEscapeSubmitRawShader_(
    _Inout_ PWINMALI_ADAPTER          Adapter,
    _Inout_ WINMALI_ESCAPE_RAW_SHADER* Io,
    _In_    SIZE_T                    PrivateDriverDataSize)
{
    NTSTATUS status;
    ULONG    shaderBytes;
    SIZE_T   need;

    if (PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_RAW_SHADER)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Io->OutValue       = 0;
    Io->IoStatus       = (ULONG)STATUS_UNSUCCESSFUL;
    Io->McuAlive       = 0;
    Io->SubmittedFence = 0;
    Io->CompletedFence = 0;
    Io->Reserved       = 0;

    Io->Header.Magic   = WINMALI_ESCAPE_MAGIC;
    Io->Header.Opcode  = WinMaliEscapeOp_SubmitRawShader;
    Io->Header.Version = WINMALI_ESCAPE_VERSION;

    if (Io->BlobMagic != WINMALI_SHADER_BLOB_MAGIC) {
        Io->IoStatus = (ULONG)STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    shaderBytes = Io->ShaderByteCount;
    if (shaderBytes > WINMALI_RAW_SHADER_MAX_BYTES) {
        Io->IoStatus = (ULONG)STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    need = (SIZE_T)WINMALI_ESCAPE_RAW_SHADER_TOTAL_BYTES(shaderBytes);
    if (PrivateDriverDataSize < need) {
        Io->IoStatus = (ULONG)STATUS_BUFFER_TOO_SMALL;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Io->McuAlive = (Adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_MCU_ALIVE) ? 1u : 0u;
    Io->SubmittedFence = (ULONGLONG)InterlockedIncrement64(&Adapter->GpuSubmittedFence);

    if (Io->McuAlive != 0) {
        if ((Adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_CSF_JOBS) != 0
            && shaderBytes == sizeof(WinMaliShaderNop)) {
            const UCHAR* sb = (const UCHAR*)Io + sizeof(WINMALI_ESCAPE_RAW_SHADER);
            if (RtlCompareMemory(sb, WinMaliShaderNop, sizeof(WinMaliShaderNop)) == sizeof(WinMaliShaderNop)) {
                status = WinMaliCsfSubmitNopJob(Adapter);
                Io->OutValue = (NT_SUCCESS(status) ? (Io->InValue ^ 0xA5A5A5A5u) : 0u);
            } else {
                WINMALI_TRACE("SubmitRawShader: MCU alive but payload is not the built-in NOP blob");
                Io->OutValue = Io->InValue;
                status       = STATUS_NOT_IMPLEMENTED;
            }
        } else if (shaderBytes >= 4u) {
            const UCHAR* sb = (const UCHAR*)Io + sizeof(WINMALI_ESCAPE_RAW_SHADER);

            WINMALI_TRACE(
                "SubmitRawShader: ctx=%p MCU alive, first shader bytes=%02x %02x %02x %02x (no CSF path)",
                Adapter,
                sb[0],
                sb[1],
                sb[2],
                sb[3]);
            Io->OutValue = Io->InValue;
            status       = STATUS_NOT_IMPLEMENTED;
        } else {
            WINMALI_TRACE("SubmitRawShader: ctx=%p MCU alive (shader payload empty)", Adapter);
            Io->OutValue = Io->InValue;
            status       = STATUS_NOT_IMPLEMENTED;
        }
    } else if ((Io->Flags & WINMALI_RAW_SHADER_FLAG_CPU_SIMULATE) != 0) {
        Io->OutValue = Io->InValue + 1u;
        status       = STATUS_SUCCESS;
    } else {
        Io->OutValue = 0;
        status       = STATUS_DEVICE_NOT_READY;
    }

    Io->CompletedFence = (ULONGLONG)InterlockedIncrement64(&Adapter->GpuCompletedFence);
    Io->IoStatus       = (ULONG)status;
    return status;
}

static NTSTATUS
WinMaliEscapeGetDiagnostics_(
    _In_  PWINMALI_ADAPTER       adapter,
    _In_  const DXGKARG_ESCAPE*  pEscape,
    _Out_ WINMALI_ESCAPE_DIAG_OUT* out)
{
    RtlZeroMemory(out, sizeof(*out));

    out->Header.Magic   = WINMALI_ESCAPE_MAGIC;
    out->Header.Opcode  = WinMaliEscapeOp_GetDiagnostics;
    out->Header.Version = WINMALI_ESCAPE_VERSION;

    out->KmdMajorVersion = WINMALI_KMD_MAJOR;
    out->KmdMinorVersion = WINMALI_KMD_MINOR;
    out->WddmVersion     = DXGKDDI_WDDMv2;
    out->AdapterFlags    = adapter->AdapterFlags;

    out->MmioPhysBase    = (ULONGLONG)adapter->GpuRegsPhys.QuadPart;
    out->MmioPhysSize    = (ULONGLONG)adapter->GpuRegsSize;
    out->MmioMapped      = adapter->GpuRegsMapped ? 1UL : 0UL;
    out->InterruptConnected = adapter->InterruptConnected ? 1UL : 0UL;

    out->GpuId           = adapter->Hw.GpuId;
    out->CsfId           = adapter->Hw.CsfId;
    out->GpuRevId        = adapter->Hw.RevId;
    out->L2Features      = adapter->Hw.L2Features;
    out->MmuFeatures     = adapter->Hw.MmuFeatures;
    out->AsPresent       = adapter->Hw.AsPresent;

    out->ArchMajor       = adapter->Hw.ArchMajor;
    out->ArchMinor       = adapter->Hw.ArchMinor;
    out->ArchRev         = adapter->Hw.ArchRev;
    out->ProdMajor       = adapter->Hw.ProdMajor;
    out->VerMajor        = adapter->Hw.VerMajor;
    out->VerMinor        = adapter->Hw.VerMinor;
    out->VerStatus       = adapter->Hw.VerStatus;

    // Counter reads are racy w.r.t. the ISR but we only ever report
    // them to a human-readable tool - no need for an interlocked read.
    out->InterruptsTotal    = (ULONGLONG)adapter->InterruptsTotal;
    out->InterruptsHandled  = (ULONGLONG)adapter->InterruptsHandled;
    out->InterruptsSpurious = (ULONGLONG)adapter->InterruptsSpurious;

    UNREFERENCED_PARAMETER(pEscape);
    return STATUS_SUCCESS;
}

static NTSTATUS
WinMaliEscapeGetMmioSnapshot_(
    _In_  PWINMALI_ADAPTER       adapter,
    _Out_ WINMALI_ESCAPE_MMIO_OUT* out)
{
    RtlZeroMemory(out, sizeof(*out));

    out->Header.Magic   = WINMALI_ESCAPE_MAGIC;
    out->Header.Opcode  = WinMaliEscapeOp_GetMmioSnapshot;
    out->Header.Version = WINMALI_ESCAPE_VERSION;

    if (!adapter->GpuRegsMapped) {
        // MMIO not mapped - callers should read back zero and combine
        // with the MmioMapped flag from GetDiagnostics to know why.
        return STATUS_SUCCESS;
    }

    out->GpuStatus       = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_STATUS);
    out->GpuFaultStatus  = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_FAULT_STATUS);
    out->GpuFaultAddrLo  = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_FAULT_ADDR_LO);
    out->GpuFaultAddrHi  = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_FAULT_ADDR_HI);
    out->GpuIntRawStat   = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_IRQ_RAWSTAT);
    out->GpuIntStat      = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_IRQ_STATUS);
    out->McuStatus       = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_MCU_STATUS);
    out->JobIntStat      = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_JOB_INT_STAT);
    out->MmuIntStat      = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_MMU_INT_STAT);

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdEscape(
    _In_ const HANDLE           hAdapter,
    _In_ const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER             adapter;
    const WINMALI_ESCAPE_HEADER* hdr;

    UNREFERENCED_PARAMETER(hAdapter);

    if (pEscape == NULL
     || pEscape->pPrivateDriverData == NULL
     || pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_HEADER)) {
        return STATUS_INVALID_PARAMETER;
    }

    // The host tool may invoke either an adapter-level escape (hDevice
    // unused) or a device-level escape. For device escapes we handed
    // back the miniport adapter as hDevice in CreateDevice, so both
    // paths eventually resolve to the same context. Try hDevice first,
    // fall back to resolving via the opaque DXGK adapter handle.
    adapter = WinMaliAdapterFromContext(pEscape->hDevice);
    if (adapter == NULL) {
        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    }
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    hdr = (const WINMALI_ESCAPE_HEADER*)pEscape->pPrivateDriverData;
    if (hdr->Magic != WINMALI_ESCAPE_MAGIC) {
        WINMALI_WARN("Escape with wrong magic 0x%08x", hdr->Magic);
        return STATUS_INVALID_PARAMETER;
    }
    if (hdr->Version != WINMALI_ESCAPE_VERSION) {
        WINMALI_WARN("Escape version mismatch: caller=%u, driver=%u",
                     hdr->Version, WINMALI_ESCAPE_VERSION);
        return STATUS_REVISION_MISMATCH;
    }

    WINMALI_TRACE("Escape opcode=%u size=%u", hdr->Opcode,
                  pEscape->PrivateDriverDataSize);

    switch (hdr->Opcode) {

    case WinMaliEscapeOp_GetDiagnostics:
        if (pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_DIAG_OUT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        return WinMaliEscapeGetDiagnostics_(
            adapter, pEscape,
            (WINMALI_ESCAPE_DIAG_OUT*)pEscape->pPrivateDriverData);

    case WinMaliEscapeOp_GetMmioSnapshot:
        if (pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_MMIO_OUT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        return WinMaliEscapeGetMmioSnapshot_(
            adapter,
            (WINMALI_ESCAPE_MMIO_OUT*)pEscape->pPrivateDriverData);

    case WinMaliEscapeOp_PingMmu:
        if (pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_MMU_OUT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        return WinMaliMmuEscapePing(
            adapter,
            (WINMALI_ESCAPE_MMU_OUT*)pEscape->pPrivateDriverData);

    case WinMaliEscapeOp_PanfrostSubmit: {
        const UCHAR* buf = (const UCHAR*)pEscape->pPrivateDriverData;
        unsigned int boCount;
        unsigned int need;

        if (pEscape->PrivateDriverDataSize
            < sizeof(WINMALI_ESCAPE_HEADER) + WINMALI_PANFROST_SUBMIT_STRUCT_BYTES) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        boCount = *(const unsigned int*)(buf + WINMALI_PANFROST_SUBMIT_BO_HANDLE_COUNT_OFFSET);
        need = WINMALI_PANFROST_SUBMIT_REQUIRED_BYTES(boCount);
        if (pEscape->PrivateDriverDataSize < need) {
            WINMALI_WARN("PanfrostSubmit: size=%u need=%u bo_count=%u",
                         pEscape->PrivateDriverDataSize, need, boCount);
            return STATUS_BUFFER_TOO_SMALL;
        }

        {
            ULONGLONG jc = *(const ULONGLONG*)(buf + sizeof(WINMALI_ESCAPE_HEADER));
            WINMALI_TRACE("PanfrostSubmit: jc=0x%llx bo_count=%u payload=%u (parse-only)",
                          jc, boCount, pEscape->PrivateDriverDataSize);
        }

        /* Full JM / memory resolve is later; returning success lets Mesa UMD verify path. */
        return STATUS_SUCCESS;
    }

    case WinMaliEscapeOp_SubmitRawShader: {
        WINMALI_ESCAPE_RAW_SHADER* io = (WINMALI_ESCAPE_RAW_SHADER*)pEscape->pPrivateDriverData;

        if (pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_RAW_SHADER)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        return WinMaliEscapeSubmitRawShader_(
            adapter,
            io,
            (SIZE_T)pEscape->PrivateDriverDataSize);
    }

    default:
        WINMALI_WARN("Escape: unsupported opcode=%u", hdr->Opcode);
        return STATUS_NOT_SUPPORTED;
    }
}
