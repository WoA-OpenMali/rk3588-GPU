#pragma once
#include "WinMaliKmd.h"

NTSTATUS APIENTRY WinMaliKmdStub_AcquireSwizzlingRange(
    IN_CONST_HANDLE                         hAdapter,
    INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE    pAcquireSwizzlingRange
    );

NTSTATUS APIENTRY WinMaliKmdStub_ReleaseSwizzlingRange(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_RELEASESWIZZLINGRANGE     pReleaseSwizzlingRange
    );

NTSTATUS APIENTRY WinMaliKmdStub_Patch(
    IN_CONST_HANDLE             hAdapter,
    IN_CONST_PDXGKARG_PATCH     pPatch
    );

NTSTATUS APIENTRY WinMaliKmdStub_PreemptCommand(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND    pPreemptCommand
    );

NTSTATUS APIENTRY WinMaliKmdStub_Render(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_RENDER   pRender
    );

NTSTATUS APIENTRY WinMaliKmdStub_Present(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_PRESENT  pPresent
    );

NTSTATUS APIENTRY WinMaliKmdStub_RenderKm(
    IN_CONST_HANDLE         hContext,
    INOUT_PDXGKARG_RENDER   pRender
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetPowerComponentFState(
    IN_CONST_HANDLE DriverContext,
    UINT            ComponentIndex,
    UINT            FState
    );

NTSTATUS APIENTRY WinMaliKmdStub_QueryDependentEngineGroup(
    IN_CONST_HANDLE                             hAdapter,
    INOUT_DXGKARG_QUERYDEPENDENTENGINEGROUP     pQueryDependentEngineGroup
    );

NTSTATUS APIENTRY WinMaliKmdStub_QueryEngineStatus(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS    pQueryEngineStatus
    );

NTSTATUS APIENTRY WinMaliKmdStub_ResetEngine(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_RESETENGINE  pResetEngine
    );

NTSTATUS APIENTRY WinMaliKmdStub_CancelCommand(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_CANCELCOMMAND pCancelCommand
    );

NTSTATUS APIENTRY WinMaliKmdStub_PowerRuntimeControlRequest(
    IN_CONST_HANDLE DriverContext,
    IN              LPCGUID PowerControlCode,
    IN OPTIONAL     PVOID InBuffer,
    IN              SIZE_T InBufferSize,
    OUT OPTIONAL    PVOID OutBuffer,
    IN              SIZE_T OutBufferSize,
    OUT OPTIONAL    PSIZE_T BytesReturned
    );

NTSTATUS APIENTRY WinMaliKmdStub_NotifySurpriseRemoval(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetPowerPState(
    IN_CONST_HANDLE DriverContext,
    UINT ComponentIndex,
    UINT RequestedComponentPState
    );

NTSTATUS APIENTRY WinMaliKmdStub_ControlInterrupt2(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2   InterruptControl
    );

NTSTATUS APIENTRY WinMaliKmdStub_CalibrateGpuClock(
    IN_CONST_HANDLE                             hAdapter,
    IN UINT32                                   NodeOrdinal,
    IN UINT32                                   EngineOrdinal,
    OUT_PDXGKARG_CALIBRATEGPUCLOCK	            pClockCalibration
    );

NTSTATUS APIENTRY WinMaliKmdStub_FormatHistoryBuffer(
    IN_CONST_HANDLE                 hContext,
    IN DXGKARG_FORMATHISTORYBUFFER* pFormatData
    );

NTSTATUS APIENTRY WinMaliKmdStub_RenderGdi(
    IN_CONST_HANDLE          hContext,
    INOUT_PDXGKARG_RENDERGDI pRenderGdi
    );


NTSTATUS APIENTRY WinMaliKmdStub_MapCpuHostAperture(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_UnmapCpuHostAperture(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE  pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_PowerRuntimeSetDeviceHandle(
    IN_CONST_HANDLE DriverContext,
    IN HANDLE PoDeviceHandle
    );

VOID APIENTRY WinMaliKmdStub_SetStablePowerState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE  pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetVideoProtectedRegion(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_ValidateUpdateAllocationProperty(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_VALIDATEUPDATEALLOCPROPERTY pValidateUpdateAllocProperty
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateHwContext(
    IN_CONST_HANDLE                 hDevice,
    INOUT_PDXGKARG_CREATEHWCONTEXT  pCreateContext
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyHwContext(
    IN_CONST_HANDLE     hHwContext
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateHwQueue(
    IN_CONST_HANDLE                 hHwContext,
    INOUT_PDXGKARG_CREATEHWQUEUE    pCreateHwQueue
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyHwQueue(
    IN_CONST_HANDLE     hHwQueue
    );

NTSTATUS APIENTRY WinMaliKmdStub_SubmitCommandToHwQueue(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE    pSubmitCommandToHwQueue
    );

NTSTATUS APIENTRY WinMaliKmdStub_SwitchToHwContextList(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SWITCHTOHWCONTEXTLIST pHwContextList
    );

NTSTATUS APIENTRY WinMaliKmdStub_ResetHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESETHWENGINE    pResetHwEngine
    );

NTSTATUS APIENTRY WinMaliKmdStub_UpdateHwContextState(
    IN_CONST_HANDLE                        hAdapter,
    IN_CONST_PDXGKARG_UPDATEHWCONTEXTSTATE pUpdateHwContextState
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateProtectedSession(
    IN_CONST_HANDLE                       hAdapter,
    INOUT_PDXGKARG_CREATEPROTECTEDSESSION pCreateProtectedSession
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyProtectedSession(
    IN_CONST_HANDLE                       hAdapter,
    IN_CONST_HANDLE                       hProtectedSession // in: Driver generated handle driver returned at DxgkDdiCreateProtectedSession.
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetSchedulingLogBuffer(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETSCHEDULINGLOGBUFFER    pSetSchedulingLogBuffer
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetupPriorityBands(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETUPPRIORITYBANDS    pSetupPriorityBands
    );

NTSTATUS APIENTRY WinMaliKmdStub_NotifyFocusPresent(
    IN_CONST_HANDLE                         hAdapter
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetContextSchedulingProperties(
    IN_CONST_HANDLE                                     hAdapter,
    IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES    pSetContextSchedulingProperties
    );

NTSTATUS APIENTRY WinMaliKmdStub_SuspendContext(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_SUSPENDCONTEXT    pSuspendContext
    );

NTSTATUS APIENTRY WinMaliKmdStub_ResumeContext(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_RESUMECONTEXT pResumeContext
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetVirtualMachineData(
    IN_CONST_HANDLE                         hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALMACHINEDATA Args
    );

NTSTATUS APIENTRY WinMaliKmdStub_BeginExclusiveAccess(
    IN_CONST_HANDLE                  hAdapter,
    IN_PDXGKARG_BEGINEXCLUSIVEACCESS pBeginExclusiveAccess
    );

NTSTATUS APIENTRY WinMaliKmdStub_EndExclusiveAccess(
    IN_CONST_HANDLE                hAdapter,
    IN_PDXGKARG_ENDEXCLUSIVEACCESS pEndExclusiveAccess
    );

NTSTATUS APIENTRY WinMaliKmdStub_QueryDiagnosticTypesSupport(
    IN_CONST_PVOID                              MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT  pArgQueryDiagnosticTypesSupport
    );

NTSTATUS APIENTRY WinMaliKmdStub_ControlDiagnosticReporting(
    IN_CONST_PVOID                          MiniportDeviceContext,
    IN_PDXGKARG_CONTROLDIAGNOSTICREPORTING  pArgControlDiagnosticReporting
    );

NTSTATUS APIENTRY WinMaliKmdStub_ResumeHwEngine(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_RESUMEHWENGINE   pResumeHwEngine
    );

NTSTATUS APIENTRY WinMaliKmdStub_SignalMonitoredFence(
    IN_CONST_HANDLE                     hContext,
    INOUT_PDXGKARG_SIGNALMONITOREDFENCE pSignalMonitoredFence
    );

NTSTATUS APIENTRY WinMaliKmdStub_PresentToHwQueue(
    IN_CONST_HANDLE         hHwQueue,
    INOUT_PDXGKARG_PRESENT  pPresent
    );

NTSTATUS APIENTRY WinMaliKmdStub_ValidateSubmitCommand(
    IN_CONST_HANDLE          hContext,
    INOUT_PDXGKARG_VALIDATESUBMITCOMMAND pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetTrackedWorkloadPowerLevel(
    IN_CONST_HANDLE                 hContext,
    INOUT_PDXGKARG_SETTRACKEDWORKLOADPOWERLEVEL   pTrackedWorkloadPowerLevel
    );

NTSTATUS APIENTRY WinMaliKmdStub_SaveMemoryForHotUpdate(
    IN_CONST_HANDLE                 hContext,
    IN_CONST_PDXGKARG_SAVEMEMORYFORHOTUPDATE pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_RestoreMemoryForHotUpdate(
    IN_CONST_HANDLE                             hContext,
    IN_CONST_PDXGKARG_RESTOREMEMORYFORHOTUPDATE pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_CollectDiagnosticInfo(
    IN_CONST_PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO pCollectDiagnosticInfo
    );

NTSTATUS APIENTRY WinMaliKmdStub_ControlInterrupt3(
    IN_CONST_HANDLE                      hAdapter,
    IN_CONST_PDXGKARG_CONTROLINTERRUPT3  InterruptControl
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetAllocationBackingStore(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SETALLOCATIONBACKINGSTORE pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateCpuEvent(
    IN_CONST_HANDLE             hAdapter,
    INOUT_PDXGKARG_CREATECPUEVENT pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyCpuEvent(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdCpuEvent
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateNativeFence(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_CREATENATIVEFENCE    pCreateNativeFence
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyNativeFence(
    INOUT_PDXGKARG_DESTROYNATIVEFENCE pDestroyNativeFence
    );

NTSTATUS APIENTRY WinMaliKmdStub_UpdateMonitoredValues(
    IN_CONST_PDXGKARG_UPDATEMONITOREDVALUES pUpdateMonitoredValues
    );

NTSTATUS APIENTRY WinMaliKmdStub_UpdateCurrentValuesFromCpu(
    IN_CONST_PDXGKARG_UPDATECURRENTVALUESFROMCPU pUpdateCurrentValuesFromCpu
    );

NTSTATUS APIENTRY WinMaliKmdStub_CreateDoorbell(
    INOUT_PDXGKARG_CREATEDOORBELL pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_ConnectDoorbell(
    INOUT_PDXGKARG_CONNECTDOORBELL pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_DisconnectDoorbell(
    INOUT_PDXGKARG_DISCONNECTDOORBELL pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyDoorbell(
    INOUT_PDXGKARG_DESTROYDOORBELL pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_NotifyWorkSubmission(
    INOUT_PDXGKARG_NOTIFYWORKSUBMISSION pArgs
    );

HANDLE APIENTRY WinMaliKmdStub_CreateMemoryBasis(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_CREATEMEMORYBASIS pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_DestroyMemoryBasis(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis
    );

NTSTATUS APIENTRY WinMaliKmdStub_StartDirtyTracking(
    IN_CONST_HANDLE  hAdapter,
    IN_CONST_HANDLE  hMemoryBasis
    );

NTSTATUS APIENTRY WinMaliKmdStub_StopDirtyTracking(
    IN_CONST_HANDLE  hAdapter,
    IN_CONST_HANDLE  hMemoryBasis
    );

NTSTATUS APIENTRY WinMaliKmdStub_QueryDirtyBitData(
    IN_CONST_HANDLE                     hAdapter,
    INOUT_PDXGKARG_QUERYDIRTYBITDATA    pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_PrepareLiveMigration(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_PREPARE_LIVE_MIGRATION   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SaveImmutableMigrationData(
    IN_CONST_HANDLE                                     hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SaveMutableMigrationData(
    IN_CONST_HANDLE                                    hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA    pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_EndLiveMigration(
    IN_CONST_HANDLE hAdapter,
    UINT            vfIndex
    );

NTSTATUS APIENTRY WinMaliKmdStub_RestoreImmutableMigrationData(
    IN_CONST_HANDLE                                         hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_RestoreMutableMigrationData(
    IN_CONST_HANDLE                                         hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_WriteVirtualizedInterrupt(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetVirtualGpuResources2(
    IN_CONST_HANDLE                             hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALGPURESOURCES2   pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetVirtualFunctionPauseState(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALFUNCTIONPAUSESTATE  pArgs
    );

NTSTATUS APIENTRY WinMaliKmdStub_OpenNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_OPENNATIVEFENCE    pOpenNativeFence
    );

NTSTATUS APIENTRY WinMaliKmdStub_CloseNativeFence(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_CLOSENATIVEFENCE   pCloseNativeFence
    );

NTSTATUS APIENTRY WinMaliKmdStub_SetNativeFenceLogBuffer(
    IN_CONST_PDXGKARG_SETNATIVEFENCELOGBUFFER pSetNativeFenceLogBuffer
    );

NTSTATUS APIENTRY WinMaliKmdStub_UpdateNativeFenceLogs(
    IN_CONST_PDXGKARG_UPDATENATIVEFENCELOGS pUpdateNativeFenceLog
    );

NTSTATUS APIENTRY WinMaliKmdStub_CollectDbgInfo2(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_COLLECTDBGINFO2  pCollectDbgInfo2
    );

NTSTATUS APIENTRY WinMaliKmdStub_NotifyContextPriorityChange(
    IN_CONST_HANDLE                                 hAdapter,
    IN_CONST_PDXGKARG_NOTIFYCONTEXTPRIORITYCHANGE   pNotifyContextPriorityChange
    );
