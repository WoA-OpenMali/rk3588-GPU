#include "Vop2Kmd.hpp"

NTSTATUS
VOP2::IsSupportedVidPn(_Inout_    DXGKARG_ISSUPPORTEDVIDPN*      pIsSupportedVidPn)
{
    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        // A null desired VidPn is supported
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    // Default to not supported, until shown it is supported
    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    CONST DXGK_VIDPN_INTERFACE* pVidPnInterface;
    NTSTATUS Status = DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("DxgkCbQueryVidPnInterface failed with Status = 0x%I64x, hDesiredVidPn = 0x%I64x", Status, pIsSupportedVidPn->hDesiredVidPn);
        return Status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("pfnGetTopology failed with Status = 0x%I64x, hDesiredVidPn = 0x%I64x", Status, pIsSupportedVidPn->hDesiredVidPn);
        return Status;
    }

    // For every source in this topology, make sure they don't have more paths than there are targets
    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < RK3588_MAX_VIEW; ++SourceId)
    {
        SIZE_T NumPathsFromSource = 0;
        Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, SourceId, &NumPathsFromSource);
        if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        {
            continue;
        }
        else if (!NT_SUCCESS(Status))
        {
            DbgPrint("pfnGetNumPathsFromSource failed with Status = 0x%I64x. hVidPnTopology = 0x%I64x, SourceId = 0x%I64x",
                           Status, hVidPnTopology, SourceId);
            return Status;
        }
        else if (NumPathsFromSource > 1)
        {
            // This VidPn is not supported, which has already been set as the default
            return STATUS_SUCCESS;
        }
    }

    // All sources succeeded so this VidPn is supported
    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Vop2DdiIsSupportedVidPn(_In_ CONST HANDLE                         hAdapter,
                        _Inout_    DXGKARG_ISSUPPORTEDVIDPN*      pIsSupportedVidPn)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(hAdapter);
    return pVOP2->IsSupportedVidPn(pIsSupportedVidPn);
}

NTSTATUS
APIENTRY
Vop2DdiQueryVidPnHWCapability(_In_ CONST HANDLE                         hAdapter,
                              _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation             = 1;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling              = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning              = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert         = 1;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay        = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendFunctionalVidPn(_In_ CONST HANDLE                                   hAdapter,
                                _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendVidPnTopology(_In_ CONST HANDLE                                 hAdapter,
                              _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendVidPnTopology);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS
APIENTRY
Vop2DdiSetVidPnSourceVisibility(_In_ CONST HANDLE                             hAdapter,
                                _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility)
{
    DbgPrint("Vop2DdiSetVidPnSourceVisibility: Entry - UNIMPLEMENTED\n");
    DbgBreakPoint();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendMonitorModes(_In_ CONST HANDLE                                hAdapter,
                             _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    DbgPrint("Vop2DdiRecommendMonitorModes: Entry\n");
    /* Here we'll list the recommended monitor modes for a device - using edid */
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(hAdapter);
    return pVOP2->RecommendMonitorModes(pRecommendMonitorModes);
}

NTSTATUS
APIENTRY
Vop2DdiEnumVidPnCofuncModality(_In_ CONST HANDLE                                  hAdapter,
                               _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(hAdapter);
    return pVOP2->EnumVidPnCofuncModality(pEnumCofuncModality);
}

NTSTATUS
APIENTRY
Vop2DdiCommitVidPn(_In_ CONST HANDLE                         hAdapter,
                   _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(hAdapter);
    return pVOP2->CommitVidPn(pCommitVidPn);
}

NTSTATUS
APIENTRY
Vop2DdiUpdateActiveVidPnPresentPath(_In_ CONST HANDLE                                       hAdapter,
                                    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(hAdapter);
    return pVOP2->UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);
}

const static SampleSourceMode C_SampleSourceMode[] = {{800,600},{1024,768},{1152,864},{1280,800},{1280,1024},{1400,1050},{1600,1200},{1680,1050},{1920,1200}};
const static UINT C_SampleSourceModeMax = sizeof(C_SampleSourceMode)/sizeof(C_SampleSourceMode[0]);
NTSTATUS VOP2::AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                    D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    // There is only one source format supported by display-only drivers, but more can be added in a
    // full WDDM driver if the hardware supports them
    for (UINT PelFmtIdx = 0; PelFmtIdx < ARRAYSIZE(gBddPixelFormats); ++PelFmtIdx)
    {
        // Create new mode info that will be populated
        D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo = NULL;
        NTSTATUS Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet, &pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint("pfnCreateNewModeInfo failed with Status = 0x%I64x, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet);
            return Status;
        }

        // Populate mode info with values from current mode and hard-coded values
        // Always report 32 bpp format, this will be color converted during the present if the mode is < 32bpp
        pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = m_CurrentModes[SourceId].DispInfo.Width;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = m_CurrentModes[SourceId].DispInfo.Height;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = m_CurrentModes[SourceId].DispInfo.Pitch;
        pVidPnSourceModeInfo->Format.Graphics.PixelFormat = gBddPixelFormats[PelFmtIdx];
        pVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        // Add the mode to the source mode set
        Status = pVidPnSourceModeSetInterface->pfnAddMode(hVidPnSourceModeSet, pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            NTSTATUS TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnSourceModeInfo);
            UNREFERENCED_PARAMETER(TempStatus);
            NT_ASSERT(NT_SUCCESS(TempStatus));

            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint("pfnAddMode failed with Status = 0x%I64x, hVidPnSourceModeSet = 0x%I64x, pVidPnSourceModeInfo = 0x%I64x", Status, hVidPnSourceModeSet, pVidPnSourceModeInfo);
                return Status;
            }
        }
    }

    UINT WidthMax = ActiveDisplayModes[SourceId].DispInfo.Width;
    UINT HeightMax = ActiveDisplayModes[SourceId].DispInfo.Height;

    // Add all predefined modes that fit within the bounds of the required (POST) mode
    for (UINT ModeIndex = 0; ModeIndex < C_SampleSourceModeMax; ++ModeIndex)
    {
        if (C_SampleSourceMode[ModeIndex].ModeWidth > WidthMax)
        {
            break;
        }
        else if (C_SampleSourceMode[ModeIndex].ModeWidth == WidthMax)
        {
            if(C_SampleSourceMode[ModeIndex].ModeHeight >= HeightMax)
            {
                break;
            }
        }
        else
        {
            if(C_SampleSourceMode[ModeIndex].ModeHeight > HeightMax)
            {
                continue;
            }
        }

        // There is only one source format supported by display-only drivers, but more can be added in a
        // full WDDM driver if the hardware supports them
        for (UINT PelFmtIdx = 0; PelFmtIdx < ARRAYSIZE(gBddPixelFormats); ++PelFmtIdx)
        {
            // Create new mode info that will be populated
            D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo = NULL;
            NTSTATUS Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet, &pVidPnSourceModeInfo);
            if (!NT_SUCCESS(Status))
            {
                // If failed to create a new mode info, continuing to the next mode and trying again isn't going to be at all helpful, so return
                // Also, mode doesn't need to be released since it was never created
                DbgPrint("pfnCreateNewModeInfo failed with Status = 0x%I64x, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet);
                return Status;
            }

            // Populate mode info with values from mode at ModeIndex and hard-coded values
            // Always report 32 bpp format, this will be color converted during the present if the mode at ModeIndex was < 32bpp
            pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
            pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = C_SampleSourceMode[ModeIndex].ModeWidth;
            pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = C_SampleSourceMode[ModeIndex].ModeHeight;
            pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
            pVidPnSourceModeInfo->Format.Graphics.Stride = 4*C_SampleSourceMode[ModeIndex].ModeWidth;
            pVidPnSourceModeInfo->Format.Graphics.PixelFormat = gBddPixelFormats[PelFmtIdx];
            pVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
            pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

            // Add the mode to the source mode set
            Status = pVidPnSourceModeSetInterface->pfnAddMode(hVidPnSourceModeSet, pVidPnSourceModeInfo);
            if (!NT_SUCCESS(Status))
            {
                if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
                {
                    DbgPrint("pfnAddMode failed with Status = 0x%I64x, hVidPnSourceModeSet = 0x%I64x, pVidPnSourceModeInfo = 0x%I64x", Status, hVidPnSourceModeSet, pVidPnSourceModeInfo);
                }

                // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked, continue to next mode anyway
                Status = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnSourceModeInfo);
                //BDD_ASSERT_CHK(NT_SUCCESS(Status));
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
VOP2::RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    DbgPrint("RecommendMonitorModes: Entry\n");
   D3DKMDT_MONITOR_SOURCE_MODE* pMonitorSourceMode = NULL;
    NTSTATUS Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        // If failed to create a new mode info, mode doesn't need to be released since it was never created
        DbgPrint("pfnCreateNewModeInfo failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet);
        return Status;
    }

    D3DDDI_VIDEO_PRESENT_SOURCE_ID CorrespondingSourceId = FindSourceForTarget(pRecommendMonitorModes->VideoPresentTargetId, TRUE);

    // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = ActiveDisplayModes[CorrespondingSourceId].DispInfo.Width;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = ActiveDisplayModes[CorrespondingSourceId].DispInfo.Height;
    pMonitorSourceMode->VideoSignalInfo.ActiveSize = pMonitorSourceMode->VideoSignalInfo.TotalSize;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    // We set the preference to PREFERRED since this is the only supported mode
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
        {
            DbgPrint("pfnAddMode failed with Status = 0x%I64x, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%I64x",
                            Status, pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        }
        else
        {
            Status = STATUS_SUCCESS;
        }

        // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
        NTSTATUS TempStatus = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
        return Status;
    }
    else
    {
        // If AddMode succeeded with something other than STATUS_SUCCESS treat it as such anyway when propagating up
        return STATUS_SUCCESS;
    }
}

NTSTATUS
VOP2::EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality)
{
    DbgPrint("EnumVidPnCofuncModality: Entry\n");
}

NTSTATUS
VOP2::CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn)
{
    DbgPrint("CommitVidPn: Entry\n");

}

NTSTATUS
VOP2::UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath)
{
    DbgPrint("UpdateActiveVidPnPresentPath: Entry\n");
}
