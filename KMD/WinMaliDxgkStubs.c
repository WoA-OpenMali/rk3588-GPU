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
    DbgPrint("[WinMali] >> Patch\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    DbgPrint("[WinMali] << Patch STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
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
    DbgPrint("[WinMali] >> QueryDependentEngineGroup\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pQueryDependentEngineGroup);
    DbgPrint("[WinMali] << QueryDependentEngineGroup STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
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

_Function_class_(DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVidPnSourceAddressWithMultiPlaneOverlay(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY
                  pSetVidPnSourceAddressWithMultiPlaneOverlay
    )
{
    DbgPrint("[WinMali] >> SetVidPnSourceAddressWithMultiPlaneOverlay\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddressWithMultiPlaneOverlay);
    DbgPrint("[WinMali] << SetVidPnSourceAddressWithMultiPlaneOverlay STATUS_NOT_SUPPORTED\n");
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

_Function_class_(DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT)
NTSTATUS
APIENTRY
WinMaliKmdStub_CheckMultiPlaneOverlaySupport(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT
                  pCheckMultiPlaneOverlaySupport
    )
{
    DbgPrint("[WinMali] >> CheckMultiPlaneOverlaySupport\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport);
    DbgPrint("[WinMali] << CheckMultiPlaneOverlaySupport STATUS_NOT_SUPPORTED\n");
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
    DbgPrint("[WinMali] >> CalibrateGpuClock\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    UNREFERENCED_PARAMETER(pClockCalibration);
    DbgPrint("[WinMali] << CalibrateGpuClock STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_FORMATHISTORYBUFFER)
NTSTATUS
APIENTRY
WinMaliKmdStub_FormatHistoryBuffer(
    IN_CONST_HANDLE                 hContext,
    IN DXGKARG_FORMATHISTORYBUFFER* pFormatData
    )
{
    DbgPrint("[WinMali] >> FormatHistoryBuffer\n");
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pFormatData);
    DbgPrint("[WinMali] << FormatHistoryBuffer STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
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

_Function_class_(DXGKDDI_SUBMITCOMMANDVIRTUAL)
NTSTATUS
APIENTRY
WinMaliKmdStub_SubmitCommandVirtual(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL  pSubmitCommand
    )
{
    DbgPrint("[WinMali] >> SubmitCommandVirtual\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSubmitCommand);
    DbgPrint("[WinMali] << SubmitCommandVirtual STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

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

_Function_class_(DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2)
NTSTATUS
APIENTRY
WinMaliKmdStub_CheckMultiPlaneOverlaySupport2(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2
                  pCheckMultiPlaneOverlaySupport
    )
{
    DbgPrint("[WinMali] >> CheckMultiPlaneOverlaySupport2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport);
    DbgPrint("[WinMali] << CheckMultiPlaneOverlaySupport2 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVidPnSourceAddressWithMultiPlaneOverlay2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2
                  pSetVidPnSourceAddressWithMultiPlaneOverlay
    )
{
    DbgPrint("[WinMali] >> SetVidPnSourceAddressWithMultiPlaneOverlay2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddressWithMultiPlaneOverlay);
    DbgPrint("[WinMali] << SetVidPnSourceAddressWithMultiPlaneOverlay2 STATUS_NOT_SUPPORTED\n");
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

_Function_class_(DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3)
NTSTATUS
APIENTRY
WinMaliKmdStub_CheckMultiPlaneOverlaySupport3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3
                  pCheckMultiPlaneOverlaySupport
    )
{
    DbgPrint("[WinMali] >> CheckMultiPlaneOverlaySupport3\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport);
    DbgPrint("[WinMali] << CheckMultiPlaneOverlaySupport3 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetVidPnSourceAddressWithMultiPlaneOverlay3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3
                  pSetVidPnSourceAddressWithMultiPlaneOverlay
    )
{
    DbgPrint("[WinMali] >> SetVidPnSourceAddressWithMultiPlaneOverlay3\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddressWithMultiPlaneOverlay);
    DbgPrint("[WinMali] << SetVidPnSourceAddressWithMultiPlaneOverlay3 STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_POSTMULTIPLANEOVERLAYPRESENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_PostMultiPlaneOverlayPresent(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_POSTMULTIPLANEOVERLAYPRESENT
                  pPostPresent
    )
{
    DbgPrint("[WinMali] >> PostMultiPlaneOverlayPresent\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPostPresent);
    DbgPrint("[WinMali] << PostMultiPlaneOverlayPresent STATUS_NOT_SUPPORTED\n");
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

_Function_class_(DXGKDDI_CONTROLMODEBEHAVIOR)
NTSTATUS
APIENTRY
WinMaliKmdStub_ControlModeBehavior(
    IN_CONST_HANDLE                             hAdapter,
    INOUT_PDXGKARG_CONTROLMODEBEHAVIOR          pControlModeBehaviorArg
    )
{
    DbgPrint("[WinMali] >> ControlModeBehavior\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pControlModeBehaviorArg);
    DbgPrint("[WinMali] << ControlModeBehavior STATUS_NOT_SUPPORTED\n");
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

_Function_class_(DXGKDDI_CREATEPERIODICFRAMENOTIFICATION)
NTSTATUS
APIENTRY
WinMaliKmdStub_CreatePeriodicFrameNotification(
    INOUT_PDXGKARG_CREATEPERIODICFRAMENOTIFICATION pCreatePeriodicFrameNotification
    )
{
    DbgPrint("[WinMali] >> CreatePeriodicFrameNotification\n");
    UNREFERENCED_PARAMETER(pCreatePeriodicFrameNotification);
    DbgPrint("[WinMali] << CreatePeriodicFrameNotification STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_DESTROYPERIODICFRAMENOTIFICATION)
NTSTATUS
APIENTRY
WinMaliKmdStub_DestroyPeriodicFrameNotification(
    IN_CONST_PDXGKARG_DESTROYPERIODICFRAMENOTIFICATION pDestroyPeriodicFrameNotification
    )
{
    DbgPrint("[WinMali] >> DestroyPeriodicFrameNotification\n");
    UNREFERENCED_PARAMETER(pDestroyPeriodicFrameNotification);
    DbgPrint("[WinMali] << DestroyPeriodicFrameNotification STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETTIMINGSFROMVIDPN)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTimingsFromVidPn(
    IN_CONST_HANDLE                             hAdapter,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN         pSetTimings
    )
{
    DbgPrint("[WinMali] >> SetTimingsFromVidPn\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetTimings);
    DbgPrint("[WinMali] << SetTimingsFromVidPn STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETTARGETGAMMA)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTargetGamma(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETTARGETGAMMA            pSetTargetGammaArg
    )
{
    DbgPrint("[WinMali] >> SetTargetGamma\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetTargetGammaArg);
    DbgPrint("[WinMali] << SetTargetGamma STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETTARGETCONTENTTYPE)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTargetContentType(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETTARGETCONTENTTYPE      pSetTargetContentTypeArg
    )
{
    DbgPrint("[WinMali] >> SetTargetContentType\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetTargetContentTypeArg);
    DbgPrint("[WinMali] << SetTargetContentType STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETTARGETANALOGCOPYPROTECTION)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTargetAnalogCopyProtection(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETTARGETANALOGCOPYPROTECTION pSetTargetAnalogCopyProtectionArg
    )
{
    DbgPrint("[WinMali] >> SetTargetAnalogCopyProtection\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetTargetAnalogCopyProtectionArg);
    DbgPrint("[WinMali] << SetTargetAnalogCopyProtection STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_GETMULTIPLANEOVERLAYCAPS)
NTSTATUS
APIENTRY
WinMaliKmdStub_GetMultiPlaneOverlayCaps(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_GETMULTIPLANEOVERLAYCAPS
                  pGetMultiPlaneOverlayCaps
    )
{
    DbgPrint("[WinMali] >> GetMultiPlaneOverlayCaps\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetMultiPlaneOverlayCaps);
    DbgPrint("[WinMali] << GetMultiPlaneOverlayCaps STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_GETPOSTCOMPOSITIONCAPS)
NTSTATUS
APIENTRY
WinMaliKmdStub_GetPostCompositionCaps(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_GETPOSTCOMPOSITIONCAPS
                  pGetPostCompositionCaps
    )
{
    DbgPrint("[WinMali] >> GetPostCompositionCaps\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetPostCompositionCaps);
    DbgPrint("[WinMali] << GetPostCompositionCaps STATUS_NOT_SUPPORTED\n");
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
    DbgPrint("[WinMali] >> SetSchedulingLogBuffer\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetSchedulingLogBuffer);
    DbgPrint("[WinMali] << SetSchedulingLogBuffer STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETUPPRIORITYBANDS)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetupPriorityBands(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETUPPRIORITYBANDS    pSetupPriorityBands
    )
{
    DbgPrint("[WinMali] >> SetupPriorityBands\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetupPriorityBands);
    DbgPrint("[WinMali] << SetupPriorityBands STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_NOTIFYFOCUSPRESENT)
NTSTATUS
APIENTRY
WinMaliKmdStub_NotifyFocusPresent(
    IN_CONST_HANDLE                         hAdapter
    )
{
    DbgPrint("[WinMali] >> NotifyFocusPresent\n");
    UNREFERENCED_PARAMETER(hAdapter);
    DbgPrint("[WinMali] << NotifyFocusPresent STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
}

_Function_class_(DXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetContextSchedulingProperties(
    IN_CONST_HANDLE                                     hAdapter,
    IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES    pSetContextSchedulingProperties
    )
{
    DbgPrint("[WinMali] >> SetContextSchedulingProperties\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetContextSchedulingProperties);
    DbgPrint("[WinMali] << SetContextSchedulingProperties STATUS_NOT_SUPPORTED\n");
    return STATUS_NOT_SUPPORTED;
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

_Function_class_(DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2)
NTSTATUS
APIENTRY
WinMaliKmdStub_SetTargetAdjustedColorimetry2(
    IN_CONST_HANDLE                                 hAdapter,
    IN_PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2       pArgSetTargetAdjustedColorimetry
    )
{
    DbgPrint("[WinMali] >> SetTargetAdjustedColorimetry2\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgSetTargetAdjustedColorimetry);
    DbgPrint("[WinMali] << SetTargetAdjustedColorimetry2 STATUS_NOT_SUPPORTED\n");
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
