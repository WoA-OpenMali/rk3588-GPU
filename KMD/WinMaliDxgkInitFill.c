#include "WinMaliKmd.h"
#include "WinMaliDxgkInitFill.h"

#define WM_UNREF1(a) (void)(a)
#define WM_UNREF2(a, b) \
    WM_UNREF1(a);     \
    WM_UNREF1(b)
#define WM_UNREF3(a, b, c) \
    WM_UNREF2(a, b);       \
    WM_UNREF1(c)
#define WM_UNREF4(a, b, c, d) \
    WM_UNREF3(a, b, c);      \
    WM_UNREF1(d)
#define WM_UNREF5(a, b, c, d, e) \
    WM_UNREF4(a, b, c, d);       \
    WM_UNREF1(e)
#define WM_UNREF6(a, b, c, d, e, f) \
    WM_UNREF5(a, b, c, d, e);       \
    WM_UNREF1(f)
#define WM_UNREF7(a, b, c, d, e, f, g) \
    WM_UNREF6(a, b, c, d, e, f);       \
    WM_UNREF1(g)

// Trace every stub DDI so bring-up logs show which callback dxgkrnl hit first.
#define WM_STUB_ENTER() WINMALI_ENTER()

// ---------------------------------------------------------------------------
// Stubs — NTSTATUS / HANDLE / VOID signatures must match d3dkmddi.h exactly.
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY WinMaliDxgkStub_DispatchIoRequest(
    IN_CONST_PVOID              MiniportDeviceContext,
    IN_ULONG                    VidPnSourceId,
    IN_PVIDEO_REQUEST_PACKET    VideoRequestPacket)
{
    WM_STUB_ENTER();
    WM_UNREF3(MiniportDeviceContext, VidPnSourceId, VideoRequestPacket);
    return STATUS_NOT_SUPPORTED;
}

VOID WinMaliDxgkStub_ControlEtwLogging(IN_BOOLEAN Enable, IN_ULONG Flags, IN_UCHAR Level)
{
    WM_STUB_ENTER();
    WM_UNREF3(Enable, Flags, Level);
}

NTSTATUS APIENTRY WinMaliDxgkStub_CollectDbgInfo(
    IN_CONST_HANDLE                  hAdapter,
    IN_CONST_PDXGKARG_COLLECTDBGINFO pCollectDbgInfo)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCollectDbgInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_AcquireSwizzlingRange(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE    pAcquireSwizzlingRange)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pAcquireSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ReleaseSwizzlingRange(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_RELEASESWIZZLINGRANGE pReleaseSwizzlingRange)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pReleaseSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetPalette(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SETPALETTE    pSetPalette)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetPalette);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetPointerPosition(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERPOSITION    pSetPointerPosition)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetPointerPosition);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetPointerShape(
    IN_CONST_HANDLE                   hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERSHAPE pSetPointerShape)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_RecommendVidPnTopology(
    IN_CONST_HANDLE                                   hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDVIDPNTOPOLOGY_CONST    pRecommendVidPnTopology)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pRecommendVidPnTopology);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_GetScanLine(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_GETSCANLINE  pGetScanLine)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pGetScanLine);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_StopCapture(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_STOPCAPTURE   pStopCapture)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pStopCapture);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateOverlay(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_CREATEOVERLAY    pCreateOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCreateOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_Present(
    IN_CONST_HANDLE         hDevice,
    INOUT_PDXGKARG_PRESENT  pPresent)
{
    WM_STUB_ENTER();
    WM_UNREF2(hDevice, pPresent);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateOverlay(
    IN_CONST_HANDLE                 hOverlay,
    IN_CONST_PDXGKARG_UPDATEOVERLAY pUpdateOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF2(hOverlay, pUpdateOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_FlipOverlay(
    IN_CONST_HANDLE             hOverlay,
    IN_CONST_PDXGKARG_FLIPOVERLAY pFlipOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF2(hOverlay, pFlipOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyOverlay(IN_CONST_HANDLE hOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF1(hOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateContext(
    IN_CONST_HANDLE                 hDevice,
    INOUT_PDXGKARG_CREATECONTEXT    pCreateContext)
{
    WM_STUB_ENTER();
    WM_UNREF2(hDevice, pCreateContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyContext(IN_CONST_HANDLE hContext)
{
    WM_STUB_ENTER();
    WM_UNREF1(hContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_LinkDevice(
    IN_CONST_PDEVICE_OBJECT   PhysicalDeviceObject,
    IN_CONST_PVOID            MiniportDeviceContext,
    INOUT_PLINKED_DEVICE      LinkedDevice)
{
    WM_STUB_ENTER();
    // Called by dxgkrnl right after AddDevice. NOT_SUPPORTED tears down the
    // stack before StartDevice (seen on build 26200). Non-LDA drivers return
    // SUCCESS like the RK3588 v3dqpu sample; leave LINKED_DEVICE as-is.
    WM_UNREF3(PhysicalDeviceObject, MiniportDeviceContext, LinkedDevice);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetDisplayPrivateDriverFormat(
    IN_CONST_HANDLE                                   hAdapter,
    IN_CONST_PDXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT     pSetDisplayPrivateDriverFormat)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetDisplayPrivateDriverFormat);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryVidPnHWCapability(
    IN_CONST_HANDLE                             i_hAdapter,
    INOUT_PDXGKARG_QUERYVIDPNHWCAPABILITY       io_pVidPnHWCaps)
{
    WM_STUB_ENTER();
    WM_UNREF2(i_hAdapter, io_pVidPnHWCaps);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetPowerComponentFState(
    IN_CONST_HANDLE hAdapter,
    IN UINT         ComponentIndex,
    IN UINT         FState)
{
    WM_STUB_ENTER();
    WM_UNREF3(hAdapter, ComponentIndex, FState);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryDependentEngineGroup(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_DXGKARG_QUERYDEPENDENTENGINEGROUP pQueryDependentEngineGroup)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pQueryDependentEngineGroup);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryEngineStatus(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS    pQueryEngineStatus)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pQueryEngineStatus);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ResetEngine(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_RESETENGINE  pResetEngine)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pResetEngine);
    return STATUS_NOT_SUPPORTED;
}

//
// SystemDisplayEnable / Write are the bugcheck / post-display path. Dxgkrnl
// uses these to draw the bluescreen, the OS boot logo handoff, and the
// "press F8" recovery menu. A driver that returns STATUS_NOT_SUPPORTED here
// causes dxgkrnl to assume we cannot own the display, fall back to the
// MS Basic Display Adapter, and unload us right after StartDevice — which
// is exactly the failure mode we're seeing. Wiring these to the captured
// GOP framebuffer is enough to clear that hurdle even before any real
// VOP2 plane programming exists.
//
NTSTATUS WinMaliDxgkStub_SystemDisplayEnable(
    IN_CONST_PVOID                       MiniportDeviceContext,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID    TargetId,
    IN PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    OUT UINT*                            Width,
    OUT UINT*                            Height,
    OUT D3DDDIFORMAT*                    ColorFormat)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(MiniportDeviceContext);
    PHYSICAL_ADDRESS phys;
    SIZE_T           bytes;

    WM_STUB_ENTER();
    WM_UNREF1(Flags);

    if (a == NULL || Width == NULL || Height == NULL || ColorFormat == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!a->Gop.Valid) {
        WINMALI_WARN("SystemDisplayEnable: no GOP fb captured");
        return STATUS_NOT_SUPPORTED;
    }
    if (TargetId != (D3DDDI_VIDEO_PRESENT_TARGET_ID)a->PrimaryConnector
        && TargetId != D3DDDI_ID_UNINITIALIZED)
    {
        WINMALI_WARN("SystemDisplayEnable: target %u not primary (%u)",
                     TargetId, a->PrimaryConnector);
        return STATUS_NOT_SUPPORTED;
    }

    //
    // Map the GOP framebuffer (NonCached, write-through) so subsequent
    // SystemDisplayWrite calls can memcpy into it. Cached/WC are NOT safe
    // here: dxgkrnl can call this from a bugcheck context where the cache
    // hierarchy is no longer trustworthy. We unmap on RemoveDevice via
    // existing teardown if needed; for now we leak the mapping until then.
    //
    if (a->Gop.SystemDisplayVa == NULL) {
        bytes = (SIZE_T)a->Gop.Pitch * (SIZE_T)a->Gop.Height;
        phys  = a->Gop.PhysBase;
        a->Gop.SystemDisplayVa = MmMapIoSpaceEx(phys, bytes, PAGE_READWRITE | PAGE_NOCACHE);
        a->Gop.SystemDisplayBytes = bytes;
        if (a->Gop.SystemDisplayVa == NULL) {
            WINMALI_WARN("SystemDisplayEnable: MmMapIoSpaceEx failed for 0x%llx (%llu bytes)",
                         phys.QuadPart, (ULONGLONG)bytes);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    *Width       = a->Gop.Width;
    *Height      = a->Gop.Height;
    *ColorFormat = a->Gop.ColorFormat;

    WINMALI_TRACE("SystemDisplayEnable: target=%u %ux%u fmt=0x%x va=%p",
                  TargetId, *Width, *Height, (ULONG)*ColorFormat, a->Gop.SystemDisplayVa);
    return STATUS_SUCCESS;
}

VOID WinMaliDxgkStub_SystemDisplayWrite(
    IN_CONST_PVOID MiniportDeviceContext,
    IN PVOID       Source,
    IN UINT        SourceWidth,
    IN UINT        SourceHeight,
    IN UINT        SourceStride,
    IN UINT        PositionX,
    IN UINT        PositionY)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(MiniportDeviceContext);
    PUCHAR           dst;
    PUCHAR           src;
    UINT             bpp_bytes;
    UINT             y;
    UINT             rowBytes;

    WM_STUB_ENTER();

    if (a == NULL || Source == NULL || a->Gop.SystemDisplayVa == NULL || !a->Gop.Valid) {
        return;
    }
    bpp_bytes = (a->Gop.Bpp + 7u) / 8u;
    if (bpp_bytes == 0
        || PositionX >= a->Gop.Width
        || PositionY >= a->Gop.Height)
    {
        return;
    }
    if (PositionX + SourceWidth > a->Gop.Width) {
        SourceWidth = a->Gop.Width - PositionX;
    }
    if (PositionY + SourceHeight > a->Gop.Height) {
        SourceHeight = a->Gop.Height - PositionY;
    }

    rowBytes = SourceWidth * bpp_bytes;
    src = (PUCHAR)Source;
    dst = (PUCHAR)a->Gop.SystemDisplayVa
        + (SIZE_T)PositionY * a->Gop.Pitch
        + (SIZE_T)PositionX * bpp_bytes;

    for (y = 0; y < SourceHeight; ++y) {
        RtlCopyMemory(dst, src, rowBytes);
        dst += a->Gop.Pitch;
        src += SourceStride;
    }
}

NTSTATUS APIENTRY WinMaliDxgkStub_GetChildContainerId(
    IN_CONST_PVOID                  MiniportDeviceContext,
    IN ULONG                        ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId)
{
    WM_STUB_ENTER();
    WM_UNREF3(MiniportDeviceContext, ChildUid, ContainerId);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_PowerRuntimeControlRequest(
    IN_CONST_HANDLE DriverContext,
    IN LPCGUID      PowerControlCode,
    IN PVOID        InBuffer,
    IN SIZE_T       InBufferSize,
    OUT PVOID       OutBuffer,
    IN SIZE_T      OutBufferSize,
    OUT PSIZE_T     BytesReturned)
{
    WM_STUB_ENTER();
    WM_UNREF7(DriverContext, PowerControlCode, InBuffer, InBufferSize, OutBuffer, OutBufferSize, BytesReturned);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay(
    IN_CONST_HANDLE                                                hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY     pSetVidPnSourceAddressWithMultiPlaneOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetVidPnSourceAddressWithMultiPlaneOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_NotifySurpriseRemoval(
    IN_CONST_PVOID                  MiniportDeviceContext,
    IN DXGK_SURPRISE_REMOVAL_TYPE   RemovalType)
{
    WM_STUB_ENTER();
    WM_UNREF2(MiniportDeviceContext, RemovalType);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_GetNodeMetadata(
    IN_CONST_HANDLE                 hAdapter,
    UINT                            NodeOrdinalAndAdapterIndex,
    OUT_PDXGKARG_GETNODEMETADATA    pGetNodeMetadata)
{
    WM_STUB_ENTER();
    WM_UNREF3(hAdapter, NodeOrdinalAndAdapterIndex, pGetNodeMetadata);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetPowerPState(
    IN_CONST_HANDLE hAdapter,
    IN UINT         ComponentIndex,
    IN UINT         PState)
{
    WM_STUB_ENTER();
    WM_UNREF3(hAdapter, ComponentIndex, PState);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ControlInterrupt2(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2   InterruptControl)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, InterruptControl);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CheckMultiPlaneOverlaySupport(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT pCheckMultiPlaneOverlaySupport)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCheckMultiPlaneOverlaySupport);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CalibrateGpuClock(
    IN_CONST_HANDLE                 hAdapter,
    IN UINT32                       NodeOrdinal,
    IN UINT32                       EngineOrdinal,
    OUT_PDXGKARG_CALIBRATEGPUCLOCK  pClockCalibration)
{
    WM_STUB_ENTER();
    WM_UNREF4(hAdapter, NodeOrdinal, EngineOrdinal, pClockCalibration);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_FormatHistoryBuffer(
    IN_CONST_HANDLE                 hContext,
    IN DXGKARG_FORMATHISTORYBUFFER* pFormatData)
{
    WM_STUB_ENTER();
    WM_UNREF2(hContext, pFormatData);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_RenderGdi(
    IN_CONST_HANDLE          hContext,
    INOUT_PDXGKARG_RENDERGDI pRenderGdi)
{
    WM_STUB_ENTER();
    WM_UNREF2(hContext, pRenderGdi);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SubmitCommandVirtual(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL  pSubmitCommandVirtual)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSubmitCommandVirtual);
    return STATUS_NOT_SUPPORTED;
}

VOID APIENTRY WinMaliDxgkStub_SetRootPageTable(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_SETROOTPAGETABLE  pSetRootPageTable)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetRootPageTable);
}

SIZE_T APIENTRY WinMaliDxgkStub_GetRootPageTableSize(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_GETROOTPAGETABLESIZE  pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(hAdapter);

    //
    // GpuMmu-capable adapters: returning 0 left dxgmms2 without a non-zero
    // root page-table allocation size on some Win11 26100 paths (AV inside
    // VIDMM_GLOBAL::ReadPhysicalAdapterConfiguration). One host page is
    // enough for dxgk to own backing storage; SetRootPageTable remains a
    // no-op until we program real Mali page tables for process VA.
    //
    if (pArgs != NULL) {
        WINMALI_TRACE(
            "GetRootPageTableSize: physIdx=%u NumberOfPte=%u -> size=%u",
            pArgs->PhysicalAdapterIndex,
            pArgs->NumberOfPte,
            (ULONG)PAGE_SIZE);
    }
    return (SIZE_T)PAGE_SIZE;
}

NTSTATUS APIENTRY WinMaliDxgkStub_MapCpuHostAperture(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE    pMapCpuHostAperture)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pMapCpuHostAperture);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UnmapCpuHostAperture(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CheckMultiPlaneOverlaySupport2(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2 pCheckMultiPlaneOverlaySupport)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCheckMultiPlaneOverlaySupport);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateProcess(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CREATEPROCESS pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyProcess(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdProcess)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hKmdProcess);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay2(
    IN_CONST_HANDLE                                                  hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2    pSetVidPnSourceAddressWithMultiPlaneOverlay2)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetVidPnSourceAddressWithMultiPlaneOverlay2);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_PowerRuntimeSetDeviceHandle(
    IN_CONST_HANDLE DriverContext,
    IN HANDLE       PoDeviceHandle)
{
    WM_STUB_ENTER();
    WM_UNREF2(DriverContext, PoDeviceHandle);
    return STATUS_NOT_SUPPORTED;
}

VOID APIENTRY WinMaliDxgkStub_SetStablePowerState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE  pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVideoProtectedRegion(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CheckMultiPlaneOverlaySupport3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 pCheckMultiPlaneOverlaySupport)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCheckMultiPlaneOverlaySupport);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3
        pSetVidPnSourceAddressWithMultiPlaneOverlay)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetVidPnSourceAddressWithMultiPlaneOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_PostMultiPlaneOverlayPresent(
    IN_CONST_HANDLE                                  hAdapter,
    IN_CONST_PDXGKARG_POSTMULTIPLANEOVERLAYPRESENT   pPostPresent)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pPostPresent);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ValidateUpdateAllocationProperty(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_VALIDATEUPDATEALLOCPROPERTY pValidateUpdateAllocProperty)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pValidateUpdateAllocProperty);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ControlModeBehavior(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_CONTROLMODEBEHAVIOR       pControlModeBehavior)
{
    WM_STUB_ENTER();
    //
    // ControlModeBehavior is dxgkrnl asking us to opt in/out of various VidPN
    // behaviors (e.g. clone-mode handling, mode switch on session change). On
    // recent Win11 builds returning STATUS_NOT_SUPPORTED here is enough to
    // make dxgkrnl decide we're not a viable primary display and unload us
    // immediately after the post-StartDevice cap probe. Returning SUCCESS
    // with no out-data changes ("accept the default behaviors") is what
    // most modern WDDM samples do.
    //
    WM_UNREF2(hAdapter, pControlModeBehavior);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateMonitorLinkInfo(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_UPDATEMONITORLINKINFO    pUpdateMonitorLinkInfo)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pUpdateMonitorLinkInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateHwContext(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_CREATEHWCONTEXT  pCreateContext)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCreateContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyHwContext(IN_CONST_HANDLE hHwContext)
{
    WM_STUB_ENTER();
    WM_UNREF1(hHwContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateHwQueue(
    IN_CONST_HANDLE             hHwContext,
    INOUT_PDXGKARG_CREATEHWQUEUE pCreateHwQueue)
{
    WM_STUB_ENTER();
    WM_UNREF2(hHwContext, pCreateHwQueue);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyHwQueue(IN_CONST_HANDLE hHwQueue)
{
    WM_STUB_ENTER();
    WM_UNREF1(hHwQueue);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SubmitCommandToHwQueue(
    IN_CONST_HANDLE                             hHwQueue,
    IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE    pSubmitCommandToHwQueue)
{
    WM_STUB_ENTER();
    WM_UNREF2(hHwQueue, pSubmitCommandToHwQueue);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SwitchToHwContextList(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SWITCHTOHWCONTEXTLIST     pHwContextList)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pHwContextList);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ResetHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESETHWENGINE    pResetHwEngine)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pResetHwEngine);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreatePeriodicFrameNotification(
    INOUT_PDXGKARG_CREATEPERIODICFRAMENOTIFICATION pCreatePeriodicFrameNotification)
{
    WM_STUB_ENTER();
    WM_UNREF1(pCreatePeriodicFrameNotification);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyPeriodicFrameNotification(
    IN_CONST_PDXGKARG_DESTROYPERIODICFRAMENOTIFICATION pDestroyPeriodicFrameNotification)
{
    WM_STUB_ENTER();
    WM_UNREF1(pDestroyPeriodicFrameNotification);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTimingsFromVidPn(
    IN_CONST_HANDLE                     hAdapter,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN pSetTimingsFromVidPn)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetTimingsFromVidPn);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTargetGamma(
    IN_CONST_HANDLE                  hAdapter,
    IN_CONST_PDXGKARG_SETTARGETGAMMA pSetTargetGamma)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetTargetGamma);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTargetContentType(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETTARGETCONTENTTYPE  pSetTargetContentType)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetTargetContentType);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTargetAnalogCopyProtection(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETTARGETANALOGCOPYPROTECTION pSetTargetAnalogCopyProtection)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetTargetAnalogCopyProtection);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTargetAdjustedColorimetry(
    IN_CONST_HANDLE                    hAdapter,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId,
    IN DXGK_COLORIMETRY                AdjustedColorimetry)
{
    WM_STUB_ENTER();
    WM_UNREF3(hAdapter, TargetId, AdjustedColorimetry);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DisplayDetectControl(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_DISPLAYDETECTCONTROL  pDisplayDetectControl)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pDisplayDetectControl);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryConnectionChange(
    IN_CONST_HANDLE                        hAdapter,
    IN_PDXGKARG_QUERYCONNECTIONCHANGE       pQueryConnectionChange)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pQueryConnectionChange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ExchangePreStartInfo(
    IN_CONST_HANDLE             hAdapter,
    IN_OUT_PDXGK_PRE_START_INFO pPreStartInfo)
{
    WM_STUB_ENTER();
    // OS calls before StartDevice; NOT_SUPPORTED fails bring-up (build 26200).
    WM_UNREF2(hAdapter, pPreStartInfo);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY WinMaliDxgkStub_GetMultiPlaneOverlayCaps(
    IN_CONST_HANDLE                         hAdapter,
    IN_OUT_PDXGKARG_GETMULTIPLANEOVERLAYCAPS pGetMultiPlaneOverlayCaps)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pGetMultiPlaneOverlayCaps);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_GetPostCompositionCaps(
    IN_CONST_HANDLE                         hAdapter,
    IN_OUT_PDXGKARG_GETPOSTCOMPOSITIONCAPS  pGetPostCompositionCaps)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pGetPostCompositionCaps);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateHwContextState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_UPDATEHWCONTEXTSTATE pUpdateHwContextState)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pUpdateHwContextState);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateProtectedSession(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_CREATEPROTECTEDSESSION   pCreateProtectedSession)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCreateProtectedSession);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyProtectedSession(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hProtectedSession)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hProtectedSession);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetSchedulingLogBuffer(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETSCHEDULINGLOGBUFFER    pSetSchedulingLogBuffer)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetSchedulingLogBuffer);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetupPriorityBands(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETUPPRIORITYBANDS    pSetupPriorityBands)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetupPriorityBands);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_NotifyFocusPresent(IN_CONST_HANDLE hAdapter)
{
    WM_STUB_ENTER();
    WM_UNREF1(hAdapter);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetContextSchedulingProperties(
    IN_CONST_HANDLE                                     hAdapter,
    IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES    pSetContextSchedulingProperties)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetContextSchedulingProperties);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SuspendContext(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SUSPENDCONTEXT pSuspendContext)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSuspendContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ResumeContext(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_RESUMECONTEXT  pResumeContext)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pResumeContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVirtualMachineData(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALMACHINEDATA Args)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, Args);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_BeginExclusiveAccess(
    IN_CONST_HANDLE                  hAdapter,
    IN_PDXGKARG_BEGINEXCLUSIVEACCESS pBeginExclusiveAccess)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pBeginExclusiveAccess);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_EndExclusiveAccess(
    IN_CONST_HANDLE                hAdapter,
    IN_PDXGKARG_ENDEXCLUSIVEACCESS pEndExclusiveAccess)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pEndExclusiveAccess);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryDiagnosticTypesSupport(
    IN_CONST_PVOID                              MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT    pArgQueryDiagnosticTypesSupport)
{
    WM_STUB_ENTER();
    WM_UNREF2(MiniportDeviceContext, pArgQueryDiagnosticTypesSupport);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ControlDiagnosticReporting(
    IN_CONST_PVOID                          MiniportDeviceContext,
    IN_PDXGKARG_CONTROLDIAGNOSTICREPORTING  pArgControlDiagnosticReporting)
{
    WM_STUB_ENTER();
    WM_UNREF2(MiniportDeviceContext, pArgControlDiagnosticReporting);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ResumeHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESUMEHWENGINE   pResumeHwEngine)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pResumeHwEngine);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SignalMonitoredFence(
    IN_CONST_HANDLE                     hContext,
    INOUT_PDXGKARG_SIGNALMONITOREDFENCE pSignalMonitoredFence)
{
    WM_STUB_ENTER();
    WM_UNREF2(hContext, pSignalMonitoredFence);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_PresentToHwQueue(
    IN_CONST_HANDLE         hHwQueue,
    INOUT_PDXGKARG_PRESENT  pPresent)
{
    WM_STUB_ENTER();
    WM_UNREF2(hHwQueue, pPresent);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ValidateSubmitCommand(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_VALIDATESUBMITCOMMAND   pValidateSubmitCommand)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pValidateSubmitCommand);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTargetAdjustedColorimetry2(
    IN_CONST_HANDLE                                 hAdapter,
    IN_PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2       pArgSetTargetAdjustedColorimetry)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgSetTargetAdjustedColorimetry);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetTrackedWorkloadPowerLevel(
    IN_CONST_HANDLE                             hContext,
    INOUT_PDXGKARG_SETTRACKEDWORKLOADPOWERLEVEL pTrackedWorkloadPowerLevel)
{
    WM_STUB_ENTER();
    WM_UNREF2(hContext, pTrackedWorkloadPowerLevel);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SaveMemoryForHotUpdate(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SAVEMEMORYFORHOTUPDATE    pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_RestoreMemoryForHotUpdate(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_RESTOREMEMORYFORHOTUPDATE    pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CollectDiagnosticInfo(
    IN_CONST_PDEVICE_OBJECT                 PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO     pCollectDiagnosticInfo)
{
    WM_STUB_ENTER();
    WM_UNREF2(PhysicalDeviceObject, pCollectDiagnosticInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ControlInterrupt3(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_PDXGKARG_CONTROLINTERRUPT3  InterruptControl)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, InterruptControl);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetFlipQueueLogBuffer(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETFLIPQUEUELOGBUFFER     pSetFlipQueueLogBuffer)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetFlipQueueLogBuffer);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateFlipQueueLog(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_UPDATEFLIPQUEUELOG   pUpdateFlipQueueLog)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pUpdateFlipQueueLog);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CancelQueuedFlips(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_CANCELQUEUEDFLIPS    pCancelQueuedFlips)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCancelQueuedFlips);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetInterruptTargetPresentId(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETINTERRUPTTARGETPRESENTID   pSetInterruptTargetPresentId)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pSetInterruptTargetPresentId);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetAllocationBackingStore(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETALLOCATIONBACKINGSTORE pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateCpuEvent(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CREATECPUEVENT pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyCpuEvent(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdCpuEvent)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hKmdCpuEvent);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CancelFlips(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CANCELFLIPS  pCancelFlips)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCancelFlips);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateNativeFence(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_CREATENATIVEFENCE    pCreateNativeFence)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCreateNativeFence);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyNativeFence(INOUT_PDXGKARG_DESTROYNATIVEFENCE pDestroyNativeFence)
{
    WM_STUB_ENTER();
    WM_UNREF1(pDestroyNativeFence);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateMonitoredValues(
    IN_CONST_PDXGKARG_UPDATEMONITOREDVALUES pUpdateMonitoredValues)
{
    WM_STUB_ENTER();
    WM_UNREF1(pUpdateMonitoredValues);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateCurrentValuesFromCpu(
    IN_CONST_PDXGKARG_UPDATECURRENTVALUESFROMCPU pUpdateCurrentValuesFromCpu)
{
    WM_STUB_ENTER();
    WM_UNREF1(pUpdateCurrentValuesFromCpu);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CreateDoorbell(INOUT_PDXGKARG_CREATEDOORBELL pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ConnectDoorbell(INOUT_PDXGKARG_CONNECTDOORBELL pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DisconnectDoorbell(INOUT_PDXGKARG_DISCONNECTDOORBELL pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyDoorbell(INOUT_PDXGKARG_DESTROYDOORBELL pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_NotifyWorkSubmission(INOUT_PDXGKARG_NOTIFYWORKSUBMISSION pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF1(pArgs);
    return STATUS_NOT_SUPPORTED;
}

HANDLE APIENTRY WinMaliDxgkStub_CreateMemoryBasis(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_CREATEMEMORYBASIS pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return NULL;
}

NTSTATUS APIENTRY WinMaliDxgkStub_DestroyMemoryBasis(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hMemoryBasis);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_StartDirtyTracking(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hMemoryBasis);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_StopDirtyTracking(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, hMemoryBasis);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_QueryDirtyBitData(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_QUERYDIRTYBITDATA pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_PrepareLiveMigration(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_PREPARE_LIVE_MIGRATION   pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SaveImmutableMigrationData(
    IN_CONST_HANDLE                                     hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA   pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SaveMutableMigrationData(
    IN_CONST_HANDLE                                    hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA    pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_EndLiveMigration(
    IN_CONST_HANDLE hAdapter,
    IN UINT         vfIndex)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, vfIndex);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_RestoreImmutableMigrationData(
    IN_CONST_HANDLE                                         hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_RestoreMutableMigrationData(
    IN_CONST_HANDLE                                        hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA  pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_WriteVirtualizedInterrupt(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX   pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVirtualGpuResources2(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALGPURESOURCES2   pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetVirtualFunctionPauseState(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALFUNCTIONPAUSESTATE  pArgs)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_OpenNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_OPENNATIVEFENCE    pOpenNativeFence)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pOpenNativeFence);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CloseNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_CLOSENATIVEFENCE   pCloseNativeFence)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCloseNativeFence);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_SetNativeFenceLogBuffer(
    IN_CONST_PDXGKARG_SETNATIVEFENCELOGBUFFER pSetNativeFenceLogBuffer)
{
    WM_STUB_ENTER();
    WM_UNREF1(pSetNativeFenceLogBuffer);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_UpdateNativeFenceLogs(
    IN_CONST_PDXGKARG_UPDATENATIVEFENCELOGS pUpdateNativeFenceLog)
{
    WM_STUB_ENTER();
    WM_UNREF1(pUpdateNativeFenceLog);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_CollectDbgInfo2(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_COLLECTDBGINFO2  pCollectDbgInfo2)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pCollectDbgInfo2);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_NotifyContextPriorityChange(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_NOTIFYCONTEXTPRIORITYCHANGE   pNotifyContextPriorityChange)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pNotifyContextPriorityChange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY WinMaliDxgkStub_ResetDisplayEngine(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_RESETDISPLAYENGINE   pResetDisplayEngine)
{
    WM_STUB_ENTER();
    WM_UNREF2(hAdapter, pResetDisplayEngine);
    return STATUS_NOT_SUPPORTED;
}

// ---------------------------------------------------------------------------
// Patch DRIVER_INITIALIZATION_DATA (WDDM 3.2 / dispmprt.h layout)
// ---------------------------------------------------------------------------

VOID
WinMaliDxgkPatchInitializationData(_Inout_ PDRIVER_INITIALIZATION_DATA p)
{
    if (p == NULL) {
        return;
    }

    p->Version = DXGKDDI_INTERFACE_VERSION;

    p->DxgkDdiAddDevice = WinMaliKmdAddDevice;
    p->DxgkDdiStartDevice = WinMaliKmdStartDevice;
    p->DxgkDdiStopDevice = WinMaliKmdStopDevice;
    p->DxgkDdiRemoveDevice = WinMaliKmdRemoveDevice;
    p->DxgkDdiDispatchIoRequest = WinMaliDxgkStub_DispatchIoRequest;
    p->DxgkDdiInterruptRoutine = WinMaliKmdInterruptRoutine;
    p->DxgkDdiDpcRoutine = WinMaliKmdDpcRoutine;
    p->DxgkDdiQueryChildRelations = WinMaliKmdQueryChildRelations;
    p->DxgkDdiQueryChildStatus = WinMaliKmdQueryChildStatus;
    p->DxgkDdiQueryDeviceDescriptor = WinMaliKmdQueryDeviceDescriptor;
    p->DxgkDdiSetPowerState = WinMaliKmdSetPowerState;
    p->DxgkDdiNotifyAcpiEvent = WinMaliKmdNotifyAcpiEvent;
    p->DxgkDdiResetDevice = WinMaliKmdResetDevice;
    p->DxgkDdiUnload = WinMaliKmdDdiUnload;
    p->DxgkDdiQueryInterface = WinMaliKmdQueryInterface;
    p->DxgkDdiControlEtwLogging = WinMaliDxgkStub_ControlEtwLogging;

    p->DxgkDdiQueryAdapterInfo = WinMaliKmdQueryAdapterInfo;
    p->DxgkDdiCreateDevice = WinMaliKmdCreateDevice;
    p->DxgkDdiCreateAllocation = WinMaliKmdCreateAllocation;
    p->DxgkDdiDestroyAllocation = WinMaliKmdDestroyAllocation;
    p->DxgkDdiDescribeAllocation = WinMaliKmdDescribeAllocation;
    p->DxgkDdiGetStandardAllocationDriverData = WinMaliKmdGetStandardAllocationDriverData;
    p->DxgkDdiAcquireSwizzlingRange = WinMaliDxgkStub_AcquireSwizzlingRange;
    p->DxgkDdiReleaseSwizzlingRange = WinMaliDxgkStub_ReleaseSwizzlingRange;
    p->DxgkDdiPatch = WinMaliKmdPatch;
    p->DxgkDdiSubmitCommand = WinMaliKmdSubmitCommand;
    p->DxgkDdiPreemptCommand = WinMaliKmdPreemptCommand;
    p->DxgkDdiBuildPagingBuffer = WinMaliKmdBuildPagingBuffer;
    p->DxgkDdiSetPalette = WinMaliDxgkStub_SetPalette;
    p->DxgkDdiSetPointerPosition = WinMaliDxgkStub_SetPointerPosition;
    p->DxgkDdiSetPointerShape = WinMaliDxgkStub_SetPointerShape;
    p->DxgkDdiResetFromTimeout = WinMaliKmdResetFromTimeout;
    p->DxgkDdiRestartFromTimeout = WinMaliKmdRestartFromTimeout;
    p->DxgkDdiEscape = WinMaliKmdEscape;
    p->DxgkDdiCollectDbgInfo = WinMaliDxgkStub_CollectDbgInfo;
    p->DxgkDdiQueryCurrentFence = WinMaliKmdQueryCurrentFence;
    p->DxgkDdiIsSupportedVidPn = WinMaliKmdIsSupportedVidPn;
    p->DxgkDdiRecommendFunctionalVidPn = WinMaliKmdRecommendFunctionalVidPn;
    p->DxgkDdiEnumVidPnCofuncModality = WinMaliKmdEnumVidPnCofuncModality;
    p->DxgkDdiSetVidPnSourceAddress = Rk3588DispSetVidPnSourceAddress;
    p->DxgkDdiSetVidPnSourceVisibility = WinMaliKmdSetVidPnSourceVisibility;
    p->DxgkDdiCommitVidPn = WinMaliKmdCommitVidPn;
    p->DxgkDdiUpdateActiveVidPnPresentPath = WinMaliKmdUpdateActiveVidPnPresentPath;
    p->DxgkDdiRecommendMonitorModes = WinMaliKmdRecommendMonitorModes;
    p->DxgkDdiRecommendVidPnTopology = WinMaliDxgkStub_RecommendVidPnTopology;
    p->DxgkDdiGetScanLine = WinMaliDxgkStub_GetScanLine;
    p->DxgkDdiStopCapture = WinMaliDxgkStub_StopCapture;
    p->DxgkDdiControlInterrupt = WinMaliKmdControlInterrupt;
    p->DxgkDdiCreateOverlay = WinMaliDxgkStub_CreateOverlay;

    p->DxgkDdiDestroyDevice = WinMaliKmdDestroyDevice;
    p->DxgkDdiOpenAllocation = WinMaliKmdOpenAllocation;
    p->DxgkDdiCloseAllocation = WinMaliKmdCloseAllocation;
    p->DxgkDdiRender = WinMaliKmdRender;
    p->DxgkDdiPresent = WinMaliDxgkStub_Present;

    p->DxgkDdiUpdateOverlay = WinMaliDxgkStub_UpdateOverlay;
    p->DxgkDdiFlipOverlay = WinMaliDxgkStub_FlipOverlay;
    p->DxgkDdiDestroyOverlay = WinMaliDxgkStub_DestroyOverlay;

    p->DxgkDdiCreateContext = WinMaliDxgkStub_CreateContext;
    p->DxgkDdiDestroyContext = WinMaliDxgkStub_DestroyContext;

    p->DxgkDdiLinkDevice = WinMaliDxgkStub_LinkDevice;
    p->DxgkDdiSetDisplayPrivateDriverFormat = WinMaliDxgkStub_SetDisplayPrivateDriverFormat;

    p->DxgkDdiDescribePageTable = NULL;
    p->DxgkDdiUpdatePageTable = NULL;
    p->DxgkDdiUpdatePageDirectory = NULL;
    p->DxgkDdiMovePageDirectory = NULL;
    p->DxgkDdiSubmitRender = NULL;
    p->DxgkDdiCreateAllocation2 = NULL;
    p->Reserved = NULL;
    p->DxgkDdiQueryVidPnHWCapability = WinMaliDxgkStub_QueryVidPnHWCapability;

    p->DxgkDdiRenderKm = WinMaliKmdRender;

    p->DxgkDdiSetPowerComponentFState = WinMaliDxgkStub_SetPowerComponentFState;
    p->DxgkDdiQueryDependentEngineGroup = WinMaliDxgkStub_QueryDependentEngineGroup;
    p->DxgkDdiQueryEngineStatus = WinMaliDxgkStub_QueryEngineStatus;
    p->DxgkDdiResetEngine = WinMaliDxgkStub_ResetEngine;
    p->DxgkDdiStopDeviceAndReleasePostDisplayOwnership =
        WinMaliKmdStopDeviceAndReleasePostDisplayOwnership;
    p->DxgkDdiSystemDisplayEnable = WinMaliDxgkStub_SystemDisplayEnable;
    p->DxgkDdiSystemDisplayWrite = WinMaliDxgkStub_SystemDisplayWrite;
    p->DxgkDdiCancelCommand = WinMaliKmdCancelCommand;
    p->DxgkDdiGetChildContainerId = WinMaliDxgkStub_GetChildContainerId;
    p->DxgkDdiPowerRuntimeControlRequest = WinMaliDxgkStub_PowerRuntimeControlRequest;
    p->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay =
        WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay;
    p->DxgkDdiNotifySurpriseRemoval = WinMaliDxgkStub_NotifySurpriseRemoval;

    p->DxgkDdiGetNodeMetadata = WinMaliKmdGetNodeMetadata;
    p->DxgkDdiSetPowerPState = WinMaliDxgkStub_SetPowerPState;
    p->DxgkDdiControlInterrupt2 = WinMaliDxgkStub_ControlInterrupt2;
    p->DxgkDdiCheckMultiPlaneOverlaySupport = WinMaliDxgkStub_CheckMultiPlaneOverlaySupport;
    p->DxgkDdiCalibrateGpuClock = WinMaliDxgkStub_CalibrateGpuClock;
    p->DxgkDdiFormatHistoryBuffer = WinMaliDxgkStub_FormatHistoryBuffer;

    p->DxgkDdiRenderGdi = WinMaliDxgkStub_RenderGdi;
    p->DxgkDdiSubmitCommandVirtual = WinMaliDxgkStub_SubmitCommandVirtual;
    p->DxgkDdiSetRootPageTable = WinMaliDxgkStub_SetRootPageTable;
    p->DxgkDdiGetRootPageTableSize = WinMaliDxgkStub_GetRootPageTableSize;
    p->DxgkDdiMapCpuHostAperture = WinMaliDxgkStub_MapCpuHostAperture;
    p->DxgkDdiUnmapCpuHostAperture = WinMaliDxgkStub_UnmapCpuHostAperture;
    p->DxgkDdiCheckMultiPlaneOverlaySupport2 = WinMaliDxgkStub_CheckMultiPlaneOverlaySupport2;
    p->DxgkDdiCreateProcess = WinMaliDxgkStub_CreateProcess;
    p->DxgkDdiDestroyProcess = WinMaliDxgkStub_DestroyProcess;
    p->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2 =
        WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay2;
    p->Reserved1 = NULL;
    p->Reserved2 = NULL;
    p->DxgkDdiPowerRuntimeSetDeviceHandle = WinMaliDxgkStub_PowerRuntimeSetDeviceHandle;
    p->DxgkDdiSetStablePowerState = WinMaliDxgkStub_SetStablePowerState;
    p->DxgkDdiSetVideoProtectedRegion = WinMaliDxgkStub_SetVideoProtectedRegion;

    p->DxgkDdiCheckMultiPlaneOverlaySupport3 = WinMaliDxgkStub_CheckMultiPlaneOverlaySupport3;
    p->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 =
        WinMaliDxgkStub_SetVidPnSourceAddressWithMultiPlaneOverlay3;
    p->DxgkDdiPostMultiPlaneOverlayPresent = WinMaliDxgkStub_PostMultiPlaneOverlayPresent;
    p->DxgkDdiValidateUpdateAllocationProperty = WinMaliDxgkStub_ValidateUpdateAllocationProperty;
    p->DxgkDdiControlModeBehavior = WinMaliDxgkStub_ControlModeBehavior;
    p->DxgkDdiUpdateMonitorLinkInfo = WinMaliDxgkStub_UpdateMonitorLinkInfo;

    p->DxgkDdiCreateHwContext = WinMaliDxgkStub_CreateHwContext;
    p->DxgkDdiDestroyHwContext = WinMaliDxgkStub_DestroyHwContext;
    p->DxgkDdiCreateHwQueue = WinMaliDxgkStub_CreateHwQueue;
    p->DxgkDdiDestroyHwQueue = WinMaliDxgkStub_DestroyHwQueue;
    p->DxgkDdiSubmitCommandToHwQueue = WinMaliDxgkStub_SubmitCommandToHwQueue;
    p->DxgkDdiSwitchToHwContextList = WinMaliDxgkStub_SwitchToHwContextList;
    p->DxgkDdiResetHwEngine = WinMaliDxgkStub_ResetHwEngine;
    p->DxgkDdiCreatePeriodicFrameNotification = WinMaliDxgkStub_CreatePeriodicFrameNotification;
    p->DxgkDdiDestroyPeriodicFrameNotification = WinMaliDxgkStub_DestroyPeriodicFrameNotification;
    p->DxgkDdiSetTimingsFromVidPn = WinMaliDxgkStub_SetTimingsFromVidPn;
    p->DxgkDdiSetTargetGamma = WinMaliDxgkStub_SetTargetGamma;
    p->DxgkDdiSetTargetContentType = WinMaliDxgkStub_SetTargetContentType;
    p->DxgkDdiSetTargetAnalogCopyProtection = WinMaliDxgkStub_SetTargetAnalogCopyProtection;
    p->DxgkDdiSetTargetAdjustedColorimetry = WinMaliDxgkStub_SetTargetAdjustedColorimetry;
    p->DxgkDdiDisplayDetectControl = WinMaliDxgkStub_DisplayDetectControl;
    p->DxgkDdiQueryConnectionChange = WinMaliDxgkStub_QueryConnectionChange;
    p->DxgkDdiExchangePreStartInfo = WinMaliDxgkStub_ExchangePreStartInfo;
    p->DxgkDdiGetMultiPlaneOverlayCaps = WinMaliDxgkStub_GetMultiPlaneOverlayCaps;
    p->DxgkDdiGetPostCompositionCaps = WinMaliDxgkStub_GetPostCompositionCaps;

    p->DxgkDdiUpdateHwContextState = WinMaliDxgkStub_UpdateHwContextState;
    p->DxgkDdiCreateProtectedSession = WinMaliDxgkStub_CreateProtectedSession;
    p->DxgkDdiDestroyProtectedSession = WinMaliDxgkStub_DestroyProtectedSession;

    p->DxgkDdiSetSchedulingLogBuffer = WinMaliDxgkStub_SetSchedulingLogBuffer;
    p->DxgkDdiSetupPriorityBands = WinMaliDxgkStub_SetupPriorityBands;
    p->DxgkDdiNotifyFocusPresent = WinMaliDxgkStub_NotifyFocusPresent;
    p->DxgkDdiSetContextSchedulingProperties = WinMaliDxgkStub_SetContextSchedulingProperties;
    p->DxgkDdiSuspendContext = WinMaliDxgkStub_SuspendContext;
    p->DxgkDdiResumeContext = WinMaliDxgkStub_ResumeContext;
    p->DxgkDdiSetVirtualMachineData = WinMaliDxgkStub_SetVirtualMachineData;
    p->DxgkDdiBeginExclusiveAccess = WinMaliDxgkStub_BeginExclusiveAccess;
    p->DxgkDdiEndExclusiveAccess = WinMaliDxgkStub_EndExclusiveAccess;
    p->DxgkDdiQueryDiagnosticTypesSupport = WinMaliDxgkStub_QueryDiagnosticTypesSupport;
    p->DxgkDdiControlDiagnosticReporting = WinMaliDxgkStub_ControlDiagnosticReporting;
    p->DxgkDdiResumeHwEngine = WinMaliDxgkStub_ResumeHwEngine;

    p->DxgkDdiSignalMonitoredFence = WinMaliDxgkStub_SignalMonitoredFence;
    p->DxgkDdiPresentToHwQueue = WinMaliDxgkStub_PresentToHwQueue;
    p->DxgkDdiValidateSubmitCommand = WinMaliDxgkStub_ValidateSubmitCommand;
    p->DxgkDdiSetTargetAdjustedColorimetry2 = WinMaliDxgkStub_SetTargetAdjustedColorimetry2;
    p->DxgkDdiSetTrackedWorkloadPowerLevel = WinMaliDxgkStub_SetTrackedWorkloadPowerLevel;

    p->DxgkDdiSaveMemoryForHotUpdate = WinMaliDxgkStub_SaveMemoryForHotUpdate;
    p->DxgkDdiRestoreMemoryForHotUpdate = WinMaliDxgkStub_RestoreMemoryForHotUpdate;
    p->DxgkDdiCollectDiagnosticInfo = WinMaliDxgkStub_CollectDiagnosticInfo;
    p->Reserved3 = NULL;

    p->DxgkDdiControlInterrupt3 = WinMaliDxgkStub_ControlInterrupt3;

    p->DxgkDdiSetFlipQueueLogBuffer = WinMaliDxgkStub_SetFlipQueueLogBuffer;
    p->DxgkDdiUpdateFlipQueueLog = WinMaliDxgkStub_UpdateFlipQueueLog;
    p->DxgkDdiCancelQueuedFlips = WinMaliDxgkStub_CancelQueuedFlips;
    p->DxgkDdiSetInterruptTargetPresentId = WinMaliDxgkStub_SetInterruptTargetPresentId;

    p->DxgkDdiSetAllocationBackingStore = WinMaliDxgkStub_SetAllocationBackingStore;
    p->DxgkDdiCreateCpuEvent = WinMaliDxgkStub_CreateCpuEvent;
    p->DxgkDdiDestroyCpuEvent = WinMaliDxgkStub_DestroyCpuEvent;
    p->DxgkDdiCancelFlips = WinMaliDxgkStub_CancelFlips;

    p->DxgkDdiCreateNativeFence = WinMaliDxgkStub_CreateNativeFence;
    p->DxgkDdiDestroyNativeFence = WinMaliDxgkStub_DestroyNativeFence;
    p->DxgkDdiUpdateMonitoredValues = WinMaliDxgkStub_UpdateMonitoredValues;
    p->DxgkDdiUpdateCurrentValuesFromCpu = WinMaliDxgkStub_UpdateCurrentValuesFromCpu;
    p->DxgkDdiCreateDoorbell = WinMaliDxgkStub_CreateDoorbell;
    p->DxgkDdiConnectDoorbell = WinMaliDxgkStub_ConnectDoorbell;
    p->DxgkDdiDisconnectDoorbell = WinMaliDxgkStub_DisconnectDoorbell;
    p->DxgkDdiDestroyDoorbell = WinMaliDxgkStub_DestroyDoorbell;
    p->DxgkDdiNotifyWorkSubmission = WinMaliDxgkStub_NotifyWorkSubmission;
    p->Reserved4 = NULL;

    p->DxgkDdiCreateMemoryBasis = WinMaliDxgkStub_CreateMemoryBasis;
    p->DxgkDdiDestroyMemoryBasis = WinMaliDxgkStub_DestroyMemoryBasis;
    p->DxgkDdiStartDirtyTracking = WinMaliDxgkStub_StartDirtyTracking;
    p->DxgkDdiStopDirtyTracking = WinMaliDxgkStub_StopDirtyTracking;
    p->DxgkDdiQueryDirtyBitData = WinMaliDxgkStub_QueryDirtyBitData;
    p->DxgkDdiPrepareLiveMigration = WinMaliDxgkStub_PrepareLiveMigration;
    p->DxgkDdiSaveImmutableMigrationData = WinMaliDxgkStub_SaveImmutableMigrationData;
    p->DxgkDdiSaveMutableMigrationData = WinMaliDxgkStub_SaveMutableMigrationData;
    p->DxgkDdiEndLiveMigration = WinMaliDxgkStub_EndLiveMigration;
    p->DxgkDdiRestoreImmutableMigrationData = WinMaliDxgkStub_RestoreImmutableMigrationData;
    p->DxgkDdiRestoreMutableMigrationData = WinMaliDxgkStub_RestoreMutableMigrationData;
    p->DxgkDdiWriteVirtualizedInterrupt = WinMaliDxgkStub_WriteVirtualizedInterrupt;
    p->DxgkDdiSetVirtualGpuResources2 = WinMaliDxgkStub_SetVirtualGpuResources2;
    p->DxgkDdiSetVirtualFunctionPauseState = WinMaliDxgkStub_SetVirtualFunctionPauseState;
    p->DxgkDdiOpenNativeFence = WinMaliDxgkStub_OpenNativeFence;
    p->DxgkDdiCloseNativeFence = WinMaliDxgkStub_CloseNativeFence;
    p->DxgkDdiSetNativeFenceLogBuffer = WinMaliDxgkStub_SetNativeFenceLogBuffer;
    p->DxgkDdiUpdateNativeFenceLogs = WinMaliDxgkStub_UpdateNativeFenceLogs;
    p->DxgkDdiCollectDbgInfo2 = WinMaliDxgkStub_CollectDbgInfo2;
    p->DxgkDdiNotifyContextPriorityChange = WinMaliDxgkStub_NotifyContextPriorityChange;
    p->DxgkDdiResetDisplayEngine = WinMaliDxgkStub_ResetDisplayEngine;
}
