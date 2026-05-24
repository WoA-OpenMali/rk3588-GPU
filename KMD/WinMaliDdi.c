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

#define WINMALI_KMD_PROCESS_MAGIC   'PrcW'
#define WINMALI_KMD_DEVICE_MAGIC    'DvcW'
#define WINMALI_KMD_CONTEXT_MAGIC   'CtxW'

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

    if (physicalAdapterIndex != 0 || nodeOrdinal != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    (VOID)RtlStringCbCopyW(pGetNodeMetadata->FriendlyName,
                           sizeof(pGetNodeMetadata->FriendlyName),
                           L"WinMali");
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* Must match PHYSICALADAPTERCAPS.Flags.GpuMmuSupported and DRIVERCAPS. */
    pGetNodeMetadata->GpuMmuSupported = TRUE;
    pGetNodeMetadata->IoMmuSupported  = FALSE;
#endif
    WINMALI_TRACE("GetNodeMetadata: node0 3D GpuMmu=1");
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
    if (pCreateContext->Flags.SystemContext == 0 && pCreateContext->NodeOrdinal != 0) {
        WINMALI_WARN("CreateContext: rejecting NodeOrdinal=%u flags=0x%x (we only own node 0)",
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

    /* What we tell dxgk about command buffers for this context. Mirrors
       NVIDIA's pattern (nvlContext.cpp:822-826, 2860). NoPatchingRequired=TRUE
       is critical: it tells dxgk to skip the DxgkDdiPatch call entirely (our
       Patch stub returns NOT_SUPPORTED, which dxgk would interpret as "this
       context is unusable" and silently refuse to schedule work on it). */
    pCreateContext->hContext                            = (HANDLE)ctx;
    pCreateContext->ContextInfo.DmaBufferSize           = PAGE_SIZE;
    pCreateContext->ContextInfo.DmaBufferSegmentSet     = (1u << (WINMALI_SEGMENT_ID_SYSMEM - 1));
    pCreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
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

/* DxgkDdiSubmitCommand wired to the real CSF kernel queue.

   Flow: dxgk fills a DMA buffer via BuildPagingBuffer (real LPAE PT work
   done CPU-side at the BPB call, plus a placeholder NOP per op). The
   buffer is then handed to SubmitCommand. We submit a Mali CSF NOP job
   through WinMaliCsfSubmitNopJob which (a) writes a CALL into the kernel
   queue ring at gpu_va=0x410000, (b) doorbells the MCU, (c) waits for
   the seqno to advance via the shared interface. On MCU ack we enqueue
   the fence id for the DPC to signal DMA_COMPLETED back to dxgk.

   This runs at PASSIVE_LEVEL (dxgk calls SubmitCommand from a worker).
   Worst case 5s for CSF timeout; on a healthy MCU it's microseconds.

   For paging submits the actual page-table work is already done by the
   time we got here (CPU-side in BPB); the CSF NOP serves as a proper
   GPU-side fence completion that dxgk can observe. */
_Function_class_(DXGKDDI_SUBMITCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdSubmitCommand(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmit)
{
    PWINMALI_ADAPTER adapter;
    KIRQL            oldIrql;
    ULONG            next;
    NTSTATUS         csfStatus;

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

    /* Real CSF kernel-queue round-trip. Submits a NOP shader call and
       blocks until MCU acks completion via seqno (or 5s timeout). */
    csfStatus = WinMaliCsfSubmitNopJob(adapter);
    if (!NT_SUCCESS(csfStatus)) {
        WINMALI_WARN("SubmitCommand: CSF NOP submit failed 0x%08x fenceId=%u",
                     csfStatus, pSubmit->SubmissionFenceId);
        /* Fail the submit - dxgk will TDR us if this persists. */
        return csfStatus;
    }

    /* CSF acked. Enqueue the fence id; the DPC drains and signals dxgk. */
    KeAcquireSpinLock(&adapter->FenceQueueLock, &oldIrql);
    next = (adapter->FenceQueueTail + 1) % WINMALI_FENCE_QUEUE_DEPTH;
    if (next == adapter->FenceQueueHead) {
        adapter->FenceQueueHead = (adapter->FenceQueueHead + 1) % WINMALI_FENCE_QUEUE_DEPTH;
    }
    adapter->FenceQueue[adapter->FenceQueueTail].SubmissionFenceId = pSubmit->SubmissionFenceId;
    adapter->FenceQueue[adapter->FenceQueueTail].NodeOrdinal       = pSubmit->NodeOrdinal;
    adapter->FenceQueue[adapter->FenceQueueTail].EngineOrdinal     = pSubmit->EngineOrdinal;
    adapter->FenceQueueTail = next;
    adapter->GpuSubmittedFence = pSubmit->SubmissionFenceId;
    KeReleaseSpinLock(&adapter->FenceQueueLock, oldIrql);

    if (adapter->DxgkInterface.DxgkCbQueueDpc != NULL && adapter->DxgkHandle != NULL) {
        adapter->DxgkInterface.DxgkCbQueueDpc(adapter->DxgkHandle);
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
