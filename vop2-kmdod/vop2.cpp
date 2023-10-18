#include "Vop2Kmd.hpp"

VOP2::VOP2(_In_ DEVICE_OBJECT* pPhysicalDeviceObject)
{
    /* we don't do much here as most of the init logic goes in StartDevice */
    DriverPDO = pPhysicalDeviceObject;
}

VOP2::~VOP2()
{
    //TODO:
}

NTSTATUS
VOP2::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                  _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                  _Out_ ULONG*             pNumberOfViews,
                  _Out_ ULONG*             pNumberOfChildren)
{
    DxgkInterface = *pDxgkInterface;
    StartInfo = *pDxgkStartInfo;

    NTSTATUS Status = DxgkInterface.DxgkCbGetDeviceInformation(DxgkInterface.DeviceHandle, &DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *pNumberOfViews = RK3588_MAX_VIEW;
    *pNumberOfChildren = 1; //TODO: Let's look at the hardware closely here.
    return Status;
}
