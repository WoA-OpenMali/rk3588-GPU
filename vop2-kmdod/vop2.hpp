class VOP2
{
private:
    DEVICE_OBJECT* DriverPDO;
    /* These are your DxgKrnl services */
    DXGKRNL_INTERFACE DxgkInterface;
    /* Startup info passed via a DDI */
    DXGK_START_INFO StartInfo;
    DXGK_DEVICE_INFO DeviceInfo;
    VOP2_ACTIVE_DISPLAY_MODE ActiveDisplayModes[RK3588_MAX_VIEW];
    PVOID RegistersAddress;
public:
    BOOLEAN IsActive;

    VOP2(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~VOP2();
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                 D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);

    // Add the current mode (or the matching to pinned source mode) to the give VidPn target mode set
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
                                 D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                 _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    /* DDIs: */
    NTSTATUS StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren);
    NTSTATUS IsSupportedVidPn(_Inout_    DXGKARG_ISSUPPORTEDVIDPN*      pIsSupportedVidPn);

    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes);
    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality);
    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn);
    NTSTATUS UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath);
};