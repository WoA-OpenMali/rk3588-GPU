
#pragma once 

extern "C"
{
    #define __CPLUSPLUS

    // Standard C-runtime headers
    #include <stddef.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>

    #include <initguid.h>

    // NTOS headers
    #include <ntddk.h>

    #ifndef FAR
    #define FAR
    #endif

    // Windows headers
    #include <windef.h>
    #include <winerror.h>

    // Windows GDI headers
    #include <wingdi.h>

    // Windows DDI headers
    #include <winddi.h>
    #include <ntddvdeo.h>

    #include <d3dkmddi.h>
    #include <d3dkmthk.h>

    #include <ntstrsafe.h>
    #include <ntintsafe.h>

    #include <dispmprt.h>
};

/* If we have a video mode that doens't meet these requirements it violates the windows license (Seriously) */
#define WIN_MIN_WIDTH                    640
#define WIN_MIN_HEIGHT                   480
#define MIN_BITS_PER_PIXEL_ALLOWED     8
#define MIN_BYTES_PER_PIXEL_REPORTED   4

/* Vop2Kmd aware hardware info */
#define VOP2TAG 'POVT'

#define RK3588_MAX_VIEW 6

typedef struct _VOP2_ACTIVE_DISPLAY_MODE
{
    DXGK_DISPLAY_INFORMATION             DxgkDispInfo;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION  Rotation;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    UINT32 HwModeWidth;
    UINT32 HwModeHeight;
    ULONG_PTR FramebufferPtr;
} VOP2_ACTIVE_DISPLAY_MODE, *PVOP2_ACTIVE_DISPLAY_MODE;

#include "vop2.hpp"


extern "C"
DRIVER_INITIALIZE DriverEntry;

/* These types are dictated by Dismpmprt, DxgKrnl will destroy you if you change them. */
NTSTATUS
Vop2DdiSetPowerState(_In_  VOID*              pDeviceContext,
                     _In_  ULONG              HardwareUid,
                     _In_  DEVICE_POWER_STATE DevicePowerState,
                     _In_  POWER_ACTION       ActionType);

NTSTATUS
Vop2DdiQueryChildRelations(_In_                             VOID*                  pDeviceContext,
                           _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                           _In_                             ULONG                  ChildRelationsSize);

NTSTATUS
Vop2DdiQueryChildStatus(_In_    VOID*              pDeviceContext,
                        _Inout_ DXGK_CHILD_STATUS* pChildStatus,
                        _In_    BOOLEAN            NonDestructiveOnly);

NTSTATUS
Vop2DdiQueryDeviceDescriptor(_In_  VOID*                     pDeviceContext,
                             _In_  ULONG                     ChildUid,
                             _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

BOOLEAN
Vop2DdiInterruptRoutine(_In_  VOID* pDeviceContext,
                        _In_  ULONG MessageNumber);

VOID
Vop2DdiDpcRoutine(_In_  VOID* pDeviceContext);


NTSTATUS
APIENTRY
Vop2DdiQueryAdapterInfo(_In_ CONST HANDLE                         hAdapter,
                        _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo);

NTSTATUS
APIENTRY
Vop2DdiSetPointerPosition(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition);

NTSTATUS
APIENTRY
Vop2DdiSetPointerShape(_In_ CONST HANDLE                         hAdapter,
                       _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape);

NTSTATUS
APIENTRY
Vop2DdiPresentDisplayOnly(_In_ CONST HANDLE                         hAdapter,
                          _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*   pPresentDisplayOnly);

NTSTATUS
APIENTRY
Vop2DdiIsSupportedVidPn(_In_ CONST HANDLE                         hAdapter,
                        _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn);

NTSTATUS
APIENTRY
Vop2DdiRecommendFunctionalVidPn(_In_ CONST HANDLE                                   hAdapter,
                                _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn);

NTSTATUS
APIENTRY
Vop2DdiRecommendVidPnTopology(_In_ CONST HANDLE                                 hAdapter,
                              _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology);

NTSTATUS
APIENTRY
Vop2DdiRecommendMonitorModes(_In_ CONST HANDLE                                hAdapter,
                             _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes);

NTSTATUS
APIENTRY
Vop2DdiEnumVidPnCofuncModality(_In_ CONST HANDLE                                  hAdapter,
                               _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality);

NTSTATUS
APIENTRY
Vop2DdiSetVidPnSourceVisibility(_In_ CONST HANDLE                             hAdapter,
                                _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility);

NTSTATUS
APIENTRY
Vop2DdiCommitVidPn(_In_ CONST HANDLE                         hAdapter,
                   _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn);

NTSTATUS
APIENTRY
Vop2DdiUpdateActiveVidPnPresentPath(_In_ CONST HANDLE                                       hAdapter,
                                    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath);

NTSTATUS
APIENTRY
Vop2DdiQueryVidPnHWCapability(_In_ CONST HANDLE                         hAdapter,
                              _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps);

NTSTATUS
APIENTRY
Vop2DdiStopDeviceAndReleasePostDisplayOwnership(_In_  VOID*                          pDeviceContext,
                                                _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo);

NTSTATUS
APIENTRY
Vop2DdiSystemDisplayEnable(_In_  VOID* pDeviceContext,
                           _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                           _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                           _Out_ UINT* Width,
                           _Out_ UINT* Height,
                           _Out_ D3DDDIFORMAT* ColorFormat);

VOID
APIENTRY
Vop2DdiSystemDisplayWrite(_In_  VOID* pDeviceContext,
                          _In_  VOID* Source,
                          _In_  UINT  SourceWidth,
                          _In_  UINT  SourceHeight,
                          _In_  UINT  SourceStride,
                          _In_  UINT  PositionX,
                          _In_  UINT  PositionY);

/* PnP */
VOID
Vop2DdiUnload(VOID);

NTSTATUS
Vop2DdiAddDevice(_In_ DEVICE_OBJECT* pPhysicalDeviceObject,
                 _Outptr_ PVOID*  ppDeviceContext);

NTSTATUS
Vop2DdiRemoveDevice(_In_  VOID* pDeviceContext);

NTSTATUS
Vop2DdiStartDevice(_In_  VOID*              pDeviceContext,
                   _In_  DXGK_START_INFO*   pDxgkStartInfo,
                   _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                   _Out_ ULONG*             pNumberOfViews,
                   _Out_ ULONG*             pNumberOfChildren);

NTSTATUS
Vop2DdiStopDevice(_In_  VOID* pDeviceContext);

VOID
Vop2DdiResetDevice(_In_  VOID* pDeviceContext);


NTSTATUS
Vop2DdiDispatchIoRequest(_In_  VOID*                 pDeviceContext,
                         _In_  ULONG                 VidPnSourceId,
                         _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);

void Vop2DebugPrint(CONST char *format, ...);

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType = PagedPool);
_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType = PagedPool);
void  __cdecl operator delete(void* pObject);
void  __cdecl operator delete(void* pObject, size_t s);
void  __cdecl operator delete[](void* pObject);
#include "debug.h"
#include "vop2hardware/Vop2Hw.hpp"
