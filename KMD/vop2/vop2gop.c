
#include "../WinMaliKmd.h"
#include "Vop2ConnectorsShared.h"

#define WINMALI_FALLBACK_WIDTH 1920u
#define WINMALI_FALLBACK_HEIGHT 1080u
#define WINMALI_FALLBACK_BPP 32u
#define WINMALI_FALLBACK_PITCH (WINMALI_FALLBACK_WIDTH * (WINMALI_FALLBACK_BPP / 8u))

static UINT
Rk3588DispModeWidth_(
    _In_opt_ const RK3588_DISP_GOP_FB* Gop)
{
    return (Gop != NULL && Gop->Width != 0) ? Gop->Width : WINMALI_FALLBACK_WIDTH;
}

static UINT
Rk3588DispModeHeight_(
    _In_opt_ const RK3588_DISP_GOP_FB* Gop)
{
    return (Gop != NULL && Gop->Height != 0) ? Gop->Height : WINMALI_FALLBACK_HEIGHT;
}

static D3DDDIFORMAT
Rk3588DispModeFormat_(
    _In_opt_ const RK3588_DISP_GOP_FB* Gop)
{
    if (Gop != NULL) {
        switch (Gop->ColorFormat) {
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
            return Gop->ColorFormat;
        default:
            break;
        }
    }

    return D3DDDIFMT_X8R8G8B8;
}

static UINT
Rk3588DispModePitch_(
    _In_opt_ const RK3588_DISP_GOP_FB* Gop)
{
    UINT width = Rk3588DispModeWidth_(Gop);
    UINT bytesPerPixel = WINMALI_FALLBACK_BPP / 8u;

    if (Gop != NULL && Gop->Bpp != 0) {
        bytesPerPixel = (Gop->Bpp + 7u) / 8u;
        if (bytesPerPixel == 0) {
            bytesPerPixel = WINMALI_FALLBACK_BPP / 8u;
        }
    }

    if (Gop != NULL && Gop->Pitch >= width * bytesPerPixel) {
        return Gop->Pitch;
    }

    return width * bytesPerPixel;
}

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
    UINT width = Rk3588DispModeWidth_(Gop);
    UINT height = Rk3588DispModeHeight_(Gop);

    Mode->Type                                      = D3DKMDT_RMT_GRAPHICS;
    Mode->Format.Graphics.PrimSurfSize.cx           = width;
    Mode->Format.Graphics.PrimSurfSize.cy           = height;
    Mode->Format.Graphics.VisibleRegionSize.cx      = width;
    Mode->Format.Graphics.VisibleRegionSize.cy      = height;
    Mode->Format.Graphics.Stride                    = width * 4u;
    Mode->Format.Graphics.PixelFormat               = D3DDDIFMT_X8R8G8B8;
    Mode->Format.Graphics.ColorBasis                = D3DKMDT_CB_SCRGB;
    Mode->Format.Graphics.PixelValueAccessMode      = D3DKMDT_PVAM_DIRECT;
}

static VOID
Rk3588DispPopulateTargetMode_(
    _In_  const RK3588_DISP_GOP_FB*          Gop,
    _In_opt_ const D3DKMDT_VIDPN_SOURCE_MODE* SourceMode,
    _Out_ D3DKMDT_VIDPN_TARGET_MODE*         Mode)
{
    UINT activeWidth = Rk3588DispModeWidth_(Gop);
    UINT activeHeight = Rk3588DispModeHeight_(Gop);

    Mode->VideoSignalInfo.VideoStandard             = D3DKMDT_VSS_OTHER;
    if (SourceMode != NULL &&
        SourceMode->Format.Graphics.PrimSurfSize.cx != 0 &&
        SourceMode->Format.Graphics.PrimSurfSize.cy != 0)
    {
        activeWidth = SourceMode->Format.Graphics.PrimSurfSize.cx;
        activeHeight = SourceMode->Format.Graphics.PrimSurfSize.cy;
    }
    Mode->VideoSignalInfo.ActiveSize.cx             = activeWidth;
    Mode->VideoSignalInfo.ActiveSize.cy             = activeHeight;
    Mode->VideoSignalInfo.TotalSize.cx              = 2200;
    Mode->VideoSignalInfo.TotalSize.cy              = 1125;
    Mode->VideoSignalInfo.VSyncFreq.Numerator       = 60;
    Mode->VideoSignalInfo.VSyncFreq.Denominator     = 1;
    Mode->VideoSignalInfo.HSyncFreq.Numerator       = 67500;
    Mode->VideoSignalInfo.HSyncFreq.Denominator     = 1;
    Mode->VideoSignalInfo.PixelRate                 = 148500000ULL;
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
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
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
    pathAlloc->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
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
    Rk3588DispPopulateTargetMode_(gop, NULL, tgtMode);
    WINMALI_TRACE("RecommendFunctionalVidPn: AddTargetMode-pre tgt=%u mode=%p active=%ux%u total=%ux%u v=%u/%u h=%u/%u pixel=%llu std=%u",
        primaryTargetId,
        tgtMode,
        tgtMode->VideoSignalInfo.ActiveSize.cx,
        tgtMode->VideoSignalInfo.ActiveSize.cy,
        tgtMode->VideoSignalInfo.TotalSize.cx,
        tgtMode->VideoSignalInfo.TotalSize.cy,
        tgtMode->VideoSignalInfo.VSyncFreq.Numerator,
        tgtMode->VideoSignalInfo.VSyncFreq.Denominator,
        tgtMode->VideoSignalInfo.HSyncFreq.Numerator,
        tgtMode->VideoSignalInfo.HSyncFreq.Denominator,
        tgtMode->VideoSignalInfo.PixelRate,
        tgtMode->VideoSignalInfo.VideoStandard);
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
    PWINMALI_ADAPTER                        a = WinMaliAdapterFromDxgkHandle(hAdapter);
    const RK3588_DISP_GOP_FB*              gop;
    const DXGK_VIDPN_INTERFACE*            vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE*    topoIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY                 hTopo = 0;
    const D3DKMDT_VIDPN_PRESENT_PATH*      path = NULL;
    const D3DKMDT_VIDPN_PRESENT_PATH*      prevPath = NULL;
    D3DKMDT_HVIDPNSOURCEMODESET            hSrc = 0;
    D3DKMDT_HVIDPNTARGETMODESET            hTgt = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE* srcIf = NULL;
    const DXGK_VIDPNTARGETMODESET_INTERFACE* tgtIf = NULL;
    const D3DKMDT_VIDPN_SOURCE_MODE*       pinnedSrc = NULL;
    const D3DKMDT_VIDPN_TARGET_MODE*       pinnedTgt = NULL;
    NTSTATUS                               status;

    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_ENTER();
    if (a == NULL || pEnumCofuncModality == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    gop = &a->Gop;
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
    if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
        return STATUS_SUCCESS;
    }

    while (NT_SUCCESS(status) && path != NULL) {
        D3DKMDT_VIDPN_SOURCE_MODE* srcMode = NULL;
        D3DKMDT_VIDPN_TARGET_MODE* tgtMode = NULL;
        D3DKMDT_VIDPN_PRESENT_PATH localPath;
        BOOLEAN releasePinnedSrc = FALSE;
        BOOLEAN supportFieldsModified = FALSE;

        status = vidPnIf->pfnAcquireSourceModeSet(
            pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
            &hSrc, &srcIf);
        if (!NT_SUCCESS(status)) {
            WINMALI_ERROR("EnumCofunc: AcquireSourceModeSet src=%u failed 0x%08x",
                path->VidPnSourceId, status);
            break;
        }

        status = srcIf->pfnAcquirePinnedModeInfo(hSrc, &pinnedSrc);
        if (!NT_SUCCESS(status)) {
            WINMALI_ERROR("EnumCofunc: AcquirePinnedSource src=%u failed 0x%08x",
                path->VidPnSourceId, status);
            break;
        }

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == path->VidPnSourceId)) &&
            pinnedSrc == NULL)
        {
            D3DKMDT_HVIDPNSOURCEMODESET hNewSrc = 0;
            const DXGK_VIDPNSOURCEMODESET_INTERFACE* newSrcIf = NULL;

            status = vidPnIf->pfnReleaseSourceModeSet(
                pEnumCofuncModality->hConstrainingVidPn, hSrc);
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: ReleaseSourceModeSet src=%u failed 0x%08x",
                    path->VidPnSourceId, status);
                break;
            }
            hSrc = 0;
            srcIf = NULL;

            status = vidPnIf->pfnCreateNewSourceModeSet(
                pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
                &hNewSrc, &newSrcIf);
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: CreateNewSourceModeSet src=%u failed 0x%08x",
                    path->VidPnSourceId, status);
            }
            if (NT_SUCCESS(status)) {
                status = newSrcIf->pfnCreateNewModeInfo(hNewSrc, &srcMode);
                if (!NT_SUCCESS(status)) {
                    WINMALI_ERROR("EnumCofunc: CreateNewSourceModeInfo src=%u failed 0x%08x",
                        path->VidPnSourceId, status);
                }
                if (NT_SUCCESS(status)) {
                    Rk3588DispPopulateSourceMode_(gop, srcMode);
                    status = newSrcIf->pfnAddMode(hNewSrc, srcMode);
                    if (!NT_SUCCESS(status)) {
                        WINMALI_ERROR("EnumCofunc: AddSourceMode src=%u failed 0x%08x (%ux%u stride=%u fmt=0x%x)",
                            path->VidPnSourceId,
                            status,
                            srcMode->Format.Graphics.PrimSurfSize.cx,
                            srcMode->Format.Graphics.PrimSurfSize.cy,
                            srcMode->Format.Graphics.Stride,
                            srcMode->Format.Graphics.PixelFormat);
                        newSrcIf->pfnReleaseModeInfo(hNewSrc, srcMode);
                    }
                    srcMode = NULL;
                }
                if (NT_SUCCESS(status)) {
                    status = vidPnIf->pfnAssignSourceModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId, hNewSrc);
                    if (!NT_SUCCESS(status)) {
                        WINMALI_ERROR("EnumCofunc: AssignSourceModeSet src=%u failed 0x%08x",
                            path->VidPnSourceId, status);
                    }
                }
                if (!NT_SUCCESS(status)) {
                    vidPnIf->pfnReleaseSourceModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, hNewSrc);
                }
            }
        } else if (pinnedSrc != NULL) {
            releasePinnedSrc = TRUE;
        }

        if (hSrc != 0) {
            if (!releasePinnedSrc) {
                status = vidPnIf->pfnReleaseSourceModeSet(
                    pEnumCofuncModality->hConstrainingVidPn, hSrc);
                hSrc = 0;
                srcIf = NULL;
                if (!NT_SUCCESS(status)) {
                    WINMALI_ERROR("EnumCofunc: ReleaseSourceModeSet src=%u failed 0x%08x",
                        path->VidPnSourceId, status);
                    break;
                }
            }
        }
        if (!NT_SUCCESS(status)) break;

        status = vidPnIf->pfnAcquireTargetModeSet(
            pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
            &hTgt, &tgtIf);
        if (!NT_SUCCESS(status)) {
            WINMALI_ERROR("EnumCofunc: AcquireTargetModeSet tgt=%u failed 0x%08x",
                path->VidPnTargetId, status);
            break;
        }

        status = tgtIf->pfnAcquirePinnedModeInfo(hTgt, &pinnedTgt);
        if (!NT_SUCCESS(status)) {
            WINMALI_ERROR("EnumCofunc: AcquirePinnedTarget tgt=%u failed 0x%08x",
                path->VidPnTargetId, status);
            break;
        }

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == path->VidPnTargetId)) &&
            pinnedTgt == NULL)
        {
            D3DKMDT_HVIDPNTARGETMODESET hNewTgt = 0;
            const DXGK_VIDPNTARGETMODESET_INTERFACE* newTgtIf = NULL;

            status = vidPnIf->pfnReleaseTargetModeSet(
                pEnumCofuncModality->hConstrainingVidPn, hTgt);
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: ReleaseTargetModeSet tgt=%u failed 0x%08x",
                    path->VidPnTargetId, status);
                break;
            }
            hTgt = 0;
            tgtIf = NULL;

            status = vidPnIf->pfnCreateNewTargetModeSet(
                pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
                &hNewTgt, &newTgtIf);
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: CreateNewTargetModeSet tgt=%u failed 0x%08x",
                    path->VidPnTargetId, status);
            }
            if (NT_SUCCESS(status)) {
                status = newTgtIf->pfnCreateNewModeInfo(hNewTgt, &tgtMode);
                if (!NT_SUCCESS(status)) {
                    WINMALI_ERROR("EnumCofunc: CreateNewTargetModeInfo tgt=%u failed 0x%08x",
                        path->VidPnTargetId, status);
                }
                if (NT_SUCCESS(status)) {
                    Rk3588DispPopulateTargetMode_(gop, pinnedSrc, tgtMode);
                    WINMALI_TRACE("EnumCofunc: AddTargetMode-pre src=%u tgt=%u mode=%p active=%ux%u total=%ux%u v=%u/%u h=%u/%u pixel=%llu std=%u pinnedSrc=%p",
                        path->VidPnSourceId,
                        path->VidPnTargetId,
                        tgtMode,
                        tgtMode->VideoSignalInfo.ActiveSize.cx,
                        tgtMode->VideoSignalInfo.ActiveSize.cy,
                        tgtMode->VideoSignalInfo.TotalSize.cx,
                        tgtMode->VideoSignalInfo.TotalSize.cy,
                        tgtMode->VideoSignalInfo.VSyncFreq.Numerator,
                        tgtMode->VideoSignalInfo.VSyncFreq.Denominator,
                        tgtMode->VideoSignalInfo.HSyncFreq.Numerator,
                        tgtMode->VideoSignalInfo.HSyncFreq.Denominator,
                        tgtMode->VideoSignalInfo.PixelRate,
                        tgtMode->VideoSignalInfo.VideoStandard,
                        pinnedSrc);
                    status = newTgtIf->pfnAddMode(hNewTgt, tgtMode);
                    if (!NT_SUCCESS(status)) {
                        WINMALI_ERROR("EnumCofunc: AddTargetMode src=%u tgt=%u failed 0x%08x (active=%ux%u total=%ux%u v=%u/%u h=%u/%u pixel=%llu pinnedSrc=%p srcSize=%ux%u)",
                            path->VidPnSourceId,
                            path->VidPnTargetId,
                            status,
                            tgtMode->VideoSignalInfo.ActiveSize.cx,
                            tgtMode->VideoSignalInfo.ActiveSize.cy,
                            tgtMode->VideoSignalInfo.TotalSize.cx,
                            tgtMode->VideoSignalInfo.TotalSize.cy,
                            tgtMode->VideoSignalInfo.VSyncFreq.Numerator,
                            tgtMode->VideoSignalInfo.VSyncFreq.Denominator,
                            tgtMode->VideoSignalInfo.HSyncFreq.Numerator,
                            tgtMode->VideoSignalInfo.HSyncFreq.Denominator,
                            tgtMode->VideoSignalInfo.PixelRate,
                            pinnedSrc,
                            pinnedSrc ? pinnedSrc->Format.Graphics.PrimSurfSize.cx : 0,
                            pinnedSrc ? pinnedSrc->Format.Graphics.PrimSurfSize.cy : 0);
                        newTgtIf->pfnReleaseModeInfo(hNewTgt, tgtMode);
                    }
                    tgtMode = NULL;
                }
                if (NT_SUCCESS(status)) {
                    status = vidPnIf->pfnAssignTargetModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId, hNewTgt);
                    if (!NT_SUCCESS(status)) {
                        WINMALI_ERROR("EnumCofunc: AssignTargetModeSet tgt=%u failed 0x%08x",
                            path->VidPnTargetId, status);
                    }
                }
                if (!NT_SUCCESS(status)) {
                    vidPnIf->pfnReleaseTargetModeSet(
                        pEnumCofuncModality->hConstrainingVidPn, hNewTgt);
                }
            }
        } else if (pinnedTgt != NULL) {
            status = tgtIf->pfnReleaseModeInfo(hTgt, pinnedTgt);
            pinnedTgt = NULL;
            if (!NT_SUCCESS(status)) break;
        }

        if (hTgt != 0) {
            status = vidPnIf->pfnReleaseTargetModeSet(
                pEnumCofuncModality->hConstrainingVidPn, hTgt);
            hTgt = 0;
            tgtIf = NULL;
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: ReleaseTargetModeSet tgt=%u failed 0x%08x",
                    path->VidPnTargetId, status);
                break;
            }
        }

        if (releasePinnedSrc && pinnedSrc != NULL && srcIf != NULL && hSrc != 0) {
            status = srcIf->pfnReleaseModeInfo(hSrc, pinnedSrc);
            pinnedSrc = NULL;
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: ReleasePinnedSource src=%u failed 0x%08x",
                    path->VidPnSourceId, status);
                break;
            }

            status = vidPnIf->pfnReleaseSourceModeSet(
                pEnumCofuncModality->hConstrainingVidPn, hSrc);
            hSrc = 0;
            srcIf = NULL;
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: ReleaseSourceModeSet-afterTarget src=%u failed 0x%08x",
                    path->VidPnSourceId, status);
                break;
            }
        }

        localPath = *path;

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_SCALING) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == path->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == path->VidPnTargetId)) &&
            path->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
        {
            RtlZeroMemory(&localPath.ContentTransformation.ScalingSupport,
                sizeof(localPath.ContentTransformation.ScalingSupport));
            localPath.ContentTransformation.ScalingSupport.Identity = 1;
            localPath.ContentTransformation.ScalingSupport.Centered = 1;
            supportFieldsModified = TRUE;
        }

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_ROTATION) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == path->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == path->VidPnTargetId)) &&
            path->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
        {
            //
            // Route B: only advertise Identity. Without a real VOP2-driven
            // scan-out we can't actually rotate, so claiming Rotate90 here
            // (which we previously did) made dxgkrnl keep proposing 90/270
            // VidPns and reject our cofunc walk because the source size
            // didn't match the rotated target. Identity-only is consistent
            // with the active GOP framebuffer geometry and lets dxgk pin
            // a topology on the first pass.
            //
            RtlZeroMemory(&localPath.ContentTransformation.RotationSupport,
                sizeof(localPath.ContentTransformation.RotationSupport));
            localPath.ContentTransformation.RotationSupport.Identity = 1;
            localPath.ContentTransformation.RotationSupport.Offset0  = 1;
            supportFieldsModified = TRUE;
        }

        if (supportFieldsModified) {
            status = topoIf->pfnUpdatePathSupportInfo(hTopo, &localPath);
            if (!NT_SUCCESS(status)) {
                WINMALI_ERROR("EnumCofunc: UpdatePathSupportInfo src=%u tgt=%u failed 0x%08x",
                    path->VidPnSourceId,
                    path->VidPnTargetId,
                    status);
                break;
            }
        }

        if (!NT_SUCCESS(status)) break;

        prevPath = path;
        status = topoIf->pfnAcquireNextPathInfo(hTopo, prevPath, &path);
        if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
            NTSTATUS releaseStatus = topoIf->pfnReleasePathInfo(hTopo, prevPath);
            prevPath = NULL;
            if (!NT_SUCCESS(releaseStatus)) {
                status = releaseStatus;
                break;
            }
            status = STATUS_SUCCESS;
            path = NULL;
            break;
        }
        if (!NT_SUCCESS(status)) break;

        status = topoIf->pfnReleasePathInfo(hTopo, prevPath);
        prevPath = NULL;
        if (!NT_SUCCESS(status)) break;
    }

    if (pinnedSrc != NULL && srcIf != NULL && hSrc != 0) {
        (void)srcIf->pfnReleaseModeInfo(hSrc, pinnedSrc);
    }
    if (pinnedTgt != NULL && tgtIf != NULL && hTgt != 0) {
        (void)tgtIf->pfnReleaseModeInfo(hTgt, pinnedTgt);
    }
    if (path != NULL && topoIf != NULL) {
        (void)topoIf->pfnReleasePathInfo(hTopo, path);
    }
    if (prevPath != NULL && topoIf != NULL) {
        (void)topoIf->pfnReleasePathInfo(hTopo, prevPath);
    }
    if (hSrc != 0 && vidPnIf != NULL) {
        (void)vidPnIf->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hSrc);
    }
    if (hTgt != 0 && vidPnIf != NULL) {
        (void)vidPnIf->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hTgt);
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
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
    const DXGK_VIDPN_INTERFACE*         vidPnIf = NULL;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* topoIf = NULL;
    D3DKMDT_HVIDPNTOPOLOGY              hTopo = 0;
    const D3DKMDT_VIDPN_PRESENT_PATH*   path = NULL;
    NTSTATUS                            status;
    BOOLEAN                             primarySeen = FALSE;

    UNREFERENCED_PARAMETER(hAdapter);   

    WINMALI_ENTER();
    if (a == NULL || pCommitVidPn == NULL) {
        WINMALI_WARN(
            "CommitVidPn: invalid args (a=%p hAdapter=%p)",
            a,
            hAdapter);
        return STATUS_INVALID_PARAMETER;
    }

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
    WINMALI_ENTER();

    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
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
    WINMALI_ENTER();
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
    if (a == NULL || pSetVidPnSourceVisibility == NULL) return STATUS_INVALID_PARAMETER;
    a->SourceVisible = pSetVidPnSourceVisibility->Visible;
    if (!pSetVidPnSourceVisibility->Visible &&
        a->Gop.Valid &&
        a->Gop.SystemDisplayVa != NULL &&
        a->Gop.SystemDisplayBytes != 0)
    {
        RtlZeroMemory(a->Gop.SystemDisplayVa, a->Gop.SystemDisplayBytes);
    }
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
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
    const RK3588_DISP_GOP_FB*                gop = &a->Gop;
    const DXGK_MONITORSOURCEMODESET_INTERFACE* setIf = NULL;
    D3DKMDT_MONITOR_SOURCE_MODE*             mode = NULL;
    NTSTATUS                                 status;
    UINT                                     width;
    UINT                                     height;

    UNREFERENCED_PARAMETER(hAdapter);
    WINMALI_ENTER();
    if (a == NULL || pRecommendMonitorModes == NULL || gop == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    setIf = pRecommendMonitorModes->pMonitorSourceModeSetInterface;
    if (setIf == NULL) return STATUS_INVALID_PARAMETER;

    width = Rk3588DispModeWidth_(gop);
    height = Rk3588DispModeHeight_(gop);

    status = setIf->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet, &mode);
    if (!NT_SUCCESS(status)) return status;

    RtlZeroMemory(mode, sizeof(*mode));
    mode->VideoSignalInfo.VideoStandard             = D3DKMDT_VSS_EIA_861B;
    mode->VideoSignalInfo.ActiveSize.cx             = width;
    mode->VideoSignalInfo.ActiveSize.cy             = height;
    mode->VideoSignalInfo.TotalSize.cx              = 2200;
    mode->VideoSignalInfo.TotalSize.cy              = 1125;
    mode->VideoSignalInfo.VSyncFreq.Numerator       = 60;
    mode->VideoSignalInfo.VSyncFreq.Denominator     = 1;
    mode->VideoSignalInfo.HSyncFreq.Numerator       = 67500;
    mode->VideoSignalInfo.HSyncFreq.Denominator     = 1;
    mode->VideoSignalInfo.PixelRate                 = 148500000ULL;
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

//
// Lazy-map the GOP framebuffer once for the runtime present path. We use a
// separate mapping from Gop.SystemDisplayVa because the latter is only
// established on SystemDisplayEnable (bugcheck handoff); SetVidPnSourceAddress
// runs in normal DWM flow and needs the CPU view available at any IRQL.
// MmMapIoSpaceEx itself must run at PASSIVE_LEVEL; we therefore only call it
// from CommitVidPn / SetVidPnSourceAddress, which dxgk dispatches at PASSIVE.
//
static NTSTATUS
Rk3588DispEnsureGopRuntimeMap_(_Inout_ PWINMALI_ADAPTER Adapter)
{
    SIZE_T bytes;
    PHYSICAL_ADDRESS phys;

    if (Adapter == NULL || !Adapter->Gop.Valid) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Adapter->GopRuntimeVa != NULL) {
        return STATUS_SUCCESS;
    }
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        // Defer; the first PASSIVE call (e.g. CommitVidPn) will set it up.
        return STATUS_DEVICE_NOT_READY;
    }
    bytes = (SIZE_T)Adapter->Gop.Pitch * Adapter->Gop.Height;
    if (bytes == 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    phys = Adapter->Gop.PhysBase;
    //
    // WriteCombined - the GOP framebuffer is uncached scan-out memory and
    // we only write into it; combining store-byte/store-word traffic into
    // larger AXI bursts gives DWM a meaningful frame-rate at the cost of
    // not guaranteeing ordering with respect to other CPUs (which is fine,
    // we don't read it back).
    //
    Adapter->GopRuntimeVa = MmMapIoSpaceEx(phys, bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (Adapter->GopRuntimeVa == NULL) {
        // Fall back to non-cached; some HALs don't honor WC for this range.
        Adapter->GopRuntimeVa = MmMapIoSpace(phys, bytes, MmNonCached);
        if (Adapter->GopRuntimeVa == NULL) {
            WINMALI_WARN(
                "SetVidPnSourceAddress: GOP runtime map failed for phys=0x%llx (%Iu bytes)",
                phys.QuadPart, bytes);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    Adapter->GopRuntimeBytes = bytes;
    WINMALI_TRACE(
        "SetVidPnSourceAddress: GOP runtime va=%p bytes=0x%Ix",
        Adapter->GopRuntimeVa, Adapter->GopRuntimeBytes);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Rk3588DispSetVidPnSourceAddress(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS     pSetVidPnSourceAddress)
{
    PWINMALI_ADAPTER a = WinMaliAdapterFromDxgkHandle(hAdapter);
    PHYSICAL_ADDRESS gopBase;
    LONGLONG         gopEnd;
    LONGLONG         primAddr;

    WINMALI_ENTER();

    if (a == NULL || pSetVidPnSourceAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    gopBase  = a->Gop.PhysBase;
    primAddr = pSetVidPnSourceAddress->PrimaryAddress.QuadPart;
    gopEnd   = gopBase.QuadPart
             + (LONGLONG)(a->Gop.Valid ? (a->Gop.Pitch * a->Gop.Height) : 0u);

    //
    // Case 0: primary is backed by the driver-owned contiguous scan-out slab
    // (QUERYSEGMENT segment id 2). Program VOP2 YRGB_MST + CFG_DONE for the
    // flip target CPU PA (segment-local offset encoded in PrimaryAddress).
    //
    if (a->ScanoutSegmentVa != NULL && a->ScanoutSegmentBytes != 0u) {
        LONGLONG scanStart = a->ScanoutSegmentPhys.QuadPart;
        LONGLONG scanEnd   = scanStart + (LONGLONG)a->ScanoutSegmentBytes;
        BOOLEAN  inSlab =
            (primAddr >= scanStart && primAddr < scanEnd);

        //
        // PrimaryAddress must be CPU PA within the contiguous slab we gave
        // QUERYSEGMENT (vidmm offsets map to physical addresses here).
        //
        if (inSlab)
        {
            ULONG    virPx = (a->Gop.Width != 0u) ? a->Gop.Width : 1920u;
            NTSTATUS voSt = WinMaliVop2SetScanoutPa(
                &a->Vop2,
                (ULONG)(primAddr & 0xFFFFFFFFull),
                virPx);

            if (!NT_SUCCESS(voSt)) {
                WINMALI_WARN(
                    "SetVidPnSourceAddress: VOP2 SetScanoutPa failed 0x%08x "
                    "addr=0x%llx seg=%u",
                    voSt,
                    (ULONGLONG)primAddr,
                    pSetVidPnSourceAddress->PrimarySegment);
                return voSt;
            }

            WINMALI_TRACE(
                "SetVidPnSourceAddress: VOP2 scan-out addr=0x%llx seg=%u virPx=%u",
                (ULONGLONG)primAddr,
                pSetVidPnSourceAddress->PrimarySegment,
                virPx);
            return STATUS_SUCCESS;
        }

        if (pSetVidPnSourceAddress->PrimarySegment == WINMALI_GOP_SEGMENT_ID
            && !inSlab)
        {
            WINMALI_WARN(
                "SetVidPnSourceAddress: seg=%u but addr=0x%llx outside scan slab "
                "[0x%llx,0x%llx)",
                WINMALI_GOP_SEGMENT_ID,
                (ULONGLONG)primAddr,
                (ULONGLONG)scanStart,
                (ULONGLONG)scanEnd);
            return STATUS_INVALID_PARAMETER;
        }
    }

    //
    // Case 1: primary still targets the firmware GOP framebuffer (same PA
    // UEFI programmed into YRGB_MST). Nothing to program; optional runtime
    // map for PresentDisplayOnly memcpy paths.
    //
    if (pSetVidPnSourceAddress->PrimarySegment == WINMALI_GOP_SEGMENT_ID
        || (a->Gop.Valid && primAddr >= gopBase.QuadPart && primAddr < gopEnd))
    {
        (void)Rk3588DispEnsureGopRuntimeMap_(a);
        WINMALI_TRACE(
            "SetVidPnSourceAddress: src=%u addr=0x%llx seg=%u offset_in_gop=0x%llx (direct scan-out)",
            pSetVidPnSourceAddress->VidPnSourceId,
            (ULONGLONG)primAddr,
            pSetVidPnSourceAddress->PrimarySegment,
            (ULONGLONG)(primAddr - gopBase.QuadPart));
        return STATUS_SUCCESS;
    }

    //
    // Case 2: primary lives in the aperture segment (id 1) or elsewhere.
    // Until VOP2 is wired we would have to memcpy the surface contents
    // into the GOP fb on every flip. Setting up the per-page mapping is
    // expensive at PROFILE level (where DWM can call SetVidPnSourceAddress),
    // and DWM is fine if we silently succeed and DWM's redirection bitmap
    // does the painting via PresentDisplayOnly / SystemDisplayWrite. We
    // log this loudly so it shows up as a single line when first seen.
    //
    (void)Rk3588DispEnsureGopRuntimeMap_(a);
    WINMALI_WARN(
        "SetVidPnSourceAddress: src=%u addr=0x%llx seg=%u OUTSIDE gop[0x%llx,0x%llx)"
        " - aperture-backed primary, scan-out fallback deferred",
        pSetVidPnSourceAddress->VidPnSourceId,
        (ULONGLONG)primAddr,
        pSetVidPnSourceAddress->PrimarySegment,
        (ULONGLONG)gopBase.QuadPart, (ULONGLONG)gopEnd);
    return STATUS_SUCCESS;
}
