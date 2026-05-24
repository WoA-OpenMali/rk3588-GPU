/*++

Module Name:

    WinMaliHw.c

Abstract:

    Mali-G610 MMIO helpers. Intentionally thin: every entry point guards
    against RegsVa == NULL so the rest of the driver can call into us
    before (or after) MMIO is mapped.

    WinMaliHwProbeIdentity() is the only non-trivial routine. It reads
    GPU_ID / CSF_ID / REVID plus a couple of "feature bit" registers and
    pre-decodes GPU_ID into arch/prod fields. The layout matches
    panthor-linux6.1's panthor_regs.h GPU_ARCH_MAJOR() / GPU_PROD_MAJOR()
    macros so grep+compare against Linux dmesg still works.

--*/

#include "..\WinMaliKmd.h"
#include "WinMaliHw.h"

NTSTATUS
WinMaliHwInitialize(
    _Out_ PWINMALI_HW Hw,
    _In_  PVOID       MappedRegsVa,
    _In_  ULONG       RegsSize)
{
    if (Hw == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Hw, sizeof(*Hw));
    Hw->RegsVa   = MappedRegsVa;
    Hw->RegsSize = RegsSize;
    return STATUS_SUCCESS;
}

VOID
WinMaliHwShutdown(_Inout_ PWINMALI_HW Hw)
{
    if (Hw == NULL) return;
    Hw->RegsVa   = NULL;
    Hw->RegsSize = 0;
}

ULONG
WinMaliHwRead32(_In_ const WINMALI_HW* Hw, _In_ ULONG Offset)
{
    if (Hw == NULL || Hw->RegsVa == NULL || Offset >= Hw->RegsSize) {
        return 0;
    }
    return READ_REGISTER_ULONG((volatile ULONG*)((UCHAR*)Hw->RegsVa + Offset));
}

VOID
WinMaliHwWrite32(_In_ const WINMALI_HW* Hw, _In_ ULONG Offset, _In_ ULONG Value)
{
    if (Hw == NULL || Hw->RegsVa == NULL || Offset >= Hw->RegsSize) {
        return;
    }
    WRITE_REGISTER_ULONG((volatile ULONG*)((UCHAR*)Hw->RegsVa + Offset), Value);
}

// -------------------------------------------------------------------------
// Identity probe. Reads the small set of "who are you" registers and
// populates the pre-decoded GPU_ID fields.
// -------------------------------------------------------------------------

NTSTATUS
WinMaliHwProbeIdentity(_Inout_ PWINMALI_HW Hw)
{
    if (Hw == NULL) return STATUS_INVALID_PARAMETER;

    if (Hw->RegsVa == NULL) {
        WINMALI_TRACE("Hw: no regs mapped, deferring identity probe");
        return STATUS_SUCCESS;
    }

    // Raw reads. On hardware, reading an un-clocked Mali block returns
    // all 0xFF, so treat 0xFFFFFFFF as "GPU is still gated" and refuse
    // to decode it.
    Hw->GpuId       = WinMaliHwRead32(Hw, WINMALI_REG_GPU_ID);
    Hw->CsfId       = WinMaliHwRead32(Hw, WINMALI_REG_GPU_CSF_ID);
    Hw->RevId       = WinMaliHwRead32(Hw, WINMALI_REG_GPU_REVID);
    Hw->L2Features  = WinMaliHwRead32(Hw, WINMALI_REG_GPU_L2_FEATURES);
    Hw->MmuFeatures = WinMaliHwRead32(Hw, WINMALI_REG_GPU_MMU_FEATURES);
    Hw->AsPresent   = WinMaliHwRead32(Hw, WINMALI_REG_GPU_AS_PRESENT);

    if (Hw->GpuId == 0xFFFFFFFFu || Hw->GpuId == 0x00000000u) {
        WINMALI_ERROR("GPU_ID reads as 0x%08x - is the GPU power domain on?",
                      Hw->GpuId);
        Hw->ArchMajor = Hw->ArchMinor = Hw->ArchRev  = 0;
        Hw->ProdMajor = Hw->VerMajor  = Hw->VerMinor = Hw->VerStatus = 0;
        return STATUS_DEVICE_NOT_READY;
    }

    Hw->ArchMajor = WINMALI_GPU_ID_ARCH_MAJOR(Hw->GpuId);
    Hw->ArchMinor = WINMALI_GPU_ID_ARCH_MINOR(Hw->GpuId);
    Hw->ArchRev   = WINMALI_GPU_ID_ARCH_REV  (Hw->GpuId);
    Hw->ProdMajor = WINMALI_GPU_ID_PROD_MAJOR(Hw->GpuId);
    Hw->VerMajor  = WINMALI_GPU_ID_VER_MAJOR (Hw->GpuId);
    Hw->VerMinor  = WINMALI_GPU_ID_VER_MINOR (Hw->GpuId);
    Hw->VerStatus = WINMALI_GPU_ID_VER_STATUS(Hw->GpuId);

    WINMALI_TRACE(
        "GPU_ID=0x%08x arch=%u.%u.%u prod=%u ver=%u.%u.%u CSF_ID=0x%08x REVID=0x%08x",
        Hw->GpuId,
        Hw->ArchMajor, Hw->ArchMinor, Hw->ArchRev,
        Hw->ProdMajor,
        Hw->VerMajor,  Hw->VerMinor,  Hw->VerStatus,
        Hw->CsfId, Hw->RevId);
    WINMALI_TRACE(
        "    L2_FEATURES=0x%08x MMU_FEATURES=0x%08x AS_PRESENT=0x%08x",
        Hw->L2Features, Hw->MmuFeatures, Hw->AsPresent);

    return STATUS_SUCCESS;
}

BOOLEAN
WinMaliHwIsMaliG610(_In_ const WINMALI_HW* Hw)
{
    if (Hw == NULL) return FALSE;
    return (Hw->ArchMajor == WINMALI_PROD_MALI_G610_ARCH)
        && (Hw->ProdMajor == WINMALI_PROD_MALI_G610_PROD);
}

PCSTR
WinMaliHwProductName(_In_ const WINMALI_HW* Hw)
{
    if (Hw == NULL) return "unknown (null hw)";

    const ULONG arch = Hw->ArchMajor;
    const ULONG prod = Hw->ProdMajor;

    if (arch == 10 && prod == 2)  return "Mali-G710";
    if (arch == 10 && prod == 3)  return "Mali-G510";
    if (arch == 10 && prod == 4)  return "Mali-G310";
    if (arch == 10 && prod == 7)  return "Mali-G610";
    if (arch == 11 && prod == 2)  return "Mali-G715 / Immortalis-G715";
    if (arch == 11 && prod == 3)  return "Mali-G615";
    if (arch == 12 && prod == 0)  return "Mali-G720 / Immortalis-G720";
    if (arch == 12 && prod == 1)  return "Mali-G620";
    if (arch == 13 && prod == 0)  return "Mali-G925 / Immortalis-G925";
    if (arch == 13 && prod == 1)  return "Mali-G625";

    return "unknown";
}
