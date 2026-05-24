
#include "WinMaliKmd.h"
#include "WinMaliDxgkInitFill.h"

static PWINMALI_ADAPTER g_WinMaliAdapter = NULL;

#define WM_TRACE_ENTER(name)         DbgPrint("[WinMali] >> " name "\n")
#define WM_TRACE_EXIT(name)          DbgPrint("[WinMali] << " name "\n")
#define WM_TRACE_EXIT_S(name, st)    DbgPrint("[WinMali] << " name " 0x%08x\n", (unsigned)(st))
#define WM_TRACE_EXIT_B(name, b)     DbgPrint("[WinMali] << " name " %s\n", (b) ? "TRUE" : "FALSE")

PWINMALI_ADAPTER
WinMaliAdapterFromContext(_In_opt_ const VOID* Context)
{
    PWINMALI_ADAPTER adapter = (PWINMALI_ADAPTER)Context;
    if (adapter == NULL || adapter->Magic != WINMALI_ADAPTER_MAGIC) {
        return NULL;
    }
    return adapter;
}

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

/* ---------------------------------------------------------------------- */
/* Entry points                                                            */
/* ---------------------------------------------------------------------- */

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA init;
    NTSTATUS                   status;

    /* Interactive iteration: break into kd immediately so the user can
       confirm exactly which binary is loaded and step through bring-up.
       DbgBreakPoint is a no-op when no kernel debugger is attached. */
    DbgPrint("[WinMali] >> DriverEntry abi=%u.%u (build " __DATE__ " " __TIME__ ")\n",
             WINMALI_ABI_MAJOR, WINMALI_ABI_MINOR);
    DbgBreakPoint();

    WinMaliDxgkPatchInitializationData(&init);

    DbgPrint("[WinMali]    init.Version=0x%08x sizeof(init)=%lu\n",
             (unsigned)init.Version, (unsigned long)sizeof(init));
    DbgPrint("[WinMali]    AddDevice=%p Escape=%p QueryAdapterInfo=%p\n",
             init.DxgkDdiAddDevice, init.DxgkDdiEscape, init.DxgkDdiQueryAdapterInfo);
    DbgPrint("[WinMali]    CreateProcess=%p CreateDevice=%p CreateContext=%p\n",
             init.DxgkDdiCreateProcess, init.DxgkDdiCreateDevice, init.DxgkDdiCreateContext);

    DbgPrint("[WinMali]    calling DxgkInitialize...\n");
    status = DxgkInitialize(DriverObject, RegistryPath, &init);
    DbgPrint("[WinMali]    DxgkInitialize returned 0x%08x\n", status);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[WinMali] << DriverEntry DxgkInitialize=0x%08x\n", status);
        return status;
    }

    DriverObject->DriverUnload = WinMaliKmdUnload;
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[WinMali] << DriverEntry STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

VOID
WinMaliKmdUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    WM_TRACE_ENTER("DriverUnload");
    UNREFERENCED_PARAMETER(DriverObject);
    WM_TRACE_EXIT("DriverUnload");
}

/* Distinct from DriverObject->DriverUnload above. DXGKDDI_UNLOAD takes
   VOID (per dispmprt.h) and is what init->DxgkDdiUnload must point at. */
VOID APIENTRY
WinMaliKmdDxgkUnload(VOID)
{
    WM_TRACE_ENTER("DxgkUnload");
    WM_TRACE_EXIT("DxgkUnload");
}

/* ---------------------------------------------------------------------- */
/* Device lifecycle                                                        */
/* ---------------------------------------------------------------------- */

NTSTATUS APIENTRY
WinMaliKmdAddDevice(
    _In_     CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Outptr_ PVOID*               MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter;
    WM_TRACE_ENTER("AddDevice");

    if (MiniportDeviceContext == NULL || PhysicalDeviceObject == NULL) {
        WM_TRACE_EXIT_S("AddDevice", STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    adapter = (PWINMALI_ADAPTER)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(*adapter), WINMALI_POOL_TAG);
    if (adapter == NULL) {
        WM_TRACE_EXIT_S("AddDevice", STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(adapter, sizeof(*adapter));
    adapter->Magic                = WINMALI_ADAPTER_MAGIC;
    adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    KeInitializeSpinLock(&adapter->FenceQueueLock);
    WinMaliBoTableInit(&adapter->BoTable);
    WinMaliSyncObjTableInit(&adapter->SyncObjTable);
    WinMaliVmTableInit(&adapter->VmTable);
    WinMaliGroupTableInit(&adapter->GroupTable);

    g_WinMaliAdapter       = adapter;
    *MiniportDeviceContext = adapter;

    DbgPrint("[WinMali]    AddDevice ctx=%p pdo=%p\n", adapter, PhysicalDeviceObject);
    WM_TRACE_EXIT_S("AddDevice", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdStartDevice(
    _In_  CONST PVOID            MiniportDeviceContext,
    _In_  PDXGK_START_INFO       DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE     DxgkInterface,
    _Out_ PULONG                 NumberOfVideoPresentSources,
    _Out_ PULONG                 NumberOfChildren)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    NTSTATUS         status;
    WM_TRACE_ENTER("StartDevice");

    if (adapter == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL) {
        WM_TRACE_EXIT_S("StartDevice", STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    adapter->DxgkStartInfo = *DxgkStartInfo;
    adapter->DxgkInterface = *DxgkInterface;
    adapter->DxgkHandle    = DxgkInterface->DeviceHandle;
    adapter->Started       = TRUE;

    /* Render-only adapter: zero VidPN sources, zero children. */
    *NumberOfVideoPresentSources = 0;
    *NumberOfChildren            = 0;

    DbgPrint("[WinMali]    StartDevice ctx=%p handle=%p\n",
             adapter, adapter->DxgkHandle);

    /* Real bring-up. Parse ACPI _CRS, map MMIO, probe GPU_ID. Lifted from
       the BadDriver where this was the part that actually worked. */
    status = WinMaliParseResources(adapter);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("ParseResources failed 0x%08x", status);
        /* Return success anyway so dxgk doesn't immediately tear us down
           via a hot-error path. We continue with no MMIO; subsequent
           registers reads return 0 and the warning is in the log. */
        WM_TRACE_EXIT_S("StartDevice", STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    status = WinMaliBringupHardware(adapter);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("BringupHardware failed 0x%08x", status);
        WM_TRACE_EXIT_S("StartDevice", STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    /* Re-seed the AS-slot allocator now that GPU_AS_PRESENT has been read. */
    WinMaliMmuInitAsAllocator(adapter);

    /* MMU scratch heap + AS1 bind. Lifted from panthor_mmu_init. */
    if (adapter->GpuRegsMapped) {
        NTSTATUS mmuStatus = WinMaliMmuInit(adapter);
        if (!NT_SUCCESS(mmuStatus)) {
            WINMALI_WARN("WinMaliMmuInit failed 0x%08x (AS1 not bound)",
                         mmuStatus);
        }
    }

    /* Record IRQ availability for the FW init path. The ISR itself is
       bound by dxgk via init->DxgkDdiInterruptRoutine. */
    {
        NTSTATUS irqStatus = WinMaliConnectInterrupt(adapter);
        if (!NT_SUCCESS(irqStatus)) {
            WINMALI_WARN("WinMaliConnectInterrupt failed 0x%08x", irqStatus);
        }
    }

    /* 256 MiB sysmem segment for dxgk. Allocated even if FW init later
       fails, so VIDMM still sees a memory pool and the cap walk has
       real numbers to point at. */
    {
        NTSTATUS segStatus = WinMaliVidmmAllocateSegment(adapter);
        if (!NT_SUCCESS(segStatus)) {
            WINMALI_WARN("WinMaliVidmmAllocateSegment failed 0x%08x", segStatus);
        }
    }

    /* CSF firmware boot. Reads \SystemRoot\System32\drivers\mali_csffw.bin,
       parses the panthor binary, loads sections to MCU AS0, sets
       MCU_CONTROL=AUTO and polls MCU_STATUS for ENABLED. */
    if (adapter->GpuRegsMapped) {
        NTSTATUS fwStatus = WinMaliFwInit(adapter);
        if (!NT_SUCCESS(fwStatus)) {
            WINMALI_WARN("WinMaliFwInit failed 0x%08x (CSF firmware not active)",
                         fwStatus);
        }
    }

    /* No-op self-test removed - it was useful while we were still proving
       MCU boot end-to-end, but now FW init itself signals MCU_ALIVE and
       CSF_JOBS, and we want every subsequent boot to land in a clean idle
       state with proper IRQ delivery, not a synchronous test submission. */

    adapter->StartDeviceEverSucceeded = TRUE;
    WINMALI_TRACE("StartDevice OK: regs=%u mmu=%u mcu_alive=%u csf_jobs=%u",
                  adapter->GpuRegsMapped,
                  (adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_MMU_READY) ? 1 : 0,
                  (adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_MCU_ALIVE) ? 1 : 0,
                  (adapter->AdapterFlags & WINMALI_ADAPTER_FLAG_CSF_JOBS) ? 1 : 0);
    WM_TRACE_EXIT_S("StartDevice", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdStopDevice(_In_ CONST PVOID MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    WM_TRACE_ENTER("StopDevice");
    if (adapter == NULL) {
        WM_TRACE_EXIT_S("StopDevice", STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    WinMaliFwTeardown(adapter);
    WinMaliDisconnectInterrupt(adapter);
    WinMaliMmuTeardown(adapter);
    WinMaliVidmmFreeSegment(adapter);
    WinMaliTeardownHardware(adapter);
    adapter->Started = FALSE;
    RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
    adapter->DxgkHandle = NULL;

    WM_TRACE_EXIT_S("StopDevice", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdRemoveDevice(_In_ CONST PVOID MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    WM_TRACE_ENTER("RemoveDevice");
    if (adapter == NULL) {
        WM_TRACE_EXIT_S("RemoveDevice", STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    /* Drain UMD escape state the caller didn't explicitly destroy. Order
       matters: Groups reference VMs (CSG slots may still be claimed); VMs
       own LPAE PT pages whose teardown also releases AS slots. BOs and
       SyncObjs are independent. */
    WinMaliGroupTableTeardown(adapter, &adapter->GroupTable);
    WinMaliVmTableTeardown(adapter, &adapter->VmTable);
    WinMaliBoTableTeardown(adapter, &adapter->BoTable);
    WinMaliSyncObjTableTeardown(adapter, &adapter->SyncObjTable);

    if (g_WinMaliAdapter == adapter) {
        g_WinMaliAdapter = NULL;
    }
    adapter->Magic = 0;
    ExFreePoolWithTag(adapter, WINMALI_POOL_TAG);
    WM_TRACE_EXIT_S("RemoveDevice", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------- */
/* DDI stubs                                                              */
/* ---------------------------------------------------------------------- */

NTSTATUS APIENTRY
WinMaliKmdDispatchIoRequest(
    _In_ CONST PVOID                MiniportDeviceContext,
    _In_ ULONG                      VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET      VideoRequestPacket)
{
    WM_TRACE_ENTER("DispatchIoRequest");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    WM_TRACE_EXIT_S("DispatchIoRequest", STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN APIENTRY
WinMaliKmdInterruptRoutine(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG       MessageNumber)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    ULONG gpuRaw, jobRaw, mmuRaw;
    BOOLEAN handled = FALSE;

    UNREFERENCED_PARAMETER(MessageNumber);

    /* No DbgPrint here - this fires at DIRQL and can repeat fast. */

    if (adapter == NULL || !adapter->GpuRegsMapped) {
        return FALSE;
    }

    /* Read the three IRQ block RAWSTAT registers. Any non-zero bit means
       the GPU is asserting that line. Ack by writing the same bits to
       the matching _CLEAR register. Without this the line stays high,
       the OS re-dispatches the ISR forever, DPCs starve, watchdog fires. */
    gpuRaw = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_GPU_IRQ_RAWSTAT);
    jobRaw = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_JOB_INT_RAWSTAT);
    mmuRaw = WinMaliHwRead32(&adapter->Hw, WINMALI_REG_MMU_INT_RAWSTAT);

    if (gpuRaw != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_GPU_IRQ_CLEAR, gpuRaw);
        handled = TRUE;
    }
    if (jobRaw != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_JOB_INT_CLEAR, jobRaw);
        handled = TRUE;
    }
    if (mmuRaw != 0) {
        WinMaliHwWrite32(&adapter->Hw, WINMALI_REG_MMU_INT_CLEAR, mmuRaw);
        handled = TRUE;
    }

    if (handled) {
        InterlockedIncrement64(&adapter->InterruptsHandled);
    } else {
        InterlockedIncrement64(&adapter->InterruptsSpurious);
    }
    InterlockedIncrement64(&adapter->InterruptsTotal);

    return handled;
}

VOID APIENTRY
WinMaliKmdDpcRoutine(_In_ CONST PVOID MiniportDeviceContext)
{
    PWINMALI_ADAPTER adapter = WinMaliAdapterFromContext(MiniportDeviceContext);
    KIRQL            oldIrql;

    /* Runs at DISPATCH_LEVEL via DxgkCbNotifyDpc / DxgkCbQueueDpc. Drains
       the synchronous fence-completion queue populated by SubmitCommand,
       calling DxgkCbNotifyInterrupt(DMA_COMPLETED) for each pending entry. */
    if (adapter == NULL) {
        return;
    }

    InterlockedExchange(&adapter->NotifyDpcPending, 0);

    if (adapter->DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        adapter->DxgkHandle == NULL) {
        return;
    }

    KeAcquireSpinLock(&adapter->FenceQueueLock, &oldIrql);
    while (adapter->FenceQueueHead != adapter->FenceQueueTail) {
        DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
        UINT fenceId   = adapter->FenceQueue[adapter->FenceQueueHead].SubmissionFenceId;
        UINT nodeOrd   = adapter->FenceQueue[adapter->FenceQueueHead].NodeOrdinal;
        UINT engineOrd = adapter->FenceQueue[adapter->FenceQueueHead].EngineOrdinal;
        adapter->FenceQueueHead = (adapter->FenceQueueHead + 1) % WINMALI_FENCE_QUEUE_DEPTH;
        KeReleaseSpinLock(&adapter->FenceQueueLock, oldIrql);

        RtlZeroMemory(&notify, sizeof(notify));
        notify.InterruptType                       = DXGK_INTERRUPT_DMA_COMPLETED;
        notify.DmaCompleted.SubmissionFenceId      = fenceId;
        notify.DmaCompleted.NodeOrdinal            = nodeOrd;
        notify.DmaCompleted.EngineOrdinal          = engineOrd;
        adapter->DxgkInterface.DxgkCbNotifyInterrupt(adapter->DxgkHandle, &notify);
        adapter->GpuCompletedFence = fenceId;

        WINMALI_TRACE("DPC: notified DMA_COMPLETED fence=%u node=%u engine=%u",
                      fenceId, nodeOrd, engineOrd);

        KeAcquireSpinLock(&adapter->FenceQueueLock, &oldIrql);
    }
    KeReleaseSpinLock(&adapter->FenceQueueLock, oldIrql);

    /* Tell dxgk it can poll our QueryCurrentFence and process the
       completions we just announced. */
    if (adapter->DxgkInterface.DxgkCbNotifyDpc != NULL) {
        adapter->DxgkInterface.DxgkCbNotifyDpc(adapter->DxgkHandle);
    }
}

/* WDDM 2.2+ pre-start handshake. dxgk calls this between AddDevice and
   StartDevice to negotiate boot-display preservation (smooth transition).
   For a render-only adapter without a boot display, both output bits stay
   zero. Returning NOT_SUPPORTED here tears the adapter down on WDDM 2.5. */
NTSTATUS APIENTRY
WinMaliKmdExchangePreStartInfo(
    _In_                              CONST PVOID            MiniportDeviceContext,
    _Inout_                           PDXGK_PRE_START_INFO   PreStartInfo)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    WM_TRACE_ENTER("ExchangePreStartInfo");
    if (PreStartInfo != NULL) {
        PreStartInfo->Output = 0;   /* render-only: no boot display */
    }
    WM_TRACE_EXIT_S("ExchangePreStartInfo", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdQueryChildRelations(
    _In_                             CONST PVOID            MiniportDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_                             ULONG                  ChildRelationsSize)
{
    WM_TRACE_ENTER("QueryChildRelations");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (ChildRelations != NULL && ChildRelationsSize > 0) {
        RtlZeroMemory(ChildRelations, ChildRelationsSize);
    }
    WM_TRACE_EXIT_S("QueryChildRelations", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdQueryChildStatus(
    _In_    CONST PVOID         MiniportDeviceContext,
    _In_    PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly)
{
    WM_TRACE_ENTER("QueryChildStatus");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    WM_TRACE_EXIT_S("QueryChildStatus", STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY
WinMaliKmdQueryDeviceDescriptor(
    _In_  CONST PVOID                  MiniportDeviceContext,
    _In_  ULONG                        ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR    DeviceDescriptor)
{
    WM_TRACE_ENTER("QueryDeviceDescriptor");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    WM_TRACE_EXIT_S("QueryDeviceDescriptor", STATUS_MONITOR_NO_DESCRIPTOR);
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

NTSTATUS APIENTRY
WinMaliKmdSetPowerState(
    _In_ CONST PVOID         MiniportDeviceContext,
    _In_ ULONG               DeviceUid,
    _In_ DEVICE_POWER_STATE  DevicePowerState,
    _In_ POWER_ACTION        ActionType)
{
    WM_TRACE_ENTER("SetPowerState");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(DevicePowerState);
    UNREFERENCED_PARAMETER(ActionType);
    WM_TRACE_EXIT_S("SetPowerState", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
WinMaliKmdNotifyAcpiEvent(
    _In_  CONST PVOID         MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE     EventType,
    _In_  ULONG               Event,
    _In_  PVOID               Argument,
    _Out_ PULONG              AcpiFlags)
{
    WM_TRACE_ENTER("NotifyAcpiEvent");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);
    if (AcpiFlags != NULL) {
        *AcpiFlags = 0;
    }
    WM_TRACE_EXIT_S("NotifyAcpiEvent", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

VOID APIENTRY
WinMaliKmdResetDevice(_In_ CONST PVOID MiniportDeviceContext)
{
    WM_TRACE_ENTER("ResetDevice");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    WM_TRACE_EXIT("ResetDevice");
}

NTSTATUS APIENTRY
WinMaliKmdQueryInterface(
    _In_ CONST PVOID    MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    WM_TRACE_ENTER("QueryInterface");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);
    WM_TRACE_EXIT_S("QueryInterface", STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

VOID APIENTRY
WinMaliKmdControlEtwLogging(
    _In_ BOOLEAN Enable,
    _In_ ULONG   Flags,
    _In_ UCHAR   Level)
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Level);
}

/* WinMaliKmdQueryAdapterInfo moved to WinMaliQai.c - cap-walk handler. */
