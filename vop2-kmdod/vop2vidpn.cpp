#include "Vop2Kmd.hpp"


NTSTATUS
APIENTRY
Vop2DdiIsSupportedVidPn(_In_ CONST HANDLE                         hAdapter,
                        _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pIsSupportedVidPn);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendFunctionalVidPn(_In_ CONST HANDLE                                   hAdapter,
                                _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendVidPnTopology(_In_ CONST HANDLE                                 hAdapter,
                              _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendVidPnTopology);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiRecommendMonitorModes(_In_ CONST HANDLE                                hAdapter,
                             _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendMonitorModes);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiEnumVidPnCofuncModality(_In_ CONST HANDLE                                  hAdapter,
                               _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEnumCofuncModality);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiSetVidPnSourceVisibility(_In_ CONST HANDLE                             hAdapter,
                                _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiCommitVidPn(_In_ CONST HANDLE                         hAdapter,
                   _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCommitVidPn);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiUpdateActiveVidPnPresentPath(_In_ CONST HANDLE                                       hAdapter,
                                    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Vop2DdiQueryVidPnHWCapability(_In_ CONST HANDLE                         hAdapter,
                              _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pVidPnHWCaps);
    return STATUS_UNSUCCESSFUL;
}
