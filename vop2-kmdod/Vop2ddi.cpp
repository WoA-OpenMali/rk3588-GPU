#include "Vop2Kmd.hpp"

NTSTATUS
Vop2DdiAddDevice(_In_ DEVICE_OBJECT* pPhysicalDeviceObject,
                 _Outptr_ PVOID*  ppDeviceContext)
{
    if ((!pPhysicalDeviceObject) ||
        (!ppDeviceContext))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *ppDeviceContext = NULL;

    /* Initialize an instance of the hardware component of driver */
    VOP2* pVOP2 = new(NonPagedPool) VOP2(pPhysicalDeviceObject);

    if (!pVOP2)
        return STATUS_NO_MEMORY;


    *ppDeviceContext = pVOP2;

    return STATUS_SUCCESS;
}

NTSTATUS
Vop2DdiStartDevice(_In_  VOID*              pDeviceContext,
                   _In_  DXGK_START_INFO*   pDxgkStartInfo,
                   _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                   _Out_ ULONG*             pNumberOfViews,
                   _Out_ ULONG*             pNumberOfChildren)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(pDeviceContext);
    return pVOP2->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
}

NTSTATUS
Vop2DdiRemoveDevice(_In_  VOID* pDeviceContext)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(pDeviceContext);
    pVOP2->~VOP2();
    return STATUS_SUCCESS;
}

NTSTATUS
Vop2DdiStopDevice(_In_  VOID* pDeviceContext)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(pDeviceContext);
    pVOP2->IsActive = FALSE;
    //TODO: this needs to do more soon.
    return STATUS_SUCCESS;
}

VOID
Vop2DdiResetDevice(_In_  VOID* pDeviceContext)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(pDeviceContext);
    pVOP2->IsActive = FALSE;
    //TODO: this needs to do more soon.
}

/*
 * One of the magic things about UEFI is that the framebuffer depends on the hardware.
 * There isn't emulation like VGA/VBE has. On a system like this the Display adapter (VOP2 here)
 * will start up in UEFI and create the framebuffer and then we modify it and return it back.
 *
 * If you notice; The framebuffer of the primary boot device will change (i.e our EDK2 currently goes up to 1080p)
 * when we return the video mode back we update the resolution so the BSoD (which is most common for this to be triggered)
 * will have the updated size! Not every driver supports this but we're going to because it's funny
 */
NTSTATUS
APIENTRY
Vop2DdiStopDeviceAndReleasePostDisplayOwnership(_In_  VOID*                          pDeviceContext,
                                                _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    VOP2* pVOP2 = reinterpret_cast<VOP2*>(pDeviceContext);
    pVOP2->IsActive = FALSE;
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(DisplayInfo);
    return STATUS_UNSUCCESSFUL;
}