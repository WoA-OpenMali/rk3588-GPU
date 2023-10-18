#include "Vop2Kmd.hpp"

NTSTATUS
Vop2DdiSetPowerState(_In_  VOID*              pDeviceContext,
                     _In_  ULONG              HardwareUid,
                     _In_  DEVICE_POWER_STATE DevicePowerState,
                     _In_  POWER_ACTION       ActionType)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiQueryChildRelations(_In_                             VOID*                  pDeviceContext,
                           _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                           _In_                             ULONG                  ChildRelationsSize)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiQueryChildStatus(_In_    VOID*              pDeviceContext,
                        _Inout_ DXGK_CHILD_STATUS* pChildStatus,
                        _In_    BOOLEAN            NonDestructiveOnly)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiQueryDeviceDescriptor(_In_  VOID*                     pDeviceContext,
                             _In_  ULONG                     ChildUid,
                             _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    return STATUS_UNSUCCESSFUL;
}

BOOLEAN
Vop2DdiInterruptRoutine(_In_  VOID* pDeviceContext,
                        _In_  ULONG MessageNumber)
{
    return FALSE;
}

VOID
Vop2DdiDpcRoutine(_In_  VOID* pDeviceContext)
{

}


NTSTATUS
APIENTRY
Vop2DdiQueryAdapterInfo(_In_ CONST HANDLE                         hAdapter,
                        _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiSetPointerPosition(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiSetPointerShape(_In_ CONST HANDLE                         hAdapter,
                       _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiPresentDisplayOnly(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*   pPresentDisplayOnly)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiIsSupportedVidPn(_In_ CONST HANDLE                         hAdapter,
                        _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendFunctionalVidPn(_In_ CONST HANDLE                                   hAdapter,
                                _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendVidPnTopology(_In_ CONST HANDLE                                 hAdapter,
                              _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendMonitorModes(_In_ CONST HANDLE                                hAdapter,
                             _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiEnumVidPnCofuncModality(_In_ CONST HANDLE                                  hAdapter,
                               _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiSetVidPnSourceVisibility(_In_ CONST HANDLE                             hAdapter,
                                _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiCommitVidPn(_In_ CONST HANDLE                         hAdapter,
                   _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiUpdateActiveVidPnPresentPath(_In_ CONST HANDLE                                       hAdapter,
                                    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiQueryVidPnHWCapability(_In_ CONST HANDLE                         hAdapter,
                              _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiStopDeviceAndReleasePostDisplayOwnership(_In_  VOID*                          pDeviceContext,
                                                _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiSystemDisplayEnable(_In_  VOID* pDeviceContext,
                           _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                           _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                           _Out_ UINT* Width,
                           _Out_ UINT* Height,
                           _Out_ D3DDDIFORMAT* ColorFormat)
{
    return STATUS_UNSUCCESSFUL;
}

VOID
APIENTRY
Vop2DdiSystemDisplayWrite(_In_  VOID* pDeviceContext,
                          _In_  VOID* Source,
                          _In_  UINT  SourceWidth,
                          _In_  UINT  SourceHeight,
                          _In_  UINT  SourceStride,
                          _In_  UINT  PositionX,
                          _In_  UINT  PositionY)
{

}

/* PnP */
VOID
Vop2DdiUnload(VOID)
{

}

NTSTATUS
Vop2DdiAddDevice(_In_ DEVICE_OBJECT* pPhysicalDeviceObject,
                 _Outptr_ PVOID*  ppDeviceContext)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiRemoveDevice(_In_  VOID* pDeviceContext)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiStartDevice(_In_  VOID*              pDeviceContext,
                   _In_  DXGK_START_INFO*   pDxgkStartInfo,
                   _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                   _Out_ ULONG*             pNumberOfViews,
                   _Out_ ULONG*             pNumberOfChildren)
{
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Vop2DdiStopDevice(_In_  VOID* pDeviceContext)
{
    return STATUS_UNSUCCESSFUL;
}

VOID
Vop2DdiResetDevice(_In_  VOID* pDeviceContext)
{

}

NTSTATUS
Vop2DdiDispatchIoRequest(_In_  VOID*                 pDeviceContext,
                         _In_  ULONG                 VidPnSourceId,
                         _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    return STATUS_UNSUCCESSFUL;
}