/*
 * PROJECT:     OpenMali Render only display driver
 * PURPOSE:     Entry point
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include "openmali.hpp"

NTSTATUS
OpenMaliAddDevice(IN_CONST_PDEVICE_OBJECT     PhysicalDeviceObject,
                  OUT_PPVOID                  MiniportDeviceContext)
{
        DbgBreakPoint();
    MaliDbgPrint("OpenMaliAddDevice: Enter\n");
    *MiniportDeviceContext = NULL;

    /* Initialize an instance of the hardware component of driver */
    MaliGpu* LocMaliGpu = new(PagedPool) MaliGpu(PhysicalDeviceObject);

    if (!LocMaliGpu)
    {
        MaliDbgPrint("OpenMaliAddDevice: failed to add memory\n");
        return STATUS_NO_MEMORY;
    }


    *MiniportDeviceContext = LocMaliGpu;
    MaliDbgPrint("OpenMaliAddDevice: Exit\n");
    return STATUS_SUCCESS;
}

NTSTATUS
OpenMaliStartDevice(IN_CONST_PVOID          MiniportDeviceContext,
                    IN_PDXGK_START_INFO     DxgkStartInfo,
                    IN_PDXGKRNL_INTERFACE   DxgkInterface,
                    OUT_PULONG              NumberOfVideoPresentSources,
                    OUT_PULONG              NumberOfChildren)
{
    DbgBreakPoint();
    MaliDbgPrint("OpenMaliStartDevice: Enter\n");
    MaliGpu* LocMaliGpu = reinterpret_cast<MaliGpu*>(MiniportDeviceContext);
    return LocMaliGpu->StartDevice(DxgkStartInfo, DxgkInterface, NumberOfVideoPresentSources, NumberOfChildren);
}

NTSTATUS
OpenMaliStopDevice(IN_CONST_PVOID  MiniportDeviceContext)
{
    DbgBreakPoint();
    //TODO:
    return 0;
}

NTSTATUS
OpenMaliRemoveDevice(IN_CONST_PVOID  MiniportDeviceContext)
{
    DbgBreakPoint();
    //TODO:
    return 0;
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(_In_  DRIVER_OBJECT*  DriverObject,
            _In_  UNICODE_STRING* RegistryPath)
{
    PAGED_CODE();
    DbgPrint("--> WoA-OpenMali Project - Mali-G610 WDDM Driver <--\n");
    DRIVER_INITIALIZATION_DATA InitialData = { 0 };

    /*
     * TODO: The DXGDDI value will increase as we add more features.
     * There's no reason to run early windows 10 on an SoC with this GPU
     * so we can get pretty high like WDDM 2.2+
     */
    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_5;
    InitialData.DxgkDdiAddDevice = OpenMaliAddDevice;
    InitialData.DxgkDdiStartDevice = OpenMaliStartDevice;
    InitialData.DxgkDdiStopDevice = OpenMaliStopDevice;
    InitialData.DxgkDdiRemoveDevice = OpenMaliRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest = OpenMaliDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine = OpenMaliInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine = OpenMaliDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations = OpenMaliQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus = OpenMaliQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor = OpenMaliQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState = OpenMaliSetPowerState;
    InitialData.DxgkDdiNotifyAcpiEvent = OpenMaliNotifyAcpiEvent;
    InitialData.DxgkDdiResetDevice = OpenMaliResetDevice;
    InitialData.DxgkDdiUnload = OpenMaliUnload;
    InitialData.DxgkDdiQueryInterface = OpenMaliQueryInterface;
    InitialData.DxgkDdiControlEtwLogging = OpenMaliControlEtwLogging;
    InitialData.DxgkDdiQueryAdapterInfo = OpenMaliQueryAdapterInfo;
    InitialData.DxgkDdiCreateDevice = NULL;
    InitialData.DxgkDdiCreateAllocation = NULL;
    InitialData.DxgkDdiDestroyAllocation = NULL;
    InitialData.DxgkDdiDescribeAllocation = NULL;
    InitialData.DxgkDdiGetStandardAllocationDriverData = NULL;
    InitialData.DxgkDdiAcquireSwizzlingRange = NULL;
    InitialData.DxgkDdiReleaseSwizzlingRange = NULL;
    InitialData.DxgkDdiPatch = NULL;
    InitialData.DxgkDdiSubmitCommand = NULL;
    InitialData.DxgkDdiPreemptCommand = NULL;
    InitialData.DxgkDdiBuildPagingBuffer = NULL;
    InitialData.DxgkDdiSetPalette = NULL;
    InitialData.DxgkDdiSetPointerPosition = NULL;
    InitialData.DxgkDdiSetPointerShape = NULL;
    InitialData.DxgkDdiResetFromTimeout = NULL;
    InitialData.DxgkDdiRestartFromTimeout = NULL;
    InitialData.DxgkDdiEscape = NULL;
    InitialData.DxgkDdiCollectDbgInfo = NULL;
    InitialData.DxgkDdiQueryCurrentFence = NULL;
    InitialData.DxgkDdiGetScanLine = NULL;
    InitialData.DxgkDdiStopCapture = NULL;
    InitialData.DxgkDdiControlInterrupt = NULL;
    InitialData.DxgkDdiCreateOverlay = NULL;
    InitialData.DxgkDdiDestroyDevice = NULL;
    InitialData.DxgkDdiOpenAllocation = NULL;
    InitialData.DxgkDdiCloseAllocation = NULL;
    InitialData.DxgkDdiRender = NULL;
    InitialData.DxgkDdiPresent = NULL;
    InitialData.DxgkDdiUpdateOverlay = NULL;
    InitialData.DxgkDdiFlipOverlay = NULL;
    InitialData.DxgkDdiDestroyOverlay = NULL;
    InitialData.DxgkDdiCreateContext = NULL;
    InitialData.DxgkDdiDestroyContext = NULL;
    InitialData.DxgkDdiLinkDevice = NULL;
    InitialData.DxgkDdiSetDisplayPrivateDriverFormat = NULL;
    InitialData.DxgkDdiDescribePageTable = NULL;
    InitialData.DxgkDdiUpdatePageTable = NULL;
    InitialData.DxgkDdiUpdatePageDirectory = NULL;
    InitialData.DxgkDdiMovePageDirectory = NULL;
    InitialData.DxgkDdiSubmitRender = NULL;
    InitialData.DxgkDdiCreateAllocation2 = NULL;
    InitialData.DxgkDdiRenderKm = NULL;
    InitialData.DxgkDdiSetPowerComponentFState = NULL;
    InitialData.DxgkDdiQueryDependentEngineGroup = NULL;
    InitialData.DxgkDdiQueryEngineStatus = NULL;
    InitialData.DxgkDdiResetEngine = NULL;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = NULL;
    InitialData.DxgkDdiSystemDisplayEnable = NULL;
    InitialData.DxgkDdiSystemDisplayWrite = NULL;
    InitialData.DxgkDdiCancelCommand = NULL;
    InitialData.DxgkDdiGetChildContainerId = NULL;
    InitialData.DxgkDdiPowerRuntimeControlRequest = NULL;
    InitialData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay = NULL;
    InitialData.DxgkDdiNotifySurpriseRemoval = NULL;
    InitialData.DxgkDdiGetNodeMetadata = NULL;
    InitialData.DxgkDdiSetPowerPState = NULL;
    InitialData.DxgkDdiControlInterrupt2 = NULL;
    InitialData.DxgkDdiCheckMultiPlaneOverlaySupport = NULL;
    InitialData.DxgkDdiCalibrateGpuClock = NULL;
    InitialData.DxgkDdiFormatHistoryBuffer = NULL;
    InitialData.DxgkDdiRenderGdi = NULL;
    InitialData.DxgkDdiSubmitCommandVirtual = NULL;
    InitialData.DxgkDdiSetRootPageTable = NULL;
    InitialData.DxgkDdiGetRootPageTableSize = NULL;
    InitialData.DxgkDdiMapCpuHostAperture = NULL;
    InitialData.DxgkDdiUnmapCpuHostAperture = NULL;
    InitialData.DxgkDdiCheckMultiPlaneOverlaySupport2 = NULL;
    InitialData.DxgkDdiCreateProcess = NULL;
    InitialData.DxgkDdiDestroyProcess = NULL;
    InitialData.DxgkDdiPowerRuntimeSetDeviceHandle = NULL;
    InitialData.DxgkDdiSetStablePowerState = NULL;
    InitialData.DxgkDdiSetVideoProtectedRegion = NULL;
    // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    InitialData.DxgkDdiCheckMultiPlaneOverlaySupport3 = NULL;
    InitialData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 = NULL;
    InitialData.DxgkDdiPostMultiPlaneOverlayPresent = NULL;
    InitialData.DxgkDdiValidateUpdateAllocationProperty = NULL;
    InitialData.DxgkDdiControlModeBehavior = NULL;
    InitialData.DxgkDdiUpdateMonitorLinkInfo = NULL;
    // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    InitialData.DxgkDdiCreateHwContext = NULL;
    InitialData.DxgkDdiDestroyHwContext = NULL;
    InitialData.DxgkDdiCreateHwQueue = NULL;
    InitialData.DxgkDdiDestroyHwQueue = NULL;
    InitialData.DxgkDdiSubmitCommandToHwQueue = NULL;
    InitialData.DxgkDdiSwitchToHwContextList = NULL;
    InitialData.DxgkDdiResetHwEngine = NULL;
    InitialData.DxgkDdiCreatePeriodicFrameNotification = NULL;
    InitialData.DxgkDdiDestroyPeriodicFrameNotification = NULL;
    InitialData.DxgkDdiSetTimingsFromVidPn = NULL;
    InitialData.DxgkDdiSetTargetGamma = NULL;
    InitialData.DxgkDdiSetTargetContentType = NULL;
    InitialData.DxgkDdiSetTargetAnalogCopyProtection = NULL;
    InitialData.DxgkDdiSetTargetAdjustedColorimetry = NULL;
    InitialData.DxgkDdiDisplayDetectControl = NULL;
    InitialData.DxgkDdiQueryConnectionChange = NULL;
    InitialData.DxgkDdiExchangePreStartInfo = NULL;
    InitialData.DxgkDdiGetMultiPlaneOverlayCaps = NULL;
    InitialData.DxgkDdiGetPostCompositionCaps = NULL;
    // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    InitialData.DxgkDdiUpdateHwContextState = NULL;
    InitialData.DxgkDdiCreateProtectedSession = NULL;
    InitialData.DxgkDdiDestroyProtectedSession = NULL;
    // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    InitialData.DxgkDdiSetSchedulingLogBuffer = NULL;
    InitialData.DxgkDdiSetupPriorityBands = NULL;
    InitialData.DxgkDdiNotifyFocusPresent = NULL;
    InitialData.DxgkDdiSetContextSchedulingProperties = NULL;
    InitialData.DxgkDdiSuspendContext = NULL;
    InitialData.DxgkDdiResumeContext = NULL;
    InitialData.DxgkDdiSetVirtualMachineData = NULL;
    InitialData.DxgkDdiBeginExclusiveAccess = NULL;
    InitialData.DxgkDdiEndExclusiveAccess = NULL;
    InitialData.DxgkDdiQueryDiagnosticTypesSupport = NULL;
    InitialData.DxgkDdiControlDiagnosticReporting = NULL;
    InitialData.DxgkDdiResumeHwEngine = NULL;
    // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    InitialData.DxgkDdiSignalMonitoredFence = NULL;
    InitialData.DxgkDdiPresentToHwQueue = NULL;
    InitialData.DxgkDdiValidateSubmitCommand = NULL;
    InitialData.DxgkDdiSetTargetAdjustedColorimetry2 = NULL;
    InitialData.DxgkDdiSetTrackedWorkloadPowerLevel = NULL;
    /*
     * As a render only display driver we don't fill these out
     * https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps
     * TODO: Let's check to make sure we don't have devices attached to the GPU.
     *  if we do, we need to use the other method
     */
    InitialData.DxgkDdiIsSupportedVidPn = NULL;
    InitialData.DxgkDdiRecommendFunctionalVidPn = NULL;
    InitialData.DxgkDdiEnumVidPnCofuncModality = NULL;
    InitialData.DxgkDdiSetVidPnSourceAddress = NULL;
    InitialData.DxgkDdiSetVidPnSourceVisibility = NULL;
    InitialData.DxgkDdiCommitVidPn = NULL;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = NULL;
    InitialData.DxgkDdiRecommendMonitorModes = NULL;
    InitialData.DxgkDdiRecommendVidPnTopology = NULL;
    InitialData.DxgkDdiQueryVidPnHWCapability = NULL;
    InitialData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2 = NULL;
    DbgBreakPoint();
    /* Since we can accelerate we use DxgkInitialize (KMDODs use a different initialize Displib function */
    NTSTATUS Status = DxgkInitialize(DriverObject, RegistryPath, &InitialData);

    if (!NT_SUCCESS(Status))
    {
        MaliDbgPrint("DxgkInitialize failed with Status: 0x%X\n", Status);
    }

    MaliDbgPrint("<-- DriverEntry exit\n");
    return Status;
}