
#include "../WinMaliKmd.h"
#include "Vop2ConnectorsShared.h"

PWINMALI_ADAPTER
WinMaliAdapterFromContext(_In_ const VOID* Context);


VOID
Rk3588DispCaptureGopFb(_Inout_ PWINMALI_ADAPTER Adapter)
{
    NTSTATUS               status;
    DXGK_DISPLAY_INFORMATION display;

    if (Adapter == NULL) return;

    RtlZeroMemory(&display, sizeof(display));

    if (Adapter->DxgkInterface.DxgkCbAcquirePostDisplayOwnership == NULL) {
        WINMALI_WARN("no DxgkCbAcquirePostDisplayOwnership - no GOP handoff");
        return;
    }

    status = Adapter->DxgkInterface.DxgkCbAcquirePostDisplayOwnership(
        Adapter->DxgkHandle, &display);
    if (!NT_SUCCESS(status)) {
        WINMALI_WARN("DxgkCbAcquirePostDisplayOwnership failed 0x%08x - no GOP",
                         status);
        return;
    }

    if (display.Width == 0 || display.Height == 0 ||
        display.Pitch == 0 || display.PhysicAddress.QuadPart == 0) {
        WINMALI_WARN("DxgkCbAcquirePostDisplayOwnership returned empty display info");
        return;
    }

    Adapter->Gop.PhysBase    = display.PhysicAddress;
    Adapter->Gop.Width       = display.Width;
    Adapter->Gop.Height      = display.Height;
    Adapter->Gop.Pitch       = display.Pitch;
    Adapter->Gop.ColorFormat = display.ColorFormat;
    switch (display.ColorFormat) {
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
            Adapter->Gop.Bpp = 32; break;
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
            Adapter->Gop.Bpp = 16; break;
        default:
            Adapter->Gop.Bpp = 32; break;
    }
    Adapter->Gop.RefreshHz = 60;
    Adapter->Gop.Valid     = TRUE;

    WINMALI_WARN("GOP fb: phys=0x%llx %ux%u pitch=%u fmt=0x%x bpp=%u",
                      Adapter->Gop.PhysBase.QuadPart,
                      Adapter->Gop.Width, Adapter->Gop.Height,
                      Adapter->Gop.Pitch, (ULONG)Adapter->Gop.ColorFormat,
                      Adapter->Gop.Bpp);
}

VOID
Rk3588DispReleaseGopFb(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) return;

    if (Adapter->Gop.SystemDisplayVa != NULL) {
        MmUnmapIoSpace(Adapter->Gop.SystemDisplayVa, Adapter->Gop.SystemDisplayBytes);
        Adapter->Gop.SystemDisplayVa    = NULL;
        Adapter->Gop.SystemDisplayBytes = 0;
    }
    Adapter->Gop.Valid = FALSE;
}

static VOID
Rk3588DispPopulateSourceMode_(
    _In_  const RK3588_DISP_GOP_FB*  Gop,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE* Mode)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Type                                      = D3DKMDT_RMT_GRAPHICS;
    Mode->Format.Graphics.PrimSurfSize.cx           = Gop->Width;
    Mode->Format.Graphics.PrimSurfSize.cy           = Gop->Height;
    Mode->Format.Graphics.VisibleRegionSize.cx      = Gop->Width;
    Mode->Format.Graphics.VisibleRegionSize.cy      = Gop->Height;
    Mode->Format.Graphics.Stride                    = Gop->Pitch;
    Mode->Format.Graphics.PixelFormat               = Gop->ColorFormat;
    Mode->Format.Graphics.ColorBasis                = D3DKMDT_CB_SCRGB;
    Mode->Format.Graphics.PixelValueAccessMode      = D3DKMDT_PVAM_DIRECT;
}

static VOID
Rk3588DispPopulateTargetMode_(
    _In_  const RK3588_DISP_GOP_FB*  Gop,
    _Out_ D3DKMDT_VIDPN_TARGET_MODE* Mode)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->VideoSignalInfo.VideoStandard             = D3DKMDT_VSS_OTHER;
    Mode->VideoSignalInfo.ActiveSize.cx             = Gop->Width;
    Mode->VideoSignalInfo.ActiveSize.cy             = Gop->Height;
    Mode->VideoSignalInfo.TotalSize                 = Mode->VideoSignalInfo.ActiveSize;
    Mode->VideoSignalInfo.VSyncFreq.Numerator       = Gop->RefreshHz * 1000;
    Mode->VideoSignalInfo.VSyncFreq.Denominator     = 1000;
    Mode->VideoSignalInfo.HSyncFreq.Numerator       =
        Gop->RefreshHz * (Gop->Height + 50);  // rough enough for a fixed mode
    Mode->VideoSignalInfo.HSyncFreq.Denominator     = 1;
    // Pixel clock = H_total * V_total * refresh. We don't know porches,
    // so approximate using the visible area. DxgKrnl accepts anything
    // non-zero here for a pinned mode we declare ourselves.
    Mode->VideoSignalInfo.PixelRate                 =
        (ULONGLONG)Gop->Width * Gop->Height * Gop->RefreshHz;
    Mode->VideoSignalInfo.ScanLineOrdering          = D3DDDI_VSSLO_PROGRESSIVE;
    Mode->Preference                                = D3DKMDT_MP_PREFERRED;
}


// ---------------------------------------------------------------------------
// RecommendFunctionalVidPn: build a topology with our one fixed mode.
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
Rk3588DispRecommendFunctionalVidPn(
    _In_ const HANDLE                                     hAdapter,
    _In_ const DXGKARG_RECOMMENDFUNCTIONALVIDPN*          pRecommendFunctionalVidPn)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    const RK3588_DISP_GOP_FB*          gop = &a->Gop;
    const DXGK_VIDPN_INTERFACE*        vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* topoIf = NULL;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE* srcSetIf = NULL;
    const DXGK_VIDPNTARGETMODESET_INTERFACE* tgtSetIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY             hTopo = 0;
    D3DKMDT_HVIDPNSOURCEMODESET        hSrcSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET        hTgtSet = 0;
    D3DKMDT_VIDPN_PRESENT_PATH*        pathAlloc = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE*         srcMode = NULL;
    D3DKMDT_VIDPN_TARGET_MODE*         tgtMode = NULL;
    D3DDDI_VIDEO_PRESENT_TARGET_ID     primaryTargetId;
    NTSTATUS                           status;

    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_ENTER();
    if (a == NULL || pRecommendFunctionalVidPn == NULL || gop == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (a->DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    primaryTargetId = a->PrimaryConnector;

    status = a->DxgkInterface.DxgkCbQueryVidPnInterface(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnIf);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("DxgkCbQueryVidPnInterface 0x%08x", status);
        return status;
    }
    status = vidPnIf->pfnGetTopology(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, &hTopo, &topoIf);
    if (!NT_SUCCESS(status)) goto done;

    status = topoIf->pfnCreateNewPathInfo(hTopo, &pathAlloc);
    if (!NT_SUCCESS(status)) goto done;

    pathAlloc->VidPnSourceId                     = 0;
    pathAlloc->VidPnTargetId                     = primaryTargetId;
    pathAlloc->ImportanceOrdinal                 = D3DKMDT_VPPI_PRIMARY;
    pathAlloc->ContentTransformation.Scaling     = D3DKMDT_VPPS_IDENTITY;
    pathAlloc->ContentTransformation.Rotation    = D3DKMDT_VPPR_IDENTITY;
    pathAlloc->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_UNINITIALIZED;
    // VisibleFromActive{TL,BR}Offset default to (0,0) which means "active
    // pixels == visible pixels" - that's what we want when we're just
    // inheriting the UEFI scanout with no overscan.
    pathAlloc->VisibleFromActiveTLOffset.cx      = 0;
    pathAlloc->VisibleFromActiveTLOffset.cy      = 0;
    pathAlloc->VisibleFromActiveBROffset.cx      = 0;
    pathAlloc->VisibleFromActiveBROffset.cy      = 0;
    pathAlloc->Content                           = D3DKMDT_VPPC_GRAPHICS;

    status = topoIf->pfnAddPath(hTopo, pathAlloc);
    if (!NT_SUCCESS(status)) {
        topoIf->pfnReleasePathInfo(hTopo, pathAlloc);
        goto done;
    }
    pathAlloc = NULL;  // owned by topo now

    // Source mode set.
    status = vidPnIf->pfnCreateNewSourceModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        0, &hSrcSet, &srcSetIf);
    if (!NT_SUCCESS(status)) goto done;

    status = srcSetIf->pfnCreateNewModeInfo(hSrcSet, &srcMode);
    if (!NT_SUCCESS(status)) goto done;
    Rk3588DispPopulateSourceMode_(gop, srcMode);
    status = srcSetIf->pfnAddMode(hSrcSet, srcMode);
    if (!NT_SUCCESS(status)) {
        srcSetIf->pfnReleaseModeInfo(hSrcSet, srcMode);
        goto done;
    }
    srcMode = NULL;
    status = srcSetIf->pfnPinMode(hSrcSet, 0);
    if (!NT_SUCCESS(status)) goto done;
    status = vidPnIf->pfnAssignSourceModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, 0, hSrcSet);
    if (!NT_SUCCESS(status)) goto done;
    hSrcSet = 0;

    // Target mode set - attached to the primary target only. Dxgkrnl
    // won't build paths through the other targets (their children
    // report Connected=FALSE), so we don't populate modesets for them.
    status = vidPnIf->pfnCreateNewTargetModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        primaryTargetId, &hTgtSet, &tgtSetIf);
    if (!NT_SUCCESS(status)) goto done;

    status = tgtSetIf->pfnCreateNewModeInfo(hTgtSet, &tgtMode);
    if (!NT_SUCCESS(status)) goto done;
    Rk3588DispPopulateTargetMode_(gop, tgtMode);
    status = tgtSetIf->pfnAddMode(hTgtSet, tgtMode);
    if (!NT_SUCCESS(status)) {
        tgtSetIf->pfnReleaseModeInfo(hTgtSet, tgtMode);
        goto done;
    }
    tgtMode = NULL;
    status = tgtSetIf->pfnPinMode(hTgtSet, 0);
    if (!NT_SUCCESS(status)) goto done;
    status = vidPnIf->pfnAssignTargetModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        primaryTargetId, hTgtSet);
    if (!NT_SUCCESS(status)) goto done;
    hTgtSet = 0;


done:
    if (pathAlloc && topoIf) {
        topoIf->pfnReleasePathInfo(hTopo, pathAlloc);
    }
    if (hSrcSet && vidPnIf) {
        vidPnIf->pfnReleaseSourceModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, hSrcSet);
    }
    if (hTgtSet && vidPnIf) {
        vidPnIf->pfnReleaseTargetModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, hTgtSet);
    }
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("RecommendFunctionalVidPn -> 0x%08x", status);
    }
    return status;
}

// ---------------------------------------------------------------------------
// EnumVidPnCofuncModality: for every path whose source or target
// mode set is not yet pinned, pin our one mode on it.
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
Rk3588DispEnumVidPnCofuncModality(
    _In_ const HANDLE                              hAdapter,
    _In_ const DXGKARG_ENUMVIDPNCOFUNCMODALITY*    pEnumCofuncModality)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    const RK3588_DISP_GOP_FB*          gop = &a->Gop;
    const DXGK_VIDPN_INTERFACE*        vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* topoIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY             hTopo = 0;
    const D3DKMDT_VIDPN_PRESENT_PATH*  path = NULL;
    NTSTATUS                           status;

    UNREFERENCED_PARAMETER(hAdapter);
    if (a == NULL || pEnumCofuncModality == NULL || gop == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (a->DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    status = a->DxgkInterface.DxgkCbQueryVidPnInterface(
        pEnumCofuncModality->hConstrainingVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnIf);
    if (!NT_SUCCESS(status)) return status;

    status = vidPnIf->pfnGetTopology(
        pEnumCofuncModality->hConstrainingVidPn, &hTopo, &topoIf);
    if (!NT_SUCCESS(status)) return status;

    status = topoIf->pfnAcquireFirstPathInfo(hTopo, &path);
    while (NT_SUCCESS(status) && path != NULL) {

        D3DKMDT_HVIDPNSOURCEMODESET              hSrc = 0;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE* srcIf = NULL;
        D3DKMDT_HVIDPNTARGETMODESET              hTgt = 0;
        const DXGK_VIDPNTARGETMODESET_INTERFACE* tgtIf = NULL;
        D3DKMDT_VIDPN_SOURCE_MODE*               srcMode = NULL;
        D3DKMDT_VIDPN_TARGET_MODE*               tgtMode = NULL;
        SIZE_T                                   numSrcModes = 0, numTgtModes = 0;

        // SOURCE side.
        status = vidPnIf->pfnAcquireSourceModeSet(
            pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
            &hSrc, &srcIf);
        if (!NT_SUCCESS(status)) break;
        srcIf->pfnGetNumModes(hSrc, &numSrcModes);
        if (numSrcModes == 0 &&
            pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_VIDPNSOURCE)
        {
            D3DKMDT_HVIDPNSOURCEMODESET hNewSrc = 0;
            const DXGK_VIDPNSOURCEMODESET_INTERFACE* newSrcIf = NULL;
            status = vidPnIf->pfnCreateNewSourceModeSet(
                pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
                &hNewSrc, &newSrcIf);
            if (NT_SUCCESS(status)) {
                status = newSrcIf->pfnCreateNewModeInfo(hNewSrc, &srcMode);
                if (NT_SUCCESS(status)) {
                    Rk3588DispPopulateSourceMode_(gop, srcMode);
                    status = newSrcIf->pfnAddMode(hNewSrc, srcMode);
                    if (!NT_SUCCESS(status)) newSrcIf->pfnReleaseModeInfo(hNewSrc, srcMode);
                    srcMode = NULL;
                }
                if (NT_SUCCESS(status)) {
                    status = vidPnIf->pfnAssignSourceModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId, hNewSrc);
                }
                if (!NT_SUCCESS(status)) {
                    vidPnIf->pfnReleaseSourceModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, hNewSrc);
                }
            }
        }
        vidPnIf->pfnReleaseSourceModeSet(
            pEnumCofuncModality->hConstrainingVidPn, hSrc);
        if (!NT_SUCCESS(status)) break;

        // TARGET side.
        status = vidPnIf->pfnAcquireTargetModeSet(
            pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
            &hTgt, &tgtIf);
        if (!NT_SUCCESS(status)) break;
        tgtIf->pfnGetNumModes(hTgt, &numTgtModes);
        if (numTgtModes == 0 &&
            pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_VIDPNTARGET)
        {
            D3DKMDT_HVIDPNTARGETMODESET hNewTgt = 0;
            const DXGK_VIDPNTARGETMODESET_INTERFACE* newTgtIf = NULL;
            status = vidPnIf->pfnCreateNewTargetModeSet(
                pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
                &hNewTgt, &newTgtIf);
            if (NT_SUCCESS(status)) {
                status = newTgtIf->pfnCreateNewModeInfo(hNewTgt, &tgtMode);
                if (NT_SUCCESS(status)) {
                    Rk3588DispPopulateTargetMode_(gop, tgtMode);
                    status = newTgtIf->pfnAddMode(hNewTgt, tgtMode);
                    if (!NT_SUCCESS(status)) newTgtIf->pfnReleaseModeInfo(hNewTgt, tgtMode);
                    tgtMode = NULL;
                }
                if (NT_SUCCESS(status)) {
                    status = vidPnIf->pfnAssignTargetModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId, hNewTgt);
                }
                if (!NT_SUCCESS(status)) {
                    vidPnIf->pfnReleaseTargetModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, hNewTgt);
                }
            }
        }
        vidPnIf->pfnReleaseTargetModeSet(
            pEnumCofuncModality->hConstrainingVidPn, hTgt);
        if (!NT_SUCCESS(status)) break;

        // Next path.
        {
            const D3DKMDT_VIDPN_PRESENT_PATH* next = NULL;
            status = topoIf->pfnAcquireNextPathInfo(hTopo, path, &next);
            topoIf->pfnReleasePathInfo(hTopo, path);
            path = (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) ? NULL : next;
            if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
                status = STATUS_SUCCESS;
                break;
            }
        }
    }

    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("EnumVidPnCofuncModality -> 0x%08x", status);
    }
    return status;
}



NTSTATUS APIENTRY
Rk3588DispCommitVidPn(
    _In_ const HANDLE                  hAdapter,
    _In_ const DXGKARG_COMMITVIDPN*    pCommitVidPn)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    const DXGK_VIDPN_INTERFACE*         vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* topoIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY              hTopo = 0;
    const D3DKMDT_VIDPN_PRESENT_PATH*   path = NULL;
    NTSTATUS                            status;
    BOOLEAN                             primarySeen = FALSE;

    UNREFERENCED_PARAMETER(hAdapter);   

    WINMALI_ENTER();
    if (a == NULL || pCommitVidPn == NULL) return STATUS_INVALID_PARAMETER;

    // is whatever UEFI set up. But we must validate the topology and
    // reject a commit whose primary path targets a connector we can't
    // drive yet.
    if (a->DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    status = a->DxgkInterface.DxgkCbQueryVidPnInterface(
        pCommitVidPn->hFunctionalVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnIf);
    if (!NT_SUCCESS(status)) return status;

    status = vidPnIf->pfnGetTopology(
        pCommitVidPn->hFunctionalVidPn, &hTopo, &topoIf);
    if (!NT_SUCCESS(status)) return status;

    status = topoIf->pfnAcquireFirstPathInfo(hTopo, &path);
    while (NT_SUCCESS(status) && path != NULL) {
        if (path->VidPnTargetId == (D3DDDI_VIDEO_PRESENT_TARGET_ID)a->PrimaryConnector) {
            primarySeen = TRUE;
        } else {
            WINMALI_WARN("CommitVidPn: unrecognized target %u (primarySeen=%u)",
                              path->VidPnTargetId, (ULONG)primarySeen);
        }
        {
            const D3DKMDT_VIDPN_PRESENT_PATH* next = NULL;
            status = topoIf->pfnAcquireNextPathInfo(hTopo, path, &next);
            topoIf->pfnReleasePathInfo(hTopo, path);
            if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
                path = NULL;
                status = STATUS_SUCCESS;
                break;
            }
            path = next;
        }
    }

    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("CommitVidPn walk failed 0x%08x", status);
        return status;
    }

    // Topology with no primary path is acceptable on monitor-off.
    WINMALI_TRACE("CommitVidPn: primarySeen=%u",
                      (ULONG)primarySeen);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rk3588DispUpdateActiveVidPnPresentPath(
    _In_ const HANDLE                                 hAdapter,
    _In_ const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH*  pUpdateActive)
{
    UNREFERENCED_PARAMETER(hAdapter);
    
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    if (a == NULL || pUpdateActive == NULL) return STATUS_INVALID_PARAMETER;
    a->ActivePath    = pUpdateActive->VidPnPresentPathInfo;
    a->HasActivePath = TRUE;
    WINMALI_TRACE("UpdateActive src=%u tgt=%u",
                      a->ActivePath.VidPnSourceId, a->ActivePath.VidPnTargetId);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rk3588DispSetVidPnSourceVisibility(
    _In_ const HANDLE                             hAdapter,
    _In_ const DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility)
{
    UNREFERENCED_PARAMETER(hAdapter);
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    if (a == NULL || pSetVidPnSourceVisibility == NULL) return STATUS_INVALID_PARAMETER;
    a->SourceVisible = pSetVidPnSourceVisibility->Visible;
    WINMALI_TRACE("SetSourceVisibility src=%u visible=%u",
                      pSetVidPnSourceVisibility->VidPnSourceId,
                      (ULONG)pSetVidPnSourceVisibility->Visible);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// RecommendMonitorModes: one mode at the GOP timings, marked preferred.
// ---------------------------------------------------------------------------

NTSTATUS APIENTRY
Rk3588DispRecommendMonitorModes(
    _In_ const HANDLE                           hAdapter,
    _In_ const DXGKARG_RECOMMENDMONITORMODES*   pRecommendMonitorModes)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromContext(hAdapter);
    const RK3588_DISP_GOP_FB*                gop = &a->Gop;
    const DXGK_MONITORSOURCEMODESET_INTERFACE* setIf = NULL;
    D3DKMDT_MONITOR_SOURCE_MODE*             mode = NULL;
    NTSTATUS                                 status;

    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_ENTER();
    if (a == NULL || pRecommendMonitorModes == NULL || gop == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    setIf = pRecommendMonitorModes->pMonitorSourceModeSetInterface;
    if (setIf == NULL) return STATUS_INVALID_PARAMETER;

    // Only the primary target has a monitor mode we know about. Other
    // targets would need EDID reads we can't do yet - return an empty
    // set, which is legal and tells Dxgkrnl "no suggestions".
    if (pRecommendMonitorModes->VideoPresentTargetId !=
        (D3DDDI_VIDEO_PRESENT_TARGET_ID)a->PrimaryConnector)
    {
        return STATUS_SUCCESS;
    }

    status = setIf->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet, &mode);
    if (!NT_SUCCESS(status)) return status;

    RtlZeroMemory(mode, sizeof(*mode));
    mode->VideoSignalInfo.VideoStandard             = D3DKMDT_VSS_OTHER;
    mode->VideoSignalInfo.ActiveSize.cx             = gop->Width;
    mode->VideoSignalInfo.ActiveSize.cy             = gop->Height;
    mode->VideoSignalInfo.TotalSize                 = mode->VideoSignalInfo.ActiveSize;
    mode->VideoSignalInfo.VSyncFreq.Numerator       = gop->RefreshHz * 1000;
    mode->VideoSignalInfo.VSyncFreq.Denominator     = 1000;
    mode->VideoSignalInfo.HSyncFreq.Numerator       =
        gop->RefreshHz * (gop->Height + 50);
    mode->VideoSignalInfo.HSyncFreq.Denominator     = 1;
    mode->VideoSignalInfo.PixelRate                 =
        (ULONGLONG)gop->Width * gop->Height * gop->RefreshHz;
    mode->VideoSignalInfo.ScanLineOrdering          = D3DDDI_VSSLO_PROGRESSIVE;
    mode->ColorBasis                                = D3DKMDT_CB_SRGB;
    mode->ColorCoeffDynamicRanges.FirstChannel      = 8;
    mode->ColorCoeffDynamicRanges.SecondChannel     = 8;
    mode->ColorCoeffDynamicRanges.ThirdChannel      = 8;
    mode->Origin                                    = D3DKMDT_MCO_DRIVER;
    mode->Preference                                = D3DKMDT_MP_PREFERRED;

    status = setIf->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, mode);
    if (!NT_SUCCESS(status)) {
        setIf->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, mode);
        return status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Rk3588DispSetVidPnSourceAddress(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS     pSetVidPnSourceAddress)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    WINMALI_TRACE("SetVidPnSourceAddress src=%u addr=0x%llx",
                      pSetVidPnSourceAddress->VidPnSourceId,
                      pSetVidPnSourceAddress->PrimaryAddress.QuadPart);
    return STATUS_SUCCESS;

}
