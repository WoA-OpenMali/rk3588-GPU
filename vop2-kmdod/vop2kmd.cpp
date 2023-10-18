
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

    //InitialData.DxgkDdiAddDevice                    = ;
    //InitialData.DxgkDdiStartDevice                  = ;
  
    DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);

    return STATUS_SUCCESS;
}
