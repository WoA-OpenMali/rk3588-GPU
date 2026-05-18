
#include "WinMaliKmd.h"
#include "WinMaliDxgkInitFill.h"

#if 0
typedef struct _DRIVER_INITIALIZATION_DATA {
    ULONG                                   Version;
    PDXGKDDI_ADD_DEVICE                     DxgkDdiAddDevice;
    PDXGKDDI_START_DEVICE                   DxgkDdiStartDevice;
    PDXGKDDI_STOP_DEVICE                    DxgkDdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE                  DxgkDdiRemoveDevice;
    PDXGKDDI_DISPATCH_IO_REQUEST            DxgkDdiDispatchIoRequest;
    PDXGKDDI_INTERRUPT_ROUTINE              DxgkDdiInterruptRoutine;
    PDXGKDDI_DPC_ROUTINE                    DxgkDdiDpcRoutine;
    PDXGKDDI_QUERY_CHILD_RELATIONS          DxgkDdiQueryChildRelations;
    PDXGKDDI_QUERY_CHILD_STATUS             DxgkDdiQueryChildStatus;
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR        DxgkDdiQueryDeviceDescriptor;
    PDXGKDDI_SET_POWER_STATE                DxgkDdiSetPowerState;
    PDXGKDDI_NOTIFY_ACPI_EVENT              DxgkDdiNotifyAcpiEvent;
    PDXGKDDI_RESET_DEVICE                   DxgkDdiResetDevice;
    PDXGKDDI_UNLOAD                         DxgkDdiUnload;
    PDXGKDDI_QUERY_INTERFACE                DxgkDdiQueryInterface;
    PDXGKDDI_CONTROL_ETW_LOGGING            DxgkDdiControlEtwLogging;
    PDXGKDDI_QUERYADAPTERINFO               DxgkDdiQueryAdapterInfo;
    PDXGKDDI_CREATEDEVICE                   DxgkDdiCreateDevice;
    PDXGKDDI_CREATEALLOCATION               DxgkDdiCreateAllocation;
    PDXGKDDI_DESTROYALLOCATION              DxgkDdiDestroyAllocation;
    PDXGKDDI_DESCRIBEALLOCATION             DxgkDdiDescribeAllocation;
    PDXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA DxgkDdiGetStandardAllocationDriverData;
    PDXGKDDI_ACQUIRESWIZZLINGRANGE          DxgkDdiAcquireSwizzlingRange;
    PDXGKDDI_RELEASESWIZZLINGRANGE          DxgkDdiReleaseSwizzlingRange;
    PDXGKDDI_PATCH                          DxgkDdiPatch;
    PDXGKDDI_SUBMITCOMMAND                  DxgkDdiSubmitCommand;
    PDXGKDDI_PREEMPTCOMMAND                 DxgkDdiPreemptCommand;
    PDXGKDDI_BUILDPAGINGBUFFER              DxgkDdiBuildPagingBuffer;
    PDXGKDDI_SETPALETTE                     DxgkDdiSetPalette;
    PDXGKDDI_SETPOINTERPOSITION             DxgkDdiSetPointerPosition;
    PDXGKDDI_SETPOINTERSHAPE                DxgkDdiSetPointerShape;
    PDXGKDDI_RESETFROMTIMEOUT               DxgkDdiResetFromTimeout;
    PDXGKDDI_RESTARTFROMTIMEOUT             DxgkDdiRestartFromTimeout;
    PDXGKDDI_ESCAPE                         DxgkDdiEscape;
    PDXGKDDI_COLLECTDBGINFO                 DxgkDdiCollectDbgInfo;
    PDXGKDDI_QUERYCURRENTFENCE              DxgkDdiQueryCurrentFence;
    PDXGKDDI_ISSUPPORTEDVIDPN               DxgkDdiIsSupportedVidPn;
    PDXGKDDI_RECOMMENDFUNCTIONALVIDPN       DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUMVIDPNCOFUNCMODALITY        DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_SETVIDPNSOURCEADDRESS          DxgkDdiSetVidPnSourceAddress;
    PDXGKDDI_SETVIDPNSOURCEVISIBILITY       DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_COMMITVIDPN                    DxgkDdiCommitVidPn;
    PDXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH   DxgkDdiUpdateActiveVidPnPresentPath;
    PDXGKDDI_RECOMMENDMONITORMODES          DxgkDdiRecommendMonitorModes;
    PDXGKDDI_RECOMMENDVIDPNTOPOLOGY         DxgkDdiRecommendVidPnTopology;
    PDXGKDDI_GETSCANLINE                    DxgkDdiGetScanLine;
    PDXGKDDI_STOPCAPTURE                    DxgkDdiStopCapture;
    PDXGKDDI_CONTROLINTERRUPT               DxgkDdiControlInterrupt;
    PDXGKDDI_CREATEOVERLAY                  DxgkDdiCreateOverlay;
    PDXGKDDI_DESTROYDEVICE                  DxgkDdiDestroyDevice;
    PDXGKDDI_OPENALLOCATIONINFO             DxgkDdiOpenAllocation;
    PDXGKDDI_CLOSEALLOCATION                DxgkDdiCloseAllocation;
    PDXGKDDI_RENDER                         DxgkDdiRender;
    PDXGKDDI_PRESENT                        DxgkDdiPresent;
    PDXGKDDI_UPDATEOVERLAY                  DxgkDdiUpdateOverlay;
    PDXGKDDI_FLIPOVERLAY                    DxgkDdiFlipOverlay;
    PDXGKDDI_DESTROYOVERLAY                 DxgkDdiDestroyOverlay;
    PDXGKDDI_CREATECONTEXT                  DxgkDdiCreateContext;
    PDXGKDDI_DESTROYCONTEXT                 DxgkDdiDestroyContext;
    PDXGKDDI_LINK_DEVICE                    DxgkDdiLinkDevice;
    PDXGKDDI_SETDISPLAYPRIVATEDRIVERFORMAT  DxgkDdiSetDisplayPrivateDriverFormat;
    PVOID                                   DxgkDdiDescribePageTable;
    PVOID                                   DxgkDdiUpdatePageTable;
    PVOID                                   DxgkDdiUpdatePageDirectory;
    PVOID                                   DxgkDdiMovePageDirectory;
    PVOID                                   DxgkDdiSubmitRender;
    PVOID                                   DxgkDdiCreateAllocation2;
    PDXGKDDI_RENDER                         DxgkDdiRenderKm;
    PDXGKDDI_QUERYVIDPNHWCAPABILITY         DxgkDdiQueryVidPnHWCapability;
    PDXGKDDISETPOWERCOMPONENTFSTATE         DxgkDdiSetPowerComponentFState;
    PDXGKDDI_QUERYDEPENDENTENGINEGROUP      DxgkDdiQueryDependentEngineGroup;
    PDXGKDDI_QUERYENGINESTATUS              DxgkDdiQueryEngineStatus;
    PDXGKDDI_RESETENGINE                    DxgkDdiResetEngine;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE          DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE           DxgkDdiSystemDisplayWrite;
    PDXGKDDI_CANCELCOMMAND                  DxgkDdiCancelCommand;
    PDXGKDDI_GET_CHILD_CONTAINER_ID         DxgkDdiGetChildContainerId;
    PDXGKDDIPOWERRUNTIMECONTROLREQUEST      DxgkDdiPowerRuntimeControlRequest;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
    PDXGKDDI_NOTIFY_SURPRISE_REMOVAL        DxgkDdiNotifySurpriseRemoval;
    PDXGKDDI_GETNODEMETADATA                DxgkDdiGetNodeMetadata;
    PDXGKDDISETPOWERPSTATE                  DxgkDdiSetPowerPState;
    PDXGKDDI_CONTROLINTERRUPT2              DxgkDdiControlInterrupt2;
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT  DxgkDdiCheckMultiPlaneOverlaySupport;
    PDXGKDDI_CALIBRATEGPUCLOCK              DxgkDdiCalibrateGpuClock;
    PDXGKDDI_FORMATHISTORYBUFFER            DxgkDdiFormatHistoryBuffer;
    PDXGKDDI_RENDERGDI                      DxgkDdiRenderGdi; 
    PDXGKDDI_SUBMITCOMMANDVIRTUAL           DxgkDdiSubmitCommandVirtual; 
    PDXGKDDI_SETROOTPAGETABLE               DxgkDdiSetRootPageTable; 
    PDXGKDDI_GETROOTPAGETABLESIZE           DxgkDdiGetRootPageTableSize; 
    PDXGKDDI_MAPCPUHOSTAPERTURE             DxgkDdiMapCpuHostAperture; 
    PDXGKDDI_UNMAPCPUHOSTAPERTURE           DxgkDdiUnmapCpuHostAperture; 
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2 DxgkDdiCheckMultiPlaneOverlaySupport2;
    PDXGKDDI_CREATEPROCESS                  DxgkDdiCreateProcess;
    PDXGKDDI_DESTROYPROCESS                 DxgkDdiDestroyProcess;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2    DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
    PDXGKDDI_POWERRUNTIMESETDEVICEHANDLE    DxgkDdiPowerRuntimeSetDeviceHandle;
    PDXGKDDI_SETSTABLEPOWERSTATE            DxgkDdiSetStablePowerState;
    PDXGKDDI_SETVIDEOPROTECTEDREGION        DxgkDdiSetVideoProtectedRegion;
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3 DxgkDdiCheckMultiPlaneOverlaySupport3;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3    DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3;
    PDXGKDDI_POSTMULTIPLANEOVERLAYPRESENT   DxgkDdiPostMultiPlaneOverlayPresent;
    PDXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY       DxgkDdiValidateUpdateAllocationProperty;
    PDXGKDDI_CONTROLMODEBEHAVIOR            DxgkDdiControlModeBehavior;
    PDXGKDDI_UPDATEMONITORLINKINFO          DxgkDdiUpdateMonitorLinkInfo;
    PDXGKDDI_CREATEHWCONTEXT                DxgkDdiCreateHwContext;
    PDXGKDDI_DESTROYHWCONTEXT               DxgkDdiDestroyHwContext;
    PDXGKDDI_CREATEHWQUEUE                  DxgkDdiCreateHwQueue;
    PDXGKDDI_DESTROYHWQUEUE                 DxgkDdiDestroyHwQueue;
    PDXGKDDI_SUBMITCOMMANDTOHWQUEUE         DxgkDdiSubmitCommandToHwQueue;
    PDXGKDDI_SWITCHTOHWCONTEXTLIST          DxgkDdiSwitchToHwContextList;
    PDXGKDDI_RESETHWENGINE                  DxgkDdiResetHwEngine;
    PDXGKDDI_CREATEPERIODICFRAMENOTIFICATION    DxgkDdiCreatePeriodicFrameNotification;
    PDXGKDDI_DESTROYPERIODICFRAMENOTIFICATION   DxgkDdiDestroyPeriodicFrameNotification;
    PDXGKDDI_SETTIMINGSFROMVIDPN            DxgkDdiSetTimingsFromVidPn;
    PDXGKDDI_SETTARGETGAMMA                 DxgkDdiSetTargetGamma;
    PDXGKDDI_SETTARGETCONTENTTYPE           DxgkDdiSetTargetContentType;
    PDXGKDDI_SETTARGETANALOGCOPYPROTECTION  DxgkDdiSetTargetAnalogCopyProtection;
    PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY   DxgkDdiSetTargetAdjustedColorimetry;
    PDXGKDDI_DISPLAYDETECTCONTROL           DxgkDdiDisplayDetectControl;
    PDXGKDDI_QUERYCONNECTIONCHANGE          DxgkDdiQueryConnectionChange;
    PDXGKDDI_EXCHANGEPRESTARTINFO           DxgkDdiExchangePreStartInfo;
    PDXGKDDI_GETMULTIPLANEOVERLAYCAPS       DxgkDdiGetMultiPlaneOverlayCaps;
    PDXGKDDI_GETPOSTCOMPOSITIONCAPS         DxgkDdiGetPostCompositionCaps;
    PDXGKDDI_UPDATEHWCONTEXTSTATE           DxgkDdiUpdateHwContextState;
    PDXGKDDI_CREATEPROTECTEDSESSION         DxgkDdiCreateProtectedSession;
    PDXGKDDI_DESTROYPROTECTEDSESSION        DxgkDdiDestroyProtectedSession;
    PDXGKDDI_SETSCHEDULINGLOGBUFFER         DxgkDdiSetSchedulingLogBuffer;
    PDXGKDDI_SETUPPRIORITYBANDS             DxgkDdiSetupPriorityBands;
    PDXGKDDI_NOTIFYFOCUSPRESENT             DxgkDdiNotifyFocusPresent;
    PDXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES DxgkDdiSetContextSchedulingProperties;
    PDXGKDDI_SUSPENDCONTEXT                 DxgkDdiSuspendContext;
    PDXGKDDI_RESUMECONTEXT                  DxgkDdiResumeContext;
    PDXGKDDI_SETVIRTUALMACHINEDATA          DxgkDdiSetVirtualMachineData;
    PDXGKDDI_BEGINEXCLUSIVEACCESS           DxgkDdiBeginExclusiveAccess;
    PDXGKDDI_ENDEXCLUSIVEACCESS             DxgkDdiEndExclusiveAccess;
    PDXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT    DxgkDdiQueryDiagnosticTypesSupport;
    PDXGKDDI_CONTROLDIAGNOSTICREPORTING     DxgkDdiControlDiagnosticReporting;
    PDXGKDDI_RESUMEHWENGINE                 DxgkDdiResumeHwEngine;
    PDXGKDDI_SIGNALMONITOREDFENCE           DxgkDdiSignalMonitoredFence;
    PDXGKDDI_PRESENTTOHWQUEUE               DxgkDdiPresentToHwQueue;
    PDXGKDDI_VALIDATESUBMITCOMMAND          DxgkDdiValidateSubmitCommand;
    PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2  DxgkDdiSetTargetAdjustedColorimetry2;
    PDXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL   DxgkDdiSetTrackedWorkloadPowerLevel;
} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;
#endif

VOID
WinMaliDxgkPatchInitializationData(_Out_ DRIVER_INITIALIZATION_DATA* init)
{
    RtlZeroMemory(init, sizeof(*init));

    init->Version = DXGKDDI_INTERFACE_VERSION;

    /* Lifecycle */
    init->DxgkDdiAddDevice              = WinMaliKmdAddDevice;
    init->DxgkDdiStartDevice            = WinMaliKmdStartDevice;
    init->DxgkDdiStopDevice             = WinMaliKmdStopDevice;
    init->DxgkDdiRemoveDevice           = WinMaliKmdRemoveDevice;
    init->DxgkDdiDispatchIoRequest      = WinMaliKmdDispatchIoRequest;
    init->DxgkDdiInterruptRoutine       = WinMaliKmdInterruptRoutine;
    init->DxgkDdiDpcRoutine             = WinMaliKmdDpcRoutine;

    /* Enumeration (render-only: zero children, zero VidPN sources) */
    init->DxgkDdiQueryChildRelations    = WinMaliKmdQueryChildRelations;
    init->DxgkDdiQueryChildStatus       = WinMaliKmdQueryChildStatus;
    init->DxgkDdiQueryDeviceDescriptor  = WinMaliKmdQueryDeviceDescriptor;

    /* Power / ACPI / reset / iface */
    init->DxgkDdiSetPowerState          = WinMaliKmdSetPowerState;
    init->DxgkDdiNotifyAcpiEvent        = WinMaliKmdNotifyAcpiEvent;
    init->DxgkDdiResetDevice            = WinMaliKmdResetDevice;
    init->DxgkDdiQueryInterface         = WinMaliKmdQueryInterface;

    /* ETW + adapter info */
    init->DxgkDdiControlEtwLogging      = WinMaliKmdControlEtwLogging;
    init->DxgkDdiQueryAdapterInfo       = WinMaliKmdQueryAdapterInfo;
}
