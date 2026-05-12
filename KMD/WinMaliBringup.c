#include "WinMaliKmd.h"

static ULONGLONG
WinMaliResMemoryLength_(_In_ const CM_PARTIAL_RESOURCE_DESCRIPTOR* Desc)
{
    switch (Desc->Type) {
        case CmResourceTypeMemory:
            return Desc->u.Memory.Length;
        case CmResourceTypeMemoryLarge:
            if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_40) {
                return (ULONGLONG)Desc->u.Memory40.Length40 << 8;
            }
            if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_48) {
                return (ULONGLONG)Desc->u.Memory48.Length48 << 16;
            }
            if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_64) {
                return (ULONGLONG)Desc->u.Memory64.Length64 << 32;
            }
            return 0;
        default:
            return 0;
    }
}

static PHYSICAL_ADDRESS
WinMaliResMemoryStart_(_In_ const CM_PARTIAL_RESOURCE_DESCRIPTOR* Desc)
{
    PHYSICAL_ADDRESS pa = { 0 };
    switch (Desc->Type) {
        case CmResourceTypeMemory:
            pa = Desc->u.Memory.Start;
            break;
        case CmResourceTypeMemoryLarge:
            // All three Large variants share the Start field layout -
            // the _only_ difference from plain Memory is how Length is
            // stored. The "Start" member aliases across the subtypes.
            pa = Desc->u.Memory40.Start;
            break;
        default:
            break;
    }
    return pa;
}

NTSTATUS
WinMaliParseResources(_Inout_ PWINMALI_ADAPTER Adapter)
{
    NTSTATUS       status;
    DXGK_DEVICE_INFO info;
    const CM_RESOURCE_LIST* list;
    ULONG          iList, iDesc;
    ULONG          memCount = 0, irqCount = 0;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&info, sizeof(info));

    if (Adapter->DxgkInterface.DxgkCbGetDeviceInformation == NULL
     || Adapter->DxgkHandle == NULL) {
        WINMALI_ERROR("no DxgkCbGetDeviceInformation callback (hdl=%p cb=%p)",
                      Adapter->DxgkHandle,
                      Adapter->DxgkInterface.DxgkCbGetDeviceInformation);
        return STATUS_DEVICE_NOT_READY;
    }

    status = Adapter->DxgkInterface.DxgkCbGetDeviceInformation(
        Adapter->DxgkHandle, &info);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("DxgkCbGetDeviceInformation failed 0x%08x", status);
        return status;
    }

    Adapter->DeviceInfo = info;
    list = info.TranslatedResourceList;
    if (list == NULL) {
        WINMALI_ERROR("TranslatedResourceList is NULL");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WINMALI_TRACE("TranslatedResourceList Count=%u", list->Count);

    for (iList = 0; iList < list->Count; ++iList) {
        const CM_FULL_RESOURCE_DESCRIPTOR* full =
            (const CM_FULL_RESOURCE_DESCRIPTOR*)
                ((const UCHAR*)list->List + (iList * sizeof(*full)));

        (void)full;
        break;
    }


    {
        const CM_FULL_RESOURCE_DESCRIPTOR* full = &list->List[0];
        const CM_PARTIAL_RESOURCE_LIST*    prl  = &full->PartialResourceList;

        WINMALI_TRACE("PartialResourceList Version=%u Revision=%u Count=%u",
                      prl->Version, prl->Revision, prl->Count);

        for (iDesc = 0; iDesc < prl->Count; ++iDesc) {
            const CM_PARTIAL_RESOURCE_DESCRIPTOR* desc =
                &prl->PartialDescriptors[iDesc];

            switch (desc->Type) {

            case CmResourceTypeMemory:
            case CmResourceTypeMemoryLarge: {
                const PHYSICAL_ADDRESS start = WinMaliResMemoryStart_(desc);
                const ULONGLONG        len   = WinMaliResMemoryLength_(desc);

                WINMALI_TRACE("  [%u] Memory phys=0x%llx len=0x%llx flags=0x%x",
                              iDesc, start.QuadPart, len, desc->Flags);

                if (memCount == 0) {
                    // First memory resource = GPU register block. On RK3588
                    // it should be {0xFB00_0000, 2 MiB}.
                    if (len == 0 || len > 0x01000000ULL /*16 MiB sanity*/) {
                        WINMALI_WARN("MMIO length 0x%llx looks wrong, bailing",
                                     len);
                        return STATUS_DEVICE_CONFIGURATION_ERROR;
                    }
                    Adapter->GpuRegsPhys = start;
                    Adapter->GpuRegsSize = (ULONG)len;
                }
                ++memCount;
                break;
            }

            case CmResourceTypeInterrupt: {
                WINMALI_TRACE("  [%u] Interrupt level=%u vector=%u affinity=0x%llx mode=%s flags=0x%x",
                              iDesc,
                              desc->u.Interrupt.Level,
                              desc->u.Interrupt.Vector,
                              (ULONGLONG)desc->u.Interrupt.Affinity,
                              (desc->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                                  ? "latched" : "levelsense",
                              desc->Flags);

                if (irqCount == 0) {
                    Adapter->GpuIrqLevel     = (KIRQL)desc->u.Interrupt.Level;
                    Adapter->GpuIrqVector    = desc->u.Interrupt.Vector;
                    Adapter->GpuIrqAffinity  = desc->u.Interrupt.Affinity;
                    Adapter->GpuIrqMode      =
                        (desc->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                            ? Latched : LevelSensitive;
                    Adapter->GpuIrqShareable =
                        (desc->ShareDisposition == CmResourceShareShared);
                }
                ++irqCount;
                break;
            }

            default:
                WINMALI_TRACE("  [%u] skip type=%u", iDesc, desc->Type);
                break;
            }
        }
    }

    if (memCount == 0) {
        WINMALI_ERROR("no Memory descriptor - cannot map GPU registers");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (irqCount == 0) {
        WINMALI_WARN("no Interrupt descriptor - running without IRQ");
    } else if (irqCount > 1u) {
        WINMALI_TRACE(
            "parsed: %u interrupt partials (first vec=%u level=%u saved for diag; "
            "Dxgk still binds ISR from full resource list)",
            irqCount,
            Adapter->GpuIrqVector,
            (ULONG)Adapter->GpuIrqLevel);
    }

    WINMALI_TRACE("parsed: MMIO phys=0x%llx size=0x%x IRQ vec=%u level=%u irq_partials=%u",
                  Adapter->GpuRegsPhys.QuadPart,
                  Adapter->GpuRegsSize,
                  Adapter->GpuIrqVector,
                  (ULONG)Adapter->GpuIrqLevel,
                  irqCount);

    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliBringupHardware(_Inout_ PWINMALI_ADAPTER Adapter)
{
    NTSTATUS status;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->GpuRegsSize == 0) {
        WINMALI_ERROR("no GPU MMIO range parsed");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (Adapter->GpuRegsVa != NULL) {
        return STATUS_SUCCESS;
    }

    Adapter->GpuRegsVa = MmMapIoSpaceEx(
        Adapter->GpuRegsPhys,
        Adapter->GpuRegsSize,
        PAGE_READWRITE | PAGE_NOCACHE);

    if (Adapter->GpuRegsVa == NULL) {
        WINMALI_ERROR("MmMapIoSpaceEx(0x%llx, 0x%x) returned NULL",
                      Adapter->GpuRegsPhys.QuadPart,
                      Adapter->GpuRegsSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Adapter->GpuRegsMapped = TRUE;

    WINMALI_TRACE("MMIO mapped: phys=0x%llx size=0x%x va=%p",
                  Adapter->GpuRegsPhys.QuadPart,
                  Adapter->GpuRegsSize,
                  Adapter->GpuRegsVa);

    status = WinMaliHwInitialize(&Adapter->Hw,
                                 Adapter->GpuRegsVa,
                                 Adapter->GpuRegsSize);
    if (!NT_SUCCESS(status)) {
        WINMALI_ERROR("WinMaliHwInitialize failed 0x%08x", status);
        return status;
    }

    status = WinMaliHwProbeIdentity(&Adapter->Hw);
    if (!NT_SUCCESS(status)) {
        // Could not read GPU_ID (e.g. power domain off, no HW). Keep
        // going so the escape channel still reports something useful.
        WINMALI_WARN("HwProbeIdentity failed 0x%08x - escape will report zeros",
                     status);
        return STATUS_SUCCESS;
    }

    // Strictly speaking the firmware and MMU bring-up will care about
    // arch v10 specifically,
    // the same KMD is expected to run on Opi5+ (G610),
    // in the future or something like mediatek?
    if (WinMaliHwIsMaliG610(&Adapter->Hw)) {
        WINMALI_TRACE("identified product: %s (match)",
                      WinMaliHwProductName(&Adapter->Hw));
    } else {
        WINMALI_WARN("unexpected product %s (arch=%u prod=%u) - "
                     "continuing in compatibility mode",
                     WinMaliHwProductName(&Adapter->Hw),
                     Adapter->Hw.ArchMajor,
                     Adapter->Hw.ProdMajor);
    }

    //
    // Best-effort VOP2 bring-up: maps the VOP2 + GRF MMIO ranges and dumps
    // the current VP/HDMI state so we can confirm UEFI handed off the
    // expected mode. Failure is non-fatal (Vop2.Initialized stays FALSE);
    // the GPU side of the driver still works and the GOP capture path keeps
    // owning the screen until phase 2c hooks CommitVidPn into VOP2.
    //
    {
        NTSTATUS vopStatus = WinMaliVop2Initialize(&Adapter->Vop2);
        if (!NT_SUCCESS(vopStatus)) {
            WINMALI_WARN("WinMaliVop2Initialize returned 0x%08x - "
                         "VOP2 path disabled, falling back to GOP scan-out",
                         vopStatus);
        } else if (!Adapter->Vop2.Initialized) {
            WINMALI_WARN("VOP2 initialize succeeded but main MMIO did not "
                         "map - VOP2 path disabled");
        } else {
            WINMALI_TRACE("VOP2 ready: VP%u driving HDMI0 at %ux%u",
                          Adapter->Vop2.ActiveVpId,
                          Adapter->Vop2.ActiveHActive,
                          Adapter->Vop2.ActiveVActive);
        }
    }

    return STATUS_SUCCESS;
}

VOID
WinMaliTeardownHardware(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) return;

    //
    // Free the scan-out sysmem slab (after optionally restoring YRGB_MST
    // to the GOP framebuffer) before unmapping VOP2 MMIO.
    //
    WinMaliVop2TeardownScanoutSegment(Adapter);

    //
    // Drop VOP2 maps before the GPU MMIO so a partial bring-up (VOP2
    // succeeded, GPU init failed) still tears down cleanly. NULL-safe.
    //
    WinMaliVop2Shutdown(&Adapter->Vop2);

    WinMaliHwShutdown(&Adapter->Hw);

    if (Adapter->GpuRegsVa != NULL) {
        MmUnmapIoSpace(Adapter->GpuRegsVa, Adapter->GpuRegsSize);
        WINMALI_TRACE("MMIO unmapped va=%p size=0x%x",
                      Adapter->GpuRegsVa, Adapter->GpuRegsSize);
        Adapter->GpuRegsVa     = NULL;
        Adapter->GpuRegsMapped = FALSE;
    }
}

// ----------------------------------------------------------------------
//
// Per-block masks (GPU / JOB / MMU) are programmed when we are ready to
// take IRQs (e.g. WinMaliFwInit unmasks JOB while polling MCU boot)
// ----------------------------------------------------------------------

NTSTATUS
WinMaliConnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->GpuIrqVector == 0) {
        WINMALI_WARN("no IRQ vector parsed - skipping connect");
        return STATUS_SUCCESS;
    }
    Adapter->InterruptConnected = TRUE;
    WINMALI_TRACE("IRQ connected (vec=%u level=%u shared=%u)",
                  Adapter->GpuIrqVector,
                  (ULONG)Adapter->GpuIrqLevel,
                  Adapter->GpuIrqShareable);
    return STATUS_SUCCESS;
}

VOID
WinMaliDisconnectInterrupt(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) return;
    Adapter->InterruptConnected = FALSE;
}
