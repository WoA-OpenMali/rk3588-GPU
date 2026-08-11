/*
 * WinMaliDdi.c - "real" implementations for DDIs that dxgk exercises
 * during adapter init and process create. Everything in here replaces
 * a STATUS_NOT_SUPPORTED stub from WinMaliDxgkStubs.c (the generator's
 * EXISTING map routes the wire-up here).
 *
 * Scope for this iteration: get past adapter init + first CreateDevice
 * cleanly so the system stays up + SSH-able. Allocation/Render/Patch are
 * still NOT_IMPLEMENTED until we have the user-mode side ready.
 */

#include "WinMaliKmd.h"
#include "WinMaliBo.h"
#include "WinMaliSync.h"
#include "WinMaliVm.h"
#include "WinMaliGroup.h"

#define WINMALI_KMD_PROCESS_MAGIC   'PrcW'
#define WINMALI_KMD_DEVICE_MAGIC    'DvcW'
#define WINMALI_KMD_CONTEXT_MAGIC   'CtxW'

/* Exported by ntoskrnl since XP but not declared in wdm.h. Returns the
   15-char image file name of the process (no path). */
NTKERNELAPI PCHAR PsGetProcessImageFileName(_In_ PEPROCESS Process);

/* TRUE when the CURRENT thread (DxgkDdiCreateDevice runs in the context of
   the D3DKMTCreateDevice caller) belongs to dwm.exe. */
static BOOLEAN
WinMaliCallerIsDwm_(VOID)
{
    PCHAR  img = PsGetProcessImageFileName(PsGetCurrentProcess());
    STRING a, b;

    if (img == NULL) {
        return FALSE;
    }
    RtlInitString(&a, img);
    RtlInitString(&b, "dwm.exe");
    return RtlEqualString(&a, &b, TRUE /* case-insensitive */);
}

/* Per-process kernel state. Returned as hKmdProcess from CreateProcess and
   referenced from every Device/Context the process opens. */
typedef struct _WINMALI_KMD_PROCESS {
    ULONG               Magic;
    PWINMALI_ADAPTER    Adapter;
    HANDLE              hDxgkProcess;    /* dxgk-owned, opaque */
    UINT                Flags;           /* mirror of DXGK_CREATEPROCESSFLAGS */
    ULONG               Pasid;           /* primary process address-space id (if any) */
    LONG                DeviceCount;     /* outstanding Devices on this process */
} WINMALI_KMD_PROCESS, *PWINMALI_KMD_PROCESS;

/* Per-device state. Each D3D11 process opens one+ devices; each device
   can own many contexts. We chain back to the owning process via
   OwnerProcess so cleanup ordering errors are visible. */
typedef struct _WINMALI_KMD_DEVICE {
    ULONG                 Magic;
    PWINMALI_ADAPTER      Adapter;
    PWINMALI_KMD_PROCESS  OwnerProcess;  /* may be NULL on pre-WDDM2 path */
    HANDLE                hRtDevice;
    UINT                  Flags;
    ULONG                 Pasid;
    LONG                  ContextCount;
} WINMALI_KMD_DEVICE, *PWINMALI_KMD_DEVICE;

/* Per-context state. Each context binds (lazily, via SetRootPageTable)
   to a Mali AS slot. dxgk hands hContext back to us for every submit, so
   the KMD_CONTEXT address must stay valid until DestroyContext. */
typedef struct _WINMALI_KMD_CONTEXT {
    ULONG               Magic;
    PWINMALI_ADAPTER    Adapter;
    PWINMALI_KMD_DEVICE OwnerDevice;
    HANDLE              hRtContext;
    UINT                NodeOrdinal;
    UINT                EngineAffinity;
    UINT                Flags;
    UINT64              SubmittedFence;
    UINT64              CompletedFence;
} WINMALI_KMD_CONTEXT, *PWINMALI_KMD_CONTEXT;

/* Validate a dxgk-supplied device handle (e.g. DXGKARG_ESCAPE.hDevice)
   and return it as an opaque ownership token for the escape-created
   resource tables (VM/BO/SyncObj/Group). NULL when the handle is absent
   or isn't one of ours - callers then create the resource unowned. */
PVOID
WinMaliDeviceOwnerToken(_In_opt_ HANDLE hDevice)
{
    PWINMALI_KMD_DEVICE dev = (PWINMALI_KMD_DEVICE)hDevice;
    if (dev != NULL && dev->Magic == WINMALI_KMD_DEVICE_MAGIC) {
        return dev;
    }
    return NULL;
}

/* Resolve a dxgk device handle to its adapter (NULL if not one of ours).
   Used by DDIs that only receive hDevice (e.g. OpenAllocation) but need
   the adapter's DxgkInterface callbacks. */
PWINMALI_ADAPTER
WinMaliAdapterFromDeviceHandle(_In_opt_ HANDLE hDevice)
{
    PWINMALI_KMD_DEVICE dev = (PWINMALI_KMD_DEVICE)hDevice;
    if (dev != NULL && dev->Magic == WINMALI_KMD_DEVICE_MAGIC) {
        return dev->Adapter;
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiGetNodeMetadata                                                   */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_GETNODEMETADATA)
NTSTATUS
APIENTRY
WinMaliKmdGetNodeMetadata(
    IN_CONST_HANDLE          hAdapter,
    UINT                     NodeOrdinalAndAdapterIndex,
    OUT_PDXGKARG_GETNODEMETADATA pGetNodeMetadata)
{
    UINT nodeOrdinal;
    UINT physicalAdapterIndex;

    UNREFERENCED_PARAMETER(hAdapter);

    if (pGetNodeMetadata == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    nodeOrdinal          = DXGKNODEMETADATA_GETNODEORDINAL(NodeOrdinalAndAdapterIndex);
    physicalAdapterIndex = DXGKNODEMETADATA_GETPHYSICALADAPTERINDEX(NodeOrdinalAndAdapterIndex);
#else
    nodeOrdinal          = NodeOrdinalAndAdapterIndex;
    physicalAdapterIndex = 0;
#endif

    /* Two nodes: 0 = 3D (real render engine), 1 = COPY (paging engine).
       dxgkrnl requires the paging node (PHYSICALADAPTERCAPS.PagingNodeIndex)
       to be a copy/DMA engine, NOT the 3D node - pointing paging at node 0
       makes it bail at StartDevice, and pointing it out-of-range materialises
       a PHANTOM paging node whose per-process scheduler state is never
       initialised (NULL D[1] deref in VidSchiProfilePerformanceTick -> 0xD1).
       So we declare a real copy node 1 for paging; its buffers are handled
       CPU-side (BuildPagingBuffer), the submit just completes a fence. */
    if (physicalAdapterIndex != 0 || nodeOrdinal > 1) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    if (nodeOrdinal == 0) {
        pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
        (VOID)RtlStringCbCopyW(pGetNodeMetadata->FriendlyName,
                               sizeof(pGetNodeMetadata->FriendlyName),
                               L"WinMali 3D");
    } else {
        pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_COPY;
        (VOID)RtlStringCbCopyW(pGetNodeMetadata->FriendlyName,
                               sizeof(pGetNodeMetadata->FriendlyName),
                               L"WinMali Paging");
    }
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* Must match PHYSICALADAPTERCAPS.Flags.GpuMmuSupported and DRIVERCAPS. */
    pGetNodeMetadata->GpuMmuSupported = TRUE;
    pGetNodeMetadata->IoMmuSupported  = FALSE;
#endif
    WINMALI_TRACE("GetNodeMetadata: node%u %s GpuMmu=1", nodeOrdinal,
                  (nodeOrdinal == 0) ? "3D" : "COPY/paging");
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiCreateProcess / DestroyProcess                                     */
/* ------------------------------------------------------------------------ */

/* CreateProcess is called once per user-mode process when it first opens
   our adapter (via D3DKMT_OPENADAPTER). We allocate a per-process state
   blob that subsequent CreateDevice calls will link to via
   pCreateDevice->hKmdProcess. */
_Function_class_(DXGKDDI_CREATEPROCESS)
NTSTATUS
APIENTRY
WinMaliKmdCreateProcess(
    IN_CONST_HANDLE              hAdapter,
    INOUT_PDXGKARG_CREATEPROCESS pCreateProcess)
{
    PWINMALI_ADAPTER     adapter;
    PWINMALI_KMD_PROCESS proc;

    if (pCreateProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    proc = (PWINMALI_KMD_PROCESS)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(*proc), WINMALI_POOL_TAG);
    if (proc == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(proc, sizeof(*proc));
    proc->Magic         = WINMALI_KMD_PROCESS_MAGIC;
    proc->Adapter       = adapter;
    proc->hDxgkProcess  = pCreateProcess->hDxgkProcess;
    proc->Flags         = pCreateProcess->Flags.Value;
    if (pCreateProcess->NumPasid > 0 && pCreateProcess->pPasid != NULL) {
        proc->Pasid = pCreateProcess->pPasid[0];
    }

    pCreateProcess->hKmdProcess = (HANDLE)proc;
    WINMALI_TRACE("CreateProcess: proc=%p hDxgk=%p flags=0x%x pasid=%u",
                  proc, proc->hDxgkProcess, proc->Flags, proc->Pasid);
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_DESTROYPROCESS)
NTSTATUS
APIENTRY
WinMaliKmdDestroyProcess(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdProcess)
{
    PWINMALI_KMD_PROCESS proc = (PWINMALI_KMD_PROCESS)hKmdProcess;
    UNREFERENCED_PARAMETER(hAdapter);

    if (proc == NULL || proc->Magic != WINMALI_KMD_PROCESS_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (proc->DeviceCount != 0) {
        WINMALI_WARN("DestroyProcess: proc=%p leaks %ld devices",
                     proc, proc->DeviceCount);
    }
    WINMALI_TRACE("DestroyProcess: proc=%p", proc);
    proc->Magic = 0;
    ExFreePoolWithTag(proc, WINMALI_POOL_TAG);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiCreateDevice / DestroyDevice                                       */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_CREATEDEVICE)
NTSTATUS
APIENTRY
WinMaliKmdCreateDevice(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CREATEDEVICE pCreateDevice)
{
    PWINMALI_ADAPTER     adapter;
    PWINMALI_KMD_DEVICE  dev;
    PWINMALI_KMD_PROCESS owner = NULL;

    if (pCreateDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    /* Bring-up gate: DWM adopting the render-only adapter is pure churn
       today (device/VM/BO create-teardown loops per composition probe).
       Reject its device up front so it stays on the Basic Display adapter.
       HISTORY: this gate was first added in v1.0.0.28 and reverted in v29
       because the box bugchecked with it in - but that crash was the
       DriverUnload-clobber 0x139 (dxgkrnl!DpiInitializeEx list corruption
       on same-boot reinstall), fixed in v1.0.0.32; the gate itself was
       innocent. Re-enabled in v1.0.0.35. Registry escape hatch (no
       rebuild): Services\WinMaliKmd\Parameters\AllowDwm = 1 + reboot. */
    if (!g_WinMaliAllowDwm && WinMaliCallerIsDwm_()) {
        static LONG blocked = 0;
        if ((InterlockedIncrement(&blocked) & 0x3F) == 1) {
            WINMALI_WARN("CreateDevice: dwm.exe blocked (AllowDwm=0, total=%ld)",
                         blocked);
        }
        return STATUS_ACCESS_DENIED;
    }

    /* WDDM 2.0+: dxgk hands us the per-process state we returned from
       CreateProcess via pCreateDevice->hKmdProcess. Validate it. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (pCreateDevice->hKmdProcess != NULL) {
        owner = (PWINMALI_KMD_PROCESS)pCreateDevice->hKmdProcess;
        if (owner->Magic != WINMALI_KMD_PROCESS_MAGIC) {
            WINMALI_WARN("CreateDevice: hKmdProcess=%p bad magic", owner);
            owner = NULL;
        }
    }
#endif

    dev = (PWINMALI_KMD_DEVICE)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(*dev), WINMALI_POOL_TAG);
    if (dev == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(dev, sizeof(*dev));
    dev->Magic        = WINMALI_KMD_DEVICE_MAGIC;
    dev->Adapter      = adapter;
    dev->OwnerProcess = owner;
    dev->hRtDevice    = pCreateDevice->hDevice;
    dev->Flags        = pCreateDevice->Flags.Value;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    dev->Pasid        = pCreateDevice->Pasid;
#endif

    if (owner != NULL) {
        InterlockedIncrement(&owner->DeviceCount);
    }

    pCreateDevice->hDevice = (HANDLE)dev;
    WINMALI_TRACE("CreateDevice: dev=%p proc=%p rtDevice=%p flags=0x%x pasid=%u",
                  dev, owner, dev->hRtDevice, dev->Flags, dev->Pasid);
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_DESTROYDEVICE)
NTSTATUS
APIENTRY
WinMaliKmdDestroyDevice(
    IN_CONST_HANDLE hDevice)
{
    PWINMALI_KMD_DEVICE dev = (PWINMALI_KMD_DEVICE)hDevice;

    if (dev == NULL || dev->Magic != WINMALI_KMD_DEVICE_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (dev->ContextCount != 0) {
        WINMALI_WARN("DestroyDevice: dev=%p leaks %ld contexts",
                     dev, dev->ContextCount);
    }
    if (dev->OwnerProcess != NULL) {
        InterlockedDecrement(&dev->OwnerProcess->DeviceCount);
    }

    /* Run down every escape-created resource this device still owns. A UMD
       that dies (or just never sends the destroy ops) must not leak: each
       leaked VM pins a Mali AS slot and only 8 exist - six leaks and every
       VmCreate adapter-wide fails -ENOMEM until reboot (seen live
       2026-07-12: dwm/shell probes exhausted AS2..AS7, gl-smoke got 0
       pixel formats). Order: groups reference VMs, BOs may be VM-bound, so
       groups -> BOs -> syncobjs -> VMs. */
    {
        PWINMALI_ADAPTER adapter = dev->Adapter;
        if (adapter != NULL) {
            ULONG groups = WinMaliGroupRundownOwner(adapter, dev);
            ULONG bos    = WinMaliBoRundownOwner(adapter, dev);
            ULONG syncs  = WinMaliSyncObjRundownOwner(adapter, dev);
            ULONG vms    = WinMaliVmRundownOwner(adapter, dev);
            if ((groups | bos | syncs | vms) != 0) {
                WINMALI_TRACE("DestroyDevice rundown: dev=%p groups=%u bos=%u "
                              "syncs=%u vms=%u", dev, groups, bos, syncs, vms);
            }
        }
    }

    WINMALI_TRACE("DestroyDevice: dev=%p", dev);
    dev->Magic = 0;
    ExFreePoolWithTag(dev, WINMALI_POOL_TAG);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiCreateContext / DestroyContext                                     */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_CREATECONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdCreateContext(
    IN_CONST_HANDLE              hDevice,
    INOUT_PDXGKARG_CREATECONTEXT pCreateContext)
{
    PWINMALI_KMD_DEVICE  dev;
    PWINMALI_KMD_CONTEXT ctx;

    if (pCreateContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    dev = (PWINMALI_KMD_DEVICE)hDevice;
    if (dev == NULL || dev->Magic != WINMALI_KMD_DEVICE_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }

    /* dxgk creates one "system" context per device for its own paging-buffer
       submits. It carries Flags.SystemContext=1 and uses NodeOrdinal=0x7FFF as
       a sentinel meaning "no real engine". Accept it without binding to a Mali
       AS slot (paging BPB submits are no-ops for us under CPU_VIRTUAL).
       For a non-system context we still only own node 0. */
    if (pCreateContext->Flags.SystemContext == 0 && pCreateContext->NodeOrdinal > 1) {
        WINMALI_WARN("CreateContext: rejecting NodeOrdinal=%u flags=0x%x (we own nodes 0=3D,1=paging)",
                     pCreateContext->NodeOrdinal, pCreateContext->Flags.Value);
        return STATUS_INVALID_PARAMETER;
    }

    ctx = (PWINMALI_KMD_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(*ctx), WINMALI_POOL_TAG);
    if (ctx == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->Magic          = WINMALI_KMD_CONTEXT_MAGIC;
    ctx->Adapter        = dev->Adapter;
    ctx->OwnerDevice    = dev;
    ctx->hRtContext     = pCreateContext->hContext;
    ctx->NodeOrdinal    = pCreateContext->NodeOrdinal;
    ctx->EngineAffinity = pCreateContext->EngineAffinity;
    ctx->Flags          = pCreateContext->Flags.Value;

    InterlockedIncrement(&dev->ContextCount);
    pCreateContext->hContext                            = (HANDLE)ctx;
    pCreateContext->ContextInfo.DmaBufferSize           = PAGE_SIZE;
    pCreateContext->ContextInfo.DmaBufferSegmentSet     = (1u << (WINMALI_SEGMENT_ID_SYSMEM - 1));
    /* Non-system contexts get DMA private data big enough for the present
       Blt descriptor (DxgkDdiPresent stashes a WINMALI_BLT_DESC there and
       SubmitCommand executes it). With 0 here, Present's size check always
       failed and presents silently became no-ops. System/paging contexts
       keep 0 so the paging path can never look like a blt. */
    pCreateContext->ContextInfo.DmaBufferPrivateDataSize =
        pCreateContext->Flags.SystemContext ? 0
                                            : (UINT)sizeof(WINMALI_BLT_DESC);
    /* System / paging contexts use static GPU VAs - no patching needed.
       Render contexts on a GpuMmu adapter also don't need patching since
       the UMD writes final GPU VAs directly into the DMA buffer. So set
       NoPatchingRequired unconditionally. */
    pCreateContext->ContextInfo.Caps.NoPatchingRequired = TRUE;
    if (pCreateContext->Flags.SystemContext) {
        /* System / kernel contexts: dxgk allocates the DMA buffer itself
           and provides no patch/alloc lists. */
        pCreateContext->ContextInfo.AllocationListSize    = 0;
        pCreateContext->ContextInfo.PatchLocationListSize = 0;
    } else {
        pCreateContext->ContextInfo.AllocationListSize    = 64;
        pCreateContext->ContextInfo.PatchLocationListSize = 128;
    }

    WINMALI_TRACE("CreateContext: ctx=%p dev=%p node=%u flags=0x%x%s",
                  ctx, dev, ctx->NodeOrdinal, ctx->Flags,
                  (pCreateContext->Flags.SystemContext) ? " [SYSTEM]" : "");
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_DESTROYCONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdDestroyContext(
    IN_CONST_HANDLE hContext)
{
    PWINMALI_KMD_CONTEXT ctx = (PWINMALI_KMD_CONTEXT)hContext;

    if (ctx == NULL || ctx->Magic != WINMALI_KMD_CONTEXT_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    /* Release the Mali AS slot bound to this context (if SetRootPageTable
       ever ran). MMU unbind is idempotent / a no-op when nothing was bound. */
    if (ctx->Adapter != NULL) {
        (VOID)WinMaliMmuUnbindContext(ctx->Adapter, ctx->hRtContext);
    }
    if (ctx->OwnerDevice != NULL) {
        InterlockedDecrement(&ctx->OwnerDevice->ContextCount);
    }
    WINMALI_TRACE("DestroyContext: ctx=%p", ctx);
    ctx->Magic = 0;
    ExFreePoolWithTag(ctx, WINMALI_POOL_TAG);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiSetRootPageTable / GetRootPageTableSize                            */
/* ------------------------------------------------------------------------ */

/* DXGKDDI_SETROOTPAGETABLE returns VOID and takes a SINGLE context per
   call. Address is {SegmentId, SegmentOffset} - we translate to a host
   physical address using the DmaSegment base, then bind to a Mali AS slot. */
_Function_class_(DXGKDDI_SETROOTPAGETABLE)
VOID
APIENTRY
WinMaliKmdSetRootPageTable(
    IN_CONST_HANDLE                       hAdapter,
    IN_CONST_PDXGKARG_SETROOTPAGETABLE    pArgs)
{
    PWINMALI_ADAPTER adapter;
    ULONG            asSlot = 0;
    NTSTATUS         bindSt;
    UINT64           rootPtPa = 0;

    if (pArgs == NULL) {
        return;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return;
    }

    if (pArgs->Address.SegmentId == WINMALI_SEGMENT_ID_SYSMEM &&
        adapter->DmaSegmentVa != NULL) {
        rootPtPa = (UINT64)adapter->DmaSegmentPhys.QuadPart
                 + (UINT64)pArgs->Address.SegmentOffset;
    } else {
        /* Unbind request (SegmentId=0) or unknown segment - pass 0 to
           WinMaliMmuBindContextRootPt which treats 0 as unbind. */
        rootPtPa = 0;
    }

    bindSt = WinMaliMmuBindContextRootPt(adapter,
                                         pArgs->hContext,
                                         rootPtPa,
                                         &asSlot);
    WINMALI_TRACE("SetRootPageTable: ctx=%p seg=%u off=0x%llx pa=0x%llx entries=%u -> AS%u (0x%08x)",
                  pArgs->hContext,
                  pArgs->Address.SegmentId,
                  (ULONGLONG)pArgs->Address.SegmentOffset,
                  (ULONGLONG)rootPtPa,
                  pArgs->NumEntries,
                  asSlot, bindSt);
}

/* Returns the root PT size in bytes and fills NumberOfPte with the root
   level's PTE count. Mali LPAE: 9 bits per level * 8 bytes per PTE =
   512 entries = 4 KiB = PAGE_SIZE. */
_Function_class_(DXGKDDI_GETROOTPAGETABLESIZE)
SIZE_T
APIENTRY
WinMaliKmdGetRootPageTableSize(
    IN_CONST_HANDLE                       hAdapter,
    INOUT_PDXGKARG_GETROOTPAGETABLESIZE   pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pArgs != NULL) {
        pArgs->NumberOfPte = 512;   /* 9-bit top-level index */
    }
    return PAGE_SIZE;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiQueryCurrentFence                                                  */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_QUERYCURRENTFENCE)
NTSTATUS
APIENTRY
WinMaliKmdQueryCurrentFence(
    IN_CONST_HANDLE               hAdapter,
    INOUT_PDXGKARG_QUERYCURRENTFENCE pArgs)
{
    PWINMALI_ADAPTER adapter;
    if (pArgs == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    pArgs->CurrentFence = (UINT)adapter->GpuCompletedFence;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiSubmitCommand                                                     */
/* ------------------------------------------------------------------------ */

/* Present phase-2: execute a WINMALI_BLT_DESC recorded by DxgkDdiPresent.
   CPU copy from the rendered backbuffer allocation into the present target
   (DWM redirection / cross-adapter surface), row by row, honoring both
   pitches and the dst rect. Runs at PASSIVE_LEVEL (SubmitCommand comes from
   a dxgk worker); both allocations are resident here because VidMm makes
   present's allocation list resident before submitting the DMA buffer.
   Any inconsistency drops the frame with a warning - never the fence. */
static VOID
WinMaliPresentExecuteBlt_(_In_ const WINMALI_BLT_DESC* d)
{
    PWINMALI_KMD_ALLOCATION src = d->Src;
    PWINMALI_KMD_ALLOCATION dst = d->Dst;
    PUCHAR srcBase, dstBase;
    SIZE_T srcBytes, dstBytes;
    ULONG  srcPitch, dstPitch;
    LONG   width, height, r;
    const ULONG bpp = 4;   /* swapchain / GDI surfaces are 32bpp linear */

    if (src == NULL || dst == NULL ||
        src->Magic != WINMALI_KMD_ALLOC_MAGIC ||
        dst->Magic != WINMALI_KMD_ALLOC_MAGIC) {
        WINMALI_WARN("PresentBlt: bad allocation descriptors (src=%p dst=%p)", src, dst);
        return;
    }
    if (src->ApertureMdl == NULL || dst->ApertureMdl == NULL) {
        WINMALI_WARN("PresentBlt: not resident (srcMdl=%p dstMdl=%p) - frame dropped",
                     src->ApertureMdl, dst->ApertureMdl);
        return;
    }

    srcPitch = src->Pitch;
    dstPitch = dst->Pitch;
    if (srcPitch == 0 || dstPitch == 0) {
        WINMALI_WARN("PresentBlt: zero pitch (src=%u dst=%u) - frame dropped",
                     srcPitch, dstPitch);
        return;
    }

    srcBase = (PUCHAR)MmGetSystemAddressForMdlSafe(src->ApertureMdl,
                                                   NormalPagePriority | MdlMappingNoExecute);
    dstBase = (PUCHAR)MmGetSystemAddressForMdlSafe(dst->ApertureMdl,
                                                   NormalPagePriority | MdlMappingNoExecute);
    if (srcBase == NULL || dstBase == NULL) {
        WINMALI_WARN("PresentBlt: MDL map failed (src=%p dst=%p) - frame dropped",
                     srcBase, dstBase);
        return;
    }
    srcBase += (SIZE_T)src->ApertureMdlOffset << PAGE_SHIFT;
    dstBase += (SIZE_T)dst->ApertureMdlOffset << PAGE_SHIFT;
    srcBytes = (SIZE_T)src->AperturePageCount << PAGE_SHIFT;
    dstBytes = (SIZE_T)dst->AperturePageCount << PAGE_SHIFT;

    width  = d->DstRight - d->DstLeft;
    height = d->DstBottom - d->DstTop;
    if (width <= 0 || height <= 0 || d->SrcLeft < 0 || d->SrcTop < 0 ||
        d->DstLeft < 0 || d->DstTop < 0) {
        return;
    }

    /* This is a 1:1 blt (no stretch): clamp the copy extent to what the SOURCE
       actually holds so a dst rect larger than the rendered backbuffer can't
       walk us past the src rows/columns (the per-row bounds check below is the
       hard backstop; this keeps a mismatched present from tearing). */
    {
        LONG srcRows = (LONG)(srcBytes / srcPitch);
        LONG srcCols = (LONG)(srcPitch / bpp);
        if (d->SrcTop + height > srcRows) height = srcRows - d->SrcTop;
        if (d->SrcLeft + width  > srcCols) width  = srcCols - d->SrcLeft;
        if (width <= 0 || height <= 0) {
            WINMALI_WARN("PresentBlt: src too small (rows=%ld cols=%ld srcTop=%d srcLeft=%d) - dropped",
                         srcRows, srcCols, d->SrcTop, d->SrcLeft);
            return;
        }
    }

    for (r = 0; r < height; ++r) {
        SIZE_T srcOff = (SIZE_T)(d->SrcTop + r) * srcPitch +
                        (SIZE_T)d->SrcLeft * bpp;
        SIZE_T dstOff = (SIZE_T)(d->DstTop + r) * dstPitch +
                        (SIZE_T)d->DstLeft * bpp;
        SIZE_T rowBytes = (SIZE_T)width * bpp;

        if (srcOff + rowBytes > srcBytes || dstOff + rowBytes > dstBytes) {
            WINMALI_WARN("PresentBlt: clipped at row %ld/%ld (src %Iu+%Iu/%Iu dst %Iu+%Iu/%Iu)",
                         r, height, srcOff, rowBytes, srcBytes, dstOff, rowBytes, dstBytes);
            break;
        }
        RtlCopyMemory(dstBase + dstOff, srcBase + srcOff, rowBytes);
    }

    /* Confirm the copy actually ran (vs the silent drops above) so on-screen
       bring-up can be verified from kd without guessing. One line per present. */
    WINMALI_TRACE("PresentBlt: copied %ldx%ld px  src(pitch=%u bytes=%Iu)->dst(pitch=%u bytes=%Iu) "
                  "src[%d,%d] dst[%d,%d]",
                  width, height, srcPitch, srcBytes, dstPitch, dstBytes,
                  d->SrcLeft, d->SrcTop, d->DstLeft, d->DstTop);
}

/* ------------------------------------------------------------------------ */
/* Async submit scheduler                                                   */
/*                                                                          */
/* DxgkDdiSubmitCommand must NOT block the dxgk scheduler worker thread on   */
/* the CSF round-trip, and WDDM 3.2/GpuMmu REQUIRES real scheduling caps     */
/* (PreemptionAware/CancelCommandAware - dxgk won't even start the adapter   */
/* without them, and stubbing the DDIs while advertising them bugchecks the  */
/* scheduler). So: SubmitCommand enqueues here and returns immediately; a    */
/* dedicated KMD worker thread runs the (proven, synchronous) CSF NOP off    */
/* the scheduler thread and completes the fence via the DPC. PreemptCommand  */
/* enqueues an in-order marker so preemption reports the exact completed-    */
/* fence boundary (DMA-buffer-boundary granularity) with no race.           */
/* ------------------------------------------------------------------------ */

/* Push a completed/preempted fence onto the DPC-drained notify queue. */
static VOID
WinMaliSchedPushFence_(_Inout_ PWINMALI_ADAPTER a, _In_ UINT fence,
                       _In_ UINT node, _In_ UINT engine,
                       _In_ BOOLEAN isPreempt, _In_ UINT lastCompleted)
{
    KIRQL irql;
    ULONG next;
    KeAcquireSpinLock(&a->FenceQueueLock, &irql);
    next = (a->FenceQueueTail + 1) % WINMALI_FENCE_QUEUE_DEPTH;
    if (next == a->FenceQueueHead) {
        a->FenceQueueHead = (a->FenceQueueHead + 1) % WINMALI_FENCE_QUEUE_DEPTH;
    }
    a->FenceQueue[a->FenceQueueTail].SubmissionFenceId    = fence;
    a->FenceQueue[a->FenceQueueTail].NodeOrdinal          = node;
    a->FenceQueue[a->FenceQueueTail].EngineOrdinal        = engine;
    a->FenceQueue[a->FenceQueueTail].IsPreempt            = isPreempt;
    a->FenceQueue[a->FenceQueueTail].LastCompletedFenceId = lastCompleted;
    a->FenceQueueTail = next;
    KeReleaseSpinLock(&a->FenceQueueLock, irql);
}

/* Enqueue a submit/preempt request for the worker. Returns FALSE if the ring
   is momentarily full (caller then completes inline so no fence is dropped). */
static BOOLEAN
WinMaliSchedEnqueue_(_Inout_ PWINMALI_ADAPTER a, _In_ UINT fence,
                     _In_ UINT node, _In_ UINT engine, _In_ BOOLEAN isPreempt)
{
    KIRQL   irql;
    ULONG   next;
    BOOLEAN queued = FALSE;

    /* No worker (SchedInit failed, or we're tearing down) => tell the caller
       to complete inline. Never enqueue into a queue nothing will drain - that
       would leave the fence forever incomplete and TDR the adapter. */
    if (a->SubmitWorkerThread == NULL ||
        InterlockedCompareExchange(&a->SubmitWorkerStop, 0, 0) != 0) {
        return FALSE;
    }

    KeAcquireSpinLock(&a->SubmitQueueLock, &irql);
    next = (a->SubmitQueueTail + 1) % WINMALI_SUBMIT_QUEUE_DEPTH;
    if (next != a->SubmitQueueHead) {
        a->SubmitQueue[a->SubmitQueueTail].FenceId       = fence;
        a->SubmitQueue[a->SubmitQueueTail].NodeOrdinal   = node;
        a->SubmitQueue[a->SubmitQueueTail].EngineOrdinal = engine;
        a->SubmitQueue[a->SubmitQueueTail].IsPreempt     = isPreempt;
        a->SubmitQueueTail = next;
        queued = TRUE;
    }
    KeReleaseSpinLock(&a->SubmitQueueLock, irql);
    if (queued) {
        KeSetEvent(&a->SubmitWorkerWake, IO_NO_INCREMENT, FALSE);
    }
    return queued;
}

/* Dedicated worker: drains the submit queue in order, runs the CSF NOP for
   real submits and completes their fences, and turns preempt markers into a
   DMA_PREEMPTED at the current completed-fence boundary. */
static VOID
WinMaliSchedWorker_(_In_ PVOID Context)
{
    PWINMALI_ADAPTER a = (PWINMALI_ADAPTER)Context;

    for (;;) {
        KeWaitForSingleObject(&a->SubmitWorkerWake, Executive, KernelMode, FALSE, NULL);
        if (InterlockedCompareExchange(&a->SubmitWorkerStop, 0, 0) != 0) {
            break;
        }
        for (;;) {
            KIRQL   irql;
            BOOLEAN have = FALSE;
            UINT    fence = 0, node = 0, engine = 0;
            BOOLEAN isPreempt = FALSE;

            KeAcquireSpinLock(&a->SubmitQueueLock, &irql);
            if (a->SubmitQueueHead != a->SubmitQueueTail) {
                fence     = a->SubmitQueue[a->SubmitQueueHead].FenceId;
                node      = a->SubmitQueue[a->SubmitQueueHead].NodeOrdinal;
                engine    = a->SubmitQueue[a->SubmitQueueHead].EngineOrdinal;
                isPreempt = a->SubmitQueue[a->SubmitQueueHead].IsPreempt;
                a->SubmitQueueHead = (a->SubmitQueueHead + 1) % WINMALI_SUBMIT_QUEUE_DEPTH;
                have = TRUE;
            }
            KeReleaseSpinLock(&a->SubmitQueueLock, irql);
            if (!have) {
                break;
            }

            if (isPreempt) {
                /* All prior real submits drained above (in order) and their
                   fences completed, so the preemption boundary is simply the
                   last completed fence. */
                UINT last = (UINT)InterlockedCompareExchange64(&a->SchedLastCompletedFence, 0, 0);
                WinMaliSchedPushFence_(a, fence, node, engine, TRUE, last);
            } else {
                NTSTATUS st = WinMaliCsfSubmitNopJob(a, 500u);
                if (!NT_SUCCESS(st) && (a->CsfSubmitFailures++ & 0xFF) == 0) {
                    WINMALI_WARN("Sched: CSF NOP unavailable 0x%08x fence=%u"
                                 " (completing CPU-side, total=%u)",
                                 st, fence, a->CsfSubmitFailures);
                }
                InterlockedExchange64(&a->SchedLastCompletedFence, (LONG64)fence);
                WinMaliSchedPushFence_(a, fence, node, engine, FALSE, 0);
            }

            if (a->DxgkInterface.DxgkCbQueueDpc != NULL && a->DxgkHandle != NULL) {
                a->DxgkInterface.DxgkCbQueueDpc(a->DxgkHandle);
            }
        }
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
WinMaliSchedInit(_Inout_ PWINMALI_ADAPTER Adapter)
{
    HANDLE            hThread = NULL;
    OBJECT_ATTRIBUTES oa;
    NTSTATUS          st;

    if (Adapter->SubmitWorkerThread != NULL) {
        return STATUS_SUCCESS;   /* already up */
    }
    KeInitializeSpinLock(&Adapter->SubmitQueueLock);
    Adapter->SubmitQueueHead = 0;
    Adapter->SubmitQueueTail = 0;
    Adapter->SubmitWorkerStop = 0;
    Adapter->SchedLastCompletedFence = 0;
    KeInitializeEvent(&Adapter->SubmitWorkerWake, SynchronizationEvent, FALSE);

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    st = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                              WinMaliSchedWorker_, Adapter);
    if (!NT_SUCCESS(st)) {
        WINMALI_WARN("SchedInit: PsCreateSystemThread 0x%08x", st);
        return st;
    }
    st = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, *PsThreadType,
                                   KernelMode, &Adapter->SubmitWorkerThread, NULL);
    ZwClose(hThread);
    if (!NT_SUCCESS(st)) {
        /* Thread is running but we couldn't ref it: signal stop so it exits. */
        Adapter->SubmitWorkerThread = NULL;
        InterlockedExchange(&Adapter->SubmitWorkerStop, 1);
        KeSetEvent(&Adapter->SubmitWorkerWake, IO_NO_INCREMENT, FALSE);
        WINMALI_WARN("SchedInit: ObReferenceObjectByHandle 0x%08x", st);
        return st;
    }
    WINMALI_TRACE("SchedInit: async submit worker up");
    return STATUS_SUCCESS;
}

VOID
WinMaliSchedTeardown(_Inout_ PWINMALI_ADAPTER Adapter)
{
    PVOID th = Adapter->SubmitWorkerThread;
    if (th == NULL) {
        return;
    }
    InterlockedExchange(&Adapter->SubmitWorkerStop, 1);
    KeSetEvent(&Adapter->SubmitWorkerWake, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(th, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(th);
    Adapter->SubmitWorkerThread = NULL;
    WINMALI_TRACE("SchedTeardown: async submit worker stopped");
}

/* DxgkDdiSubmitCommand - enqueue to the async worker (see above) and return.

   For a Blt present the pixel copy (WinMaliPresentExecuteBlt_) runs here
   first (fast CPU memcpy of an already-rendered backbuffer); only the CSF
   fence is deferred to the worker. Paging submits carry no private data and
   just get a fence. */
_Function_class_(DXGKDDI_SUBMITCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdSubmitCommand(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmit)
{
    PWINMALI_ADAPTER adapter;

    if (pSubmit == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    WINMALI_TRACE("SubmitCommand: fenceId=%u node=%u engine=%u dmaSize=%u start=0x%x end=0x%x",
                  pSubmit->SubmissionFenceId,
                  pSubmit->NodeOrdinal,
                  pSubmit->EngineOrdinal,
                  pSubmit->DmaBufferSize,
                  pSubmit->DmaBufferSubmissionStartOffset,
                  pSubmit->DmaBufferSubmissionEndOffset);

    /* Present phase-2: a magic'd blt descriptor in the DMA private data
       means this submit is a DxgkDdiPresent Blt - execute the pixel copy
       before fencing. Paging submits ride the system context whose
       DmaBufferPrivateDataSize is 0, so they can never enter here. */
    if (pSubmit->pDmaBufferPrivateData != NULL &&
        pSubmit->DmaBufferPrivateDataSize >= sizeof(WINMALI_BLT_DESC)) {
        PWINMALI_BLT_DESC blt = (PWINMALI_BLT_DESC)pSubmit->pDmaBufferPrivateData;
        if (blt->Magic == WINMALI_BLT_DESC_MAGIC) {
            WinMaliPresentExecuteBlt_(blt);
            blt->Magic = 0;   /* consume: a TDR resubmit must not replay stale pointers */
        }
    }

    /* Hand off to the async worker and RETURN IMMEDIATELY - never block the
       dxgk scheduler thread on the CSF round-trip. The worker runs the CSF
       NOP fence off-thread and completes it via the DPC (DMA_COMPLETED). The
       worker's SubmitLock-serialized CSF path also serializes with escape
       GroupSubmits on the single CSG slot.

       If the worker's queue is momentarily full, complete inline (the old
       synchronous path) so a fence is never dropped - a lost fence would wedge
       VidMm's paging wait and TDR the adapter. */
    adapter->GpuSubmittedFence = pSubmit->SubmissionFenceId;
    if (!WinMaliSchedEnqueue_(adapter, pSubmit->SubmissionFenceId,
                              pSubmit->NodeOrdinal, pSubmit->EngineOrdinal, FALSE)) {
        (VOID)WinMaliCsfSubmitNopJob(adapter, 500u);
        InterlockedExchange64(&adapter->SchedLastCompletedFence,
                              (LONG64)pSubmit->SubmissionFenceId);
        WinMaliSchedPushFence_(adapter, pSubmit->SubmissionFenceId,
                               pSubmit->NodeOrdinal, pSubmit->EngineOrdinal, FALSE, 0);
        if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL && adapter->DxgkHandle != NULL) {
            adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
        }
    }
    return STATUS_SUCCESS;
}

/* DxgkDdiSubmitCommandVirtual - the GpuMmu (GPU-virtual-address) submit path.
   Because we report GpuMmuSupported=1, dxgkrnl submits DMA buffers through THIS
   DDI, not the legacy physical DxgkDdiSubmitCommand - in particular the paging
   buffers for our copy node (node 1). A stubbed NOT_SUPPORTED here bugchecks
   0x119 (VIDEO_SCHEDULER_INTERNAL_ERROR, arg2=0xC00000BB). Behaviour mirrors
   WinMaliKmdSubmitCommand: run a present blt if tagged, then hand the fence to
   the async worker (inline fallback if the queue is full) and return. */
_Function_class_(DXGKDDI_SUBMITCOMMANDVIRTUAL)
NTSTATUS
APIENTRY
WinMaliKmdSubmitCommandVirtual(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL pSubmit)
{
    PWINMALI_ADAPTER adapter;

    if (pSubmit == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    WINMALI_TRACE("SubmitCommandVirtual: fenceId=%u node=%u engine=%u dmaVa=0x%llx dmaSize=%u",
                  pSubmit->SubmissionFenceId,
                  pSubmit->NodeOrdinal,
                  pSubmit->EngineOrdinal,
                  (unsigned long long)pSubmit->DmaBufferVirtualAddress,
                  pSubmit->DmaBufferSize);

    /* Present phase-2 blt (same tagging as the physical path). Paging submits
       carry no private data so they never enter here. */
    if (pSubmit->pDmaBufferPrivateData != NULL &&
        pSubmit->DmaBufferPrivateDataSize >= sizeof(WINMALI_BLT_DESC)) {
        PWINMALI_BLT_DESC blt = (PWINMALI_BLT_DESC)pSubmit->pDmaBufferPrivateData;
        if (blt->Magic == WINMALI_BLT_DESC_MAGIC) {
            WinMaliPresentExecuteBlt_(blt);
            blt->Magic = 0;
        }
    }

    /* Phase 3 (dxgk DMA-buffer submission, KMD half): if the DMA buffer is
       tagged with a WINMALI_CS_SUBMIT_DESC, RUN the UMD's command stream on the
       CSG - resolve the named VM to its Mali AS slot and CALL the stream, the
       same way escape GroupSubmit does. This is what lets rendering move off
       the escape onto dxgk-scheduled DMA buffers. Inert for paging buffers (no
       CS tag). Runs inline like the present blt above; a later step can move it
       onto the async worker so it doesn't hold the scheduler thread. */
    if (pSubmit->pDmaBufferPrivateData != NULL &&
        pSubmit->DmaBufferPrivateDataSize >= sizeof(WINMALI_CS_SUBMIT_DESC)) {
        PWINMALI_CS_SUBMIT_DESC cs =
            (PWINMALI_CS_SUBMIT_DESC)pSubmit->pDmaBufferPrivateData;
        if (cs->Magic == WINMALI_CS_SUBMIT_DESC_MAGIC &&
            cs->StreamGpuVa != 0 && cs->StreamSize != 0) {
            /* Phase 2 coupling: when the UMD marks this CS as dxgk-addressed
               (WINMALI_DXGK_ADDR mapped its allocations via GpuMmu), the stream
               references VAs in THIS dxgk context's page table - run against the
               AS bound to it via SetRootPageTable. Otherwise (escape VmBind
               baseline) use the escape VM's AS. Both resolve to a Mali AS slot
               the CSG rebinds to before the CALL. The flag keeps the baseline
               correct even if dxgk pre-binds an (empty) context AS. */
            ULONG asSlot = WINMALI_AS_SLOT_MAX;
            if (cs->Flags & WINMALI_CS_SUBMIT_FLAG_DXGK_ADDR) {
                asSlot = WinMaliMmuGetContextAsSlot(adapter, pSubmit->hContext);
            }
            if (asSlot >= WINMALI_AS_SLOT_MAX) {
                PWINMALI_VM vm = WinMaliVmGet(adapter, cs->VmId);
                if (vm != NULL) {
                    asSlot = vm->Pt.AsSlot;
                    WinMaliVmPut(vm);
                }
            }
            if (asSlot < WINMALI_AS_SLOT_MAX) {
                WinMaliBoFlushVm(adapter, cs->VmId, TRUE);   /* CPU->DRAM */
                (VOID)WinMaliCsfSubmitGroupStream(adapter, asSlot,
                                                  cs->StreamGpuVa, cs->StreamSize,
                                                  0u, 500u);
                WinMaliBoFlushVm(adapter, cs->VmId, FALSE);  /* DRAM->CPU */
            } else {
                WINMALI_WARN("SubmitCommandVirtual: CS desc vm=%u -> no AS slot",
                             cs->VmId);
            }
            cs->Magic = 0;   /* consume */
        }
    }

    adapter->GpuSubmittedFence = pSubmit->SubmissionFenceId;
    if (!WinMaliSchedEnqueue_(adapter, pSubmit->SubmissionFenceId,
                              pSubmit->NodeOrdinal, pSubmit->EngineOrdinal, FALSE)) {
        (VOID)WinMaliCsfSubmitNopJob(adapter, 500u);
        InterlockedExchange64(&adapter->SchedLastCompletedFence,
                              (LONG64)pSubmit->SubmissionFenceId);
        WinMaliSchedPushFence_(adapter, pSubmit->SubmissionFenceId,
                               pSubmit->NodeOrdinal, pSubmit->EngineOrdinal, FALSE, 0);
        if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL && adapter->DxgkHandle != NULL) {
            adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
        }
    }
    return STATUS_SUCCESS;
}

/* DxgkDdiPreemptCommand - honor a preempt request at DMA-buffer boundary.
   We can't preempt mid-buffer; enqueue an in-order marker so the worker
   drains every already-submitted buffer (completing their fences) and then
   reports DMA_PREEMPTED with the exact completed-fence boundary. If the queue
   is full, report immediately against the last completed fence. */
_Function_class_(DXGKDDI_PREEMPTCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdPreemptCommand(
    IN_CONST_HANDLE                  hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND pPreempt)
{
    PWINMALI_ADAPTER adapter;

    if (pPreempt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    WINMALI_TRACE("PreemptCommand: node=%u engine=%u preemptFence=%u",
                  pPreempt->NodeOrdinal, pPreempt->EngineOrdinal,
                  pPreempt->PreemptionFenceId);

    if (!WinMaliSchedEnqueue_(adapter, pPreempt->PreemptionFenceId,
                              pPreempt->NodeOrdinal, pPreempt->EngineOrdinal, TRUE)) {
        UINT last = (UINT)InterlockedCompareExchange64(&adapter->SchedLastCompletedFence, 0, 0);
        WinMaliSchedPushFence_(adapter, pPreempt->PreemptionFenceId,
                               pPreempt->NodeOrdinal, pPreempt->EngineOrdinal, TRUE, last);
        if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL && adapter->DxgkHandle != NULL) {
            adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
        }
    }
    return STATUS_SUCCESS;
}

/* DxgkDdiCancelCommand - cancel a queued-but-not-submitted DMA buffer. Our
   submits complete near-instantly through the worker, so by the time a cancel
   arrives there is nothing outstanding to cancel; acknowledge success. */
_Function_class_(DXGKDDI_CANCELCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdCancelCommand(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_CANCELCOMMAND pCancel)
{
    UNREFERENCED_PARAMETER(pCancel);
    if (WinMaliAdapterFromDxgkHandle((PVOID)hAdapter) == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    WINMALI_TRACE("CancelCommand: nothing outstanding (async worker drains in order)");
    return STATUS_SUCCESS;
}

/* DxgkDdiQueryEngineStatus - the scheduler's health probe (we advertise
   SupportPerEngineTDR). Report the engine as responsive whenever completed
   has caught up to submitted, OR the worker is alive and draining: our
   submits are NOP fences that complete within the 500ms CSF bound, so the
   engine is always making progress. Reporting Responsive=1 keeps the
   scheduler from escalating a false hang to a TDR. */
_Function_class_(DXGKDDI_QUERYENGINESTATUS)
NTSTATUS
APIENTRY
WinMaliKmdQueryEngineStatus(
    IN_CONST_HANDLE                  hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS pArgs)
{
    PWINMALI_ADAPTER adapter;

    if (pArgs == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    /* Responsive if nothing is outstanding, or the worker thread is up (it
       drains in order and always completes each fence). */
    pArgs->EngineStatus.Value = 0;
    pArgs->EngineStatus.Responsive =
        (adapter->GpuCompletedFence >= adapter->GpuSubmittedFence ||
         adapter->SubmitWorkerThread != NULL) ? 1u : 0u;
    return STATUS_SUCCESS;
}

/* DxgkDdiResetEngine - per-engine TDR. We hold no persistent GPU state for a
   dxgk submit (paging work is done CPU-side in BuildPagingBuffer, present is
   a CPU blt, and the fence is a NOP), so "resetting" is just draining any
   queued fences to completion so no waiter is stranded; the next submit's
   unconditional CSG rebind re-establishes a clean slot. Nothing was aborted
   mid-execution (LastAbortedFenceId = 0). */
_Function_class_(DXGKDDI_RESETENGINE)
NTSTATUS
APIENTRY
WinMaliKmdResetEngine(
    IN_CONST_HANDLE            hAdapter,
    INOUT_PDXGKARG_RESETENGINE pReset)
{
    PWINMALI_ADAPTER adapter;
    KIRQL            irql;
    UINT             drained = 0;

    if (pReset == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    /* Drain any pending submit requests to COMPLETED so nothing is stranded.
       Dequeue under the lock (the worker uses the same lock, so we never
       double-consume an entry). Preempt markers are dropped - a reset
       supersedes an in-flight preempt. */
    for (;;) {
        UINT    fence = 0, node = 0, engine = 0;
        BOOLEAN isPreempt = FALSE, have = FALSE;
        KeAcquireSpinLock(&adapter->SubmitQueueLock, &irql);
        if (adapter->SubmitQueueHead != adapter->SubmitQueueTail) {
            fence     = adapter->SubmitQueue[adapter->SubmitQueueHead].FenceId;
            node      = adapter->SubmitQueue[adapter->SubmitQueueHead].NodeOrdinal;
            engine    = adapter->SubmitQueue[adapter->SubmitQueueHead].EngineOrdinal;
            isPreempt = adapter->SubmitQueue[adapter->SubmitQueueHead].IsPreempt;
            adapter->SubmitQueueHead =
                (adapter->SubmitQueueHead + 1) % WINMALI_SUBMIT_QUEUE_DEPTH;
            have = TRUE;
        }
        KeReleaseSpinLock(&adapter->SubmitQueueLock, irql);
        if (!have) {
            break;
        }
        if (!isPreempt) {
            InterlockedExchange64(&adapter->SchedLastCompletedFence, (LONG64)fence);
            WinMaliSchedPushFence_(adapter, fence, node, engine, FALSE, 0);
            ++drained;
        }
    }
    if (drained != 0 &&
        adapter->DxgkInterface.DxgkCbQueueDpc != NULL && adapter->DxgkHandle != NULL) {
        adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
    }

    pReset->LastAbortedFenceId = 0;   /* nothing was executing on the GPU */
    WINMALI_TRACE("ResetEngine: node=%u engine=%u drained=%u pending fences",
                  pReset->NodeOrdinal, pReset->EngineOrdinal, drained);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiPresent - swap-chain present for the render-only adapter.          */
/*                                                                          */
/* For a Blt present, record a WINMALI_BLT_DESC (src back buffer + dst      */
/* present target + rects) in the DMA buffer private data. The private data */
/* rides the DMA buffer to DxgkDdiSubmitCommand, which recognises the magic */
/* and runs WinMaliPresentExecuteBlt_ (the phase-2 CPU row copy) before     */
/* fencing the submit - so the rendered backbuffer pixels actually reach    */
/* the present target. The copy lives in SubmitCommand (not here) so it is  */
/* fenced like any other submit and is guarded by a magic that the paging   */
/* NOP path (private-data size 0) can never hit.                            */
/* Flip / ColorFill / other flags: emit nothing and succeed.                */
/* ------------------------------------------------------------------------ */
_Function_class_(DXGKDDI_PRESENT)
NTSTATUS
APIENTRY
WinMaliKmdPresent(
    IN_CONST_HANDLE        hContext,
    INOUT_PDXGKARG_PRESENT pPresent)
{
    PWINMALI_KMD_ALLOCATION src;
    PWINMALI_KMD_ALLOCATION dst;
    PWINMALI_BLT_DESC       desc;

    UNREFERENCED_PARAMETER(hContext);
    if (pPresent == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!pPresent->Flags.Blt ||
        pPresent->pAllocationList == NULL ||
        pPresent->NumSrcAllocations < 1 ||
        pPresent->NumDstAllocations < 1 ||
        pPresent->pDmaBufferPrivateData == NULL ||
        pPresent->DmaBufferPrivateDataSize < sizeof(WINMALI_BLT_DESC)) {
        /* Nothing we can (or need to) do; complete so the swap chain proceeds. */
        return STATUS_SUCCESS;
    }

    src = (PWINMALI_KMD_ALLOCATION)
        pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation;
    dst = (PWINMALI_KMD_ALLOCATION)
        pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation;
    if (src == NULL || dst == NULL) {
        return STATUS_SUCCESS;
    }

    desc = (PWINMALI_BLT_DESC)pPresent->pDmaBufferPrivateData;
    desc->Magic      = WINMALI_BLT_DESC_MAGIC;
    desc->SubRectCnt = pPresent->SubRectCnt;
    desc->Src        = src;
    desc->Dst        = dst;
    desc->SrcLeft    = pPresent->SrcRect.left;
    desc->SrcTop     = pPresent->SrcRect.top;
    desc->DstLeft    = pPresent->DstRect.left;
    desc->DstTop     = pPresent->DstRect.top;
    desc->DstRight   = pPresent->DstRect.right;
    desc->DstBottom  = pPresent->DstRect.bottom;

    /* Render-only present blt (RosKmd model): there is no display engine and no
       DMA copy engine, so do the copy on the CPU right here. Both allocations
       must be resident (ApertureMdl set by BuildPagingBuffer MAP_APERTURE_SEGMENT)
       - the UMD pins the backbuffer via LockCb, and dxgk makes the destination
       resident for the present. If the destination is a cross-adapter surface we
       do not own pages for (windowed DWM redirection), ApertureMdl is NULL and we
       log + skip (that case needs a cross-adapter path, not this direct blt). */
    {
        PUCHAR srcBase = NULL, dstBase = NULL;

        if (src->ApertureMdl != NULL) {
            srcBase = (PUCHAR)MmGetSystemAddressForMdlSafe(
                src->ApertureMdl, NormalPagePriority | MdlMappingNoExecute);
            if (srcBase != NULL)
                srcBase += (SIZE_T)src->ApertureMdlOffset * PAGE_SIZE;
        }
        if (dst->ApertureMdl != NULL) {
            dstBase = (PUCHAR)MmGetSystemAddressForMdlSafe(
                dst->ApertureMdl, NormalPagePriority | MdlMappingNoExecute);
            if (dstBase != NULL)
                dstBase += (SIZE_T)dst->ApertureMdlOffset * PAGE_SIZE;
        }

        if (srcBase != NULL && dstBase != NULL &&
            src->Pitch != 0 && dst->Pitch != 0) {
            ULONG bpp = (dst->Width != 0) ? (dst->Pitch / dst->Width) : 4u;
            LONG  w   = pPresent->DstRect.right  - pPresent->DstRect.left;
            LONG  h   = pPresent->DstRect.bottom - pPresent->DstRect.top;
            LONG  y;

            if (bpp == 0) bpp = 4u;
            for (y = 0; y < h; ++y) {
                LONG   sy = pPresent->SrcRect.top  + y;
                LONG   dy = pPresent->DstRect.top  + y;
                SIZE_T sOff, dOff, rowBytes;

                if (sy < 0 || dy < 0 || w <= 0)
                    continue;
                sOff = (SIZE_T)sy * src->Pitch + (SIZE_T)pPresent->SrcRect.left * bpp;
                dOff = (SIZE_T)dy * dst->Pitch + (SIZE_T)pPresent->DstRect.left * bpp;
                rowBytes = (SIZE_T)w * bpp;
                /* Never read/write past either allocation. */
                if (sOff + rowBytes > src->Size || dOff + rowBytes > dst->Size)
                    break;
                RtlCopyMemory(dstBase + dOff, srcBase + sOff, rowBytes);
            }
            WINMALI_TRACE("Present: Blt COPIED %dx%d bpp=%u src=%p dst=%p "
                          "dst=[%d,%d..%d,%d]",
                          w, h, bpp, src, dst, pPresent->DstRect.left,
                          pPresent->DstRect.top, pPresent->DstRect.right,
                          pPresent->DstRect.bottom);
        } else {
            WINMALI_WARN("Present: Blt SKIPPED (src resident=%d dst resident=%d) "
                         "- destination likely cross-adapter (DWM), needs a "
                         "cross-adapter copy path",
                         srcBase != NULL, dstBase != NULL);
        }
    }

    /* Consume a DMA-buffer token so dxgk still routes this through the submit
       path (fence/complete); the copy above already happened synchronously. */
    if (pPresent->pDmaBuffer != NULL && pPresent->DmaSize >= sizeof(ULONG)) {
        *(ULONG UNALIGNED*)pPresent->pDmaBuffer = WINMALI_BLT_DESC_MAGIC;
        pPresent->pDmaBuffer = (PVOID)((PUCHAR)pPresent->pDmaBuffer + sizeof(ULONG));
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiControlInterrupt - WDDM 2.x wants a real implementation; minimal. */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_CONTROLINTERRUPT)
NTSTATUS
APIENTRY
WinMaliKmdControlInterrupt(
    IN_CONST_HANDLE             hAdapter,
    IN_CONST_DXGK_INTERRUPT_TYPE InterruptType,
    IN_BOOLEAN                  EnableInterrupt)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptType);
    UNREFERENCED_PARAMETER(EnableInterrupt);
    /* IRQ enable/disable goes through Mali register writes from FW init.
       Just acknowledge the request here. */
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiResetFromTimeout / RestartFromTimeout - TDR. No-op success.        */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_RESETFROMTIMEOUT)
NTSTATUS
APIENTRY
WinMaliKmdResetFromTimeout(
    IN_CONST_HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_TRACE("ResetFromTimeout");
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_RESTARTFROMTIMEOUT)
NTSTATUS
APIENTRY
WinMaliKmdRestartFromTimeout(
    IN_CONST_HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_TRACE("RestartFromTimeout");
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiCollectDbgInfo - return a tiny ASCII blob with our flags.          */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_COLLECTDBGINFO)
NTSTATUS
APIENTRY
WinMaliKmdCollectDbgInfo(
    IN_CONST_HANDLE                hAdapter,
    IN_CONST_PDXGKARG_COLLECTDBGINFO pArgs)
{
    PWINMALI_ADAPTER adapter;
    if (pArgs == NULL || pArgs->pBuffer == NULL || pArgs->BufferSize == 0) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    /* One-line summary; truncate to caller's buffer. */
    (VOID)RtlStringCbPrintfA((PCHAR)pArgs->pBuffer, pArgs->BufferSize,
        "WinMali flags=0x%08x regs=%u mcu=%u csf=%u",
        adapter->AdapterFlags,
        adapter->GpuRegsMapped,
        (adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_MCU_ALIVE) ? 1 : 0,
        (adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_CSF_JOBS) ? 1 : 0);
    return STATUS_SUCCESS;
}
