
#include "Vop2Kmd.hpp"

/* Entry */
extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    KMDDOD_INITIALIZATION_DATA InitialData = {0};

    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WDDM1_3;

    /* First let's fillout the needed DDI callbacks for a KMDOD */
    InitialData.DxgkDdiAddDevice                    = Vop2DdiAddDevice;
    InitialData.DxgkDdiStartDevice                  = Vop2DdiStartDevice;
    InitialData.DxgkDdiStopDevice                   = Vop2DdiStopDevice;
    InitialData.DxgkDdiResetDevice                  = Vop2DdiResetDevice;
    InitialData.DxgkDdiRemoveDevice                 = Vop2DdiRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest            = Vop2DdiDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine             = Vop2DdiInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine                   = Vop2DdiDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations          = Vop2DdiQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus             = Vop2DdiQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor        = Vop2DdiQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState                = Vop2DdiSetPowerState;
    InitialData.DxgkDdiUnload                       = Vop2DdiUnload;
    InitialData.DxgkDdiQueryAdapterInfo             = Vop2DdiQueryAdapterInfo;
    InitialData.DxgkDdiSetPointerPosition           = Vop2DdiSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape              = Vop2DdiSetPointerShape;
    InitialData.DxgkDdiIsSupportedVidPn             = Vop2DdiIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn     = Vop2DdiRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality      = Vop2DdiEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility     = Vop2DdiSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn                  = Vop2DdiCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = Vop2DdiUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiRecommendMonitorModes        = Vop2DdiRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability       = Vop2DdiQueryVidPnHWCapability;
    InitialData.DxgkDdiPresentDisplayOnly           = Vop2DdiPresentDisplayOnly;
    InitialData.DxgkDdiSystemDisplayEnable          = Vop2DdiSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite           = Vop2DdiSystemDisplayWrite;
  
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Vop2DdiStopDeviceAndReleasePostDisplayOwnership;

    DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);

    return STATUS_SUCCESS;
}

//TODO: The VOP2 doesn't support much but let's see if this can be expanded.
NTSTATUS
APIENTRY
Vop2DdiQueryAdapterInfo(_In_ CONST HANDLE                         hAdapter,
                        _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo)
{
    UNREFERENCED_PARAMETER(hAdapter);
    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

            RtlZeroMemory(pDriverCaps, sizeof(DXGK_DRIVERCAPS));

            pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_3;
            pDriverCaps->HighestAcceptableAddress.QuadPart = -1;

            pDriverCaps->SupportNonVGA = TRUE;
            pDriverCaps->SupportSmoothRotation = TRUE;

            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
        {
            DXGK_DISPLAY_DRIVERCAPS_EXTENSION* pDriverDisplayCaps;

            if (pQueryAdapterInfo->OutputDataSize < sizeof(*pDriverDisplayCaps))
            {
                return STATUS_INVALID_PARAMETER;
            }

            pDriverDisplayCaps = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION*)pQueryAdapterInfo->pOutputData;

            RtlZeroMemory(pDriverDisplayCaps, pQueryAdapterInfo->OutputDataSize);
            pDriverDisplayCaps->VirtualModeSupport = 1;

            return STATUS_SUCCESS;
        }

        default:
        {

            return STATUS_NOT_SUPPORTED;
        }
    }
}
