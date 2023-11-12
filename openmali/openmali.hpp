/*
 * PROJECT:     OpenMali Render only display driver
 * PURPOSE:     Primary Header
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#pragma once

extern "C"
{
    #define __CPLUSPLUS

    #include <stddef.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>

    #include <initguid.h>
    #include <ntddk.h>
    #include <windef.h>
    #include <winerror.h>
    #include <wingdi.h>
    #include <winddi.h>
    #include <ntddvdeo.h>

    #include <d3dkmddi.h>
    #include <d3dkmthk.h>

    #include <dispmprt.h>
};

#include "hw/device/csf/regs.h"
#include "debug.h"
#include "mali.hpp"
#define MALITAG 'ILAM'

#define DRM_PANTHOR_ARCH_MAJOR(x)              ((x) >> 28)
#define DRM_PANTHOR_ARCH_MINOR(x)              (((x) >> 24) & 0xf)
#define DRM_PANTHOR_ARCH_REV(x)                        (((x) >> 20) & 0xf)
#define DRM_PANTHOR_PRODUCT_MAJOR(x)           (((x) >> 16) & 0xf)
#define DRM_PANTHOR_VERSION_MAJOR(x)           (((x) >> 12) & 0xf)
#define DRM_PANTHOR_VERSION_MINOR(x)           (((x) >> 4) & 0xff)
#define DRM_PANTHOR_VERSION_STATUS(x)          ((x) & 0xf)
#define DRM_PANTHOR_CSHW_MAJOR(x)              (((x) >> 26) & 0x3f)
#define DRM_PANTHOR_CSHW_MINOR(x)              (((x) >> 20) & 0x3f)
#define DRM_PANTHOR_CSHW_REV(x)                        (((x) >> 16) & 0xf)
#define DRM_PANTHOR_MCU_MAJOR(x)               (((x) >> 10) & 0x3f)
#define DRM_PANTHOR_MCU_MINOR(x)               (((x) >> 4) & 0x3f)
#define DRM_PANTHOR_MCU_REV(x)                 ((x) & 0xf)
#define DRM_PANTHOR_MMU_VA_BITS(x)             ((x) & 0xff)

typedef struct _HW_GPU_INFO {
       /* GPU ID. */
       UINT32 gpu_id;

       /* GPU revision. */
       UINT32 gpu_rev;

       /* Command stream frontend ID. */
       UINT32 csf_id;

       /* L2-cache features. */
       UINT32 l2_features;

       /* Tiler features. */
       UINT32 tiler_features;

       /* Memory features. */
       UINT32 mem_features;

       /* MMU features. */
       UINT32 mmu_features;

       /* Thread features. */
       UINT32 thread_features;
       UINT32 max_threads;
       UINT32 thread_max_workgroup_size;

       /**
        * Maximum number of threads that can wait
        * simultaneously on a barrier.
        */
       UINT32 thread_max_barrier_size;

       /* Coherency features. */
       UINT32 coherency_features;

       /* Texture features. */
       UINT32 texture_features[4];

       /*  Bitmask encoding the number of address-space exposed by the MMU. */
       UINT32 as_present;
       UINT32 core_group_count;

       /* Zero on return. */
       UINT32 pad;

       /* @shader_present: Bitmask encoding the shader cores exposed by the
GPU */
       UINT64 shader_present;

      /** @l2_present: Bitmask encoding the L2 caches exposed by the GPU. */
       UINT64 l2_present;
       UINT64 tiler_present;
} HW_GPU_INFO, *PHW_GPU_INFO;

/* Dxgkrnl Callbacks */

NTSTATUS
OpenMaliAddDevice(IN_CONST_PDEVICE_OBJECT     PhysicalDeviceObject,
                  OUT_PPVOID                  MiniportDeviceContext);
NTSTATUS
OpenMaliStartDevice(IN_CONST_PVOID          MiniportDeviceContext,
                    IN_PDXGK_START_INFO     DxgkStartInfo,
                    IN_PDXGKRNL_INTERFACE   DxgkInterface,
                    OUT_PULONG              NumberOfVideoPresentSources,
                    OUT_PULONG              NumberOfChildren);
NTSTATUS
OpenMaliStopDevice(IN_CONST_PVOID  MiniportDeviceContext);

NTSTATUS
OpenMaliRemoveDevice(IN_CONST_PVOID  MiniportDeviceContext);

NTSTATUS
OpenMaliDispatchIoRequest(IN_CONST_PVOID              MiniportDeviceContext,
                          IN_ULONG                    VidPnSourceId,
                          IN_PVIDEO_REQUEST_PACKET    VideoRequestPacket);
BOOLEAN
OpenMaliInterruptRoutine(IN_CONST_PVOID  MiniportDeviceContext,
                         IN_ULONG        MessageNumber);

VOID
OpenMaliDpcRoutine(IN_CONST_PVOID  MiniportDeviceContext); 

NTSTATUS
OpenMaliQueryChildRelations(IN_CONST_PVOID                                                    MiniportDeviceContext,
                            _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR  ChildRelations,
                            _In_ ULONG                                                        ChildRelationsSize);

NTSTATUS
OpenMaliQueryChildStatus(IN_CONST_PVOID              MiniportDeviceContext,
                         INOUT_PDXGK_CHILD_STATUS    ChildStatus,
                         IN_BOOLEAN                  NonDestructiveOnly);

NTSTATUS
OpenMaliQueryDeviceDescriptor(IN_CONST_PVOID                  MiniportDeviceContext,
                              IN_ULONG                        ChildUid,
                              INOUT_PDXGK_DEVICE_DESCRIPTOR   DeviceDescriptor);

NTSTATUS
OpenMaliSetPowerState(IN_CONST_PVOID          MiniportDeviceContext,
                      IN_ULONG                DeviceUid,
                      IN_DEVICE_POWER_STATE   DevicePowerState,
                      IN_POWER_ACTION         ActionType);

NTSTATUS
OpenMaliNotifyAcpiEvent(IN_CONST_PVOID      MiniportDeviceContext,
                        IN_DXGK_EVENT_TYPE  EventType,
                        IN_ULONG            Event,
                        IN_PVOID            Argument,
                        OUT_PULONG          AcpiFlags);

VOID
OpenMaliResetDevice(IN_CONST_PVOID  MiniportDeviceContext);

VOID
OpenMaliUnload(VOID);

NTSTATUS
OpenMaliQueryInterface(IN_CONST_PVOID          MiniportDeviceContext,
                       IN_PQUERY_INTERFACE     QueryInterface);

VOID
OpenMaliControlEtwLogging(IN_BOOLEAN  Enable,
                          IN_ULONG    Flags,
                          IN_UCHAR    Level);

NTSTATUS
APIENTRY
OpenMaliQueryAdapterInfo(IN_CONST_HANDLE                         hAdapter,
                         IN_CONST_PDXGKARG_QUERYADAPTERINFO      pQueryAdapterInfo);
/* Utils */
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType = PagedPool);
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType = PagedPool);
void  __cdecl operator delete(void* pObject);
void  __cdecl operator delete(void* pObject, size_t s);
void  __cdecl operator delete[](void* pObject);