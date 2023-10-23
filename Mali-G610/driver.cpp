#include "driver.hpp"
#include "debug.h"
NTSTATUS
G610AddDevice(_In_ DEVICE_OBJECT* pPhysicalDeviceObject,
              _Outptr_ PVOID*  ppDeviceContext)
{
    PVOID VirtualAddress;
    PAGED_CODE();
    PHYSICAL_ADDRESS    PhysicalAddressOfRegisters;
    PhysicalAddressOfRegisters.QuadPart = 0xfb000000;
    DPRINT1("--> %s\n", __FUNCTION__);
    UNIMPLEMENTED;
    VirtualAddress = MmMapIoSpace(PhysicalAddressOfRegisters, 0x20000, MmNonCached);
    DPRINT1("Checking GPU %X\n", READ_REGISTER_ULONG((PULONG)VirtualAddress));
    DbgBreakPoint();
    DPRINT1("<-- %s: ppDeviceContext = %p\n", __FUNCTION__, ppDeviceContext);
    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(_In_  DRIVER_OBJECT*  DriverObject,
            _In_  UNICODE_STRING* RegistryPath)
{
    PAGED_CODE();
    DPRINT1("--> WoA-OpenMali Project - Mali-G610 WDDM Driver <--\n");
    DRIVER_INITIALIZATION_DATA InitialData = { 0 };

    /*
     * TODO: The DXGDDI value will increase as we add more features.
     * There's no reason to run early windows 10 on an SoC with this GPU
     * so we can get pretty high like WDDM 2.2+
     */
    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WDDM1_3;

    /* The first step to any WDDM driver is to fill out these call backs and send it back to dxgkrnl */
    InitialData.DxgkDdiAddDevice = G610AddDevice;
    InitialData.DxgkDdiStartDevice = NULL;
    InitialData.DxgkDdiStopDevice =  NULL;
    InitialData.DxgkDdiRemoveDevice = NULL;
    InitialData.DxgkDdiDispatchIoRequest =  NULL;
    InitialData.DxgkDdiInterruptRoutine =  NULL;
    InitialData.DxgkDdiDpcRoutine =  NULL;
    InitialData.DxgkDdiQueryChildRelations =  NULL;
    InitialData.DxgkDdiQueryChildStatus =  NULL;
    InitialData.DxgkDdiQueryDeviceDescriptor =  NULL;
    InitialData.DxgkDdiSetPowerState =  NULL;
    InitialData.DxgkDdiResetDevice =  NULL;
    InitialData.DxgkDdiUnload = NULL;
    InitialData.DxgkDdiQueryAdapterInfo = NULL;
    InitialData.DxgkDdiEscape = NULL;
    InitialData.DxgkDdiCreateAllocation = NULL;
    InitialData.DxgkDdiOpenAllocation = NULL;
    InitialData.DxgkDdiCloseAllocation = NULL;
    InitialData.DxgkDdiDescribeAllocation = NULL;
    InitialData.DxgkDdiDestroyAllocation = NULL;
    InitialData.DxgkDdiGetStandardAllocationDriverData = NULL;
    InitialData.DxgkDdiBuildPagingBuffer = NULL;
    InitialData.DxgkDdiCreateContext = NULL;
    InitialData.DxgkDdiDestroyContext = NULL;
    InitialData.DxgkDdiPresent = NULL;
    InitialData.DxgkDdiRender = NULL;
    InitialData.DxgkDdiPatch = NULL;
    InitialData.DxgkDdiSubmitCommand = NULL;
    InitialData.DxgkDdiSetPointerPosition = NULL;
    InitialData.DxgkDdiSetPointerShape = NULL;
    InitialData.DxgkDdiCreateDevice =  NULL;
    InitialData.DxgkDdiDestroyDevice = NULL;
    InitialData.DxgkDdiPreemptCommand =  NULL;
    InitialData.DxgkDdiResetFromTimeout =  NULL;
    InitialData.DxgkDdiRestartFromTimeout =  NULL;
    InitialData.DxgkDdiCollectDbgInfo =  NULL;
    InitialData.DxgkDdiQueryCurrentFence =  NULL;
    InitialData.DxgkDdiQueryEngineStatus =  NULL;
    InitialData.DxgkDdiResetEngine =  NULL;
    InitialData.DxgkDdiCancelCommand =  NULL;
    InitialData.DxgkDdiGetNodeMetadata =  NULL;
    InitialData.DxgkDdiControlInterrupt =  NULL;
    InitialData.DxgkDdiGetScanLine = NULL;

    /*
     * As a render only display driver we don't fill these out
     * https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps
     * TODO: Let's check to make sure we don't have devices attached to the GPU.
     *  if we do, we need to use the other method
     */
    InitialData.DxgkDdiIsSupportedVidPn = NULL;
    InitialData.DxgkDdiRecommendFunctionalVidPn = NULL;
    InitialData.DxgkDdiEnumVidPnCofuncModality = NULL;
    InitialData.DxgkDdiSetVidPnSourceVisibility = NULL;
    InitialData.DxgkDdiCommitVidPn = NULL;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = NULL;
    InitialData.DxgkDdiSetVidPnSourceAddress = NULL;
    InitialData.DxgkDdiRecommendMonitorModes =  NULL;
    InitialData.DxgkDdiQueryVidPnHWCapability = NULL;
    InitialData.DxgkDdiSystemDisplayEnable = NULL;
    InitialData.DxgkDdiSystemDisplayWrite = NULL;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = NULL;

    /* Since we can accelerate we use DxgkInitialize (KMDODs use a different initialize Displib function */
    NTSTATUS Status = DxgkInitialize(DriverObject, RegistryPath, &InitialData);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("DxgkInitialize failed with Status: 0x%X\n", Status);
    }

    DPRINT("<-- DriverEntry exit\n");
    return Status;
}
