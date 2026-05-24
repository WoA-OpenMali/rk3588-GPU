/*++

Module Name:

    WinMaliBringup.c

Abstract:

    StartDevice-time hardware bring-up. Parses ACPI _CRS for the GPU MMIO
    range and IRQ vector, maps the MMIO, then probes GPU_ID and friends
    via WinMaliHw.

    Lifted (and trimmed) from the BadDriver tree - VOP2/display bring-up
    is removed because we're now a render-only adapter.

--*/

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
            // All three Large variants share the Start field layout - the
            // only difference from plain Memory is how Length is stored.
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
    NTSTATUS         status;
    DXGK_DEVICE_INFO info;
    const CM_RESOURCE_LIST* list;
    ULONG            iDesc;
    ULONG            memCount = 0, irqCount = 0;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->DxgkInterface.DxgkCbGetDeviceInformation == NULL
     || Adapter->DxgkHandle == NULL) {
        WINMALI_ERROR("ParseResources: no DxgkCbGetDeviceInformation callback (hdl=%p cb=%p)",
                      Adapter->DxgkHandle,
                      Adapter->DxgkInterface.DxgkCbGetDeviceInformation);
        return STATUS_DEVICE_NOT_READY;
    }

    RtlZeroMemory(&info, sizeof(info));
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
        WINMALI_TRACE("parsed: %u interrupt partials (first kept for diag)",
                      irqCount);
    }

    WINMALI_TRACE("parsed: MMIO phys=0x%llx size=0x%x IRQ vec=%u level=%u",
                  Adapter->GpuRegsPhys.QuadPart,
                  Adapter->GpuRegsSize,
                  Adapter->GpuIrqVector,
                  (ULONG)Adapter->GpuIrqLevel);
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
        return STATUS_SUCCESS;   // already mapped
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
        // GPU power domain may be gated. Don't fail StartDevice over this -
        // the rest of the driver paths still want to come up so we can
        // diagnose what's going on.
        WINMALI_WARN("HwProbeIdentity failed 0x%08x - GPU likely powered off",
                     status);
        return STATUS_SUCCESS;
    }

    if (WinMaliHwIsMaliG610(&Adapter->Hw)) {
        WINMALI_TRACE("identified product: %s (match)",
                      WinMaliHwProductName(&Adapter->Hw));
    } else {
        WINMALI_WARN("unexpected product %s (arch=%u prod=%u)",
                     WinMaliHwProductName(&Adapter->Hw),
                     Adapter->Hw.ArchMajor,
                     Adapter->Hw.ProdMajor);
    }

    return STATUS_SUCCESS;
}

VOID
WinMaliTeardownHardware(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL) return;

    WinMaliHwShutdown(&Adapter->Hw);

    if (Adapter->GpuRegsVa != NULL) {
        MmUnmapIoSpace(Adapter->GpuRegsVa, Adapter->GpuRegsSize);
        WINMALI_TRACE("MMIO unmapped va=%p size=0x%x",
                      Adapter->GpuRegsVa, Adapter->GpuRegsSize);
        Adapter->GpuRegsVa     = NULL;
        Adapter->GpuRegsMapped = FALSE;
    }
}

/* WDDM binds the ISR via init->DxgkDdiInterruptRoutine; this routine is
   informational only - it records that an IRQ was parsed and lets the
   FW init code know to expect notifications. */
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
