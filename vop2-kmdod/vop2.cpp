#include "Vop2Kmd.hpp"

VOP2::VOP2(_In_ DEVICE_OBJECT* pPhysicalDeviceObject)
{
    DbgPrint("VOP2::VOP2: Enter\n");
    /* we don't do much here as most of the init logic goes in StartDevice */
    DriverPDO = pPhysicalDeviceObject;
}

VOP2::~VOP2()
{
    //TODO:
}

#define VOPREGSSIZE 0x4200
#define VOPREGPA 0xfdd90000
#if 0
    MEMORY32FIXED( ReadWrite, 0xfdd90000, 0x4200, ) // regs
    MEMORY32FIXED( ReadWrite, 0xfdd95000, 0x1000, ) //gamma lut /
    MEMORY32FIXED( ReadWrite, 0xfde80000, 0x2000, ) // hdmi 0/
    MEMORY32FIXED( ReadWrite, 0xfdea0000, 0x2000, ) // hdmi 1/
    MEMORY32FIXED( ReadWrite, 0xfdec0000, 0x1000, ) // eDP/
#endif
NTSTATUS
VOP2::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                  _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                  _Out_ ULONG*             pNumberOfViews,
                  _Out_ ULONG*             pNumberOfChildren)
{
    PHYSICAL_ADDRESS RegistersAddressPA;
    DbgPrint("VOP2::StartDevice: Enter\n");
    DxgkInterface = *pDxgkInterface;
    StartInfo = *pDxgkStartInfo;

    NTSTATUS Status = DxgkInterface.DxgkCbGetDeviceInformation(DxgkInterface.DeviceHandle, &DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    //TODO: Obtain the BA at runtime
    RegistersAddressPA.QuadPart = VOPREGPA;
    /* Map Registers */
    Status = DxgkInterface.DxgkCbMapMemory(DxgkInterface.DeviceHandle,
                                             RegistersAddressPA,
                                             VOPREGSSIZE,
                                             FALSE,
                                             FALSE,
                                             MmNonCached,
                                             &RegistersAddress);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("VOP2::StartDevice: failed to map registers with status: %X\n", Status);
        return STATUS_UNSUCCESSFUL;
    }

    /* Obtain control of the framebuffer - disable the BDD */
    Status = DxgkInterface.DxgkCbAcquirePostDisplayOwnership(DxgkInterface.DeviceHandle, &(ActiveDisplayModes[0].DxgkDispInfo));
    if (!NT_SUCCESS(Status) || ActiveDisplayModes[0].DxgkDispInfo.Width == 0)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (Status == STATUS_SUCCESS)
        IsActive = TRUE;

    *pNumberOfViews = RK3588_MAX_VIEW;
    *pNumberOfChildren = 1; //TODO: Let's look at the hardware closely here.
    HwVop2Init(RegistersAddress);
    DbgPrint("VOP2::StartDevice: Exit\n");
    return Status;
}
