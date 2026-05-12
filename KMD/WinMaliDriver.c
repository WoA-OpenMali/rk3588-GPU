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

    //
    // Process list + AS-slot allocator are touched from PASSIVE (CreateProcess
    // / DestroyProcess / DxgkDdiSetRootPageTable) but the spinlocks let us run
    // safely at DISPATCH if dxgk ever escalates. Initialised here so QAI calls
    // (which can arrive before StartDevice on some PnP races) see a sane state.
    //
    KeInitializeSpinLock(&adapter->ProcessListLock);
    InitializeListHead(&adapter->ProcessList);
    adapter->ActiveProcessCount = 0;
    WinMaliMmuInitAsAllocator(adapter);

    //
    // Route B aperture page table. dxgk MAP/UNMAP_APERTURE_SEGMENT can fire
    // as soon as the first VIDMM allocation lands in the aperture - typically
    // right after StartDevice - so we allocate the tracking table up-front
    // and surface it through QUERYSEGMENT*. Pool allocation can fail under
    // tight memory; we treat that as "no aperture", but still proceed: the
    // adapter will fall back to publishing only the GOP local segment.
    //
    adapter->AperturePageCount = WINMALI_APERTURE_SEGMENT_PAGES;
    adapter->AperturePageTable = (PPFN_NUMBER)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        adapter->AperturePageCount * sizeof(PFN_NUMBER),
        WINMALI_POOL_TAG);
    if (adapter->AperturePageTable == NULL) {
        WINMALI_WARN(
            "AddDevice: aperture page table alloc failed (%llu bytes); "
            "disabling aperture segment",
            (ULONGLONG)(adapter->AperturePageCount * sizeof(PFN_NUMBER)));
        adapter->AperturePageCount   = 0;
        adapter->ApertureSegmentBytes = 0;
    } else {
        adapter->ApertureSegmentBytes = WINMALI_APERTURE_SEGMENT_BYTES;
    }

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

    //
    // BringupHardware probed `GPU_AS_PRESENT`. Re-seed the allocator so
    // dynamic AS slots are picked from the real HW bitmap (G610 reports 0xFF
    // = 8 slots) instead of the placeholder we set up in AddDevice.
    //
    WinMaliMmuInitAsAllocator(adapter);

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
    {
        NTSTATUS voSt = WinMaliVop2SetupSysmemScanout(adapter);
        if (!NT_SUCCESS(voSt)) {
            WINMALI_WARN(
                "WinMaliVop2SetupSysmemScanout failed 0x%08x — "
                "single-segment render-only path, primaries stay on seg 1",
                voSt);
        }
    }

    //
    // Sources: one VidPN source (the desktop). We always claim 1 so the
    // OS can build a desktop topology even if GOP capture failed.
    // Children: expose the primary connector unconditionally, matching
    // BasicDisplay's display-first bring-up. QueryDeviceDescriptor already
    // warns if the synthesized 1080p EDID doesn't match the captured GOP.
    //
    if (NumberOfVideoPresentSources != NULL) {
        *NumberOfVideoPresentSources = 1;
    }
    if (NumberOfChildren != NULL) {
        *NumberOfChildren = 1u;
    }

    WINMALI_TRACE("StartDevice OK: sources=1 children=%u gop_valid=%u "
                  "mmio_mapped=%u irq_ok=%u",
                  1u,
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
        KIRQL irql;
        ULONG leaked = 0;

        if (!adapter->StartDeviceEverSucceeded) {
            WINMALI_WARN(
                "RemoveDevice: StartDevice never reached success (ctx=%p) -- "
                "dxgk/PnP failed before or during start",
                adapter);
        }

        //
        // Drain the KMD process list. Normally DxgkDdiDestroyProcess clears
        // every entry; if dxgk teardown skipped any (surprise remove, failed
        // init), we free them here so RemoveDevice doesn't leak pool. AS-slot
        // teardown is implicit via WinMaliMmuTeardown below.
        //
        KeAcquireSpinLock(&adapter->ProcessListLock, &irql);
        while (!IsListEmpty(&adapter->ProcessList)) {
            PLIST_ENTRY entry = RemoveHeadList(&adapter->ProcessList);
            PWINMALI_KMD_PROCESS proc = CONTAINING_RECORD(
                entry, WINMALI_KMD_PROCESS, AdapterLink);
            KeReleaseSpinLock(&adapter->ProcessListLock, irql);
            WINMALI_WARN(
                "RemoveDevice: leaked KmdProcess=%p hDxgk=%p flags=0x%x",
                proc, proc->hDxgkProcess, proc->Flags);
            proc->Magic = 0;
            ExFreePoolWithTag(proc, WINMALI_POOL_TAG);
            ++leaked;
            KeAcquireSpinLock(&adapter->ProcessListLock, &irql);
        }
        adapter->ActiveProcessCount = 0;
        KeReleaseSpinLock(&adapter->ProcessListLock, irql);
        if (leaked != 0) {
            WINMALI_WARN("RemoveDevice: freed %u leaked KmdProcess struct(s)", leaked);
        }

        // Defensive - StopDevice should already have run, but be
        // idempotent so surprise removal still unmaps MMIO.
        Rk3588DispReleaseGopFb(adapter);
        WinMaliDisconnectInterrupt(adapter);
        WinMaliFwTeardown(adapter);
        adapter->GpuFwParkedForD3 = FALSE;
        WinMaliMmuTeardown(adapter);
        WinMaliTeardownHardware(adapter);

        if (adapter->GopRuntimeVa != NULL) {
            MmUnmapIoSpace(adapter->GopRuntimeVa, adapter->GopRuntimeBytes);
            adapter->GopRuntimeVa    = NULL;
            adapter->GopRuntimeBytes = 0;
        }
        if (adapter->AperturePageTable != NULL) {
            ExFreePoolWithTag(adapter->AperturePageTable, WINMALI_POOL_TAG);
            adapter->AperturePageTable   = NULL;
            adapter->AperturePageCount   = 0;
            adapter->ApertureSegmentBytes = 0;
        }

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

//
// Route B segment publication. The four QUERYSEGMENT* variants
// (QUERYSEGMENTCOUNT, QUERYSEGMENT3, QUERYSEGMENT4, QUERYSEGMENT5) all need
// the same data: a list of {Size, BaseAddress, CpuTranslatedAddress, Flags}.
// We compute it once via WinMaliBuildSegmentList_ and each query handler
// copies the relevant fields into its version-specific descriptor.
//
typedef struct _WINMALI_SEGMENT_DESC {
    UINT              SegmentId;            // 1-based dxgk id
    SIZE_T            Size;                 // bytes (page aligned)
    PHYSICAL_ADDRESS  BaseAddress;          // GPU logical base PA
    PHYSICAL_ADDRESS  CpuTranslatedAddress; // CPU physical address
    ULONG             Flags;                // WINMALI_SEGFLAG_*
} WINMALI_SEGMENT_DESC;

#define WINMALI_SEGFLAG_APERTURE        0x00000001u
#define WINMALI_SEGFLAG_CPU_VISIBLE     0x00000002u
#define WINMALI_SEGFLAG_CACHE_COHERENT  0x00000004u
#define WINMALI_SEGFLAG_DIRECT_FLIP     0x00000008u
#define WINMALI_SEGFLAG_NONLOCAL_BUDGET 0x00000010u
#define WINMALI_SEGFLAG_POP_SYSMEM      0x00000020u
#define WINMALI_SEGFLAG_LOCAL_BUDGET    0x00000040u

static UINT
WinMaliBuildSegmentList_(
    _In_  PWINMALI_ADAPTER       adapter,
    _Out_writes_(2) WINMALI_SEGMENT_DESC* descs)
{
    UINT n = 0;

    RtlZeroMemory(descs, sizeof(WINMALI_SEGMENT_DESC) * 2u);

    //
    // Segment id 1: dedicated PopulatedFromSystemMemory + CpuVisible block
    // backing kernel DMA buffers and small non-primary D3D allocations.
    //
    // History:
    //   * v1 attempted a 64 MB Aperture=1 segment here. With every flag
    //     valid per MSDN AND SectionBackedPrimary cleared, dxgmms2 still
    //     silently StopDeviced us right after QUERYSEGMENT4 pass-2 - the
    //     diagnostic dump showed exactly the intended dxgk flags 0x00100015
    //     (Aperture|CpuVisible|CacheCoherent|NonLocalBudget). Conclusion:
    //     in GpuMmu mode (we report GpuMmuSupported=1 + GpuMmu update
    //     mode CPU_VIRTUAL) the legacy "aperture window" model is
    //     redundant and dxgmms2 treats Aperture=1 + GpuMmu=1 as a
    //     contract conflict.
    //   * v2 dropped this segment entirely and published only the GOP
    //     local. dxgk proceeded past QUERYSEGMENT4, but then bug-checked
    //     in dxgmms2!AddDmaBufferToPool+0x6c8 (AV) when csrss.exe's
    //     CreateContext tried to back its DMA buffer in a non-existent
    //     "sysmem" placeholder. DmaBufferSegmentSet=0 is not a valid
    //     "anywhere in sysmem" signal on Win11 26100 - dxgmms2 needs a
    //     real segment dxgk owns to place 4 KiB DMA buffers into.
    //   * v3 (current): publish a real PopulatedFromSystemMemory segment
    //     backed by Adapter->DmaSegment* (allocated in WinMaliMmuInit).
    //     PopulatedFromSystemMemory + CpuVisible + NonLocalBudget is the
    //     same flag combination that the previous "fallback" segment
    //     used successfully, just promoted to a primary publication.
    //     This gives dxgmms2 a 4 MiB pool for DMA buffers without
    //     touching the GOP framebuffer (segment id 2).
    //
    if (adapter != NULL
        && adapter->DmaSegmentVa != NULL
        && adapter->DmaSegmentBytes != 0)
    {
        descs[n].SegmentId                     = WINMALI_APERTURE_SEGMENT_ID;
        descs[n].Size                          = adapter->DmaSegmentBytes;
        descs[n].BaseAddress.QuadPart          = (LONGLONG)WINMALI_APERTURE_GPU_BASE;
        descs[n].CpuTranslatedAddress          = adapter->DmaSegmentPhys;
        descs[n].Flags = WINMALI_SEGFLAG_CPU_VISIBLE
                       | WINMALI_SEGFLAG_POP_SYSMEM
                       | WINMALI_SEGFLAG_NONLOCAL_BUDGET;
        ++n;
    }

    //
    // Segment id 2: same slab WinMaliVop2SetupSysmemScanout allocates —
    // contiguous cached CPU RAM, copied from the GOP image then handed
    // to VOP2 via YRGB_MST. PopulatedFromSystemMemory + CpuVisible +
    // DirectFlip + LocalBudgetGroup — accepted by dxgk as a pair with
    // segment 1 (both sysmem-class), unlike the old BIOS-reserved GOP PA.
    //
    if (adapter != NULL
        && adapter->ScanoutSegmentVa != NULL
        && adapter->ScanoutSegmentBytes != 0)
    {
        descs[n].SegmentId                     = WINMALI_GOP_SEGMENT_ID;
        descs[n].Size                          = adapter->ScanoutSegmentBytes;
        descs[n].BaseAddress.QuadPart          = (LONGLONG)WINMALI_GOP_GPU_BASE;
        descs[n].CpuTranslatedAddress          = adapter->ScanoutSegmentPhys;
        descs[n].Flags =
            WINMALI_SEGFLAG_CPU_VISIBLE
          | WINMALI_SEGFLAG_POP_SYSMEM
          | WINMALI_SEGFLAG_DIRECT_FLIP
          | WINMALI_SEGFLAG_NONLOCAL_BUDGET;
        ++n;
    }

    //
    // Historical note: publishing BIOS-reserved GOP PA as segment id 2
    // alongside segment 1 failed dxgk validation (StopDevice after
    // QUERYSEGMENT4). Segment id 2 is now driver-allocated contiguous
    // sysmem (see WinMaliVop2SetupSysmemScanout).
    //
    // Phase 2 (VOP2 sprint) brings up the VOP2 IP, allocates a real
    // sysmem-backed primary slab, publishes it as id 2 with
    // CpuVisible + DirectFlip + LocalBudgetGroup, sets DRIVERCAPS
    // .MemoryManagementCaps.SectionBackedPrimary=1 and
    // .SupportDirectFlip=1, and points VOP2's VP-A scan-out address at
    // each new flip target via SetVidPnSourceAddress. At that point the
    // 2-segment publication is a sysmem+sysmem pair, which is the topology
    // dxgk accepts under GpuMmu.
    //

    //
    // Belt-and-braces fallback: if neither the aperture nor the GOP was
    // available (out of pool, no GOP capture), expose the static MMU scratch
    // heap as a single sysmem segment so VIDMM at least has SOMETHING to
    // bind to. Without this dxgkrnl aborts with no segments published.
    //
    // Even the fallback descriptor uses WINMALI_APERTURE_GPU_BASE because
    // PHYSICALADAPTERCAPS.MinimumAddress is 0x10000 and the raw
    // MmuScratchHeapPhys is below it. CacheCoherent is intentionally NOT
    // set here (the fallback is a memory segment, not an aperture, so
    // CacheCoherent is illegal per the same MSDN rule above).
    //
    if (n == 0 && adapter != NULL && adapter->MmuScratchHeapVa != NULL) {
        descs[0].SegmentId                     = WINMALI_APERTURE_SEGMENT_ID;
        descs[0].Size                          = adapter->MmuScratchHeapBytes;
        descs[0].BaseAddress.QuadPart          = (LONGLONG)WINMALI_APERTURE_GPU_BASE;
        descs[0].CpuTranslatedAddress          = adapter->MmuScratchHeapPhys;
        descs[0].Flags = WINMALI_SEGFLAG_CPU_VISIBLE
                       | WINMALI_SEGFLAG_POP_SYSMEM
                       | WINMALI_SEGFLAG_NONLOCAL_BUDGET;
        n = 1;
    }

    return n;
}

static NTSTATUS
WinMaliKmdQueryAdapterInfoImpl(
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
        SIZE_T           outSz = pQueryAdapterInfo->OutputDataSize;

        //
        // Buffer sizing follows the negotiated DDI surface in
        // DRIVER_INITIALIZATION_DATA.Version (WinMaliDxgkInitFill.c pins
        // DXGKDDI_INTERFACE_VERSION_WDDM2_4). On Win11 26100 dxgk passes
        // OutputDataSize = 584 for DRIVERCAPS — the layout through MiscCaps
        // only. Our WDK still compiles DXGK_DRIVERCAPS with the WDDM2_9 tail
        // (MaxHwQueuedFlips + HwQueuedFlipCaps), so sizeof(*caps) == 592.
        //
        // Requiring sizeof(*caps) caused STATUS_BUFFER_TOO_SMALL / invalid
        // reject; RtlZeroMemory(caps, sizeof(*caps)) also scribbled 8 bytes
        // past the end of dxgk's 584-byte buffer (undefined behaviour).
        //
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
        CONST SIZE_T kDriverCapsMinOut = FIELD_OFFSET(DXGK_DRIVERCAPS, MaxHwQueuedFlips);
#else
        CONST SIZE_T kDriverCapsMinOut = sizeof(DXGK_DRIVERCAPS);
#endif
        if (caps == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        if (outSz < kDriverCapsMinOut) {
            WINMALI_WARN(
                "DRIVERCAPS: buffer too small have=%Iu need>=%Iu fullStruct=%Iu",
                outSz,
                kDriverCapsMinOut,
                (SIZE_T)sizeof(DXGK_DRIVERCAPS));
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(caps, outSz);

        //
        // DRIVERCAPS payload mirrored from render-only-sample
        // (roskmd\RosKmdAdapter.cpp DXGKQAITYPE_DRIVERCAPS) with the
        // "display" sub-block from its !IsRenderOnly() branch, because
        // our WinMali KMD is the unified Mali render + RK3588 VOP2
        // display "Full Graphics" adapter.
        //
        // Most-important consistency invariants dxgk validates here on
        // Win11 26200:
        //   * SchedulingCaps.PreemptionAware = 1 REQUIRES non-zero
        //     PreemptionCaps.Graphics/ComputePreemptionGranularity.
        //     Leaving those at zero contradicts PreemptionAware and is
        //     enough by itself to make dxgk decide we're malformed
        //     after the cap walk and surprise-remove us.
        //   * SupportNonVGA = 1 implies SupportSmoothRotation = 1
        //     (render-only-sample comment: "Must support updating path
        //     rotation in DxgkDdiUpdateActiveVidPnPresentPath").
        //   * SupportDirectFlip = 1 implies FlipCaps.FlipIndependent = 1
        //     and FlipCaps.FlipOnVSyncWithNoWait = 1; otherwise the
        //     DWM independent-flip path has nowhere to land.
        //   * Either set MemoryManagementCaps.SectionBackedPrimary = 1
        //     AND publish a DirectFlip-capable segment, or set neither.
        //     We are setting neither for now; primary lives in our
        //     normal segment once we add real VRAM.
        //
        caps->WDDMVersion = DXGKDDI_WDDMv2_4;
        caps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;

        //
        // PresentationCaps: copy the render-only-sample defaults so the
        // GDI/redirection bitmap path has sane numbers (DWM-off case
        // does screen-to-screen blit by default; we disable that).
        //
        caps->PresentationCaps.SupportKernelModeCommandBuffer = FALSE;
        caps->PresentationCaps.SupportSoftwareDeviceBitmaps   = TRUE;
        caps->PresentationCaps.NoScreenToScreenBlt            = TRUE;
        caps->PresentationCaps.NoOverlapScreenBlt             = TRUE;
        caps->PresentationCaps.MaxTextureWidthShift           = 3;   // 16K
        caps->PresentationCaps.MaxTextureHeightShift          = 3;   // 16K

        //
        // FlipCaps: full-graphics adapter, so we copy the !IsRenderOnly()
        // arm of the sample. MaxQueuedFlipOnVSync = 1 says the hardware
        // can store one pending flip; FlipOnVSyncMmIo + FlipOnVSyncWithNoWait
        // says "I program the new source address immediately and it
        // latches at the next VSync".
        //
        // FlipIndependent / SupportDirectFlip: RtlZeroMemory leaves them
        // false / zero. When WinMaliVop2SetupSysmemScanout succeeds we
        // publish segment id 2 with DirectFlip and enable the trio at the
        // end of this case (before TRACE); see WinMaliBuildSegmentList_.
        //
        caps->MaxQueuedFlipOnVSync           = 1;
        caps->FlipCaps.FlipOnVSyncMmIo       = TRUE;
        caps->FlipCaps.FlipOnVSyncWithNoWait = TRUE;
        caps->FlipCaps.FlipInterval          = FALSE;
        caps->FlipCaps.FlipImmediateMmIo     = FALSE;

        //
        // SchedulingCaps: render-only-sample sets MultiEngineAware,
        // CancelCommandAware, and PreemptionAware. We additionally keep
        // VSyncPowerSaveAware so dxgk's VSync power-save logic knows we
        // honor its requests.
        //
        caps->SchedulingCaps.MultiEngineAware    = 1;
        caps->SchedulingCaps.CancelCommandAware  = 1;
        caps->SchedulingCaps.PreemptionAware     = 1;
        caps->SchedulingCaps.VSyncPowerSaveAware = 1;

        //
        // MemoryManagementCaps:
        //   * CrossAdapterResource = 1 (sample): we allow allocations
        //     to be shared across adapters via the cross-adapter
        //     resource model.
        //   * SectionBackedPrimary: zero here; enabled at end of case when
        //     segment id 2 (sysmem scan-out slab) is published.
        //
        // GpuMmuSupported + VirtualAddressingSupported MUST agree with
        // DXGK_PHYSICALADAPTERCAPS.Flags.GpuMmuSupported and
        // DXGK_NODEMETADATA.GpuMmuSupported on every node. Leaving these
        // at zero while PHYSICALADAPTERCAPS claims GpuMmu=1 caused Win11
        // 26100 dxgk to abort the post-Start cap walk right after
        // GetNodeMetadata (no GPUMMUCAPS / QUERYSEGMENT* / display-ext
        // queries) and PnP-stop the device.
        //
        // DedicatedPagingEngine stays 0: PHYSICALADAPTERCAPS.PagingNodeIndex
        // == NumExecutionNodes (no dedicated paging DMA engine).
        //
        caps->MemoryManagementCaps.CrossAdapterResource         = 1;
        //
        // SectionBackedPrimary tells dxgk that primaries can be backed by a
        // Win32 section object. Section objects are sysmem-backed, so dxgk
        // requires the DirectFlip segment that will hold the primary to be
        // sysmem-compatible (typically PopulatedFromSystemMemory=1).
        //
        // SectionBackedPrimary defaults to 0 from RtlZeroMemory. When the
        // sysmem scan-out slab is published (segment 2, DirectFlip), we
        // set it to 1 at the end of this case — must match QUERYSEGMENT*.
        //
        caps->MemoryManagementCaps.SectionBackedPrimary         = 0;
        caps->MemoryManagementCaps.VirtualAddressingSupported   = 1;
        caps->MemoryManagementCaps.GpuMmuSupported              = 1;
        caps->MemoryManagementCaps.PagingNode                   = 0;
        caps->MemoryManagementCaps.IoMmuSupported               = 0;

        caps->MaxAllocationListSlotId    = 7;
        //
        // ApertureSegmentCommitLimit is the global "aperture-bytes
        // committable across all aperture segments" budget. We currently
        // publish zero aperture segments (see WinMaliBuildSegmentList_) -
        // GpuMmu+page-tables make the legacy aperture window redundant -
        // so the budget is zero.
        //
        caps->ApertureSegmentCommitLimit = 0;

        //
        // PreemptionCaps: REQUIRED whenever SchedulingCaps.PreemptionAware
        // is set (see invariants above). Render-only-sample reports
        // primitive-boundary graphics preemption and dispatch-boundary
        // compute preemption; those are the smallest claims that match a
        // modern WDDM context-scheduling driver. We can tighten later
        // once we wire real Mali pre-emption (mid-primitive / mid-triangle).
        //
        caps->PreemptionCaps.GraphicsPreemptionGranularity =
            D3DKMDT_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY;
        caps->PreemptionCaps.ComputePreemptionGranularity  =
            D3DKMDT_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY;

        //
        // SupportNonVGA tells dxgkrnl we're not a VGA-compatible adapter;
        // per the render-only-sample invariant, this REQUIRES
        // DxgkDdiStopDeviceAndReleasePostDisplayOwnership AND
        // SupportSmoothRotation = TRUE.
        //
        // Route B note: even though our EnumVidPnCofuncModality now only
        // advertises RotationSupport.Identity (we don't have a rotating
        // scan-out plane yet), SupportSmoothRotation MUST stay TRUE to keep
        // SupportNonVGA legal. The cap and the per-path support are not
        // contradictory: SupportSmoothRotation says "any rotation that I
        // *do* advertise per path can be applied without a mode change",
        // and since we advertise only Identity per path, the cap is
        // trivially true.
        //
        // SupportDirectFlip defaults to 0 (RtlZeroMemory). Enabled together
        // with FlipIndependent + SectionBackedPrimary when segment id 2 is
        // published — see block before WINMALI_TRACE below.
        //
        // SupportPerEngineTDR matches SchedulingCaps.MultiEngineAware.
        //
        caps->SupportNonVGA                 = TRUE;
        caps->SupportSmoothRotation         = TRUE;
        caps->SupportPerEngineTDR           = 1;
        caps->SupportRuntimePowerManagement = FALSE;

        //
        // WDDM 2.0+: GPU VA span managed by VIDMM. Must cover the VA space
        // implied by DXGKQAITYPE_GPUMMUCAPS (we report 48-bit VA there).
        // Zero,zero left dxgk with no span after we turned GpuMmuSupported on
        // in MemoryManagementCaps.
        //
        caps->InternalGpuVirtualAddressRangeStart = (D3DGPU_VIRTUAL_ADDRESS)0x0000000000010000ULL;
        caps->InternalGpuVirtualAddressRangeEnd   =
            (D3DGPU_VIRTUAL_ADDRESS)0x000000003FFFFFFFULL;

        //
        // One 3-D node is the usual minimum for a render adapter. Zero
        // nodes plus a stub GetNodeMetadata can prevent dxgk from ever
        // calling DxgkDdiStartDevice on recent builds.
        //
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;

        //
        // DirectFlip trio when contiguous scan-out sysmem is live (matches
        // WinMaliBuildSegmentList_ segment id 2).
        //
        {
            PWINMALI_ADAPTER adapterDf = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
            BOOLEAN          scanoutSegLive =
                (adapterDf != NULL
                 && adapterDf->ScanoutSegmentVa != NULL
                 && adapterDf->ScanoutSegmentBytes != 0);

            if (scanoutSegLive) {
                caps->FlipCaps.FlipIndependent                      = TRUE;
                caps->SupportDirectFlip                             = 1;
                caps->MemoryManagementCaps.SectionBackedPrimary     = 1;
            }
        }
        WINMALI_TRACE(
            "DRIVERCAPS: WDDM=0x%x preempt=gfx:Prim/cmp:Disp sched=ME+Cnl+Pmt+VSps "
            "flip=Mmio+NoWait maxQ=%u nonvga=1 smooth=1 dirFlip=%u indFlip=%u "
            "pTDR=1 memMgr=XAdpt+GpuMmu+VirtAddr gpuVA=[0x%llx,0x%llx] slots=%u "
            "apertureLimit=%llu nodes=%u",
            caps->WDDMVersion,
            caps->MaxQueuedFlipOnVSync,
            caps->SupportDirectFlip,
            caps->FlipCaps.FlipIndependent ? 1u : 0u,
            (ULONGLONG)caps->InternalGpuVirtualAddressRangeStart,
            (ULONGLONG)caps->InternalGpuVirtualAddressRangeEnd,
            caps->MaxAllocationListSlotId,
            (ULONGLONG)caps->ApertureSegmentCommitLimit,
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
        // Buffer sizing: with init.Version pinned to WDDM 2.4, dxgk passes
        // OutputDataSize = 24 — the layout through VPRPagingNode only. Our
        // WDK's DXGK_PHYSICALADAPTERCAPS also includes VirtualCopyNodeIndex
        // (WDDM 2.7), so sizeof(*phys) is 28. Requiring sizeof(*phys) or
        // RtlZeroMemory(phys, sizeof(*phys)) scribbled 4 bytes past the end
        // of dxgk's buffer and/or skipped returning valid caps, which showed
        // up as PnP-stop right after GetNodeMetadata despite GPUMMUCAPS
        // succeeding (dxgk had a corrupted / incomplete physical-adapter
        // snapshot).
        //
        DXGK_PHYSICALADAPTERCAPS* phys = (DXGK_PHYSICALADAPTERCAPS*)pQueryAdapterInfo->pOutputData;
        PWINMALI_ADAPTER          adapter;
        const DXGK_QUERYPHYSICALADAPTERCAPSIN* physIn;
        SIZE_T                    outSz = pQueryAdapterInfo->OutputDataSize;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
        CONST SIZE_T kPhysCapsMinOut = FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, VirtualCopyNodeIndex);
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        CONST SIZE_T kPhysCapsMinOut = sizeof(DXGK_PHYSICALADAPTERCAPS);
#else
        CONST SIZE_T kPhysCapsMinOut = sizeof(DXGK_PHYSICALADAPTERCAPS);
#endif

        if (phys == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        if (outSz < kPhysCapsMinOut) {
            WINMALI_WARN(
                "PHYSICALADAPTERCAPS: buffer too small have=%Iu need>=%Iu fullStruct=%Iu",
                outSz,
                kPhysCapsMinOut,
                (SIZE_T)sizeof(DXGK_PHYSICALADAPTERCAPS));
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

        RtlZeroMemory(phys, outSz);
        phys->NumExecutionNodes        = 1;
        //
        // PagingNodeIndex semantics per MSDN DXGK_PHYSICALADAPTERCAPS:
        //   * value < NumExecutionNodes: "node at this index is a DEDICATED
        //                                  paging engine" (EngineType must be
        //                                  DXGK_ENGINE_TYPE_PAGING in the
        //                                  matching DxgkDdiGetNodeMetadata).
        //   * value == NumExecutionNodes: "no dedicated paging node; paging
        //                                  buffers are dispatched on regular
        //                                  execution engines".
        //
        // Previously we wrote PagingNodeIndex=0 with NumExecutionNodes=1 AND
        // GetNodeMetadata(node 0) reported DXGK_ENGINE_TYPE_3D. Win11 26100
        // dxgk detected the contradiction (dedicated paging node also claims
        // to be a 3D execution engine), skipped the rest of the cap walk
        // (no GPUMMUCAPS, PAGETABLELEVELDESC, QUERYSEGMENT*, or DISPLAY
        // extension queries), and issued IRP_MN_STOP_DEVICE - exactly what
        // we were seeing right after GetNodeMetadata.
        //
        // Mali-G610 has no separate paging DMA engine; the CSF firmware
        // shares the JCS slot with rendering work, so paging happens on the
        // same node as 3D. Use the "no dedicated paging node" encoding.
        //
        phys->PagingNodeIndex          = phys->NumExecutionNodes;
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
        // are wired in WinMaliDxgkInitFill.c, backed by the AS-slot allocator
        // in WinMaliMmu.c (AsSlotInUseMask + AsBindings[]):
        //   - SetRootPageTable     : binds dxgk's root-PT PA to a free Mali
        //                            AS slot (AS2..AS7 dynamic; AS0/AS1 are
        //                            reserved for CSF MCU + bring-up).
        //                            Calls WinMaliMmuAsEnable for real.
        //   - GetRootPageTableSize : PAGE_SIZE (mandatory for LPAE-4-level/9
        //                            idx-bits as we publish in PT_LEVEL_DESC).
        //   - {Map,Unmap}CpuHostAperture : success no-op; we don't expose a
        //                            host-visible aperture yet, and
        //                            NOT_SUPPORTED here was tripping caps
        //                            walks on some 26100 paths.
        //
        phys->Flags.Value              = 0;
        phys->Flags.GpuMmuSupported    = 1;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        phys->VPRPagingNode            = phys->NumExecutionNodes;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
        if (outSz >= sizeof(DXGK_PHYSICALADAPTERCAPS)) {
            phys->VirtualCopyNodeIndex = phys->NumExecutionNodes;
        }
#endif

        WINMALI_TRACE(
            "PHYSICALADAPTERCAPS: stamp=%u outSz=%Iu hAdapter=%p DeviceHandle=%p ctx=%p "
            "execNodes=%u pagingIdx=%u(no-dedicated) flags=0x%x (GpuMmu=1)",
            WINMALI_KMD_CAP_STAMP,
            outSz,
            hAdapter,
            phys->DxgkPhysicalAdapterHandle,
            adapter,
            phys->NumExecutionNodes,
            phys->PagingNodeIndex,
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

    //
    // Some dxgkrnl adapter-init paths size internal GPU-MMU capability arrays
    // by the physical-adapter count. We are a single-physical-adapter device.
    // Answering this explicitly avoids any "implicit zero/uninitialized count"
    // fallback inside kernel-side bookkeeping.
    //
    // WDK headers differ by version; keep numeric fallback values so this
    // remains buildable across kits while still matching the decompiled
    // KMTQAITYPE_PHYSICALADAPTERCOUNT (0x1E, 4-byte output) usage.
    //
#ifndef DXGKQAITYPE_PHYSICALADAPTERCOUNT
#define DXGKQAITYPE_PHYSICALADAPTERCOUNT ((DXGK_QUERYADAPTERINFOTYPE)30)
#endif
#ifndef DXGKQAITYPE_PHYSICALADAPTERDEVICEIDS
#define DXGKQAITYPE_PHYSICALADAPTERDEVICEIDS ((DXGK_QUERYADAPTERINFOTYPE)31)
#endif
    case DXGKQAITYPE_PHYSICALADAPTERCOUNT: {
        UINT* pCount;

        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(UINT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        pCount = (UINT*)pQueryAdapterInfo->pOutputData;
        *pCount = 1u;
        WINMALI_TRACE("PHYSICALADAPTERCOUNT: count=%u", *pCount);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICALADAPTERDEVICEIDS: {
        UINT* out;
        UINT  dwordCount;
        UINT  i;
        //
        // Decomp shows dxgkrnl requesting 28 bytes for this query and then
        // consuming the first 5 DWORDs as persistent adapter identity fields.
        // Publish a stable PCI-style identity for our single RK3588 Mali-G610:
        //   vendor=0x13B5 (ARM), device=0xA867 (from GPU_ID 0xA8670005),
        //   subsys vendor=0x2207 (Rockchip), subsys id=0x3588, rev=0x5.
        //
        static const UINT kIds[7] = {
            0x000013B5u, // VendorId
            0x0000A867u, // DeviceId
            0x00002207u, // SubVendorId
            0x00003588u, // SubSystemId
            0x00000005u, // RevisionId
            0x00000000u, // Reserved / bus-specific
            0x00000000u  // Reserved / bus-specific
        };

        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < sizeof(UINT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        out = (UINT*)pQueryAdapterInfo->pOutputData;
        dwordCount = (UINT)(pQueryAdapterInfo->OutputDataSize / sizeof(UINT));

        RtlZeroMemory(out, (SIZE_T)dwordCount * sizeof(UINT));
        for (i = 0; i < dwordCount && i < RTL_NUMBER_OF(kIds); ++i) {
            out[i] = kIds[i];
        }

        WINMALI_TRACE(
            "PHYSICALADAPTERDEVICEIDS: outSz=%u dwords=%u "
            "ven=0x%04x dev=0x%04x subven=0x%04x subsys=0x%04x rev=0x%x",
            pQueryAdapterInfo->OutputDataSize,
            dwordCount,
            kIds[0], kIds[1], kIds[2], kIds[3], kIds[4]);
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    //
    // Queried after QUERYSEGMENT4 when preemption / history-buffer formatting
    // is enabled. NOT_SUPPORTED makes dxgkrnl bail before paging-process
    // setup (see user trace: type=10 size=4 then CreateProcess then Stop).
    //
    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        DXGKARG_HISTORYBUFFERPRECISION* p;
        UINT                            n;

        if (pQueryAdapterInfo->pOutputData == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGKARG_HISTORYBUFFERPRECISION)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (pQueryAdapterInfo->pInputData != NULL
            && pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYHISTORYBUFFERPRECISIONIN))
        {
            const DXGK_QUERYHISTORYBUFFERPRECISIONIN* in =
                (const DXGK_QUERYHISTORYBUFFERPRECISIONIN*)pQueryAdapterInfo->pInputData;

            if (in->PhysicalAdapterIndex != 0) {
                return STATUS_INVALID_PARAMETER;
            }
        }
        n = (UINT)(pQueryAdapterInfo->OutputDataSize / sizeof(DXGKARG_HISTORYBUFFERPRECISION));
        p = (DXGKARG_HISTORYBUFFERPRECISION*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(p, (SIZE_T)n * sizeof(DXGKARG_HISTORYBUFFERPRECISION));
        for (UINT i = 0; i < n; i++) {
            p[i].PrecisionBits = 64;
        }
        WINMALI_TRACE("HISTORYBUFFERPRECISION: count=%u bits=64", n);
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
        //
        // Keep GPUMMUCAPS aligned with DRIVERCAPS.InternalGpuVirtualAddressRange*
        // while we are in the 1 GiB bring-up sandbox. Advertising 48-bit VA
        // here forces dxgk to size internal VA bookkeeping for a much larger
        // space than we actually expose.
        //
        caps->VirtualAddressBitCount    = 30;
        caps->LeafPageTableSizeFor64KPagesInBytes = (UINT)PAGE_SIZE;
        caps->PageTableLevelCount       = 2;
        WINMALI_TRACE(
            "GPUMMUCAPS: VA_bits=30 levels=2 update=CPU_VIRTUAL "
            "(per-level PageTableSegmentId reported in PAGETABLELEVELDESC)");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN* in;
        DXGK_PAGE_TABLE_LEVEL_DESC*           desc;
        const UINT                            levels = 2;
        const UINT                            idxBits = 9;
        //
        // PageTableSegmentId MUST reference a segment we publish in
        // QUERYSEGMENT3/4/5 (MSDN: "the ID of the memory segment to
        // allocate the page table from. This must be one of the segments
        // returned by DxgkDdiQueryAdapterInfo with type
        // DXGKQAITYPE_QUERYSEGMENT3"). Reporting zero here while we
        // publish segments 1 (sysmem) and 2 (GOP local) was tolerated
        // by dxgk in the single-segment runs (silent fallback to "use
        // sysmem"), but as soon as a second segment appeared dxgk
        // enforced the rule and silently StopDeviced the adapter
        // immediately after QUERYSEGMENT4 pass-2 - same failure mode
        // as the DirectFlip / SectionBackedPrimary contract.
        //
        // PageTableUpdateMode = CPU_VIRTUAL means CPU writes patch the
        // page tables directly, so the tables must live in a
        // CpuVisible + sysmem-backed segment. Our segment id 1
        // (Adapter->DmaSegment*) is exactly that:
        // CpuVisible | PopulatedFromSystemMemory | NonLocalBudgetGroup,
        // 4 MiB of contiguous cached sysmem reserved for dxgk
        // allocations (DMA buffers + page tables).
        //
        const UINT pageTableSegmentId = WINMALI_APERTURE_SEGMENT_ID;

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
        desc->PageTableSegmentId               = pageTableSegmentId;
        desc->PagingProcessPageTableSegmentId  = pageTableSegmentId;
        desc->PageTableSizeInBytes             = (UINT)PAGE_SIZE;
        desc->PageTableAlignmentInBytes        = 0;
        WINMALI_TRACE(
            "PAGETABLELEVELDESC: level=%u idxBits=%u size=%u seg=%u",
            (ULONG)in->LevelIndex,
            idxBits,
            (ULONG)PAGE_SIZE,
            pageTableSegmentId);
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    case DXGKQAITYPE_QUERYSEGMENT3: {
        DXGK_QUERYSEGMENTOUT3*     qo;
        DXGK_SEGMENTDESCRIPTOR3*   segs;
        PWINMALI_ADAPTER           adapter;
        WINMALI_SEGMENT_DESC       descs[2];
        UINT                       n;
        UINT                       i;

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        n = WinMaliBuildSegmentList_(adapter, descs);
        if (n == 0) {
            WINMALI_WARN("QUERYSEGMENT3: no segments available");
            return STATUS_DEVICE_NOT_READY;
        }

        if (pQueryAdapterInfo->OutputDataSize
            < sizeof(DXGK_QUERYSEGMENTOUT3) + (SIZE_T)n * sizeof(DXGK_SEGMENTDESCRIPTOR3)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        qo = (DXGK_QUERYSEGMENTOUT3*)pQueryAdapterInfo->pOutputData;
        if (qo == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        segs = qo->pSegmentDescriptor;
        if (segs == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        RtlZeroMemory(qo, sizeof(*qo));
        qo->pSegmentDescriptor = segs;
        RtlZeroMemory(segs, sizeof(DXGK_SEGMENTDESCRIPTOR3) * n);

        for (i = 0; i < n; ++i) {
            DXGK_SEGMENTDESCRIPTOR3* seg = &segs[i];
            const WINMALI_SEGMENT_DESC* d = &descs[i];

            seg->Flags.Aperture                  =
                (d->Flags & WINMALI_SEGFLAG_APERTURE)       ? 1u : 0u;
            seg->Flags.CpuVisible                =
                (d->Flags & WINMALI_SEGFLAG_CPU_VISIBLE)    ? 1u : 0u;
            seg->Flags.CacheCoherent             =
                (d->Flags & WINMALI_SEGFLAG_CACHE_COHERENT) ? 1u : 0u;
            seg->Flags.DirectFlip                =
                (d->Flags & WINMALI_SEGFLAG_DIRECT_FLIP)    ? 1u : 0u;
            seg->Flags.LocalBudgetGroup          =
                (d->Flags & WINMALI_SEGFLAG_LOCAL_BUDGET)   ? 1u : 0u;
            seg->Flags.NonLocalBudgetGroup       =
                (d->Flags & WINMALI_SEGFLAG_NONLOCAL_BUDGET)? 1u : 0u;
            seg->Flags.PopulatedFromSystemMemory =
                (d->Flags & WINMALI_SEGFLAG_POP_SYSMEM)     ? 1u : 0u;

            seg->BaseAddress          = d->BaseAddress;
            seg->CpuTranslatedAddress = d->CpuTranslatedAddress;
            seg->Size                 = d->Size;
            seg->NbOfBanks            = 0;
            seg->pBankRangeTable      = NULL;
            seg->CommitLimit          = d->Size;
            seg->SystemMemoryEndAddress = 0;
            seg->Reserved             = 0;
        }

        qo->NbSegment                   = n;
        //
        // PagingBufferSegmentId: must reference a published segment id
        // (or 0 to mean "allocate from generic sysmem"). The Microsoft
        // render-only-sample anchors paging on its aperture (id 1) -
        // we mirror that and anchor it on our sysmem segment (id 1,
        // CpuVisible | PopulatedFromSystemMemory). Returning 0 while
        // *any* sysmem-backed segment is published was tolerated by dxgk
        // for single-segment publication but dxgk silently StopDeviced
        // the adapter immediately after QUERYSEGMENT4 pass-2 in the
        // 2-segment case (page-table allocator had no valid segment to
        // place the paging buffer in).
        //
        qo->PagingBufferSegmentId       =
            (n != 0) ? descs[0].SegmentId : 0u;
        qo->PagingBufferSize            = WINMALI_VIDMM_PAGING_BUFFER_BYTES;
        qo->PagingBufferPrivateDataSize = 0;

        WINMALI_TRACE(
            "QUERYSEGMENT3: n=%u seg0 size=0x%llx flags=0x%x seg1 size=0x%llx flags=0x%x paging_seg=%u",
            n,
            (ULONGLONG)descs[0].Size, descs[0].Flags,
            (ULONGLONG)(n > 1 ? descs[1].Size : 0),
            (n > 1 ? descs[1].Flags : 0),
            qo->PagingBufferSegmentId);
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    //
    // WDDM 2.x VIDMM uses QUERYSEGMENT4 (not only QUERYSEGMENT3). Returning
    // NOT_SUPPORTED here makes dxgkrnl tear the adapter down (StopDevice).
    //
    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4*    qo;
        DXGK_SEGMENTDESCRIPTOR4*  segs;
        PWINMALI_ADAPTER          adapter;
        BYTE*                     pOutBase;
        BYTE*                     pOutEnd;
        UINT                      nbSeg;
        SIZE_T                    stride;
        SIZE_T                    needTotal;
        WINMALI_SEGMENT_DESC      descs[2];
        UINT                      avail;
        UINT                      i;

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

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        avail   = WinMaliBuildSegmentList_(adapter, descs);
        if (avail == 0) {
            WINMALI_WARN("QUERYSEGMENT4: no segments available");
            return STATUS_DEVICE_NOT_READY;
        }

        qo       = (DXGK_QUERYSEGMENTOUT4*)pQueryAdapterInfo->pOutputData;
        //
        // STRIDE IS HARD-CODED BY DXGMMS2 — DO NOT USE sizeof().
        //
        // Decomp shows dxgmms2.sys reads/zeroes descriptor[i] at a literal
        // 104-byte stride in every code path that touches pass-2 output:
        //   dxgmms2!CreatePhysicalAdapterSegments
        //     memset( buf, 0, 104 * NbSeg );                    line 99698
        //     operator new[]( 104 * NbSeg, 'Vi07', ... );       line 99718
        //   dxgmms2!ValidateSegmentDescriptors
        //     v11 = (int *)(a2 + 104i64 * i);                   line 100759
        //   dxgmms2!InitializePhysicalAdapterSegments
        //     v17 = v77 + 104 * v11;                            line 100199
        // None of these consult qo->SegmentDescriptorStride — the field is
        // set by dxgmms2 (= 104) right before the pass-2 call and is never
        // read after the miniport returns. Confirmed by exhaustive search
        // of CreatePhysicalAdapterSegments: v28 is assigned 104 at line
        // 99727 and never referenced again.
        //
        // sizeof(DXGK_SEGMENTDESCRIPTOR4) on this WDK (10.0.26100.0,
        // d3dkmddi.h lines 2720-2749, WDDM2_2 layout with
        // NumUEFIFrameBufferRanges) computes to exactly 96 bytes:
        //   Flags(4) + pad(4) + BaseAddress(8) + Size(8) + CommitLimit(8)
        //   + SystemMemoryEndAddress(8) + Cpu*/CpuHostAperture union(16)
        //   + NumInvalidMemoryRanges(4) + pad(4) + VprRangeStartOffset(8)
        //   + VprRangeSize(8) + VprAlignment(4) + NumVprSupported(4)
        //   + VprReserveSize(4) + NumUEFIFrameBufferRanges(4) = 96
        // If we naively use that as the stride, descriptor[i] is written
        // at byte 96*i but dxgmms2 reads it at byte 104*i. For NbSeg >= 2,
        // descriptor 1's "Flags" (byte 104 in dxgmms2's view) lands on
        // byte 8 of OUR descriptor 1 = LowPart of BaseAddress
        // (= 0x30000000 for our GOP segment). ValidateSegmentDescriptors
        // rejects with the "Flags >= 0x400000" rule at WdLine 0xEB and
        // CreatePhysicalAdapterSegments returns STATUS_INVALID_PARAMETER,
        // which propagates up to DxgkAddAdapter -> StopDevice. This is
        // the exact symptom comments throughout this file describe as
        // "silently StopDevice immediately after QUERYSEGMENT4 pass-2 in
        // the 2-segment case".
        //
        stride   = 104u;
        pOutBase = (BYTE*)pQueryAdapterInfo->pOutputData;
        pOutEnd  = pOutBase + pQueryAdapterInfo->OutputDataSize;

        //
        // MSDN DXGK_QUERYSEGMENTOUT4: first call has NbSegment == 0. The driver
        // must return SUCCESS and set only NbSegment - do not read or write any
        // other member (including RtlZeroMemory of the struct). Violating this
        // caused dxgkrnl to repeat 40-byte calls and then fault.
        //
        if (qo->NbSegment == 0) {
            qo->NbSegment = avail;
            WINMALI_TRACE(
                "QUERYSEGMENT4: pass1 count-only nb=%u (out=%u hdr=%u)",
                avail,
                pQueryAdapterInfo->OutputDataSize,
                (ULONG)sizeof(DXGK_QUERYSEGMENTOUT4));
            return STATUS_SUCCESS;
        }

        nbSeg = qo->NbSegment;
        if (nbSeg != avail) {
            WINMALI_WARN(
                "QUERYSEGMENT4: pass2 mismatched nbSeg=%u (expected %u)",
                nbSeg, avail);
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
                    segs = (DXGK_SEGMENTDESCRIPTOR4*)psd;
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
                    segs = (DXGK_SEGMENTDESCRIPTOR4*)psd;
                } else {
                    segs = (DXGK_SEGMENTDESCRIPTOR4*)psd;
                }
            } else if (pQueryAdapterInfo->OutputDataSize >= needTotal) {
                segs = (DXGK_SEGMENTDESCRIPTOR4*)pAfterHdr;
            } else {
                WINMALI_TRACE(
                    "QUERYSEGMENT4: pass2 no psd and out=%u < need=%llu",
                    pQueryAdapterInfo->OutputDataSize,
                    (ULONGLONG)needTotal);
                return STATUS_BUFFER_TOO_SMALL;
            }
        }

        //
        // Walk descriptors at the wire stride (104) — NOT sizeof — so each
        // descriptor lands exactly where dxgmms2 will later read it.
        // sizeof(DXGK_SEGMENTDESCRIPTOR4) = 96 on this WDK while dxgmms2's
        // hard-coded read stride is 104; using &segs[i] (which steps by 96)
        // mis-aligns descriptor[1+] in the 104*N buffer dxgmms2 allocated.
        // See the long comment above the `stride = 104u` assignment for the
        // exact decomp lines that prove this.
        //
        // Zero the trailing 8-byte gap per descriptor too: dxgmms2 reads
        // bytes 96..103 of each descriptor (the synthetic "internal"
        // tail) when walking the segment array, and we cannot leave them
        // holding our caller's stack garbage.
        //
        {
            BYTE* base = (BYTE*)segs;
            RtlZeroMemory(base, (SIZE_T)stride * (SIZE_T)nbSeg);
        }

        for (i = 0; i < nbSeg; ++i) {
            DXGK_SEGMENTDESCRIPTOR4* seg =
                (DXGK_SEGMENTDESCRIPTOR4*)((BYTE*)segs + (SIZE_T)i * stride);
            const WINMALI_SEGMENT_DESC* d = &descs[i];

            seg->Flags.Value                     = 0;
            seg->Flags.Aperture                  =
                (d->Flags & WINMALI_SEGFLAG_APERTURE)       ? 1u : 0u;
            seg->Flags.CpuVisible                =
                (d->Flags & WINMALI_SEGFLAG_CPU_VISIBLE)    ? 1u : 0u;
            seg->Flags.CacheCoherent             =
                (d->Flags & WINMALI_SEGFLAG_CACHE_COHERENT) ? 1u : 0u;
            seg->Flags.DirectFlip                =
                (d->Flags & WINMALI_SEGFLAG_DIRECT_FLIP)    ? 1u : 0u;
            seg->Flags.LocalBudgetGroup          =
                (d->Flags & WINMALI_SEGFLAG_LOCAL_BUDGET)   ? 1u : 0u;
            seg->Flags.NonLocalBudgetGroup       =
                (d->Flags & WINMALI_SEGFLAG_NONLOCAL_BUDGET)? 1u : 0u;
            seg->Flags.PopulatedFromSystemMemory =
                (d->Flags & WINMALI_SEGFLAG_POP_SYSMEM)     ? 1u : 0u;
            seg->Flags.SupportsCpuHostAperture   = 0;

            seg->BaseAddress              = d->BaseAddress;
            seg->CpuTranslatedAddress     = d->CpuTranslatedAddress;
            seg->Size                     = d->Size;
            seg->CommitLimit              = d->Size;
            seg->SystemMemoryEndAddress   = 0;
            seg->NumInvalidMemoryRanges   = 0;
            seg->VprRangeStartOffset      = 0;
            seg->VprRangeSize             = 0;
            seg->VprAlignment             = 0;
            seg->NumVprSupported          = 0;
            seg->VprReserveSize           = 0;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            seg->NumUEFIFrameBufferRanges = 0;
#endif
        }

        qo->pSegmentDescriptor          = (BYTE*)segs;
        qo->SegmentDescriptorStride     = stride;
        qo->NbSegment                   = nbSeg;
        //
        // PagingBufferSegmentId must reference a published segment id
        // (or 0 to mean "allocate from generic sysmem"). The Microsoft
        // render-only-sample anchors paging on its aperture (id 1); we
        // mirror that and anchor it on our sysmem segment (id 1,
        // CpuVisible | PopulatedFromSystemMemory) - the same segment we
        // ask dxgk to use for page tables in PAGETABLELEVELDESC. The
        // previous behaviour of returning 0 while a sysmem-backed
        // segment was published was tolerated by dxgk for single-segment
        // publication but caused dxgk to silently StopDevice the
        // adapter immediately after QUERYSEGMENT4 pass-2 in the
        // 2-segment case (page-table allocator could not place the
        // paging buffer).
        //
        qo->PagingBufferSegmentId       =
            (nbSeg != 0) ? descs[0].SegmentId : 0u;
        qo->PagingBufferSize            = WINMALI_VIDMM_PAGING_BUFFER_BYTES;
        qo->PagingBufferPrivateDataSize = 0;

        //
        // Diagnostics: log the actual DXGK_SEGMENTFLAGS.Value bits we wrote
        // (not our internal WINMALI_SEGFLAG_*) plus stride / addresses so
        // we can cross-check what dxgkrnl is consuming when it silently
        // StopDevices the adapter post-pass2.
        //
        WINMALI_TRACE(
            "QUERYSEGMENT4: descSize=%Iu stride=%Iu (sizeof=%Iu, dxgmms2 ABI=104)",
            (SIZE_T)sizeof(DXGK_SEGMENTDESCRIPTOR4),
            stride,
            (SIZE_T)sizeof(DXGK_SEGMENTDESCRIPTOR4));
        for (i = 0; i < nbSeg; ++i) {
            const DXGK_SEGMENTDESCRIPTOR4* seg =
                (const DXGK_SEGMENTDESCRIPTOR4*)((BYTE*)segs + (SIZE_T)i * stride);
            WINMALI_TRACE(
                "QUERYSEGMENT4: seg[%u] dxgkFlags=0x%08x size=0x%llx "
                "base=0x%llx cpuPa=0x%llx commit=0x%llx",
                i,
                seg->Flags.Value,
                (ULONGLONG)seg->Size,
                (ULONGLONG)seg->BaseAddress.QuadPart,
                (ULONGLONG)seg->CpuTranslatedAddress.QuadPart,
                (ULONGLONG)seg->CommitLimit);
        }

        WINMALI_TRACE(
            "QUERYSEGMENT4: pass2 OK out=%u psd=%p nb=%u seg0 size=0x%llx flags=0x%x "
            "seg1 size=0x%llx flags=0x%x paging_seg=%u",
            pQueryAdapterInfo->OutputDataSize,
            (void*)qo->pSegmentDescriptor,
            nbSeg,
            (ULONGLONG)descs[0].Size, descs[0].Flags,
            (ULONGLONG)(nbSeg > 1 ? descs[1].Size : 0),
            (nbSeg > 1 ? descs[1].Flags : 0),
            qo->PagingBufferSegmentId);
        return STATUS_SUCCESS;
    }
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    // Colorimetry override query (per-target). No override support yet.
    case DXGKQAITYPE_QUERYCOLORIMETRYOVERRIDES: {
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize == 0)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        WINMALI_TRACE("QUERYCOLORIMETRYOVERRIDES: zeroed %u bytes",
                      pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    // DisplayID descriptor query (per-target). EDID path is active; DisplayID not yet.
    case DXGKQAITYPE_DISPLAYID_DESCRIPTOR: {
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize == 0)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        WINMALI_TRACE("DISPLAYID_DESCRIPTOR: zeroed %u bytes (not provided)",
                      pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    // Perf telemetry surfaces. Zero is a valid "unknown/not-reported" response.
    case DXGKQAITYPE_NODEPERFDATA:
    case DXGKQAITYPE_ADAPTERPERFDATA: {
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize == 0)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        WINMALI_TRACE(
            "%s: zeroed %u bytes",
            (pQueryAdapterInfo->Type == DXGKQAITYPE_NODEPERFDATA)
                ? "NODEPERFDATA"
                : "ADAPTERPERFDATA",
            pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    //
    // WDDM 3.2+ (e.g. Windows 11 24H2 / 26200): dxgkrnl may use these instead
    // fails VIDMM init; the stack stops the device right after GetNodeMetadata
    // and never loads the UMD — with no obvious "segment" line in older logs.
    //
    case DXGKQAITYPE_QUERYSEGMENTCOUNT: {
        const DXGK_QUERYSEGMENTCOUNTIN* in;
        DXGK_QUERYSEGMENTCOUNTOUT*      out;
        PWINMALI_ADAPTER                adapter;
        WINMALI_SEGMENT_DESC            descs[2];
        UINT                            n;

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
        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        n       = WinMaliBuildSegmentList_(adapter, descs);
        out     = (DXGK_QUERYSEGMENTCOUNTOUT*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(*out));
        //
        // Must match WinMaliBuildSegmentList_ / QUERYSEGMENT4 NbSegment. The old
        // "(n != 0) ? n : 1" lied when n==0 (no DMA/scratch yet): COUNT said 1
        // while QUERYSEGMENT4 returned DEVICE_NOT_READY — VIDMM could mis-schedule
        // DpiFdoStartAdapter work (memory/quota-class failures on some builds).
        //
        if (n == 0) {
            WINMALI_WARN(
                "QUERYSEGMENTCOUNT: zero segments (DMA/scratch not published)");
            return STATUS_DEVICE_NOT_READY;
        }
        out->SegmentCount = (UINT16)n;
        WINMALI_TRACE("QUERYSEGMENTCOUNT: SegmentCount=%u", out->SegmentCount);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT5: {
        const DXGK_QUERYSEGMENTIN5* in;
        DXGK_QUERYSEGMENTOUT5*       qo;
        DXGK_SEGMENTDESCRIPTOR5*     segs;
        PWINMALI_ADAPTER             adapter;
        WINMALI_SEGMENT_DESC         descs[2];
        SIZE_T                       need;
        UINT                         n;
        UINT                         i;

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
        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        n       = WinMaliBuildSegmentList_(adapter, descs);
        if (n == 0) {
            WINMALI_WARN("QUERYSEGMENT5: no segments available");
            return STATUS_DEVICE_NOT_READY;
        }
        need = sizeof(DXGK_QUERYSEGMENTOUT5) + (SIZE_T)n * sizeof(DXGK_SEGMENTDESCRIPTOR5);
        if (pQueryAdapterInfo->pOutputData == NULL
            || pQueryAdapterInfo->OutputDataSize < need) {
            WINMALI_WARN(
                "QUERYSEGMENT5: need %llu bytes have %u",
                (ULONGLONG)need,
                pQueryAdapterInfo->OutputDataSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo   = (DXGK_QUERYSEGMENTOUT5*)pQueryAdapterInfo->pOutputData;
        segs = qo->SegmentDescriptors;
        if (segs == NULL) {
            segs = (DXGK_SEGMENTDESCRIPTOR5*)((BYTE*)qo + sizeof(DXGK_QUERYSEGMENTOUT5));
        }
        if ((BYTE*)segs + (SIZE_T)n * sizeof(DXGK_SEGMENTDESCRIPTOR5)
            > (BYTE*)pQueryAdapterInfo->pOutputData + pQueryAdapterInfo->OutputDataSize) {
            WINMALI_WARN("QUERYSEGMENT5: descriptors past buffer end");
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(qo, sizeof(*qo));
        qo->SegmentDescriptors = segs;

        for (i = 0; i < n; ++i) {
            DXGK_SEGMENTDESCRIPTOR5* seg = &segs[i];
            const WINMALI_SEGMENT_DESC* d = &descs[i];

            RtlZeroMemory(seg, sizeof(*seg));

            //
            // DXGK_SEGMENTTYPE (WDDM 3.x): only SYSMEM vs LOCAL. ROS / MSDN:
            // segments backed by PopulatedFromSystemMemory (our DMA carve-out
            // and the scan-out slab) are system-memory segments. Marking them
            // DXGK_SEGMENTTYPE_LOCAL contradicted Flags.PopulatedFromSystemMemory
            // and caused dxgmms2 / VIDMM to treat budgets incorrectly during
            // DpiFdoStartAdapter (failures surfaced as STATUS_NO_MEMORY).
            //
            seg->SegmentType =
                (d->Flags & WINMALI_SEGFLAG_POP_SYSMEM)
                    ? DXGK_SEGMENTTYPE_SYSMEM
                    : DXGK_SEGMENTTYPE_LOCAL;
            seg->Flags.Value                     = 0;
            seg->Flags.Aperture                  =
                (d->Flags & WINMALI_SEGFLAG_APERTURE)       ? 1u : 0u;
            seg->Flags.CpuVisible                =
                (d->Flags & WINMALI_SEGFLAG_CPU_VISIBLE)    ? 1u : 0u;
            seg->Flags.CacheCoherent             =
                (d->Flags & WINMALI_SEGFLAG_CACHE_COHERENT) ? 1u : 0u;
            seg->Flags.DirectFlip                =
                (d->Flags & WINMALI_SEGFLAG_DIRECT_FLIP)    ? 1u : 0u;
            seg->Flags.LocalBudgetGroup          =
                (d->Flags & WINMALI_SEGFLAG_LOCAL_BUDGET)   ? 1u : 0u;
            seg->Flags.NonLocalBudgetGroup       =
                (d->Flags & WINMALI_SEGFLAG_NONLOCAL_BUDGET)? 1u : 0u;
            seg->Flags.PopulatedFromSystemMemory =
                (d->Flags & WINMALI_SEGFLAG_POP_SYSMEM)     ? 1u : 0u;

            seg->SlabSize                = DXGK_PAGESIZE_4KB;
            seg->BaseAddress             = d->BaseAddress;
            seg->Size                    = d->Size;
            seg->CpuTranslatedAddress    = d->CpuTranslatedAddress;
            seg->SystemMemoryEndAddress  = 0;
            seg->VprRangeStartOffset     = 0;
            seg->VprRangeSize            = 0;
            seg->VprAlignment            = 0;
            seg->NumInvalidMemoryRanges  = 0;
            seg->NumVprSupported         = 0;
            seg->VprReserveSize          = 0;
            seg->NumUEFIFrameBufferRanges = 0;
        }

        WINMALI_TRACE(
            "QUERYSEGMENT5: n=%u seg0 size=0x%llx flags=0x%x seg1 size=0x%llx flags=0x%x",
            n,
            (ULONGLONG)descs[0].Size, descs[0].Flags,
            (ULONGLONG)(n > 1 ? descs[1].Size : 0),
            (n > 1 ? descs[1].Flags : 0));
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
WinMaliKmdQueryAdapterInfo(
    _In_ const HANDLE                    hAdapter,
    _In_ const DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    NTSTATUS st;

    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    st = WinMaliKmdQueryAdapterInfoImpl(hAdapter, pQueryAdapterInfo);
    WINMALI_TRACE(
        "QueryAdapterInfo: type=%lu out=%u -> 0x%08x",
        (ULONG)pQueryAdapterInfo->Type,
        pQueryAdapterInfo->OutputDataSize,
        st);
    return st;
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
    // ContextSchedulingSupported = 1 advertises WDDM 2.5+ hardware-queue
    // scheduling, which requires functional DxgkDdiCreateHwContext /
    // CreateHwQueue / SubmitCommandToHwQueue DDIs. Those are still stubs in
    // this driver, so claiming the feature makes dxgk abort init right after
    // GetNodeMetadata when it validates the HW scheduler entry points.
    // Leave the flag clear until the HW-queue DDIs are real (matches what
    // render-only-sample/RosKmAdapter::GetNodeMetadata does).
    //
    pGetNodeMetadata->Flags.Value = 0;
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

//
// Synthesized VESA EDID 1.3 base block for a 1920x1080@60 sink.
//
// Why we synthesize: WinMaliKmdQueryChildRelations declares the HDMI child
// with HpdAwareness = HpdAwarenessAlwaysConnected. Per dispmprt.h that is a
// HARD CONTRACT that the monitor is always present AND the driver delivers
// a valid descriptor through DxgkDdiQueryDeviceDescriptor. Returning
// STATUS_MONITOR_NO_DESCRIPTOR caused dxgk to synthesize a placeholder
// monitor ("MSNILNOEDID_1414_008D_FFFFFFFF_FFFFFFFF_0"); DispBroker then
// flagged that target as removed ("SessionHandlerBase::EvaluateTargets
// found removed target") and issued IRP_MN_STOP_DEVICE on us. Hence the
// regular DxgkDdiStopDevice we saw after GetNodeMetadata.
//
// Until vop2connectors.c grows real DDC/I2C-over-AUX EDID reads, we hand
// the OS a hand-built block describing exactly what the UEFI GOP gave us
// (1920x1080@60Hz, 8bpc, digital separate sync, sRGB). The math (DTD,
// chromaticity, checksum) is pre-computed: the final byte (offset 127) is
// 0xE2 so that the unsigned 8-bit sum of all 128 bytes is zero, which is
// what VESA mandates. Don't edit individual bytes without re-running the
// checksum or dxgk will reject the EDID.
//
// Detailed timing #1 is the canonical CEA 861 mode 16 (1920x1080@60Hz,
// 148.5 MHz pixel clock, +/+ sync, H-active 1920 / H-blank 280 / H-front
// 88 / H-sync 44, V-active 1080 / V-blank 45 / V-front 4 / V-sync 5).
// "WinMali" is in monitor descriptor FC. Range descriptor FD says
// 56-75 Hz V, 30-70 kHz H, 200 MHz max pixel clock.
//
static const UCHAR s_WinMaliEdid_1920x1080_60[128] = {
    // Header
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    // Manufacturer "WMI", product 0x0001, serial 0x00000001
    0x5D, 0xA9, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    // Week 1, year 2026 (=36), EDID 1.3, digital input, 53x30 cm, gamma 2.2,
    // features (RGB color, preferred timing in DTD#1)
    0x01, 0x24, 0x01, 0x03, 0x80, 0x35, 0x1E, 0x78,
    // Chromaticity (standard sRGB primaries)
    0x0A, 0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26,
    0x0F, 0x50, 0x54,
    // Established timings I/II/manufacturer: none claimed (DTD is preferred)
                      0x00, 0x00, 0x00,
    // Standard timings: 8x unused (0x01 0x01)
                                        0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    // DTD #1: 1920x1080@60 @ 148.5 MHz (CEA 861 mode 16)
                                        0x02, 0x3A,
    0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
    0x45, 0x00, 0x14, 0x2B, 0x21, 0x00, 0x00, 0x1E,
    // Monitor descriptor FC: monitor name "WinMali\n     "
    0x00, 0x00, 0x00, 0xFC, 0x00, 0x57, 0x69, 0x6E,
    0x4D, 0x61, 0x6C, 0x69, 0x0A, 0x20, 0x20, 0x20,
    0x20, 0x20,
    // Monitor descriptor FD: monitor range limits (V 56-75, H 30-70, 200 MHz)
              0x00, 0x00, 0x00, 0xFD, 0x00, 0x38,
    0x4B, 0x1E, 0x46, 0x14, 0x00, 0x0A, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    // Monitor descriptor 0x10: dummy padding
                            0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Extension flag = 0 (no CEA extension block this round)
    // Checksum 0xE2 so unsigned 8-bit sum across [0..127] == 0
                                        0x00, 0xE2,
};

static VOID
WinMaliLogEdidSelfCheckOnce_(VOID)
{
    static volatile LONG s_WinMaliEdidSelfCheckLogged = 0;
    ULONG                index;
    ULONG                sum;

    if (InterlockedCompareExchange(&s_WinMaliEdidSelfCheckLogged, 1, 0) != 0) {
        return;
    }

    sum = 0;
    for (index = 0; index < RTL_NUMBER_OF(s_WinMaliEdid_1920x1080_60); ++index) {
        sum += s_WinMaliEdid_1920x1080_60[index];
    }

    WINMALI_TRACE(
        "EDID self-check: len=%u sum8=0x%02x checksum=0x%02x ext=%u version=%u.%u "
        "hdr=%02x %02x %02x %02x %02x %02x %02x %02x",
        (UINT)RTL_NUMBER_OF(s_WinMaliEdid_1920x1080_60),
        (UINT)(sum & 0xFFu),
        (UINT)s_WinMaliEdid_1920x1080_60[127],
        (UINT)s_WinMaliEdid_1920x1080_60[126],
        (UINT)s_WinMaliEdid_1920x1080_60[18],
        (UINT)s_WinMaliEdid_1920x1080_60[19],
        (UINT)s_WinMaliEdid_1920x1080_60[0],
        (UINT)s_WinMaliEdid_1920x1080_60[1],
        (UINT)s_WinMaliEdid_1920x1080_60[2],
        (UINT)s_WinMaliEdid_1920x1080_60[3],
        (UINT)s_WinMaliEdid_1920x1080_60[4],
        (UINT)s_WinMaliEdid_1920x1080_60[5],
        (UINT)s_WinMaliEdid_1920x1080_60[6],
        (UINT)s_WinMaliEdid_1920x1080_60[7]);
}

NTSTATUS APIENTRY
WinMaliKmdQueryDeviceDescriptor(
    _In_    const PVOID             MiniportDeviceContext,
    _In_    ULONG                   ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    ULONG            primaryUid;
    ULONG            offset;
    ULONG            length;
    ULONG            remaining;
    ULONG            toCopy;

    WINMALI_ENTER();

    if (adapter == NULL || DeviceDescriptor == NULL) {
        WINMALI_WARN(
            "QueryDeviceDescriptor: invalid args ctx=%p desc=%p",
            adapter,
            DeviceDescriptor);
        return STATUS_INVALID_PARAMETER;
    }

    WinMaliLogEdidSelfCheckOnce_();

    WINMALI_TRACE(
        "QueryDeviceDescriptor: enter child=%lu off=%lu len=%lu buf=%p",
        ChildUid,
        DeviceDescriptor->DescriptorOffset,
        DeviceDescriptor->DescriptorLength,
        DeviceDescriptor->DescriptorBuffer);

    primaryUid = (ULONG)adapter->PrimaryConnector;

    //
    // We only own one child (the GOP-backed connector enumerated in
    // QueryChildRelations). Anything else means dxgk asked about a uid we
    // never advertised - return STATUS_INVALID_PARAMETER. There is no
    // STATUS_MONITOR_INVALID_DESCRIPTOR in ntstatus.h on this WDK, only
    // checksum / detailed-timing / standard-timing variants which would
    // misleadingly imply our EDID itself is malformed.
    //
    if (ChildUid != primaryUid) {
        //
        // Single-output bring-up path: dxgk/DispBroker occasionally probes
        // child 0 before all bookkeeping converges across restart cycles.
        // Serving the same canned EDID for uid 0 avoids the MSNILNOEDID
        // fallback/target-removal loop while we still expose exactly one
        // connected HDMI output.
        //
        if (ChildUid == 0u) {
            WINMALI_WARN(
                "QueryDeviceDescriptor: ChildUid=0 differs from primary=%lu; "
                "serving single-output EDID anyway",
                primaryUid);
        } else {
            WINMALI_WARN(
                "QueryDeviceDescriptor: unknown ChildUid=%lu (primary=%lu)",
                ChildUid,
                primaryUid);
            return STATUS_INVALID_PARAMETER;
        }
    }

    offset = DeviceDescriptor->DescriptorOffset;
    length = DeviceDescriptor->DescriptorLength;

    //
    // dxgk paginates the descriptor read. First call is offset=0,
    // length>=128; subsequent calls walk forward looking for extension
    // blocks. Once we're past the base EDID we hand back the documented
    // STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA to terminate the walk; that
    // is NOT an error - dxgk treats it as "done".
    //
    if (offset >= sizeof(s_WinMaliEdid_1920x1080_60)) {
        WINMALI_TRACE(
            "QueryDeviceDescriptor: child=%lu off=%lu -> NO_MORE_DESCRIPTOR_DATA",
            ChildUid,
            offset);
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    remaining = sizeof(s_WinMaliEdid_1920x1080_60) - offset;
    if (DeviceDescriptor->DescriptorBuffer == NULL || length == 0) {
        //
        // dxgk may probe with a short/empty descriptor buffer before issuing
        // the real read. Returning INVALID_PARAMETER here can make it fall
        // back to MSNILNOEDID; report the required bytes instead.
        //
        DeviceDescriptor->DescriptorLength = remaining;
        WINMALI_TRACE(
            "QueryDeviceDescriptor: child=%lu off=%lu req=%lu buf=%p -> BUFFER_TOO_SMALL need=%lu",
            ChildUid,
            offset,
            length,
            DeviceDescriptor->DescriptorBuffer,
            remaining);
        return STATUS_BUFFER_TOO_SMALL;
    }
    toCopy    = (length < remaining) ? length : remaining;

    RtlCopyMemory(
        DeviceDescriptor->DescriptorBuffer,
        s_WinMaliEdid_1920x1080_60 + offset,
        toCopy);

    //
    // Per dispmprt.h, the miniport must write back the byte count it
    // actually filled so dxgk can advance its read cursor correctly.
    //
    DeviceDescriptor->DescriptorLength = toCopy;

    if (!adapter->Gop.Valid
        || adapter->Gop.Width  != 1920
        || adapter->Gop.Height != 1080)
    {
        //
        // The synthesized EDID claims 1920x1080@60 unconditionally. If
        // UEFI handed us a different GOP (e.g. 4K), the EDID lies and
        // the OS will pick a mode that mismatches what GOP is currently
        // showing. Surface this with a single warning per call so it is
        // obvious in the logs once we wire vop2connectors.c for real
        // EDID reads.
        //
        WINMALI_WARN(
            "QueryDeviceDescriptor: GOP %ux%u valid=%u doesn't match "
            "canned 1920x1080 EDID; expect mode mismatch",
            (UINT)adapter->Gop.Width,
            (UINT)adapter->Gop.Height,
            (UINT)adapter->Gop.Valid);
    }

    WINMALI_TRACE(
        "QueryDeviceDescriptor: child=%lu offset=%lu req=%lu copied=%lu first=%02x %02x %02x %02x",
        ChildUid,
        offset,
        length,
        toCopy,
        (UINT)s_WinMaliEdid_1920x1080_60[offset + 0],
        (UINT)((offset + 1 < RTL_NUMBER_OF(s_WinMaliEdid_1920x1080_60)) ? s_WinMaliEdid_1920x1080_60[offset + 1] : 0),
        (UINT)((offset + 2 < RTL_NUMBER_OF(s_WinMaliEdid_1920x1080_60)) ? s_WinMaliEdid_1920x1080_60[offset + 2] : 0),
        (UINT)((offset + 3 < RTL_NUMBER_OF(s_WinMaliEdid_1920x1080_60)) ? s_WinMaliEdid_1920x1080_60[offset + 3] : 0));

    return STATUS_SUCCESS;
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
static CONST GUID kWinMaliGuidGpuPartition = {0x462bc153, 0x40eb, 0x484a, {0x81, 0x68, 0x99, 0x72, 0xe3, 0xcd, 0x5a, 0xef}};
static CONST GUID kWinMaliGuidBrightness = {0xFDE5BBA4, 0xB3F9, 0x46FB, {0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4}};
static CONST GUID kWinMaliGuidBrightness2 = {0x148A3C98, 0x0ECD, 0x465A, {0xB6, 0x34, 0xB0, 0x5F, 0x19, 0x5F, 0x77, 0x39}};
static CONST GUID kWinMaliGuidBrightness3 = {0x197A4A6E, 0x0391, 0x4322, {0x96, 0xEA, 0xC2, 0x76, 0x0F, 0x88, 0x1D, 0x3A}};
static CONST GUID kWinMaliGuidDp = {0x2d09818e, 0xdfeb, 0x4173, {0xb5, 0xe9, 0xae, 0xfd, 0x66, 0xb2, 0x02, 0xf3}};
static CONST GUID kWinMaliGuidDisplayDiagnostics = {0x962639f3, 0xe9dc, 0x42ab, {0x94, 0xeb, 0x06, 0x51, 0x6d, 0xec, 0xa1, 0x26}};
static CONST GUID kWinMaliGuidPnpExtendedAddress = {0xb8e992ec, 0xa797, 0x4dc4, {0x88, 0x46, 0x84, 0xd0, 0x41, 0x70, 0x74, 0x46}};
static CONST GUID kWinMaliGuidPnpLocation = {0x70211b0e, 0x0afb, 0x47db, {0xaf, 0xc1, 0x41, 0x0b, 0xf8, 0x42, 0x49, 0x7a}};
static CONST GUID kWinMaliGuidIommuBus = {0x1efee0b2, 0xd278, 0x4ae4, {0xbd, 0xdc, 0x1b, 0x34, 0xdd, 0x64, 0x80, 0x43}};
static CONST GUID kWinMaliGuidD3ColdSupport = {0xb38290e5, 0x3cd0, 0x4f9d, {0x99, 0x37, 0xf5, 0xfe, 0x2b, 0x44, 0xd4, 0x7a}};
static CONST GUID kWinMaliGuidReenumerateSelf = {0x2aeb0243, 0x6a6e, 0x486b, {0x82, 0xfc, 0xd8, 0x15, 0xf6, 0xb9, 0x70, 0x06}};
static CONST GUID kWinMaliGuidDxgkMipiDsi = {0x14f9db8b, 0x85e1, 0x4aa5, {0x8d, 0xaf, 0xff, 0x4a, 0x78, 0x06, 0xd5, 0xe9}};

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
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidGpuPartition)) {
        return "GPU_PARTITION";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidBrightness)) {
        return "BRIGHTNESS";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidBrightness2)) {
        return "BRIGHTNESS_2";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidBrightness3)) {
        return "BRIGHTNESS_3";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidDp)) {
        return "DP_INTERFACE";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidDisplayDiagnostics)) {
        return "DISPLAY_DIAGNOSTICS";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidPnpExtendedAddress)) {
        return "PNP_EXTENDED_ADDRESS";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidPnpLocation)) {
        return "PNP_LOCATION";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidIommuBus)) {
        return "IOMMU_BUS";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidD3ColdSupport)) {
        return "D3COLD_SUPPORT";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidReenumerateSelf)) {
        return "REENUMERATE_SELF";
    }
    if (WinMaliKmdGuidEq_(g, &kWinMaliGuidDxgkMipiDsi)) {
        return "DXGK_MIPI_DSI_INTERFACE";
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
//
// DPC also fires when WinMaliKmdSubmitCommand / WinMaliKmdPreemptCommand
// queue a fake DMA_COMPLETED / DMA_PREEMPTED notification (no real GPU
// fence machinery yet); both paths set adapter->NotifyDpcPending so the
// DPC body calls DxgkCbNotifyDpc once and clears the flag.
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
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    LONG pending;

    if (adapter == NULL) return;

    // Read-only snapshot at DPC level. We print the first few events
    // loudly so bring-up can spot them in DbgView, then throttle.
    if (adapter->InterruptsHandled <= 8) {
        WINMALI_TRACE("DPC: handled=%lld total=%lld spurious=%lld",
                      adapter->InterruptsHandled,
                      adapter->InterruptsTotal,
                      adapter->InterruptsSpurious);
    }

    //
    // Drain any pending DMA_COMPLETED / DMA_PREEMPTED notifications that
    // SubmitCommand / PreemptCommand filed via DxgkCbNotifyInterrupt. The
    // count is opportunistic — dxgk re-collates internally, so it's safe to
    // call NotifyDpc once per DPC even if multiple interrupts were filed.
    //
    pending = InterlockedExchange(&adapter->NotifyDpcPending, 0);
    if (pending != 0
        && adapter->DxgkInterface.DxgkCbNotifyDpc != NULL
        && adapter->DxgkHandle != NULL)
    {
        adapter->DxgkInterface.DxgkCbNotifyDpc(adapter->DxgkHandle);
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
    // Display stack may toggle VSYNC-style interrupts during bring-up.
    // Treat unsupported interrupt types as a benign no-op success so we
    // don't fail adapter start before explicit VSync wiring lands.
    WINMALI_TRACE("ControlInterrupt: type=%d enable=%u", InterruptType, Enable);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Render / allocation pipeline.
//
// These DDIs are the bridge between the runtime (D3D, OpenGL, Vulkan via
// translation layers) and the Mali GPU. Real GPU command-stream emission
// will land in WinMaliKmdRender + WinMaliKmdSubmitCommand once Valhall
// command-stream assembly is wired (see Compiler/ + render-only-sample/
// for the eventual shape). Until then SubmitCommand / PreemptCommand
// fake-complete every submission via DxgkCbNotifyInterrupt so the dxgkrnl
// scheduler doesn't time-out waiting for a fence we will never raise.
//
// CreateAllocation / DestroyAllocation / Open / Close / Describe are real:
// they allocate WINMALI_KMD_ALLOCATION pool blobs and publish sensible
// DXGK_ALLOCATIONINFO so VIDMM can place allocations in our single system-
// memory segment. The Mali AS-slot programming still happens via
// SetRootPageTable, which is independent of these DDIs.
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

//
// Helper: page-align a 64-bit size, capping at MAXSIZE_T - PAGE_SIZE so the
// downstream SIZE_T cast can't overflow. dxgk-supplied sizes are always
// well within this bound, but a malicious UMD blob could try to wrap us.
//
static SIZE_T
WinMaliPageAlignSize_(_In_ ULONGLONG bytes)
{
    const ULONGLONG mask = (ULONGLONG)(PAGE_SIZE - 1);
    ULONGLONG aligned;

    if (bytes == 0) {
        return PAGE_SIZE;   // one-page minimum so we always have a valid alloc
    }
    if (bytes > (ULONGLONG)MAXSIZE_T - PAGE_SIZE) {
        return (SIZE_T)(((ULONGLONG)MAXSIZE_T - PAGE_SIZE) & ~mask);
    }
    aligned = (bytes + mask) & ~mask;
    return (SIZE_T)aligned;
}

//
// Helper: synthesize a default WINMALI_ALLOC_PRIVATE when the UMD didn't
// provide one (legacy / external callers like the Mesa pan_kmod bridge).
// We use it as the source of truth for size / format / dim, then the
// caller fills the same fields into the WINMALI_KMD_ALLOCATION struct.
//
static VOID
WinMaliBuildDefaultAllocPrivate_(_Out_ WINMALI_ALLOC_PRIVATE* out, _In_ ULONGLONG sizeBytes)
{
    RtlZeroMemory(out, sizeof(*out));
    out->Magic     = WINMALI_ALLOC_PRIVATE_MAGIC;
    out->Version   = WINMALI_ALLOC_PRIVATE_VERSION;
    out->Flags     = WINMALI_ALLOC_FLAG_CPU_VISIBLE | WINMALI_ALLOC_FLAG_CMDBUFFER;
    out->SizeBytes = (sizeBytes != 0) ? sizeBytes : (ULONGLONG)PAGE_SIZE;
    out->Alignment = PAGE_SIZE;
    // Width/Height/Pitch/Bpp/Format/RefreshRate left at zero — only primaries
    // populate those; buffer-typed allocations should answer 0 to Describe.
}

//
// Fill DXGK_ALLOCATIONINFO from a freshly-created WINMALI_KMD_ALLOCATION.
// Segment-id arithmetic note: VIDMM uses 1-based segment ids in
// DXGK_SEGMENTPREFERENCE (0 == "unused slot"), and the SupportedSet masks
// are bit i == segment (i + 1).
//
// Route B segment routing:
//   * Primary (display scan-out) -> segment id 2 when
//     WinMaliVop2SetupSysmemScanout succeeded: contiguous sysmem slab at
//     CpuTranslatedAddress, GPU logical base WINMALI_GOP_GPU_BASE.
//   * Everything else -> sysmem segment (id 1, Adapter->DmaSegment*).
//
static VOID
WinMaliPublishAllocationInfo_(
    _In_  PWINMALI_KMD_ALLOCATION alloc,
    _In_opt_ PWINMALI_ADAPTER     adapter,
    _Inout_ DXGK_ALLOCATIONINFO*  info)
{
    BOOLEAN gopAvailable;
    BOOLEAN apertureAvailable;
    UINT    primarySegmentId;
    UINT    generalSegmentId;
    UINT    primaryMask;
    UINT    generalMask;

    info->hAllocation               = (HANDLE)alloc;
    info->Size                      = alloc->Size;
    info->PitchAlignedSize          = 0;
    info->Alignment                 = 0;
    info->AllocationPriority        = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
    info->PhysicalAdapterIndex      = 0;

    //
    // Primary routing uses segment id 2 only when QUERYSEGMENT* actually
    // publishes that slab (SetupSysmemScanout + WinMaliBuildSegmentList_).
    //
    gopAvailable =
        (adapter != NULL
         && adapter->ScanoutSegmentVa != NULL
         && adapter->ScanoutSegmentBytes != 0);

    //
    // "apertureAvailable" historically meant "AperturePageTable is wired
    // to back a CpuVisible aperture segment". After the GpuMmu rewrite we
    // publish segment id 1 as a PopulatedFromSystemMemory block instead,
    // so the right gating condition is now "the sysmem DMA segment was
    // allocated". The local variable keeps its name to limit churn in
    // the segment-routing block below; semantically it's the v3 sysmem
    // segment that the WINMALI_SYSMEM_SEGMENT_ID alias names.
    //
    apertureAvailable = (adapter != NULL
                         && adapter->DmaSegmentVa != NULL
                         && adapter->DmaSegmentBytes != 0);

    if (gopAvailable && apertureAvailable) {
        primarySegmentId = WINMALI_GOP_SEGMENT_ID;
        generalSegmentId = WINMALI_APERTURE_SEGMENT_ID;
        primaryMask = (1u << (WINMALI_GOP_SEGMENT_ID - 1u))
                    | (1u << (WINMALI_APERTURE_SEGMENT_ID - 1u));
        generalMask = (1u << (WINMALI_APERTURE_SEGMENT_ID - 1u));
    } else if (gopAvailable) {
        primarySegmentId = WINMALI_GOP_SEGMENT_ID;
        generalSegmentId = WINMALI_GOP_SEGMENT_ID;
        primaryMask = generalMask = (1u << (WINMALI_GOP_SEGMENT_ID - 1u));
    } else if (apertureAvailable) {
        primarySegmentId = WINMALI_APERTURE_SEGMENT_ID;
        generalSegmentId = WINMALI_APERTURE_SEGMENT_ID;
        primaryMask = generalMask = (1u << (WINMALI_APERTURE_SEGMENT_ID - 1u));
    } else {
        // Fallback: only the static scratch segment exists. Use the same
        // 1-based id 1 we publish for it in WinMaliBuildSegmentList_.
        primarySegmentId = WINMALI_APERTURE_SEGMENT_ID;
        generalSegmentId = WINMALI_APERTURE_SEGMENT_ID;
        primaryMask = generalMask = (1u << (WINMALI_APERTURE_SEGMENT_ID - 1u));
    }

    info->PreferredSegment.Value      = 0;
    if (alloc->Flags & WINMALI_ALLOC_FLAG_PRIMARY) {
        info->PreferredSegment.SegmentId0 = (UCHAR)primarySegmentId;
        info->SupportedReadSegmentSet     = primaryMask;
        info->SupportedWriteSegmentSet    = primaryMask;
        alloc->PreferredSegmentId         = primarySegmentId;
    } else {
        info->PreferredSegment.SegmentId0 = (UCHAR)generalSegmentId;
        info->SupportedReadSegmentSet     = generalMask;
        info->SupportedWriteSegmentSet    = generalMask;
        alloc->PreferredSegmentId         = generalSegmentId;
    }
    info->EvictionSegmentSet        = 0;

    info->HintedBank.Value          = 0;
    info->pAllocationUsageHint      = NULL;

    info->FlagsWddm2.Value          = 0;
    info->FlagsWddm2.CpuVisible     = (alloc->Flags & WINMALI_ALLOC_FLAG_CPU_VISIBLE) ? 1u : 0u;
    info->FlagsWddm2.Cached         = 0;
    //
    // PermanentSysMem hints VIDMM to treat the backing pages as resident
    // for the lifetime of the allocation. Both our segments (id 1 sysmem
    // DMA, future id 2 VOP2 primary) ARE PopulatedFromSystemMemory, so
    // setting it would be defensible. We leave it cleared for now to
    // match Microsoft's render-only-sample default and keep VIDMM in
    // charge of paging decisions; flip to 1 if AddDmaBufferToPool starts
    // misbehaving against the new segment.
    //
    info->FlagsWddm2.PermanentSysMem= 0;
}

NTSTATUS APIENTRY
WinMaliKmdCreateAllocation(_In_ const HANDLE hAdapter, _Inout_ DXGKARG_CREATEALLOCATION* p)
{
    PWINMALI_ADAPTER       adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    PWINMALI_KMD_RESOURCE  resource = NULL;
    UINT                   i;
    WINMALI_ALLOC_PRIVATE  scratchPriv;
    NTSTATUS               status = STATUS_SUCCESS;

    WINMALI_ENTER();

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (adapter == NULL) {
        WINMALI_WARN("CreateAllocation: bad hAdapter=%p", hAdapter);
        return STATUS_INVALID_PARAMETER;
    }
    if (p->NumAllocations == 0 || p->pAllocationInfo == NULL) {
        WINMALI_WARN("CreateAllocation: n=%u pInfo=%p", p->NumAllocations, p->pAllocationInfo);
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Resource-mode: the runtime is creating a logical grouping (swapchain,
    // mip-chain, ...) and dxgk fills hResource with our pointer on return.
    // The same hResource flows into DestroyAllocation so we can free both
    // halves in one shot when the runtime tears down the resource.
    //
    if (p->Flags.Resource) {
        resource = (PWINMALI_KMD_RESOURCE)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*resource), WINMALI_POOL_TAG);
        if (resource == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        resource->Magic          = WINMALI_KMD_RESOURCE_MAGIC;
        resource->Adapter        = adapter;
        resource->NumAllocations = 0;
        resource->Flags          = 0;
        InitializeListHead(&resource->AllocationListHead);
    }

    for (i = 0; i < p->NumAllocations; ++i) {
        DXGK_ALLOCATIONINFO*       info = &p->pAllocationInfo[i];
        const WINMALI_ALLOC_PRIVATE* priv;
        PWINMALI_KMD_ALLOCATION    alloc;

        // Prefer the per-allocation private blob; fall back to a synthetic
        // default (CPU-visible buffer, one page) so external callers can
        // open a device against us without porting their UMD yet.
        if (info->pPrivateDriverData != NULL
            && info->PrivateDriverDataSize >= sizeof(WINMALI_ALLOC_PRIVATE)
            && ((const WINMALI_ALLOC_PRIVATE*)info->pPrivateDriverData)->Magic
                  == WINMALI_ALLOC_PRIVATE_MAGIC)
        {
            priv = (const WINMALI_ALLOC_PRIVATE*)info->pPrivateDriverData;
        } else {
            WinMaliBuildDefaultAllocPrivate_(
                &scratchPriv,
                (info->PrivateDriverDataSize != 0) ? PAGE_SIZE : PAGE_SIZE);
            priv = &scratchPriv;
            WINMALI_TRACE(
                "CreateAllocation[%u]: synthetic priv (UMD blob: ptr=%p size=%u)",
                i, info->pPrivateDriverData, info->PrivateDriverDataSize);
        }

        alloc = (PWINMALI_KMD_ALLOCATION)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*alloc), WINMALI_POOL_TAG);
        if (alloc == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        alloc->Magic              = WINMALI_KMD_ALLOC_MAGIC;
        alloc->Adapter            = adapter;
        alloc->Size               = WinMaliPageAlignSize_(priv->SizeBytes);
        alloc->Alignment          = (priv->Alignment != 0) ? priv->Alignment : PAGE_SIZE;
        alloc->Flags              = priv->Flags;
        alloc->Format             = priv->Format;
        alloc->Width              = priv->Width;
        alloc->Height             = priv->Height;
        alloc->Pitch              = priv->Pitch;
        alloc->BytesPerPixel      = priv->BytesPerPixel;
        alloc->RefreshNumerator   = priv->RefreshNumerator;
        alloc->RefreshDenominator = priv->RefreshDenominator;
        alloc->MultisampleMethod  = priv->MultisampleMethod;
        alloc->Rotation           = priv->Rotation;
        alloc->OwnerResource      = resource;
        InitializeListHead(&alloc->ResourceLink);

        if (resource != NULL) {
            InsertTailList(&resource->AllocationListHead, &alloc->ResourceLink);
            resource->NumAllocations++;
        }

        WinMaliPublishAllocationInfo_(alloc, adapter, info);

        WINMALI_TRACE(
            "CreateAllocation[%u]: alloc=%p size=0x%llx fmt=%u %ux%u flags=0x%x seg=%u res=%p",
            i, alloc, (ULONGLONG)alloc->Size,
            alloc->Format, alloc->Width, alloc->Height, alloc->Flags,
            alloc->PreferredSegmentId, resource);
    }

    //
    // Bail-out cleanup: if any allocation failed mid-loop, walk what we
    // already created and free it so we don't leak. dxgk WILL NOT call
    // DestroyAllocation on this batch when CreateAllocation returns failure.
    //
    if (!NT_SUCCESS(status)) {
        for (UINT j = 0; j < i; ++j) {
            DXGK_ALLOCATIONINFO*    info = &p->pAllocationInfo[j];
            PWINMALI_KMD_ALLOCATION a    = (PWINMALI_KMD_ALLOCATION)info->hAllocation;
            if (a != NULL && a->Magic == WINMALI_KMD_ALLOC_MAGIC) {
                if (!IsListEmpty(&a->ResourceLink)) {
                    RemoveEntryList(&a->ResourceLink);
                }
                a->Magic = 0;
                ExFreePoolWithTag(a, WINMALI_POOL_TAG);
            }
            info->hAllocation = NULL;
        }
        if (resource != NULL) {
            resource->Magic = 0;
            ExFreePoolWithTag(resource, WINMALI_POOL_TAG);
        }
        return status;
    }

    if (resource != NULL) {
        p->hResource = (HANDLE)resource;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdDestroyAllocation(_In_ const HANDLE hAdapter, _In_ const DXGKARG_DESTROYALLOCATION* p)
{
    PWINMALI_ADAPTER       adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    PWINMALI_KMD_RESOURCE  resource;
    UINT                   i;

    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(adapter);

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < p->NumAllocations; ++i) {
        PWINMALI_KMD_ALLOCATION a;

        if (p->pAllocationList == NULL) {
            break;
        }
        a = (PWINMALI_KMD_ALLOCATION)p->pAllocationList[i];
        if (a == NULL) {
            continue;
        }
        if (a->Magic != WINMALI_KMD_ALLOC_MAGIC) {
            WINMALI_WARN("DestroyAllocation: bad magic on alloc=%p val=0x%x", a, a->Magic);
            continue;
        }
        if (!IsListEmpty(&a->ResourceLink)) {
            RemoveEntryList(&a->ResourceLink);
        }
        if (a->OwnerResource != NULL && a->OwnerResource->Magic == WINMALI_KMD_RESOURCE_MAGIC) {
            if (a->OwnerResource->NumAllocations > 0) {
                a->OwnerResource->NumAllocations--;
            }
        }
        a->Magic = 0;
        ExFreePoolWithTag(a, WINMALI_POOL_TAG);
    }

    resource = (PWINMALI_KMD_RESOURCE)p->hResource;
    if (resource != NULL && p->Flags.DestroyResource) {
        if (resource->Magic != WINMALI_KMD_RESOURCE_MAGIC) {
            WINMALI_WARN("DestroyAllocation: bad resource magic res=%p val=0x%x",
                         resource, resource->Magic);
            return STATUS_SUCCESS;
        }
        if (!IsListEmpty(&resource->AllocationListHead)) {
            WINMALI_WARN(
                "DestroyAllocation: resource=%p still has %u allocations, reaping",
                resource, resource->NumAllocations);
            // Defensive: free leaked allocations. dxgk shouldn't leak, but
            // if it does we don't want to leak pool either.
            while (!IsListEmpty(&resource->AllocationListHead)) {
                PLIST_ENTRY e = RemoveHeadList(&resource->AllocationListHead);
                PWINMALI_KMD_ALLOCATION a =
                    CONTAINING_RECORD(e, WINMALI_KMD_ALLOCATION, ResourceLink);
                a->Magic = 0;
                ExFreePoolWithTag(a, WINMALI_POOL_TAG);
            }
        }
        resource->Magic = 0;
        ExFreePoolWithTag(resource, WINMALI_POOL_TAG);
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdOpenAllocation(_In_ const HANDLE hDevice, _In_ const DXGKARG_OPENALLOCATION* p)
{
    PWINMALI_KMD_ALLOCATION firstAlloc = NULL;
    PWINMALI_ADAPTER adapter;
    UINT i;

    WINMALI_ENTER();

    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hDevice);
    if (adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (p == NULL || p->pOpenAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // dxgk gives us an array of {hAllocation, pPrivateDriverData} pairs that
    // it wants to "open" against a device. For a UMA, no-aperture driver
    // there's no per-device transformation to do — the runtime sees the
    // same physical buffer regardless of which device opens it. We echo
    // the driver allocation handle as hDeviceSpecificAllocation so Patch /
    // SubmitCommand can recover the underlying WINMALI_KMD_ALLOCATION.
    //
    for (i = 0; i < p->NumAllocations; ++i) {
        DXGK_OPENALLOCATIONINFO* info = &p->pOpenAllocation[i];
        PWINMALI_KMD_ALLOCATION  a;

        // Sanity-check the per-allocation private blob if the runtime sent
        // one; mismatched / missing data is logged but non-fatal.
        if (info->pPrivateDriverData != NULL
            && info->PrivateDriverDataSize >= sizeof(WINMALI_ALLOC_PRIVATE))
        {
            const WINMALI_ALLOC_PRIVATE* priv =
                (const WINMALI_ALLOC_PRIVATE*)info->pPrivateDriverData;
            if (priv->Magic != WINMALI_ALLOC_PRIVATE_MAGIC) {
                WINMALI_WARN(
                    "OpenAllocation[%u]: priv magic mismatch 0x%x", i, priv->Magic);
            }
        }

        if (adapter->DxgkInterface.DxgkCbGetHandleData != NULL) {
            DXGKARGCB_GETHANDLEDATA getHandleData;

            RtlZeroMemory(&getHandleData, sizeof(getHandleData));
            getHandleData.hObject               = (D3DKMT_HANDLE)(ULONG_PTR)info->hAllocation;
            getHandleData.Type                  = DXGK_HANDLE_ALLOCATION;
            getHandleData.Flags.DeviceSpecific  = 0;
            a = (PWINMALI_KMD_ALLOCATION)
                adapter->DxgkInterface.DxgkCbGetHandleData(&getHandleData);
        } else {
            a = (PWINMALI_KMD_ALLOCATION)(ULONG_PTR)info->hAllocation;
        }
        if (a == NULL || a->Magic != WINMALI_KMD_ALLOC_MAGIC) {
            WINMALI_WARN("OpenAllocation[%u]: bad hAllocation=%p", i, a);
            return STATUS_INVALID_HANDLE;
        }
        info->hDeviceSpecificAllocation = (HANDLE)a;
        if (i == 0) {
            firstAlloc = a;
        }
    }

    //
    // Per-resource OUT fields on the parent (only meaningful for shared
    // GDI-compatible allocations in an aperture segment). We don't have an
    // aperture, so these stay at the dxgk-zeroed defaults: Pitch=0 and
    // SubresourceOffset=0. They're conditionally compiled in WIN8+, hence
    // the guard.
    //
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    if (p->NumAllocations > 0 && p->pOpenAllocation != NULL) {
        // Use mutable cast: dxgk hands us a CONST DXGKARG_OPENALLOCATION but
        // documents these OUT fields on it.
        DXGKARG_OPENALLOCATION* pm = (DXGKARG_OPENALLOCATION*)p;
        pm->Pitch             = (firstAlloc != NULL && firstAlloc->Magic == WINMALI_KMD_ALLOC_MAGIC)
                                  ? firstAlloc->Pitch : 0;
        pm->SubresourceOffset = 0;
    }
#endif

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdCloseAllocation(_In_ const HANDLE hDevice, _In_ const DXGKARG_CLOSEALLOCATION* p)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(p);
    WINMALI_ENTER();
    // Nothing to undo — OpenAllocation just echoed handles back; the actual
    // WINMALI_KMD_ALLOCATION lifetime is owned by CreateAllocation/DestroyAllocation.
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdDescribeAllocation(
    _In_ const HANDLE hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* p)
{
    PWINMALI_KMD_ALLOCATION a;

    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    a = (PWINMALI_KMD_ALLOCATION)p->hAllocation;
    if (a == NULL || a->Magic != WINMALI_KMD_ALLOC_MAGIC) {
        WINMALI_WARN("DescribeAllocation: bad hAlloc=%p", a);
        return STATUS_INVALID_HANDLE;
    }

    //
    // dxgk calls this for primaries (to query their scan-out parameters)
    // and for shared-resource imports (to learn the layout of a swapchain
    // surface that came from another adapter). Buffer-typed allocations
    // can answer zero/D3DDDIFMT_UNKNOWN; we just echo whatever the UMD
    // / pan_kmod publishd at CreateAllocation time.
    //
    p->Width                              = a->Width;
    p->Height                             = a->Height;
    p->Format                             = (D3DDDIFORMAT)a->Format;
    p->MultisampleMethod.NumSamples       = a->MultisampleMethod;
    p->MultisampleMethod.NumQualityLevels = 0;
    p->RefreshRate.Numerator        = a->RefreshNumerator;
    p->RefreshRate.Denominator      = a->RefreshDenominator;
    p->PrivateDriverFormatAttribute = 0;
    p->Rotation                     = (D3DDDI_ROTATION)a->Rotation;
    p->Flags.Value                  = 0;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdGetStandardAllocationDriverData(
    _In_ const HANDLE hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* p)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // The runtime can ask the KMD to manufacture private-driver-data blobs
    // for "standard" allocation types (primary, shadow, staging, GDI). dxgk
    // first calls us with both buffer pointers NULL to query sizes; then
    // re-calls with allocated buffers to fill in. We synthesize a sensible
    // WINMALI_ALLOC_PRIVATE for every supported standard type and an empty
    // WINMALI_RESOURCE_PRIVATE so the swapchain path round-trips cleanly.
    //
    // First pass (size query): just publish the buffer sizes and return.
    //
    if (p->pAllocationPrivateDriverData == NULL && p->pResourcePrivateDriverData == NULL) {
        p->AllocationPrivateDriverDataSize = sizeof(WINMALI_ALLOC_PRIVATE);
        p->ResourcePrivateDriverDataSize   = sizeof(WINMALI_RESOURCE_PRIVATE);
        return STATUS_SUCCESS;
    }

    if (p->pAllocationPrivateDriverData != NULL) {
        WINMALI_ALLOC_PRIVATE* priv = (WINMALI_ALLOC_PRIVATE*)p->pAllocationPrivateDriverData;
        RtlZeroMemory(priv, sizeof(*priv));
        priv->Magic     = WINMALI_ALLOC_PRIVATE_MAGIC;
        priv->Version   = WINMALI_ALLOC_PRIVATE_VERSION;
        priv->Flags     = WINMALI_ALLOC_FLAG_CPU_VISIBLE;
        priv->Alignment = PAGE_SIZE;

        switch (p->StandardAllocationType) {
        case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE: {
            const D3DKMDT_SHAREDPRIMARYSURFACEDATA* d = p->pCreateSharedPrimarySurfaceData;
            priv->Flags |= WINMALI_ALLOC_FLAG_PRIMARY | WINMALI_ALLOC_FLAG_RENDERTARGET;
            if (d != NULL) {
                priv->Width             = d->Width;
                priv->Height            = d->Height;
                priv->Format            = d->Format;
                priv->BytesPerPixel     = 4;   // dxgk only standardises 32-bpp primaries
                priv->Pitch             = priv->Width * priv->BytesPerPixel;
                priv->SizeBytes         = (ULONGLONG)priv->Pitch * priv->Height;
                priv->RefreshNumerator  = d->RefreshRate.Numerator;
                priv->RefreshDenominator= d->RefreshRate.Denominator;
            }
        } break;

        case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE: {
            const D3DKMDT_SHADOWSURFACEDATA* d = p->pCreateShadowSurfaceData;
            priv->Flags |= WINMALI_ALLOC_FLAG_RENDERTARGET;
            if (d != NULL) {
                priv->Width         = d->Width;
                priv->Height        = d->Height;
                priv->Format        = d->Format;
                priv->BytesPerPixel = 4;
                priv->Pitch         = d->Pitch;
                priv->SizeBytes     = (ULONGLONG)d->Pitch * d->Height;
            }
        } break;

        case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE: {
            const D3DKMDT_STAGINGSURFACEDATA* d = p->pCreateStagingSurfaceData;
            priv->Flags |= WINMALI_ALLOC_FLAG_TEXTURE;
            if (d != NULL) {
                priv->Width         = d->Width;
                priv->Height        = d->Height;
                priv->Pitch         = d->Pitch;
                priv->BytesPerPixel = 4;
                priv->SizeBytes     = (ULONGLONG)d->Pitch * d->Height;
            }
        } break;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
        case D3DKMDT_STANDARDALLOCATION_GDISURFACE: {
            const D3DKMDT_GDISURFACEDATA* d = p->pCreateGdiSurfaceData;
            priv->Flags |= WINMALI_ALLOC_FLAG_TEXTURE;
            if (d != NULL) {
                priv->Width         = d->Width;
                priv->Height        = d->Height;
                priv->Format        = d->Format;
                priv->BytesPerPixel = 4;
                priv->Pitch         = priv->Width * priv->BytesPerPixel;
                priv->SizeBytes     = (ULONGLONG)priv->Pitch * priv->Height;
            }
        } break;
#endif
        default:
            WINMALI_WARN(
                "GetStandardAllocationDriverData: unrecognised type=%u",
                p->StandardAllocationType);
            priv->SizeBytes = PAGE_SIZE;
            break;
        }

        p->AllocationPrivateDriverDataSize = sizeof(WINMALI_ALLOC_PRIVATE);
    }

    if (p->pResourcePrivateDriverData != NULL) {
        WINMALI_RESOURCE_PRIVATE* res = (WINMALI_RESOURCE_PRIVATE*)p->pResourcePrivateDriverData;
        RtlZeroMemory(res, sizeof(*res));
        res->Magic          = WINMALI_RESOURCE_PRIVATE_MAGIC;
        res->Version        = WINMALI_RESOURCE_PRIVATE_VERSION;
        res->NumAllocations = 1;
        p->ResourcePrivateDriverDataSize = sizeof(WINMALI_RESOURCE_PRIVATE);
    }

    WINMALI_TRACE(
        "GetStandardAllocationDriverData: type=%u alloc=%u res=%u",
        p->StandardAllocationType,
        p->AllocationPrivateDriverDataSize,
        p->ResourcePrivateDriverDataSize);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdPatch(_In_ const HANDLE hAdapter, _In_ const DXGKARG_PATCH* p)
{
    WINMALI_DMABUF_PRIVATE* priv;
    UINT                    end;
    UINT                    i;

    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);

    if (p == NULL || p->pDmaBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (p->pDmaBufferPrivateData == NULL
        || p->DmaBufferPrivateDataSize < sizeof(WINMALI_DMABUF_PRIVATE))
    {
        WINMALI_WARN("Patch: priv buf too small (%u, need %llu)",
                     p->DmaBufferPrivateDataSize,
                     (ULONGLONG)sizeof(WINMALI_DMABUF_PRIVATE));
        return STATUS_INVALID_PARAMETER;
    }

    priv = (WINMALI_DMABUF_PRIVATE*)p->pDmaBufferPrivateData;

    //
    // Iterate the slice of patch locations dxgk wants resolved this
    // submission. For each entry we look up the post-paging address of
    // the referenced allocation and (eventually) splice it into the DMA
    // buffer at PatchOffset. Until the real Valhall command-stream
    // assembler lands we still walk the list — both to validate that the
    // command + patch layout the runtime is sending us is well-formed
    // and so the trace tells us what addresses Valhall will need.
    //
    end = p->PatchLocationListSubmissionStart + p->PatchLocationListSubmissionLength;
    if (end > p->PatchLocationListSize) {
        WINMALI_WARN("Patch: range [%u, %u) exceeds list size %u",
                     p->PatchLocationListSubmissionStart, end, p->PatchLocationListSize);
        return STATUS_INVALID_PARAMETER;
    }

    for (i = p->PatchLocationListSubmissionStart; i < end; ++i) {
        const D3DDDI_PATCHLOCATIONLIST* pl = &p->pPatchLocationList[i];
        const DXGK_ALLOCATIONLIST*       al;
        BYTE*                            target;

        if (pl->AllocationIndex >= p->AllocationListSize) {
            WINMALI_WARN("Patch[%u]: bad AllocationIndex=%u (n=%u)",
                         i, pl->AllocationIndex, p->AllocationListSize);
            continue;
        }
        al = &p->pAllocationList[pl->AllocationIndex];

        if ((UINT64)pl->PatchOffset + sizeof(UINT64) > (UINT64)p->DmaBufferSize) {
            // Out-of-range patch slot (likely a runtime fence-write
            // sentinel) — skip safely. Real submission will need these
            // once we emit CSF streams.
            continue;
        }

        // Splice the GPU virtual address (or physical if no virtual paging
        // is wired) at PatchOffset. dxgk always patches a 64-bit slot.
        target = ((BYTE*)p->pDmaBuffer) + pl->PatchOffset;
        {
            UINT64 va = (UINT64)al->VirtualAddress;
            if (va == 0) {
                va = (UINT64)al->PhysicalAddress.QuadPart;
            }
            // Add the patch's own AllocationOffset so the runtime can patch
            // arbitrary sub-allocation slices.
            va += pl->AllocationOffset;
            RtlCopyMemory(target, &va, sizeof(va));
        }
    }

    priv->Flags          |= WINMALI_DMABUF_FLAG_PATCHED;
    priv->NumPatches      = p->PatchLocationListSubmissionLength;
    priv->PatchedAtOffset = p->DmaBufferSubmissionEndOffset;
    priv->DmaBufferPhys   = (UINT64)p->DmaBufferPhysicalAddress.QuadPart;

    WINMALI_TRACE(
        "Patch: dma=%p priv=%p fence=%u patches=%u range=[%u,%u) flags=0x%x",
        p->pDmaBuffer, priv, p->SubmissionFenceId,
        p->PatchLocationListSubmissionLength,
        p->PatchLocationListSubmissionStart, end, p->Flags.Value);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdSubmitCommand(
    _In_ const HANDLE hAdapter,
    _In_ const DXGKARG_SUBMITCOMMAND* p)
{
    PWINMALI_ADAPTER         adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    PWINMALI_KMD_CONTEXT     ctx;
    WINMALI_DMABUF_PRIVATE*  priv;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA  notify;

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (adapter == NULL || adapter->DxgkHandle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    ctx = (PWINMALI_KMD_CONTEXT)p->hContext;
    if (ctx == NULL || ctx->Magic != WINMALI_KMD_CONTEXT_MAGIC) {
        WINMALI_WARN("SubmitCommand: bad hContext=%p", ctx);
        return STATUS_INVALID_HANDLE;
    }
    if (p->pDmaBufferPrivateData == NULL
        || p->DmaBufferPrivateDataSize < sizeof(WINMALI_DMABUF_PRIVATE))
    {
        WINMALI_WARN("SubmitCommand: priv buf too small (%u, need %llu)",
                     p->DmaBufferPrivateDataSize,
                     (ULONGLONG)sizeof(WINMALI_DMABUF_PRIVATE));
        return STATUS_INVALID_PARAMETER;
    }
    priv = (WINMALI_DMABUF_PRIVATE*)p->pDmaBufferPrivateData;

    //
    // Record the submission against our per-context + per-adapter fence
    // counters so QueryCurrentFence / Escape diagnostics can see progress.
    // The "fence id" dxgk hands us is a UINT and starts >0 (dxgk skips 0).
    //
    priv->Flags             |= WINMALI_DMABUF_FLAG_SUBMITTED;
    priv->SubmissionFenceId  = (UINT64)p->SubmissionFenceId;
    priv->DmaBufferPhys      = (UINT64)p->DmaBufferPhysicalAddress.QuadPart;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    priv->DmaBufferGpuVa     = (UINT64)p->DmaBufferVirtualAddress;
#endif
    //
    // dxgk passes NodeOrdinal in DXGKARG_SUBMITCOMMAND only "when hContext
    // is NULL" (system submissions on WDDM 2.0+). For a context-bound
    // submission we re-derive it from the context, which we already
    // clamped at CreateContext-time from the 0x7FFF "any node" sentinel.
    //
    priv->NodeOrdinal        = ctx->NodeOrdinal;
    priv->EngineOrdinal      = p->EngineOrdinal;

    ctx->SubmittedFence      = (UINT64)p->SubmissionFenceId;
    InterlockedExchange64(&adapter->GpuSubmittedFence, (LONG64)p->SubmissionFenceId);

    //
    // Fake-completion path. Without a working Valhall command-stream
    // emitter the GPU never raises a real DMA_COMPLETED interrupt, so we
    // synthesise one immediately. dxgk treats every DXGK_INTERRUPT_DMA_*
    // notification as if it came from the ISR (queue head, fence id,
    // engine/node), and our DPC fires NotifyDpc to flush them. When real
    // submission lands this block will be replaced by a CSF kernel-queue
    // submit + the ISR will raise the real interrupt.
    //
    priv->Flags             |= WINMALI_DMABUF_FLAG_COMPLETED;
    priv->CompletionFenceId  = priv->SubmissionFenceId;
    ctx->CompletedFence      = priv->SubmissionFenceId;
    InterlockedExchange64(&adapter->GpuCompletedFence, (LONG64)p->SubmissionFenceId);

    RtlZeroMemory(&notify, sizeof(notify));
    notify.InterruptType                   = DXGK_INTERRUPT_DMA_COMPLETED;
    notify.DmaCompleted.SubmissionFenceId  = p->SubmissionFenceId;
    notify.DmaCompleted.NodeOrdinal        = ctx->NodeOrdinal;
    notify.DmaCompleted.EngineOrdinal      = p->EngineOrdinal;

    if (adapter->DxgkInterface.DxgkCbNotifyInterrupt != NULL) {
        adapter->DxgkInterface.DxgkCbNotifyInterrupt(adapter->DxgkHandle, &notify);
    }
    InterlockedExchange(&adapter->NotifyDpcPending, 1);
    if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL) {
        (VOID)adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
    }

    WINMALI_TRACE(
        "SubmitCommand: ctx=%p fence=%u node=%u eng=%u flags=0x%x dma=%llx priv=%p (fake-complete)",
        ctx, p->SubmissionFenceId,
        notify.DmaCompleted.NodeOrdinal, p->EngineOrdinal,
        p->Flags.Value, priv->DmaBufferPhys, priv);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdPreemptCommand(
    _In_ const HANDLE hAdapter,
    _In_ const DXGKARG_PREEMPTCOMMAND* p)
{
    PWINMALI_ADAPTER                  adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    DXGKARGCB_NOTIFY_INTERRUPT_DATA   notify;

    WINMALI_ENTER();

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (adapter == NULL || adapter->DxgkHandle == NULL) {
        // Without a dxgk back-ref we can't notify; succeed quietly so the
        // scheduler doesn't get stuck waiting for a never-arriving preempt.
        return STATUS_SUCCESS;
    }

    //
    // Mirror SubmitCommand's fake-completion model for preemption. Since
    // every submission was fake-completed in-place above, by the time dxgk
    // asks us to preempt the engine has nothing left to run; we report
    // "preempted at fence N" where N == LastSubmittedFence to keep dxgk's
    // accounting consistent.
    //
    RtlZeroMemory(&notify, sizeof(notify));
    notify.InterruptType                          = DXGK_INTERRUPT_DMA_PREEMPTED;
    notify.DmaPreempted.PreemptionFenceId         = p->PreemptionFenceId;
    notify.DmaPreempted.LastCompletedFenceId      =
        (UINT)ReadULong64Acquire((volatile ULONG64*)&adapter->GpuCompletedFence);
    notify.DmaPreempted.NodeOrdinal               = p->NodeOrdinal;
    notify.DmaPreempted.EngineOrdinal             = p->EngineOrdinal;

    if (adapter->DxgkInterface.DxgkCbNotifyInterrupt != NULL) {
        adapter->DxgkInterface.DxgkCbNotifyInterrupt(adapter->DxgkHandle, &notify);
    }
    InterlockedExchange(&adapter->NotifyDpcPending, 1);
    if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL) {
        (VOID)adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
    }

    WINMALI_TRACE(
        "PreemptCommand: preemptFence=%u lastDone=%u node=%u eng=%u",
        p->PreemptionFenceId, notify.DmaPreempted.LastCompletedFenceId,
        p->NodeOrdinal, p->EngineOrdinal);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdBuildPagingBuffer(
    _In_ const HANDLE hAdapter,
    _In_ DXGKARG_BUILDPAGINGBUFFER* p)
{
    PWINMALI_ADAPTER         adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    WINMALI_DMABUF_PRIVATE*  priv    = NULL;

    WINMALI_ENTER();

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (p->pDmaBufferPrivateData != NULL
        && p->DmaBufferPrivateDataSize >= sizeof(WINMALI_DMABUF_PRIVATE))
    {
        priv = (WINMALI_DMABUF_PRIVATE*)p->pDmaBufferPrivateData;
        priv->Flags |= WINMALI_DMABUF_FLAG_PAGING;
    }

    //
    // PhysicalAdapterCaps publishes PageTableUpdateMode = CPU_VIRTUAL and a
    // single non-aperture system-memory segment, so VIDMM almost never has
    // real GPU paging work for us. We treat every operation as "already
    // done by VIDMM at CPU level" and return success without consuming any
    // DMA-buffer space (do not advance pDmaBuffer / pDmaBufferPrivateData).
    //
    // Once the Mali MMU paging path is wired in we'll handle UPDATE_PAGE_TABLE
    // / FLUSH_TLB / TRANSFER_VIRTUAL here by emitting Valhall CSF stream ops.
    //
    switch (p->Operation) {
    //
    // Route B aperture maintenance. VIDMM walks our published aperture segment
    // page-by-page, calling MAP_APERTURE_SEGMENT to install sysmem PFNs at a
    // specific offset and UNMAP_APERTURE_SEGMENT to remove them. We don't have
    // a real GPU MMU bound to this aperture yet, but VIDMM still asserts on
    // descriptor inconsistencies, so we MUST mirror the page table into
    // adapter->AperturePageTable. SetVidPnSourceAddress reads the same table
    // to memcpy aperture-backed primaries into the GOP framebuffer when DWM
    // routes the primary outside the GOP local segment.
    //
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT: {
        if (adapter == NULL || adapter->AperturePageTable == NULL) {
            WINMALI_WARN("MAP_APERTURE_SEGMENT: no aperture page table");
            return STATUS_SUCCESS;
        }
        if (p->MapApertureSegment.SegmentId != WINMALI_APERTURE_SEGMENT_ID) {
            WINMALI_WARN("MAP_APERTURE_SEGMENT: seg=%u (expected %u)",
                p->MapApertureSegment.SegmentId, WINMALI_APERTURE_SEGMENT_ID);
            return STATUS_SUCCESS;
        }
        if (p->MapApertureSegment.pMdl == NULL) {
            WINMALI_WARN("MAP_APERTURE_SEGMENT: NULL pMdl");
            return STATUS_INVALID_PARAMETER;
        }
        {
            SIZE_T pageIndex = p->MapApertureSegment.OffsetInPages;
            SIZE_T pageCount = p->MapApertureSegment.NumberOfPages;
            SIZE_T mdlOff    = p->MapApertureSegment.MdlOffset;
            PPFN_NUMBER pfns;
            SIZE_T i;

            if (pageIndex + pageCount > adapter->AperturePageCount) {
                WINMALI_WARN(
                    "MAP_APERTURE_SEGMENT: range [%Iu,%Iu) past aperture cap %Iu",
                    pageIndex, pageIndex + pageCount,
                    adapter->AperturePageCount);
                return STATUS_INVALID_PARAMETER;
            }
            pfns = MmGetMdlPfnArray(p->MapApertureSegment.pMdl);
            for (i = 0; i < pageCount; ++i) {
                adapter->AperturePageTable[pageIndex + i] = pfns[mdlOff + i];
            }
            WINMALI_TRACE(
                "MAP_APERTURE_SEGMENT: seg=%u off=%Iu npages=%Iu mdlOff=%Iu",
                p->MapApertureSegment.SegmentId,
                pageIndex, pageCount, mdlOff);
        }
        if (priv != NULL && priv->Magic == 0) {
            priv->Magic   = WINMALI_DMABUF_PRIVATE_MAGIC;
            priv->Version = WINMALI_DMABUF_PRIVATE_VERSION;
        }
        return STATUS_SUCCESS;
    }

    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT: {
        if (adapter == NULL || adapter->AperturePageTable == NULL) {
            return STATUS_SUCCESS;
        }
        if (p->MapApertureSegment.SegmentId != WINMALI_APERTURE_SEGMENT_ID) {
            return STATUS_SUCCESS;
        }
        {
            SIZE_T pageIndex = p->MapApertureSegment.OffsetInPages;
            SIZE_T pageCount = p->MapApertureSegment.NumberOfPages;
            SIZE_T i;

            if (pageIndex + pageCount > adapter->AperturePageCount) {
                WINMALI_WARN(
                    "UNMAP_APERTURE_SEGMENT: range [%Iu,%Iu) past aperture cap %Iu",
                    pageIndex, pageIndex + pageCount,
                    adapter->AperturePageCount);
                return STATUS_INVALID_PARAMETER;
            }
            for (i = 0; i < pageCount; ++i) {
                adapter->AperturePageTable[pageIndex + i] = 0;
            }
            WINMALI_TRACE(
                "UNMAP_APERTURE_SEGMENT: seg=%u off=%Iu npages=%Iu",
                p->MapApertureSegment.SegmentId,
                pageIndex, pageCount);
        }
        if (priv != NULL && priv->Magic == 0) {
            priv->Magic   = WINMALI_DMABUF_PRIVATE_MAGIC;
            priv->Version = WINMALI_DMABUF_PRIVATE_VERSION;
        }
        return STATUS_SUCCESS;
    }

    case DXGK_OPERATION_TRANSFER:
    case DXGK_OPERATION_FILL:
    case DXGK_OPERATION_DISCARD_CONTENT:
    case DXGK_OPERATION_READ_PHYSICAL:
    case DXGK_OPERATION_WRITE_PHYSICAL:
    case DXGK_OPERATION_SPECIAL_LOCK_TRANSFER:
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    case DXGK_OPERATION_VIRTUAL_TRANSFER:
    case DXGK_OPERATION_VIRTUAL_FILL:
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    case DXGK_OPERATION_INIT_CONTEXT_RESOURCE:
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    case DXGK_OPERATION_UPDATE_PAGE_TABLE:
    case DXGK_OPERATION_FLUSH_TLB:
    case DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION:
    case DXGK_OPERATION_COPY_PAGE_TABLE_ENTRIES:
    case DXGK_OPERATION_NOTIFY_RESIDENCY:
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    case DXGK_OPERATION_SIGNAL_MONITORED_FENCE:
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT2:
    case DXGK_OPERATION_NOTIFY_FENCE_RESIDENCY:
#endif
        if (priv != NULL && priv->Magic == 0) {
            priv->Magic   = WINMALI_DMABUF_PRIVATE_MAGIC;
            priv->Version = WINMALI_DMABUF_PRIVATE_VERSION;
        }
        WINMALI_TRACE(
            "BuildPagingBuffer: op=%u dma=%p sz=%u priv=%p (CPU paging - no-op)",
            p->Operation, p->pDmaBuffer, p->DmaSize, p->pDmaBufferPrivateData);
        return STATUS_SUCCESS;

    default:
        WINMALI_WARN("BuildPagingBuffer: unsupported op=%u", p->Operation);
        return STATUS_NOT_SUPPORTED;
    }
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
    _In_ const DXGKARG_CANCELCOMMAND* p)
{
    WINMALI_ENTER();
    UNREFERENCED_PARAMETER(hAdapter);

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    //
    // Cancellation only happens for DMA buffers we never submitted, so
    // there's no GPU state to roll back. Mark the per-DMA private data
    // so a stray Patch/Submit on it would assert and return success.
    //
    if (p->pDmaBufferPrivateData != NULL
        && p->DmaBufferPrivateDataSize >= sizeof(WINMALI_DMABUF_PRIVATE))
    {
        WINMALI_DMABUF_PRIVATE* priv = (WINMALI_DMABUF_PRIVATE*)p->pDmaBufferPrivateData;
        priv->Flags |= WINMALI_DMABUF_FLAG_COMPLETED;
    }
    WINMALI_TRACE("CancelCommand: dma=%p sz=%u", p->pDmaBuffer, p->DmaBufferSize);
    return STATUS_SUCCESS;
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
WinMaliKmdRender(_In_ const HANDLE hContext, _Inout_ DXGKARG_RENDER* p)
{
    PWINMALI_KMD_CONTEXT     ctx = (PWINMALI_KMD_CONTEXT)hContext;
    WINMALI_DMABUF_PRIVATE*  priv;
    UINT                     patchBytes;

    WINMALI_ENTER();

    if (p == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ctx == NULL || ctx->Magic != WINMALI_KMD_CONTEXT_MAGIC) {
        WINMALI_WARN("Render: bad hContext=%p", hContext);
        return STATUS_INVALID_HANDLE;
    }
    if (p->pDmaBuffer == NULL || p->DmaSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (p->pDmaBufferPrivateData == NULL
        || p->DmaBufferPrivateDataSize < sizeof(WINMALI_DMABUF_PRIVATE))
    {
        WINMALI_WARN("Render: priv buf too small (%u, need %llu)",
                     p->DmaBufferPrivateDataSize,
                     (ULONGLONG)sizeof(WINMALI_DMABUF_PRIVATE));
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Bounds-check the UMD command buffer against the dxgk-supplied DMA
    // buffer. If the runtime asks us to render a buffer bigger than the
    // DMA window the context advertised, the right answer is
    // STATUS_BUFFER_TOO_SMALL - dxgk will reallocate a larger DMA buffer
    // and re-call us. Render-only-sample uses the same convention.
    //
    if (p->CommandLength > p->DmaSize) {
        WINMALI_WARN("Render: command (%u) > DMA buf (%u), asking for bigger",
                     p->CommandLength, p->DmaSize);
        return STATUS_BUFFER_TOO_SMALL;
    }

    priv = (WINMALI_DMABUF_PRIVATE*)p->pDmaBufferPrivateData;
    RtlZeroMemory(priv, sizeof(*priv));
    priv->Magic         = WINMALI_DMABUF_PRIVATE_MAGIC;
    priv->Version       = WINMALI_DMABUF_PRIVATE_VERSION;
    priv->Flags         = WINMALI_DMABUF_FLAG_RENDERED;
    priv->CommandLength = p->CommandLength;
    priv->NodeOrdinal   = ctx->NodeOrdinal;
    priv->EngineOrdinal = 0;

    //
    // Copy the UMD's command stream verbatim into the DMA buffer. Once the
    // Valhall command-stream assembler is up, this will be replaced by a
    // proper transcode step that consumes UMD ops and emits CSF queue
    // entries. The patch-location list-out conventionally mirrors the
    // list-in (one entry per allocation reference); dxgk uses it as the
    // bridge between Render (passive) and Patch (passive, post-paging).
    //
    if (p->CommandLength > 0 && p->pCommand != NULL) {
        RtlCopyMemory(p->pDmaBuffer, p->pCommand, p->CommandLength);
    }

    if (p->pPatchLocationListIn != NULL
        && p->pPatchLocationListOut != NULL
        && p->PatchLocationListInSize > 0
        && p->PatchLocationListOutSize >= p->PatchLocationListInSize)
    {
        patchBytes = p->PatchLocationListInSize * (UINT)sizeof(D3DDDI_PATCHLOCATIONLIST);
        RtlCopyMemory(
            p->pPatchLocationListOut,
            p->pPatchLocationListIn,
            patchBytes);
        // Advance the OUT pointer past the entries we wrote so dxgk knows
        // how many patches it should hand to WinMaliKmdPatch.
        p->pPatchLocationListOut += p->PatchLocationListInSize;
    } else if (p->PatchLocationListInSize > p->PatchLocationListOutSize) {
        WINMALI_WARN("Render: patch list-out (%u) too small for list-in (%u)",
                     p->PatchLocationListOutSize, p->PatchLocationListInSize);
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // Advance pDmaBuffer past the bytes we wrote so dxgk records the
    // correct submission slice. pDmaBufferPrivateData is per-DMA-buffer
    // (not per-submission), so we DO NOT advance it - dxgk hands it back
    // unchanged to Patch and SubmitCommand.
    //
    p->pDmaBuffer = ((BYTE*)p->pDmaBuffer) + p->CommandLength;

    WINMALI_TRACE(
        "Render: ctx=%p cmd=%u patches=%u/%u dma=%u multipass=%u",
        ctx, p->CommandLength,
        p->PatchLocationListInSize, p->PatchLocationListOutSize,
        p->DmaSize, p->MultipassOffset);
    return STATUS_SUCCESS;
}


NTSTATUS APIENTRY
WinMaliKmdIsSupportedVidPn(_In_ const HANDLE hAdapter, _Inout_ DXGKARG_ISSUPPORTEDVIDPN* p)
{
    WINMALI_ENTER();
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    const DXGK_VIDPN_INTERFACE* vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* topoIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopo = 0;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId;
    NTSTATUS status;

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

    if (a == NULL || a->DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    p->IsVidPnSupported = FALSE;

    status = a->DxgkInterface.DxgkCbQueryVidPnInterface(
        p->hDesiredVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &vidPnIf);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = vidPnIf->pfnGetTopology(p->hDesiredVidPn, &hTopo, &topoIf);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    for (sourceId = 0; sourceId < 1; ++sourceId) {
        SIZE_T numPathsFromSource = 0;

        status = topoIf->pfnGetNumPathsFromSource(hTopo, sourceId, &numPathsFromSource);
        if (status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY) {
            continue;
        }
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (numPathsFromSource > 1) {
            return STATUS_SUCCESS;
        }
    }

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
