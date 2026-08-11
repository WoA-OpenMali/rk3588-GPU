#include "WinMaliKmd.h"
#include "WinMaliDxgkStubs.h"

_Function_class_(DXGKDDI_ACQUIRESWIZZLINGRANGE)
NTSTATUS
APIENTRY
WinMaliKmdStub_AcquireSwizzlingRange(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE    pAcquireSwizzlingRange
    )
{
    DbgPrint("[WinMali] >> AcquireSwizzlingRange\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pAcquireSwizzlingRange);
    DbgPrint("[WinMali] << AcquireSwizzlingRange STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RELEASESWIZZLINGRANGE)
NTSTATUS
APIENTRY
WinMaliKmdStub_ReleaseSwizzlingRange(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_RELEASESWIZZLINGRANGE     pReleaseSwizzlingRange
    )
{
    DbgPrint("[WinMali] >> ReleaseSwizzlingRange\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pReleaseSwizzlingRange);
    DbgPrint("[WinMali] << ReleaseSwizzlingRange STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_PATCH)
NTSTATUS
APIENTRY
WinMaliKmdStub_Patch(
    IN_CONST_HANDLE             hAdapter,
    IN_CONST_PDXGKARG_PATCH     pPatch
    )
{
    /* dxgk calls Patch to resolve allocation references in a DMA buffer before
       it is submitted - notably the present DMA buffer built by DxgkDdiPresent
       (src/dst allocations). Returning NOT_SUPPORTED here fails the whole
       submit right after our synchronous present copy, so present never
       completes. This driver does NOT need real address patching: the present
       blt is done on the CPU in DxgkDdiPresent, and render submits use GPU VAs
       through SubmitCommandVirtual (no patch locations to resolve). So this is
       a valid no-op - acknowledge success and let the pipeline proceed. */
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_PREEMPTCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdStub_PreemptCommand(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND    pPreemptCommand
    )
{
    DbgPrint("[WinMali] >> PreemptCommand\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    DbgPrint("[WinMali] << PreemptCommand STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RENDER)
NTSTATUS
APIENTRY
WinMaliKmdStub_Render(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_RENDER   pRender
    )
{
    DbgPrint("[WinMali] >> Render\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    DbgPrint("[WinMali] << Render STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_PRESENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_Present(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_PRESENT  pPresent
    )
{
    DbgPrint("[WinMali] >> Present\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPresent);
    DbgPrint("[WinMali] << Present STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RENDER)
NTSTATUS
APIENTRY
WinMaliKmdStub_RenderKm(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_RENDER   pRender
    )
{
    DbgPrint("[WinMali] >> RenderKm\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    DbgPrint("[WinMali] << RenderKm STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDISETPOWERCOMPONENTFSTATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetPowerComponentFState(
    IN_CONST_HANDLE DriverContext,
    UINT            ComponentIndex,
    UINT            FState
    )
{
    DbgPrint("[WinMali] >> SetPowerComponentFState\n");
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(ComponentIndex);
    UNREFERENCED_PARAMETER(FState);
    DbgPrint("[WinMali] << SetPowerComponentFState STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_QUERYDEPENDENTENGINEGROUP)
NTSTATUS
APIENTRY
WinMaliKmdStub_QueryDependentEngineGroup(
    IN_CONST_HANDLE                             hAdapter,
    INOUT_DXGKARG_QUERYDEPENDENTENGINEGROUP     pQueryDependentEngineGroup
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pQueryDependentEngineGroup == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* One engine per node; resetting a node affects only that node. */
    pQueryDependentEngineGroup->DependentNodeOrdinalMask =
        (1ULL << pQueryDependentEngineGroup->NodeOrdinal);
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_QUERYENGINESTATUS)
NTSTATUS
APIENTRY
WinMaliKmdStub_QueryEngineStatus(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS    pQueryEngineStatus
    )
{
    DbgPrint("[WinMali] >> QueryEngineStatus\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pQueryEngineStatus);
    DbgPrint("[WinMali] << QueryEngineStatus STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESETENGINE)
NTSTATUS
APIENTRY
WinMaliKmdStub_ResetEngine(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_RESETENGINE  pResetEngine
    )
{
    DbgPrint("[WinMali] >> ResetEngine\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pResetEngine);
    DbgPrint("[WinMali] << ResetEngine STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CANCELCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdStub_CancelCommand(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_CANCELCOMMAND pCancelCommand
    )
{
    DbgPrint("[WinMali] >> CancelCommand\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);
    DbgPrint("[WinMali] << CancelCommand STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDIPOWERRUNTIMECONTROLREQUEST)
NTSTATUS
APIENTRY
WinMaliKmdStub_PowerRuntimeControlRequest(
    IN_CONST_HANDLE DriverContext,
    IN              LPCGUID PowerControlCode,
    IN OPTIONAL     PVOID InBuffer,
    IN              SIZE_T InBufferSize,
    OUT OPTIONAL    PVOID OutBuffer,
    IN              SIZE_T OutBufferSize,
    OUT OPTIONAL    PSIZE_T BytesReturned
    )
{
    DbgPrint("[WinMali] >> PowerRuntimeControlRequest\n");
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);
    UNREFERENCED_PARAMETER(BytesReturned);
    DbgPrint("[WinMali] << PowerRuntimeControlRequest STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_NOTIFY_SURPRISE_REMOVAL)
NTSTATUS
APIENTRY
WinMaliKmdStub_NotifySurpriseRemoval(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType
    )
{
    DbgPrint("[WinMali] >> NotifySurpriseRemoval\n");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(RemovalType);
    DbgPrint("[WinMali] << NotifySurpriseRemoval STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDISETPOWERPSTATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetPowerPState(
    IN_CONST_HANDLE DriverContext,
    UINT ComponentIndex,
    UINT RequestedComponentPState
    )
{
    DbgPrint("[WinMali] >> SetPowerPState\n");
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(ComponentIndex);
    UNREFERENCED_PARAMETER(RequestedComponentPState);
    DbgPrint("[WinMali] << SetPowerPState STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CONTROLINTERRUPT2)
NTSTATUS
APIENTRY
WinMaliKmdStub_ControlInterrupt2(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2   InterruptControl
    )
{
    DbgPrint("[WinMali] >> ControlInterrupt2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptControl);
    DbgPrint("[WinMali] << ControlInterrupt2 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CALIBRATEGPUCLOCK)
NTSTATUS
APIENTRY
WinMaliKmdStub_CalibrateGpuClock(
    IN_CONST_HANDLE                             hAdapter,
    IN UINT32                                   NodeOrdinal,
    IN UINT32                                   EngineOrdinal,
    OUT_PDXGKARG_CALIBRATEGPUCLOCK	            pClockCalibration
    )
{
    LARGE_INTEGER qpc;
    LARGE_INTEGER freq;
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    if (pClockCalibration == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* No dedicated GPU timestamp register is wired; model GPU time as the
       system performance counter (same clock the escape TimestampInfo uses).
       CPU == GPU counter -> identity correlation. */
    qpc = KeQueryPerformanceCounter(&freq);
    RtlZeroMemory(pClockCalibration, sizeof(*pClockCalibration));
    pClockCalibration->GpuFrequency    = (ULONGLONG)freq.QuadPart;
    pClockCalibration->GpuClockCounter = (ULONGLONG)qpc.QuadPart;
    pClockCalibration->CpuClockCounter = (ULONGLONG)qpc.QuadPart;
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_FORMATHISTORYBUFFER)
NTSTATUS
APIENTRY
WinMaliKmdStub_FormatHistoryBuffer(
    IN_CONST_HANDLE                 hContext,
    IN DXGKARG_FORMATHISTORYBUFFER* pFormatData
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (pFormatData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* We advertise HistoryBufferPrecision, so dxgkrnl may ask us to format a
       GPU-timestamp history buffer on demand. We have no GPU-written history;
       present a well-formed empty formatted buffer. */
    if (pFormatData->pFormattedBuffer != NULL && pFormatData->FormattedBufferSize != 0) {
        RtlZeroMemory(pFormatData->pFormattedBuffer, pFormatData->FormattedBufferSize);
    }
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_RENDERGDI)
NTSTATUS
APIENTRY
WinMaliKmdStub_RenderGdi(
    IN_CONST_HANDLE          hContext,
    INOUT_PDXGKARG_RENDERGDI pRenderGdi
    )
{
    DbgPrint("[WinMali] >> RenderGdi\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRenderGdi);
    DbgPrint("[WinMali] << RenderGdi STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

/* NOTE: DxgkDdiSubmitCommandVirtual is implemented for real as
   WinMaliKmdSubmitCommandVirtual (WinMaliDdi.c) and wired in
   WinMaliDxgkStubsWire.h - a GpuMmu driver submits DMA buffers through it, so
   a NOT_SUPPORTED stub here would bugcheck 0x119. The stub was removed. */

_Function_class_(DXGKDDI_MAPCPUHOSTAPERTURE)
NTSTATUS
APIENTRY
WinMaliKmdStub_MapCpuHostAperture(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE pArgs
    )
{
    DbgPrint("[WinMali] >> MapCpuHostAperture\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << MapCpuHostAperture STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_UNMAPCPUHOSTAPERTURE)
NTSTATUS
APIENTRY
WinMaliKmdStub_UnmapCpuHostAperture(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE  pArgs
    )
{
    DbgPrint("[WinMali] >> UnmapCpuHostAperture\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << UnmapCpuHostAperture STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_POWERRUNTIMESETDEVICEHANDLE)
NTSTATUS
APIENTRY
WinMaliKmdStub_PowerRuntimeSetDeviceHandle(
    IN_CONST_HANDLE DriverContext,
    IN HANDLE PoDeviceHandle
    )
{
    DbgPrint("[WinMali] >> PowerRuntimeSetDeviceHandle\n");
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(PoDeviceHandle);
    DbgPrint("[WinMali] << PowerRuntimeSetDeviceHandle STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETSTABLEPOWERSTATE)
VOID
APIENTRY
WinMaliKmdStub_SetStablePowerState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE  pArgs
    )
{
    DbgPrint("[WinMali] >> SetStablePowerState\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SetStablePowerState\n");
}

_Function_class_(DXGKDDI_SETVIDEOPROTECTEDREGION)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVideoProtectedRegion(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION   pArgs
    )
{
    DbgPrint("[WinMali] >> SetVideoProtectedRegion\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SetVideoProtectedRegion STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY)
NTSTATUS
APIENTRY
WinMaliKmdStub_ValidateUpdateAllocationProperty(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_VALIDATEUPDATEALLOCPROPERTY pValidateUpdateAllocProperty
    )
{
    DbgPrint("[WinMali] >> ValidateUpdateAllocationProperty\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pValidateUpdateAllocProperty);
    DbgPrint("[WinMali] << ValidateUpdateAllocationProperty STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATEHWCONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateHwContext(
    IN_CONST_HANDLE                 hDevice,
    INOUT_PDXGKARG_CREATEHWCONTEXT  pCreateContext
    )
{
    DbgPrint("[WinMali] >> CreateHwContext\n");
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCreateContext);
    DbgPrint("[WinMali] << CreateHwContext STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYHWCONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyHwContext(
    IN_CONST_HANDLE     hHwContext
    )
{
    DbgPrint("[WinMali] >> DestroyHwContext\n");
    UNREFERENCED_PARAMETER(hHwContext);
    DbgPrint("[WinMali] << DestroyHwContext STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATEHWQUEUE)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateHwQueue(
    IN_CONST_HANDLE                 hHwContext,
    INOUT_PDXGKARG_CREATEHWQUEUE    pCreateHwQueue
    )
{
    DbgPrint("[WinMali] >> CreateHwQueue\n");
    UNREFERENCED_PARAMETER(hHwContext);
    UNREFERENCED_PARAMETER(pCreateHwQueue);
    DbgPrint("[WinMali] << CreateHwQueue STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYHWQUEUE)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyHwQueue(
    IN_CONST_HANDLE     hHwQueue
    )
{
    DbgPrint("[WinMali] >> DestroyHwQueue\n");
    UNREFERENCED_PARAMETER(hHwQueue);
    DbgPrint("[WinMali] << DestroyHwQueue STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SUBMITCOMMANDTOHWQUEUE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SubmitCommandToHwQueue(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE    pSubmitCommandToHwQueue
    )
{
    DbgPrint("[WinMali] >> SubmitCommandToHwQueue\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSubmitCommandToHwQueue);
    DbgPrint("[WinMali] << SubmitCommandToHwQueue STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SWITCHTOHWCONTEXTLIST)
NTSTATUS
APIENTRY
WinMaliKmdStub_SwitchToHwContextList(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SWITCHTOHWCONTEXTLIST pHwContextList
    )
{
    DbgPrint("[WinMali] >> SwitchToHwContextList\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pHwContextList);
    DbgPrint("[WinMali] << SwitchToHwContextList STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESETHWENGINE)
NTSTATUS
APIENTRY
WinMaliKmdStub_ResetHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESETHWENGINE    pResetHwEngine
    )
{
    DbgPrint("[WinMali] >> ResetHwEngine\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pResetHwEngine);
    DbgPrint("[WinMali] << ResetHwEngine STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_UPDATEHWCONTEXTSTATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_UpdateHwContextState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_UPDATEHWCONTEXTSTATE pUpdateHwContextState
    )
{
    DbgPrint("[WinMali] >> UpdateHwContextState\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pUpdateHwContextState);
    DbgPrint("[WinMali] << UpdateHwContextState STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATEPROTECTEDSESSION)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateProtectedSession(
    IN_CONST_HANDLE                       hAdapter,
    INOUT_PDXGKARG_CREATEPROTECTEDSESSION pCreateProtectedSession
    )
{
    DbgPrint("[WinMali] >> CreateProtectedSession\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateProtectedSession);
    DbgPrint("[WinMali] << CreateProtectedSession STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYPROTECTEDSESSION)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyProtectedSession(
    IN_CONST_HANDLE                       hAdapter,
    IN_CONST_HANDLE                       hProtectedSession // in: Driver generated handle driver returned at DxgkDdiCreateProtectedSession.
    )
{
    DbgPrint("[WinMali] >> DestroyProtectedSession\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hProtectedSession);
    DbgPrint("[WinMali] << DestroyProtectedSession STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETSCHEDULINGLOGBUFFER)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetSchedulingLogBuffer(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETSCHEDULINGLOGBUFFER    pSetSchedulingLogBuffer
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetSchedulingLogBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Accept the per-node/engine scheduling log buffer. dxgkrnl allocates it and
       records CPU-side context-switch / profiling events there (this is exactly
       the buffer VidSchiProfilePerformanceTick writes to). We have no GPU-side
       markers to program (submission is escape-driven), so accepting is
       sufficient and lets dxgkrnl finish wiring its per-engine profiling state
       instead of leaving the per-engine log-buffer pointer NULL. */
    DbgPrint("[WinMali] SetSchedulingLogBuffer node=%u eng=%u entries=%u cpuVa=%p ACCEPT\n",
             pSetSchedulingLogBuffer->NodeOrdinal,
             pSetSchedulingLogBuffer->EngineOrdinal,
             pSetSchedulingLogBuffer->NumberOfEntries,
             (PVOID)pSetSchedulingLogBuffer->LogBufferCpuVa);
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_SETUPPRIORITYBANDS)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetupPriorityBands(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETUPPRIORITYBANDS    pSetupPriorityBands
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetupPriorityBands == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* We advertise priority scheduling in DRIVERCAPS.SchedulingCaps, so dxgkrnl
       configures its priority bands through us at init. We schedule uniformly
       (single CSG), so there is no hardware band state to program; accept the
       configuration so dxgkrnl's scheduler state is consistent with the caps we
       reported. */
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_NOTIFYFOCUSPRESENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_NotifyFocusPresent(
    IN_CONST_HANDLE                         hAdapter
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    /* Informational: the focus window presented. Nothing to do; acknowledge. */
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetContextSchedulingProperties(
    IN_CONST_HANDLE                                     hAdapter,
    IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES    pSetContextSchedulingProperties
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetContextSchedulingProperties == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Accept per-context scheduling properties (priority band / quantum /
       grace period). Our scheduler treats contexts uniformly; record nothing
       but succeed so dxgkrnl considers the context fully schedulable. */
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_SUSPENDCONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdStub_SuspendContext(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_SUSPENDCONTEXT    pSuspendContext
    )
{
    DbgPrint("[WinMali] >> SuspendContext\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSuspendContext);
    DbgPrint("[WinMali] << SuspendContext STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESUMECONTEXT)
NTSTATUS
APIENTRY
WinMaliKmdStub_ResumeContext(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_RESUMECONTEXT pResumeContext
    )
{
    DbgPrint("[WinMali] >> ResumeContext\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pResumeContext);
    DbgPrint("[WinMali] << ResumeContext STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETVIRTUALMACHINEDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVirtualMachineData(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALMACHINEDATA Args
    )
{
    DbgPrint("[WinMali] >> SetVirtualMachineData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(Args);
    DbgPrint("[WinMali] << SetVirtualMachineData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_BEGINEXCLUSIVEACCESS)
NTSTATUS
APIENTRY
WinMaliKmdStub_BeginExclusiveAccess(
    IN_CONST_HANDLE                  hAdapter,
    IN_PDXGKARG_BEGINEXCLUSIVEACCESS pBeginExclusiveAccess
    )
{
    DbgPrint("[WinMali] >> BeginExclusiveAccess\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pBeginExclusiveAccess);
    DbgPrint("[WinMali] << BeginExclusiveAccess STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_ENDEXCLUSIVEACCESS)
NTSTATUS
APIENTRY
WinMaliKmdStub_EndExclusiveAccess(
    IN_CONST_HANDLE                hAdapter,
    IN_PDXGKARG_ENDEXCLUSIVEACCESS pEndExclusiveAccess
    )
{
    DbgPrint("[WinMali] >> EndExclusiveAccess\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEndExclusiveAccess);
    DbgPrint("[WinMali] << EndExclusiveAccess STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT)
NTSTATUS
APIENTRY
WinMaliKmdStub_QueryDiagnosticTypesSupport(
    IN_CONST_PVOID                              MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT  pArgQueryDiagnosticTypesSupport
    )
{
    DbgPrint("[WinMali] >> QueryDiagnosticTypesSupport\n");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pArgQueryDiagnosticTypesSupport);
    DbgPrint("[WinMali] << QueryDiagnosticTypesSupport STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CONTROLDIAGNOSTICREPORTING)
NTSTATUS
APIENTRY
WinMaliKmdStub_ControlDiagnosticReporting(
    IN_CONST_PVOID                          MiniportDeviceContext,
    IN_PDXGKARG_CONTROLDIAGNOSTICREPORTING  pArgControlDiagnosticReporting
    )
{
    DbgPrint("[WinMali] >> ControlDiagnosticReporting\n");
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pArgControlDiagnosticReporting);
    DbgPrint("[WinMali] << ControlDiagnosticReporting STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESUMEHWENGINE)
NTSTATUS
APIENTRY
WinMaliKmdStub_ResumeHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESUMEHWENGINE   pResumeHwEngine
    )
{
    DbgPrint("[WinMali] >> ResumeHwEngine\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pResumeHwEngine);
    DbgPrint("[WinMali] << ResumeHwEngine STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SIGNALMONITOREDFENCE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SignalMonitoredFence(
    IN_CONST_HANDLE                     hContext,
    INOUT_PDXGKARG_SIGNALMONITOREDFENCE pSignalMonitoredFence
    )
{
    DbgPrint("[WinMali] >> SignalMonitoredFence\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSignalMonitoredFence);
    DbgPrint("[WinMali] << SignalMonitoredFence STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_PRESENTTOHWQUEUE)
NTSTATUS
APIENTRY
WinMaliKmdStub_PresentToHwQueue(
    IN_CONST_HANDLE         hHwQueue,
    INOUT_PDXGKARG_PRESENT  pPresent
    )
{
    DbgPrint("[WinMali] >> PresentToHwQueue\n");
    UNREFERENCED_PARAMETER(hHwQueue);
    UNREFERENCED_PARAMETER(pPresent);
    DbgPrint("[WinMali] << PresentToHwQueue STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_VALIDATESUBMITCOMMAND)
NTSTATUS
APIENTRY
WinMaliKmdStub_ValidateSubmitCommand(
    IN_CONST_HANDLE          hContext,
    INOUT_PDXGKARG_VALIDATESUBMITCOMMAND pArgs
    )
{
    DbgPrint("[WinMali] >> ValidateSubmitCommand\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << ValidateSubmitCommand STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTrackedWorkloadPowerLevel(
    IN_CONST_HANDLE                 hContext,
    INOUT_PDXGKARG_SETTRACKEDWORKLOADPOWERLEVEL   pTrackedWorkloadPowerLevel
    )
{
    DbgPrint("[WinMali] >> SetTrackedWorkloadPowerLevel\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pTrackedWorkloadPowerLevel);
    DbgPrint("[WinMali] << SetTrackedWorkloadPowerLevel STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SAVEMEMORYFORHOTUPDATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SaveMemoryForHotUpdate(
    IN_CONST_HANDLE                 hContext,
    IN_CONST_PDXGKARG_SAVEMEMORYFORHOTUPDATE pArgs
    )
{
    DbgPrint("[WinMali] >> SaveMemoryForHotUpdate\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SaveMemoryForHotUpdate STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESTOREMEMORYFORHOTUPDATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_RestoreMemoryForHotUpdate(
    IN_CONST_HANDLE                             hContext,
    IN_CONST_PDXGKARG_RESTOREMEMORYFORHOTUPDATE pArgs
    )
{
    DbgPrint("[WinMali] >> RestoreMemoryForHotUpdate\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << RestoreMemoryForHotUpdate STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_COLLECTDIAGNOSTICINFO)
NTSTATUS
APIENTRY
WinMaliKmdStub_CollectDiagnosticInfo(
    IN_CONST_PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO pCollectDiagnosticInfo
    )
{
    DbgPrint("[WinMali] >> CollectDiagnosticInfo\n");
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);
    UNREFERENCED_PARAMETER(pCollectDiagnosticInfo);
    DbgPrint("[WinMali] << CollectDiagnosticInfo STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CONTROLINTERRUPT3)
NTSTATUS
APIENTRY
WinMaliKmdStub_ControlInterrupt3(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_PDXGKARG_CONTROLINTERRUPT3  InterruptControl
    )
{
    DbgPrint("[WinMali] >> ControlInterrupt3\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptControl);
    DbgPrint("[WinMali] << ControlInterrupt3 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETALLOCATIONBACKINGSTORE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetAllocationBackingStore(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SETALLOCATIONBACKINGSTORE pArgs
    )
{
    DbgPrint("[WinMali] >> SetAllocationBackingStore\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SetAllocationBackingStore STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATECPUEVENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateCpuEvent(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CREATECPUEVENT pArgs
    )
{
    DbgPrint("[WinMali] >> CreateCpuEvent\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << CreateCpuEvent STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYCPUEVENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyCpuEvent(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdCpuEvent
    )
{
    DbgPrint("[WinMali] >> DestroyCpuEvent\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hKmdCpuEvent);
    DbgPrint("[WinMali] << DestroyCpuEvent STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATENATIVEFENCE)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateNativeFence(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_CREATENATIVEFENCE    pCreateNativeFence
    )
{
    DbgPrint("[WinMali] >> CreateNativeFence\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateNativeFence);
    DbgPrint("[WinMali] << CreateNativeFence STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYNATIVEFENCE)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyNativeFence(
    INOUT_PDXGKARG_DESTROYNATIVEFENCE pDestroyNativeFence
    )
{
    DbgPrint("[WinMali] >> DestroyNativeFence\n");
    UNREFERENCED_PARAMETER(pDestroyNativeFence);
    DbgPrint("[WinMali] << DestroyNativeFence STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_UPDATEMONITOREDVALUES)
NTSTATUS
APIENTRY
WinMaliKmdStub_UpdateMonitoredValues(
    IN_CONST_PDXGKARG_UPDATEMONITOREDVALUES pUpdateMonitoredValues
    )
{
    DbgPrint("[WinMali] >> UpdateMonitoredValues\n");
    UNREFERENCED_PARAMETER(pUpdateMonitoredValues);
    DbgPrint("[WinMali] << UpdateMonitoredValues STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_UPDATECURRENTVALUESFROMCPU)
NTSTATUS
APIENTRY
WinMaliKmdStub_UpdateCurrentValuesFromCpu(
    IN_CONST_PDXGKARG_UPDATECURRENTVALUESFROMCPU pUpdateCurrentValuesFromCpu
    )
{
    DbgPrint("[WinMali] >> UpdateCurrentValuesFromCpu\n");
    UNREFERENCED_PARAMETER(pUpdateCurrentValuesFromCpu);
    DbgPrint("[WinMali] << UpdateCurrentValuesFromCpu STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATEDOORBELL)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreateDoorbell(
    INOUT_PDXGKARG_CREATEDOORBELL pArgs
    )
{
    DbgPrint("[WinMali] >> CreateDoorbell\n");
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << CreateDoorbell STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CONNECTDOORBELL)
NTSTATUS
APIENTRY
WinMaliKmdStub_ConnectDoorbell(
    INOUT_PDXGKARG_CONNECTDOORBELL pArgs
    )
{
    DbgPrint("[WinMali] >> ConnectDoorbell\n");
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << ConnectDoorbell STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DISCONNECTDOORBELL)
NTSTATUS
APIENTRY
WinMaliKmdStub_DisconnectDoorbell(
    INOUT_PDXGKARG_DISCONNECTDOORBELL pArgs
    )
{
    DbgPrint("[WinMali] >> DisconnectDoorbell\n");
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << DisconnectDoorbell STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYDOORBELL)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyDoorbell(
    INOUT_PDXGKARG_DESTROYDOORBELL pArgs
    )
{
    DbgPrint("[WinMali] >> DestroyDoorbell\n");
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << DestroyDoorbell STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_NOTIFYWORKSUBMISSION)
NTSTATUS
APIENTRY
WinMaliKmdStub_NotifyWorkSubmission(
    INOUT_PDXGKARG_NOTIFYWORKSUBMISSION pArgs
    )
{
    DbgPrint("[WinMali] >> NotifyWorkSubmission\n");
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << NotifyWorkSubmission STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CREATEMEMORYBASIS)
HANDLE
APIENTRY
WinMaliKmdStub_CreateMemoryBasis(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_CREATEMEMORYBASIS pArgs
    )
{
    DbgPrint("[WinMali] >> CreateMemoryBasis\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << CreateMemoryBasis 0\n");
    return (HANDLE)0;
}

_Function_class_(DXGKDDI_DESTROYMEMORYBASIS)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyMemoryBasis(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis
    )
{
    DbgPrint("[WinMali] >> DestroyMemoryBasis\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hMemoryBasis);
    DbgPrint("[WinMali] << DestroyMemoryBasis STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_STARTDIRTYTRACKING)
NTSTATUS
APIENTRY
WinMaliKmdStub_StartDirtyTracking(
    IN_CONST_HANDLE  hAdapter,
    IN_CONST_HANDLE  hMemoryBasis
    )
{
    DbgPrint("[WinMali] >> StartDirtyTracking\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hMemoryBasis);
    DbgPrint("[WinMali] << StartDirtyTracking STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_STOPDIRTYTRACKING)
NTSTATUS
APIENTRY
WinMaliKmdStub_StopDirtyTracking(
    IN_CONST_HANDLE  hAdapter,
    IN_CONST_HANDLE  hMemoryBasis
    )
{
    DbgPrint("[WinMali] >> StopDirtyTracking\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hMemoryBasis);
    DbgPrint("[WinMali] << StopDirtyTracking STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_QUERYDIRTYBITDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_QueryDirtyBitData(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_QUERYDIRTYBITDATA    pArgs
    )
{
    DbgPrint("[WinMali] >> QueryDirtyBitData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << QueryDirtyBitData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_PREPARELIVEMIGRATION)
NTSTATUS
APIENTRY
WinMaliKmdStub_PrepareLiveMigration(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_PREPARE_LIVE_MIGRATION   pArgs
    )
{
    DbgPrint("[WinMali] >> PrepareLiveMigration\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << PrepareLiveMigration STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SAVEIMMUTABLEMIGRATIONDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_SaveImmutableMigrationData(
    IN_CONST_HANDLE                                     hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA   pArgs
    )
{
    DbgPrint("[WinMali] >> SaveImmutableMigrationData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SaveImmutableMigrationData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SAVEMUTABLEMIGRATIONDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_SaveMutableMigrationData(
    IN_CONST_HANDLE                                    hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA    pArgs
    )
{
    DbgPrint("[WinMali] >> SaveMutableMigrationData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SaveMutableMigrationData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_ENDLIVEMIGRATION)
NTSTATUS
APIENTRY
WinMaliKmdStub_EndLiveMigration(
    IN_CONST_HANDLE hAdapter,
    UINT            vfIndex
    )
{
    DbgPrint("[WinMali] >> EndLiveMigration\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(vfIndex);
    DbgPrint("[WinMali] << EndLiveMigration STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_RestoreImmutableMigrationData(
    IN_CONST_HANDLE                                         hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA pArgs
    )
{
    DbgPrint("[WinMali] >> RestoreImmutableMigrationData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << RestoreImmutableMigrationData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_RESTOREMUTABLEMIGRATIONDATA)
NTSTATUS
APIENTRY
WinMaliKmdStub_RestoreMutableMigrationData(
    IN_CONST_HANDLE                                         hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA   pArgs
    )
{
    DbgPrint("[WinMali] >> RestoreMutableMigrationData\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << RestoreMutableMigrationData STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_WRITEVIRTUALIZEDINTERRUPT)
NTSTATUS
APIENTRY
WinMaliKmdStub_WriteVirtualizedInterrupt(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX   pArgs
    )
{
    DbgPrint("[WinMali] >> WriteVirtualizedInterrupt\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << WriteVirtualizedInterrupt STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETVIRTUALGPURESOURCES2)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVirtualGpuResources2(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALGPURESOURCES2   pArgs
    )
{
    DbgPrint("[WinMali] >> SetVirtualGpuResources2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SetVirtualGpuResources2 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVirtualFunctionPauseState(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALFUNCTIONPAUSESTATE  pArgs
    )
{
    DbgPrint("[WinMali] >> SetVirtualFunctionPauseState\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    DbgPrint("[WinMali] << SetVirtualFunctionPauseState STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_OPENNATIVEFENCE)
NTSTATUS
APIENTRY
WinMaliKmdStub_OpenNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_OPENNATIVEFENCE    pOpenNativeFence
    )
{
    DbgPrint("[WinMali] >> OpenNativeFence\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pOpenNativeFence);
    DbgPrint("[WinMali] << OpenNativeFence STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_CLOSENATIVEFENCE)
NTSTATUS
APIENTRY
WinMaliKmdStub_CloseNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_CLOSENATIVEFENCE   pCloseNativeFence
    )
{
    DbgPrint("[WinMali] >> CloseNativeFence\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCloseNativeFence);
    DbgPrint("[WinMali] << CloseNativeFence STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETNATIVEFENCELOGBUFFER)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetNativeFenceLogBuffer(
    IN_CONST_PDXGKARG_SETNATIVEFENCELOGBUFFER pSetNativeFenceLogBuffer
    )
{
    DbgPrint("[WinMali] >> SetNativeFenceLogBuffer\n");
    UNREFERENCED_PARAMETER(pSetNativeFenceLogBuffer);
    DbgPrint("[WinMali] << SetNativeFenceLogBuffer STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_UPDATENATIVEFENCELOGS)
NTSTATUS
APIENTRY
WinMaliKmdStub_UpdateNativeFenceLogs(
    IN_CONST_PDXGKARG_UPDATENATIVEFENCELOGS pUpdateNativeFenceLog
    )
{
    DbgPrint("[WinMali] >> UpdateNativeFenceLogs\n");
    UNREFERENCED_PARAMETER(pUpdateNativeFenceLog);
    DbgPrint("[WinMali] << UpdateNativeFenceLogs STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_COLLECTDBGINFO2)
NTSTATUS
APIENTRY
WinMaliKmdStub_CollectDbgInfo2(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_COLLECTDBGINFO2  pCollectDbgInfo2
    )
{
    DbgPrint("[WinMali] >> CollectDbgInfo2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCollectDbgInfo2);
    DbgPrint("[WinMali] << CollectDbgInfo2 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE)
NTSTATUS
APIENTRY
WinMaliKmdStub_NotifyContextPriorityChange(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_NOTIFYCONTEXTPRIORITYCHANGE   pNotifyContextPriorityChange
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pNotifyContextPriorityChange == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Informational: a context's priority changed. We schedule uniformly, so
       there is nothing to reprogram; acknowledge so dxgkrnl proceeds. */
    return STATUS_SUCCESS;
}
