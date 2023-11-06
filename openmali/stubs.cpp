/*
 * PROJECT:     OpenMali Render only display driver
 * PURPOSE:     TODO Functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include "openmali.hpp"

NTSTATUS
OpenMaliDispatchIoRequest(IN_CONST_PVOID              MiniportDeviceContext,
                          IN_ULONG                    VidPnSourceId,
                          IN_PVIDEO_REQUEST_PACKET    VideoRequestPacket)
{
    return 0;
}

BOOLEAN
OpenMaliInterruptRoutine(IN_CONST_PVOID  MiniportDeviceContext,
                         IN_ULONG        MessageNumber)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}

VOID
OpenMaliDpcRoutine(IN_CONST_PVOID  MiniportDeviceContext)
{
    DbgPrint("--> Entry <--\n");
}

NTSTATUS
OpenMaliQueryChildRelations(IN_CONST_PVOID                                                    MiniportDeviceContext,
                            _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR  ChildRelations,
                            _In_ ULONG                                                        ChildRelationsSize)
{
        DbgPrint("--> Entry <--\n");
    return 0;
}

NTSTATUS
OpenMaliQueryChildStatus(IN_CONST_PVOID              MiniportDeviceContext,
                         INOUT_PDXGK_CHILD_STATUS    ChildStatus,
                         IN_BOOLEAN                  NonDestructiveOnly)
{
        DbgPrint("--> Entry <--\n");
    return 0;
}

NTSTATUS
OpenMaliQueryDeviceDescriptor(IN_CONST_PVOID                  MiniportDeviceContext,
                              IN_ULONG                        ChildUid,
                              INOUT_PDXGK_DEVICE_DESCRIPTOR   DeviceDescriptor)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}

NTSTATUS
OpenMaliSetPowerState(IN_CONST_PVOID          MiniportDeviceContext,
                      IN_ULONG                DeviceUid,
                      IN_DEVICE_POWER_STATE   DevicePowerState,
                      IN_POWER_ACTION         ActionType)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}

NTSTATUS
OpenMaliNotifyAcpiEvent(IN_CONST_PVOID      MiniportDeviceContext,
                        IN_DXGK_EVENT_TYPE  EventType,
                        IN_ULONG            Event,
                        IN_PVOID            Argument,
                        OUT_PULONG          AcpiFlags)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}

VOID
OpenMaliResetDevice(IN_CONST_PVOID  MiniportDeviceContext)
{
    DbgPrint("--> Entry <--\n");
}

VOID
OpenMaliUnload(VOID)
{
    DbgPrint("--> Entry <--\n");
}

NTSTATUS
OpenMaliQueryInterface(IN_CONST_PVOID          MiniportDeviceContext,
                       IN_PQUERY_INTERFACE     QueryInterface)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}

VOID
OpenMaliControlEtwLogging(IN_BOOLEAN  Enable,
                          IN_ULONG    Flags,
                          IN_UCHAR    Level)
{
    DbgPrint("--> Entry <--\n");
}

NTSTATUS
APIENTRY
OpenMaliQueryAdapterInfo(IN_CONST_HANDLE                         hAdapter,
                         IN_CONST_PDXGKARG_QUERYADAPTERINFO      pQueryAdapterInfo)
{
    DbgPrint("--> Entry <--\n");
    return 0;
}
