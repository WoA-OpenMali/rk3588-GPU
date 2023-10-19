#include "Vop2Kmd.hpp"

/*
 * due to the seperate of VOP2 vs the Mali GPU i don't think cursor acceleration is possible.
 * This is because when Window's cursor is based on releative monitor size and location.
 * Thankfully the rk3588 is fast enough this "limitation" won't matter at all.
 */
NTSTATUS
APIENTRY
Vop2DdiSetPointerPosition(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
Vop2DdiSetPointerShape(_In_ CONST HANDLE                         hAdapter,
                       _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
Vop2DdiDispatchIoRequest(_In_  VOID*                 pDeviceContext,
                         _In_  ULONG                 VidPnSourceId,
                         _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);
    return STATUS_NOT_IMPLEMENTED;
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
    DbgPrint("Vop2DdiSystemDisplayEnable: Entry\n");
    DbgBreakPoint();
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(ColorFormat);
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
    DbgPrint("Vop2DdiSystemDisplayWrite: Entry\n");
    DbgBreakPoint();
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(SourceWidth);
    UNREFERENCED_PARAMETER(SourceHeight);
    UNREFERENCED_PARAMETER(SourceStride);
    UNREFERENCED_PARAMETER(PositionX);
    UNREFERENCED_PARAMETER(PositionY);
}

/* Hardware BLT */
NTSTATUS
APIENTRY
Vop2DdiPresentDisplayOnly(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*   pPresentDisplayOnly)
{
    DbgPrint("Vop2DdiPresentDisplayOnly: Entry\n");
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPresentDisplayOnly);
    return STATUS_NOT_IMPLEMENTED;
}


NTSTATUS
Vop2DdiSetPowerState(_In_  VOID*              pDeviceContext,
                     _In_  ULONG              HardwareUid,
                     _In_  DEVICE_POWER_STATE DevicePowerState,
                     _In_  POWER_ACTION       ActionType)
{
    DbgPrint("Vop2DdiSetPowerState: Entry\n");
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(HardwareUid);
    UNREFERENCED_PARAMETER(DevicePowerState);
    UNREFERENCED_PARAMETER(ActionType);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
Vop2DdiQueryChildRelations(_In_                             VOID*                  pDeviceContext,
                           _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                           _In_                             ULONG                  ChildRelationsSize)
{
    DbgPrint("Vop2DdiQueryChildRelations: Entry\n");
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(pChildRelations);
    UNREFERENCED_PARAMETER(ChildRelationsSize);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
Vop2DdiQueryChildStatus(_In_    VOID*              pDeviceContext,
                        _Inout_ DXGK_CHILD_STATUS* pChildStatus,
                        _In_    BOOLEAN            NonDestructiveOnly)
{
    DbgPrint("Vop2DdiQueryChildStatus: Entry\n");
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(pChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
Vop2DdiQueryDeviceDescriptor(_In_  VOID*                     pDeviceContext,
                             _In_  ULONG                     ChildUid,
                             _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    DbgPrint("Vop2DdiQueryDeviceDescriptor: Entry\n");
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(pDeviceDescriptor);
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
Vop2DdiInterruptRoutine(_In_  VOID* pDeviceContext,
                        _In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID
Vop2DdiDpcRoutine(_In_  VOID* pDeviceContext)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
}

/* PnP */
VOID
Vop2DdiUnload(VOID)
{
    DbgPrint("Vop2DdiUnload: Entry\n");
}
