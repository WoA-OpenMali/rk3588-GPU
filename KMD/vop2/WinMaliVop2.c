/** @file
 *
 * RK3588 VOP2 sub-block: MMIO mapping, probe, and optional smoke tests.
 * See WinMaliVop2.h for the planned phase 2c/2d work.
 *
 * WinMaliVop2Initialize runs during WinMaliBringupHardware (before GOP
 * capture) and only reads registers + logs. WinMaliVop2SmokeVisualDraw
 * runs after Rk3588DispCaptureGopFb and performs CPU writes into the live
 * GOP framebuffer plus YRGB_MST / REG_CFG_DONE exercises.
 */

#include "../WinMaliKmd.h"
#include "WinMaliVop2.h"

//
// Local helper: read a 32-bit register at `Offset` from `Base`. Returns
// 0xFFFFFFFF if Base is NULL so callers can treat NULL the same way they
// treat "MMIO didn't decode".
//
static __forceinline ULONG
Vop2Read32_(
    _In_opt_ PVOID Base,
    _In_ ULONG Offset)
{
    if (Base == NULL) {
        return 0xFFFFFFFFul;
    }
    return READ_REGISTER_ULONG((volatile ULONG*)((PUCHAR)Base + Offset));
}

//
// Write a 32-bit register at `Offset` from `Base`. No-op if Base is NULL;
// caller is expected to bail before this point but we guard anyway so a
// misconfigured WinMaliVop2SetScanoutPa() never AVes the kernel.
//
static __forceinline VOID
Vop2Write32_(
    _In_opt_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    if (Base == NULL) {
        return;
    }
    WRITE_REGISTER_ULONG((volatile ULONG*)((PUCHAR)Base + Offset), Value);
}

//
// Pulse REG_CFG_DONE for a single VP. The CFG_DONE_EN arming bit MUST be
// set in the same write or VOP2 ignores the request. RK3568/RK3588 latch
// shadow registers on the next vsync edge after this write.
//
static __forceinline VOID
Vop2RegCfgDoneForVp_(
    _In_opt_ PVOID Vop2Regs,
    _In_ UINT32 VpId)
{
    Vop2Write32_(Vop2Regs,
                 WINMALI_VOP2_REG_CFG_DONE,
                 WINMALI_VOP2_CFG_DONE_EN_BIT | (1u << (VpId & 0x3u)));
}

//
// Map VP id to its interrupt-register base offset (stride 0x10 from 0xA0).
//
static __forceinline ULONG
Vop2VpIntBase_(_In_ UINT32 VpId)
{
    return WINMALI_VOP2_VP_INT_BASE + (VpId & 0x3u) * WINMALI_VOP2_VP_INT_STRIDE;
}

//
// Per-VP timing decoder. RK3568_VP*_DSP_HACT_ST_END encodes [start:16][end:0]
// where end - start == HActive. Same shape for VACT_ST_END.
//
static UINT32
Vop2DecodeActive_(_In_ UINT32 ActStEnd)
{
    UINT32 start = (ActStEnd >> 16) & 0xFFFFu;
    UINT32 end   = ActStEnd & 0xFFFFu;
    return (end > start) ? (end - start) : 0u;
}

//
// Map one MMIO region. Returns the mapped VA or NULL with a log line on
// failure - we deliberately do NOT propagate the failure to the caller
// (Initialize) because we want the GPU bring-up to continue even when one
// of the GRF blocks isn't reachable on a given platform variant (some
// RK3588 SKUs gate VO1_GRF until HDMI is brought up by firmware).
//
static PVOID
Vop2Map_(
    _In_ ULONGLONG PhysBase,
    _In_ ULONG     Size,
    _In_ PCSTR     Name)
{
    PHYSICAL_ADDRESS phys;
    PVOID            va;

    phys.QuadPart = (LONGLONG)PhysBase;
    va = MmMapIoSpaceEx(phys, Size, PAGE_READWRITE | PAGE_NOCACHE);
    if (va == NULL) {
        WINMALI_WARN(
            "VOP2: MmMapIoSpaceEx(%s phys=0x%llx size=0x%x) failed",
            Name, PhysBase, Size);
    } else {
        WINMALI_TRACE(
            "VOP2: %s mapped phys=0x%llx size=0x%x va=%p",
            Name, PhysBase, Size, va);
    }
    return va;
}

NTSTATUS
WinMaliVop2Initialize(_Inout_ PWINMALI_VOP2 Vop2)
{
    if (Vop2 == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Vop2->Initialized) {
        return STATUS_SUCCESS;
    }

    Vop2->Vop2Regs   = Vop2Map_(WINMALI_VOP2_REG_BASE,
                                WINMALI_VOP2_REG_SIZE,
                                "VOP2");
    Vop2->VopGrfRegs = Vop2Map_(WINMALI_VOP2_VOP_GRF_BASE,
                                WINMALI_VOP2_VOP_GRF_SIZE,
                                "VOP_GRF");
    Vop2->Vo1GrfRegs = Vop2Map_(WINMALI_VOP2_VO1_GRF_BASE,
                                WINMALI_VOP2_VO1_GRF_SIZE,
                                "VO1_GRF");
    Vop2->SysGrfRegs = Vop2Map_(WINMALI_VOP2_SYS_GRF_BASE,
                                WINMALI_VOP2_SYS_GRF_SIZE,
                                "SYS_GRF");

    //
    // We treat "VOP2 main block mapped" as the success criterion. The GRF
    // blocks are auxiliary; we only need them when we start writing them
    // in phase 2b (HDMI tx enable, sync polarity). For phase 2a all we
    // need is the main VOP2 block to read VP timings.
    //
    if (Vop2->Vop2Regs == NULL) {
        WINMALI_ERROR(
            "VOP2: main VOP2 region map failed - VOP2 path disabled");
        WinMaliVop2Shutdown(Vop2);
        //
        // Still return SUCCESS so the GPU bring-up continues. The caller
        // checks Vop2->Initialized to know whether VOP2 is usable.
        //
        return STATUS_SUCCESS;
    }

    Vop2->Initialized = TRUE;
    WinMaliVop2DumpState(Vop2);

    return STATUS_SUCCESS;
}

//
// X8R8G8B8 colors (same convention as DWM / DXGI BGRA packed in ULONG).
//
#define VOP2_SMOKE_GREEN  0x0000FF00u
#define VOP2_SMOKE_RED    0x00FF0000u

static VOID
Vop2SmokeDrawSquareX8R8G8B8_(
    _In_ PUCHAR Base,
    _In_ ULONG PitchBytes,
    _In_ ULONG SurfWpx,
    _In_ ULONG SurfHpx)
{
    //
    // Bordered square: green outline, red interior — unmistakable on the
    // UEFI desktop / early Win boot background.
    //
    const ULONG originX = 48;
    const ULONG originY = 48;
    const ULONG boxSize = 200;
    const ULONG border  = 8;

    for (ULONG dy = 0; dy < boxSize; dy++) {
        ULONG py = originY + dy;
        if (py >= SurfHpx) {
            break;
        }
        PULONG row = (PULONG)(Base + py * PitchBytes);
        for (ULONG dx = 0; dx < boxSize; dx++) {
            ULONG px = originX + dx;
            if (px >= SurfWpx) {
                continue;
            }
            BOOLEAN edge =
                (dx < border || dx >= boxSize - border ||
                 dy < border || dy >= boxSize - border);
            row[px] = edge ? VOP2_SMOKE_GREEN : VOP2_SMOKE_RED;
        }
    }
}

VOID
WinMaliVop2SmokeVisualDraw(_Inout_ PWINMALI_ADAPTER Adapter)
{
    SIZE_T                    mapBytes;
    PVOID                     va = NULL;
    ULONG                     surfW;
    ULONG                     surfH;
    NTSTATUS                  regStatus;

    if (Adapter == NULL) {
        return;
    }
    if (!Adapter->Vop2.Initialized || Adapter->Vop2.Vop2Regs == NULL) {
        WINMALI_TRACE("VOP2: smoke visual skipped (VOP2 not initialized)");
        return;
    }
    if (!Adapter->Gop.Valid || Adapter->Gop.Pitch == 0 || Adapter->Gop.Height == 0) {
        WINMALI_WARN("VOP2: smoke visual skipped (GOP not captured)");
        return;
    }
    if (Adapter->Gop.Bpp != 32u) {
        WINMALI_WARN(
            "VOP2: smoke visual skipped (bpp=%u != 32 — add 16bpp path if needed)",
            Adapter->Gop.Bpp);
        return;
    }

    mapBytes = (SIZE_T)Adapter->Gop.Pitch * (SIZE_T)Adapter->Gop.Height;
    if (mapBytes == 0 || mapBytes > (SIZE_T)(1920u * 1080u * 4u * 2u)) {
        //
        // Absurdly large — refuse rather than map gigabytes by mistake.
        //
        WINMALI_WARN(
            "VOP2: smoke visual skipped (mapBytes=%Iu suspicious)",
            mapBytes);
        return;
    }

    surfW = Adapter->Gop.Width;
    surfH = Adapter->Gop.Height;
    if (surfW == 0 || surfH == 0) {
        WINMALI_WARN("VOP2: smoke visual skipped (surface size 0)");
        return;
    }

    //
    // GOP phys should match the YRGB_MST we read from Esmart — if not,
    // we still draw at GOP (what dxgk handed us) but warn loudly.
    //
    if ((ULONG)(Adapter->Gop.PhysBase.QuadPart & 0xFFFFFFFFull)
            != Adapter->Vop2.ScanoutYrgbMst
        || (Adapter->Gop.PhysBase.QuadPart >> 32) != 0) {
        WINMALI_WARN(
            "VOP2: smoke: GOP phys 0x%llx != VOP2 YRGB_MST 0x%08x — "
            "drawing at GOP; display may not update if routing differs",
            Adapter->Gop.PhysBase.QuadPart,
            Adapter->Vop2.ScanoutYrgbMst);
    }

    va = MmMapIoSpaceEx(
        Adapter->Gop.PhysBase,
        mapBytes,
        PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (va == NULL) {
        va = MmMapIoSpaceEx(
            Adapter->Gop.PhysBase,
            mapBytes,
            PAGE_READWRITE | PAGE_NOCACHE);
    }
    if (va == NULL) {
        WINMALI_ERROR(
            "VOP2: smoke visual MmMapIoSpaceEx failed phys=0x%llx bytes=%Iu",
            Adapter->Gop.PhysBase.QuadPart,
            mapBytes);
        return;
    }

    Vop2SmokeDrawSquareX8R8G8B8_(
        (PUCHAR)va,
        Adapter->Gop.Pitch,
        surfW,
        surfH);

    KeMemoryBarrier();

    MmUnmapIoSpace(va, mapBytes);
    va = NULL;

    WINMALI_TRACE(
        "VOP2: smoke visual drew %ux%u bordered square at (48,48) "
        "(pitch=%u phys=0x%llx)",
        surfW,
        surfH,
        Adapter->Gop.Pitch,
        Adapter->Gop.PhysBase.QuadPart);

    //
    // Re-latch the same YRGB_MST + pulse CFG_DONE so the register write
    // path stays exercised after pixel smoke (deploy builds only).
    //
    if (Adapter->Vop2.ScanoutLayer < WINMALI_VOP2_LAYER_COUNT
        && Adapter->Vop2.ScanoutYrgbMst != 0) {
        regStatus = WinMaliVop2SetScanoutPa(
            &Adapter->Vop2,
            Adapter->Vop2.ScanoutYrgbMst,
            0);
        if (NT_SUCCESS(regStatus)) {
            WINMALI_TRACE(
                "VOP2: smoke CFG_DONE pass — YRGB_MST=0x%08x VP%u",
                Adapter->Vop2.ScanoutYrgbMst,
                Adapter->Vop2.ActiveVpId);
        } else {
            WINMALI_WARN(
                "VOP2: smoke SetScanoutPa failed 0x%08x (pixels still drawn)",
                regStatus);
        }
    }
}

VOID
WinMaliVop2Shutdown(_Inout_ PWINMALI_VOP2 Vop2)
{
    if (Vop2 == NULL) {
        return;
    }
    if (Vop2->Vop2Regs != NULL) {
        MmUnmapIoSpace(Vop2->Vop2Regs, WINMALI_VOP2_REG_SIZE);
        Vop2->Vop2Regs = NULL;
    }
    if (Vop2->VopGrfRegs != NULL) {
        MmUnmapIoSpace(Vop2->VopGrfRegs, WINMALI_VOP2_VOP_GRF_SIZE);
        Vop2->VopGrfRegs = NULL;
    }
    if (Vop2->Vo1GrfRegs != NULL) {
        MmUnmapIoSpace(Vop2->Vo1GrfRegs, WINMALI_VOP2_VO1_GRF_SIZE);
        Vop2->Vo1GrfRegs = NULL;
    }
    if (Vop2->SysGrfRegs != NULL) {
        MmUnmapIoSpace(Vop2->SysGrfRegs, WINMALI_VOP2_SYS_GRF_SIZE);
        Vop2->SysGrfRegs = NULL;
    }
    Vop2->Initialized = FALSE;
}

//
// Map a 0..3 VP id to its register-block base offset within the main VOP2
// register window.
//
static ULONG
Vop2VpBaseOffset_(_In_ UINT32 VpId)
{
    switch (VpId) {
        case 0: return WINMALI_VOP2_VP0_BASE;
        case 1: return WINMALI_VOP2_VP1_BASE;
        case 2: return WINMALI_VOP2_VP2_BASE;
        case 3: return WINMALI_VOP2_VP3_BASE;
        default: return WINMALI_VOP2_VP0_BASE;
    }
}

//
// Read VPx timings into the snapshot fields. Pure register reads; no
// VP3-on-RK3588-only-with-DSC special-casing yet. If a VP is in standby
// the timings will read zero, which is the desired sentinel for "this VP
// isn't being driven".
//
static VOID
Vop2DumpVp_(
    _Inout_ PWINMALI_VOP2 Vop2,
    _In_ UINT32 VpId)
{
    ULONG base   = Vop2VpBaseOffset_(VpId);
    ULONG ctrl   = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_CTRL_OFFSET);
    ULONG hTotal = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_HTOTAL_OFFSET);
    ULONG hAct   = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_HACT_OFFSET);
    ULONG vTotal = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_VTOTAL_OFFSET);
    ULONG vAct   = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_VACT_OFFSET);
    UINT32 hActive = Vop2DecodeActive_(hAct);
    UINT32 vActive = Vop2DecodeActive_(vAct);
    BOOLEAN standby = (ctrl & WINMALI_VOP2_VP_DSP_CTRL_STANDBY_BIT) != 0;

    WINMALI_TRACE(
        "VOP2: VP%u %s ctrl=0x%08x htotal=0x%08x hact=0x%08x "
        "vtotal=0x%08x vact=0x%08x active=%ux%u",
        VpId,
        standby ? "STBY" : "RUN ",
        ctrl, hTotal, hAct, vTotal, vAct,
        hActive, vActive);
}

//
// RK3588 layer descriptor table. Matches mWinDataRK3588[] in
// edk2-rk3588 Vop2Dxe.c so we read the same registers the firmware did
// for setup. Indexed by WINMALI_VOP2_LAYER_ID; entry .Id MUST equal the
// index for the LayerSelWinID decode logic below.
//
typedef struct {
    PCSTR                    Name;
    WINMALI_VOP2_LAYER_TYPE  Type;
    UINT32                   WinSelPortOffset; // bit slot in OVL_PORT_SEL
    UINT32                   LayerSelWinID;    // value in OVL_LAYER_SEL
    ULONG                    RegBase;          // VOP2-space offset
} VOP2_LAYER_DESC;

//
// Cluster bases (0x1000) and Esmart bases (0x1800) plus per-layer RegOffset
// (0x000 / 0x200 / 0x400 / 0x600) from Vop2Dxe.c mWinDataRK3588:
//   Cluster0  base=0x1000  port_off=0  winId=0
//   Cluster1  base=0x1200  port_off=1  winId=1
//   Esmart0   base=0x1800  port_off=4  winId=2
//   Esmart1   base=0x1A00  port_off=5  winId=3
//   Cluster2  base=0x1400  port_off=2  winId=4
//   Cluster3  base=0x1600  port_off=3  winId=5
//   Esmart2   base=0x1C00  port_off=6  winId=6
//   Esmart3   base=0x1E00  port_off=7  winId=7
//
static const VOP2_LAYER_DESC kVop2Layers_[WINMALI_VOP2_LAYER_COUNT] = {
    { "Cluster0", WINMALI_VOP2_LAYER_TYPE_CLUSTER, 0, 0, WINMALI_VOP2_CLUSTER_BASE + 0 * WINMALI_VOP2_LAYER_STRIDE },
    { "Cluster1", WINMALI_VOP2_LAYER_TYPE_CLUSTER, 1, 1, WINMALI_VOP2_CLUSTER_BASE + 1 * WINMALI_VOP2_LAYER_STRIDE },
    { "Esmart0",  WINMALI_VOP2_LAYER_TYPE_ESMART,  4, 2, WINMALI_VOP2_ESMART_BASE  + 0 * WINMALI_VOP2_LAYER_STRIDE },
    { "Esmart1",  WINMALI_VOP2_LAYER_TYPE_ESMART,  5, 3, WINMALI_VOP2_ESMART_BASE  + 1 * WINMALI_VOP2_LAYER_STRIDE },
    { "Cluster2", WINMALI_VOP2_LAYER_TYPE_CLUSTER, 2, 4, WINMALI_VOP2_CLUSTER_BASE + 2 * WINMALI_VOP2_LAYER_STRIDE },
    { "Cluster3", WINMALI_VOP2_LAYER_TYPE_CLUSTER, 3, 5, WINMALI_VOP2_CLUSTER_BASE + 3 * WINMALI_VOP2_LAYER_STRIDE },
    { "Esmart2",  WINMALI_VOP2_LAYER_TYPE_ESMART,  6, 6, WINMALI_VOP2_ESMART_BASE  + 2 * WINMALI_VOP2_LAYER_STRIDE },
    { "Esmart3",  WINMALI_VOP2_LAYER_TYPE_ESMART,  7, 7, WINMALI_VOP2_ESMART_BASE  + 3 * WINMALI_VOP2_LAYER_STRIDE },
};

VOID
WinMaliVop2DumpLayers(_Inout_ PWINMALI_VOP2 Vop2)
{
    UINT32 i;
    UINT32 portSel;

    if (Vop2 == NULL || Vop2->Vop2Regs == NULL) {
        return;
    }

    Vop2->OvlLayerSel = Vop2Read32_(Vop2->Vop2Regs,
                                    WINMALI_VOP2_REG_OVL_LAYER_SEL);
    Vop2->OvlPortSel  = Vop2Read32_(Vop2->Vop2Regs,
                                    WINMALI_VOP2_REG_OVL_PORT_SEL);
    portSel = Vop2->OvlPortSel;

    WINMALI_TRACE(
        "VOP2: OVL_LAYER_SEL=0x%08x OVL_PORT_SEL=0x%08x",
        Vop2->OvlLayerSel, Vop2->OvlPortSel);

    Vop2->ScanoutLayer    = WINMALI_VOP2_LAYER_COUNT;
    Vop2->ScanoutYrgbMst  = 0;

    for (i = 0; i < WINMALI_VOP2_LAYER_COUNT; i++) {
        const VOP2_LAYER_DESC*    desc  = &kVop2Layers_[i];
        PWINMALI_VOP2_LAYER_STATE state = &Vop2->Layers[i];
        ULONG ctrlOff, mstOff, virOff, actOff, dspInfoOff, dspStOff;
        UINT32 portShift;

        if (desc->Type == WINMALI_VOP2_LAYER_TYPE_CLUSTER) {
            ctrlOff    = WINMALI_VOP2_CLUSTER_WIN0_CTRL0_OFFSET;
            mstOff     = WINMALI_VOP2_CLUSTER_WIN0_YRGB_MST_OFFSET;
            virOff     = WINMALI_VOP2_CLUSTER_WIN0_VIR_OFFSET;
            actOff     = WINMALI_VOP2_CLUSTER_WIN0_ACT_INFO_OFFSET;
            dspInfoOff = WINMALI_VOP2_CLUSTER_WIN0_DSP_INFO_OFFSET;
            dspStOff   = WINMALI_VOP2_CLUSTER_WIN0_DSP_ST_OFFSET;
            state->AfbcdHdrPtr = Vop2Read32_(Vop2->Vop2Regs,
                                             desc->RegBase + WINMALI_VOP2_CLUSTER_WIN0_AFBCD_HDR_OFFSET);
        } else {
            ctrlOff    = WINMALI_VOP2_ESMART_R0_CTRL_OFFSET;
            mstOff     = WINMALI_VOP2_ESMART_R0_YRGB_MST_OFFSET;
            virOff     = WINMALI_VOP2_ESMART_R0_VIR_OFFSET;
            actOff     = WINMALI_VOP2_ESMART_R0_ACT_INFO_OFFSET;
            dspInfoOff = WINMALI_VOP2_ESMART_R0_DSP_INFO_OFFSET;
            dspStOff   = WINMALI_VOP2_ESMART_R0_DSP_ST_OFFSET;
            state->AfbcdHdrPtr = 0;
        }

        state->Id       = (WINMALI_VOP2_LAYER_ID)i;
        state->Type     = desc->Type;
        state->Name     = desc->Name;
        state->RegBase  = desc->RegBase;
        state->Ctrl     = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + ctrlOff);
        state->YrgbMst  = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + mstOff);
        state->Vir      = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + virOff);
        state->ActInfo  = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + actOff);
        state->DspInfo  = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + dspInfoOff);
        state->DspSt    = Vop2Read32_(Vop2->Vop2Regs, desc->RegBase + dspStOff);
        state->Enabled  = (state->Ctrl & WINMALI_VOP2_WIN_EN_BIT) != 0;
        portShift       = WINMALI_VOP2_OVL_PORT_SEL_SHIFT_BASE
                          + desc->WinSelPortOffset * 2u;
        state->VpRouting = (portSel >> portShift) & WINMALI_VOP2_OVL_PORT_SEL_MASK;

        WINMALI_TRACE(
            "VOP2: %s %s vp=%u ctrl=0x%08x yrgb_mst=0x%08x vir=0x%08x "
            "act=0x%08x dsp_info=0x%08x dsp_st=0x%08x afbc_hdr=0x%08x",
            desc->Name,
            state->Enabled ? "EN " : "off",
            state->VpRouting,
            state->Ctrl,
            state->YrgbMst,
            state->Vir,
            state->ActInfo,
            state->DspInfo,
            state->DspSt,
            state->AfbcdHdrPtr);

        //
        // First enabled layer feeding ActiveVpId with a non-zero YrgbMst
        // wins the "this is the GOP scan-out source" badge. UEFI's vendor
        // GOP path always programs exactly one layer per VP, so a more
        // sophisticated tie-breaker isn't necessary at this stage.
        //
        if (state->Enabled
         && state->VpRouting == Vop2->ActiveVpId
         && state->YrgbMst != 0
         && Vop2->ScanoutLayer == WINMALI_VOP2_LAYER_COUNT) {
            Vop2->ScanoutLayer   = state->Id;
            Vop2->ScanoutYrgbMst = state->YrgbMst;
        }
    }

    if (Vop2->ScanoutLayer != WINMALI_VOP2_LAYER_COUNT) {
        WINMALI_TRACE(
            "VOP2: scan-out source = %s on VP%u, YRGB_MST=0x%08x "
            "(act=0x%08x dsp_info=0x%08x)",
            kVop2Layers_[Vop2->ScanoutLayer].Name,
            Vop2->ActiveVpId,
            Vop2->ScanoutYrgbMst,
            Vop2->Layers[Vop2->ScanoutLayer].ActInfo,
            Vop2->Layers[Vop2->ScanoutLayer].DspInfo);
    } else {
        WINMALI_WARN(
            "VOP2: no enabled layer found feeding VP%u - UEFI scan-out "
            "may not be programmed via VOP2 (BSP-specific composer?)",
            Vop2->ActiveVpId);
    }
}

VOID
WinMaliVop2DumpState(_Inout_ PWINMALI_VOP2 Vop2)
{
    UINT32 vpForHdmi0;
    UINT32 dspIfEn;

    if (Vop2 == NULL || Vop2->Vop2Regs == NULL) {
        return;
    }

    Vop2->VersionInfo = Vop2Read32_(Vop2->Vop2Regs,
                                    WINMALI_VOP2_REG_VERSION_INFO);
    Vop2->DspIfEn     = Vop2Read32_(Vop2->Vop2Regs,
                                    WINMALI_VOP2_REG_DSP_IF_EN);

    WINMALI_TRACE(
        "VOP2: probe ok version=0x%08x dsp_if_en=0x%08x "
        "(hdmi0_en=%u hdmi1_en=%u hdmi0_mux=%u hdmi1_mux=%u)",
        Vop2->VersionInfo,
        Vop2->DspIfEn,
        (Vop2->DspIfEn >> 3) & 0x1u,                  // RK3588_HDMI0_EN_SHIFT
        (Vop2->DspIfEn >> 4) & 0x1u,                  // RK3588_HDMI1_EN_SHIFT
        (Vop2->DspIfEn >> 16) & 0x3u,                 // RK3588_HDMI_EDP0_MUX_SHIFT
        (Vop2->DspIfEn >> 18) & 0x3u);                // RK3588_HDMI_EDP1_MUX_SHIFT

    //
    // Walk every VP and dump its timings. UEFI typically only programs the
    // one feeding HDMI0, but logging all four lets us catch surprises (e.g.
    // a non-zero VP1/VP2 standby because the firmware staged a second mode).
    //
    Vop2DumpVp_(Vop2, 0);
    Vop2DumpVp_(Vop2, 1);
    Vop2DumpVp_(Vop2, 2);
    Vop2DumpVp_(Vop2, 3);

    //
    // Snapshot the VP currently driving HDMI0 - that's the one whose mode
    // we'll later feed back into VidPn cofunc enumeration so dxgk picks the
    // identical timings VOP2 is already running. If HDMI0 is disabled we
    // fall back to "VP0" as a placeholder; consumers must check
    // Vop2->ActiveHActive != 0 before trusting the modeline.
    //
    dspIfEn = Vop2->DspIfEn;
    if ((dspIfEn >> 3) & 0x1u) {
        vpForHdmi0 = (dspIfEn >> 16) & 0x3u;
    } else if ((dspIfEn >> 4) & 0x1u) {
        vpForHdmi0 = (dspIfEn >> 18) & 0x3u;
    } else {
        vpForHdmi0 = 0;
    }
    {
        ULONG base = Vop2VpBaseOffset_(vpForHdmi0);
        Vop2->ActiveVpId          = vpForHdmi0;
        Vop2->ActiveDspCtrl       = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_CTRL_OFFSET);
        Vop2->ActiveHTotalHsEnd   = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_HTOTAL_OFFSET);
        Vop2->ActiveHActStEnd     = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_HACT_OFFSET);
        Vop2->ActiveVTotalVsEnd   = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_VTOTAL_OFFSET);
        Vop2->ActiveVActStEnd     = Vop2Read32_(Vop2->Vop2Regs, base + WINMALI_VOP2_VP_DSP_VACT_OFFSET);
        Vop2->ActiveHActive       = Vop2DecodeActive_(Vop2->ActiveHActStEnd);
        Vop2->ActiveVActive       = Vop2DecodeActive_(Vop2->ActiveVActStEnd);
        WINMALI_TRACE(
            "VOP2: active VP%u for HDMI0 -> %ux%u (dsp_ctrl=0x%08x)",
            Vop2->ActiveVpId,
            Vop2->ActiveHActive, Vop2->ActiveVActive,
            Vop2->ActiveDspCtrl);
    }

    //
    // Now that ActiveVpId is known, walk all 8 layers and identify the
    // one currently feeding it. Phase 2c will rewrite this layer's
    // YRGB_MST when dxgk presents a new buffer; for now we just record
    // the live programming so we know exactly what UEFI handed off.
    //
    WinMaliVop2DumpLayers(Vop2);

    //
    // Per-VP IRQ snapshot. Useful for two reasons:
    //  (a) confirms VOP2 has its IRQ logic powered up (a non-zero
    //      INT_STATUS means at least one event has fired since reset),
    //  (b) tells us which bits UEFI was using (so we know what we'll
    //      need to mask before reconfiguring the IRQs ourselves).
    //
    // We do NOT clear or enable anything here - this is read-only.
    //
    {
        ULONG intBase = Vop2VpIntBase_(Vop2->ActiveVpId);
        ULONG intEn   = Vop2Read32_(Vop2->Vop2Regs, intBase + WINMALI_VOP2_VP_INT_EN_OFFSET);
        ULONG intSts  = Vop2Read32_(Vop2->Vop2Regs, intBase + WINMALI_VOP2_VP_INT_STATUS_OFFSET);
        WINMALI_TRACE(
            "VOP2: VP%u INT_EN=0x%08x INT_STATUS=0x%08x (read-only probe)",
            Vop2->ActiveVpId, intEn, intSts);
    }
}

NTSTATUS
WinMaliVop2SetScanoutPa(
    _Inout_ PWINMALI_VOP2 Vop2,
    _In_    UINT32        YrgbMst,
    _In_    UINT32        VirPixels)
{
    const VOP2_LAYER_DESC*    desc;
    PWINMALI_VOP2_LAYER_STATE state;
    ULONG mstOff;
    ULONG virOff;
    ULONG alignMask;

    if (Vop2 == NULL || Vop2->Vop2Regs == NULL || !Vop2->Initialized) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Vop2->ScanoutLayer >= WINMALI_VOP2_LAYER_COUNT) {
        WINMALI_WARN("Vop2SetScanoutPa: no scan-out layer identified - "
                     "did the boot dump fail to find an enabled layer?");
        return STATUS_DEVICE_NOT_READY;
    }
    if (YrgbMst == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    desc  = &kVop2Layers_[Vop2->ScanoutLayer];
    state = &Vop2->Layers[Vop2->ScanoutLayer];

    //
    // Cluster layers require 16-byte (AFBC) or 4-byte (linear) alignment;
    // Esmart layers require 4-byte alignment. We pick the stricter
    // condition for Cluster to be safe with AFBC, and the looser one for
    // Esmart since current bring-up only uses linear formats.
    //
    if (desc->Type == WINMALI_VOP2_LAYER_TYPE_CLUSTER) {
        alignMask = 0xFu;
        mstOff = WINMALI_VOP2_CLUSTER_WIN0_YRGB_MST_OFFSET;
        virOff = WINMALI_VOP2_CLUSTER_WIN0_VIR_OFFSET;
    } else {
        alignMask = 0x3u;
        mstOff = WINMALI_VOP2_ESMART_R0_YRGB_MST_OFFSET;
        virOff = WINMALI_VOP2_ESMART_R0_VIR_OFFSET;
    }
    if ((YrgbMst & alignMask) != 0) {
        WINMALI_WARN(
            "Vop2SetScanoutPa: YrgbMst=0x%08x violates %s alignment 0x%x",
            YrgbMst, desc->Name, alignMask + 1u);
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Write the new base PA first, then VIR (if caller wants it changed),
    // then arm CFG_DONE so VOP2 latches both on the next vsync. Doing
    // CFG_DONE last means the layer transitions atomically from "old PA /
    // old VIR" to "new PA / new VIR" - no torn frame.
    //
    Vop2Write32_(Vop2->Vop2Regs, desc->RegBase + mstOff, YrgbMst);
    if (VirPixels != 0) {
        Vop2Write32_(Vop2->Vop2Regs, desc->RegBase + virOff, VirPixels);
    }
    Vop2RegCfgDoneForVp_(Vop2->Vop2Regs, Vop2->ActiveVpId);

    //
    // Mirror the new state into our shadow so the next probe / log line
    // shows what we just programmed. The hardware won't actually scan out
    // from YrgbMst until the next vsync edge, but everything we expose
    // describes the queued state, not the latched state.
    //
    state->YrgbMst       = YrgbMst;
    if (VirPixels != 0) {
        state->Vir = VirPixels;
    }
    Vop2->ScanoutYrgbMst = YrgbMst;

    WINMALI_TRACE(
        "Vop2SetScanoutPa: %s on VP%u YRGB_MST=0x%08x VIR=%u (CFG_DONE pulsed)",
        desc->Name, Vop2->ActiveVpId, YrgbMst,
        VirPixels ? VirPixels : state->Vir);
    return STATUS_SUCCESS;
}

static SIZE_T
Vop2RoundUpPages_(_In_ SIZE_T bytes)
{
    return (bytes + (SIZE_T)PAGE_SIZE - 1u) & ~(SIZE_T)(PAGE_SIZE - 1u);
}

NTSTATUS
WinMaliVop2SetupSysmemScanout(_Inout_ PWINMALI_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS low;
    PHYSICAL_ADDRESS high;
    SIZE_T           fbBytes;
    SIZE_T           mapBytes;
    PVOID            scanVa = NULL;
    PVOID            gopVa   = NULL;
    PHYSICAL_ADDRESS scanPa;
    NTSTATUS         st;
    ULONG            virPx;

    if (Adapter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->ScanoutSegmentVa != NULL) {
        return STATUS_SUCCESS;
    }
    if (!Adapter->Vop2.Initialized || Adapter->Vop2.Vop2Regs == NULL) {
        WINMALI_WARN("VOP2 sysmem scanout: VOP2 not initialized");
        return STATUS_DEVICE_NOT_READY;
    }
    if (!Adapter->Gop.Valid || Adapter->Gop.PhysBase.QuadPart == 0u
        || Adapter->Gop.Pitch == 0u || Adapter->Gop.Height == 0u)
    {
        WINMALI_WARN("VOP2 sysmem scanout: GOP not captured");
        return STATUS_DEVICE_NOT_READY;
    }
    if (Adapter->Vop2.ScanoutLayer >= WINMALI_VOP2_LAYER_COUNT
        || Adapter->Vop2.ScanoutYrgbMst == 0u)
    {
        WINMALI_WARN("VOP2 sysmem scanout: no scan-out layer from probe");
        return STATUS_DEVICE_NOT_READY;
    }

    fbBytes = (SIZE_T)Adapter->Gop.Pitch * (SIZE_T)Adapter->Gop.Height;
    if (fbBytes == 0u
        || fbBytes > (SIZE_T)(3840ull * 2160ull * 4ull))
    {
        WINMALI_WARN("VOP2 sysmem scanout: fbBytes=%Iu invalid", fbBytes);
        return STATUS_INVALID_PARAMETER;
    }

    mapBytes = Vop2RoundUpPages_(fbBytes);

    low.QuadPart  = 0;
    high.QuadPart = (ULONG64)-1LL;

    scanVa = MmAllocateContiguousMemorySpecifyCache(
        mapBytes,
        low,
        high,
        low,
        MmCached);
    if (scanVa == NULL) {
        WINMALI_ERROR(
            "VOP2 sysmem scanout: MmAllocateContiguousMemorySpecifyCache(%Iu) failed",
            mapBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    gopVa = MmMapIoSpaceEx(
        Adapter->Gop.PhysBase,
        fbBytes,
        PAGE_READWRITE | PAGE_NOCACHE);
    if (gopVa == NULL) {
        WINMALI_ERROR(
            "VOP2 sysmem scanout: map GOP phys 0x%llx failed",
            Adapter->Gop.PhysBase.QuadPart);
        MmFreeContiguousMemory(scanVa);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(scanVa, gopVa, fbBytes);
    if (mapBytes > fbBytes) {
        RtlFillMemory((PUCHAR)scanVa + fbBytes, mapBytes - fbBytes, 0);
    }
    MmUnmapIoSpace(gopVa, fbBytes);
    gopVa = NULL;

    KeMemoryBarrier();

    scanPa = MmGetPhysicalAddress(scanVa);
    if (scanPa.QuadPart == 0u) {
        WINMALI_ERROR("VOP2 sysmem scanout: MmGetPhysicalAddress returned 0");
        MmFreeContiguousMemory(scanVa);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    virPx = (Adapter->Gop.Width != 0u) ? Adapter->Gop.Width : 1920u;

    st = WinMaliVop2SetScanoutPa(&Adapter->Vop2,
                                 (ULONG)(scanPa.QuadPart & 0xFFFFFFFFull),
                                 virPx);
    if (!NT_SUCCESS(st)) {
        WINMALI_ERROR(
            "VOP2 sysmem scanout: SetScanoutPa failed 0x%08x — freeing slab",
            st);
        MmFreeContiguousMemory(scanVa);
        return st;
    }

    Adapter->ScanoutSegmentVa     = scanVa;
    Adapter->ScanoutSegmentPhys   = scanPa;
    Adapter->ScanoutSegmentBytes  = mapBytes;

    WINMALI_TRACE(
        "VOP2: sysmem scanout ready va=%p phys=0x%llx bytes=%Iu virPx=%u "
        "(dxgk segment id %u / GPU base 0x%llx)",
        scanVa,
        (ULONGLONG)scanPa.QuadPart,
        mapBytes,
        virPx,
        WINMALI_GOP_SEGMENT_ID,
        (ULONGLONG)WINMALI_GOP_GPU_BASE);

    return STATUS_SUCCESS;
}

VOID
WinMaliVop2TeardownScanoutSegment(_Inout_ PWINMALI_ADAPTER Adapter)
{
    if (Adapter == NULL || Adapter->ScanoutSegmentVa == NULL) {
        return;
    }

    //
    // Point VOP2 back at the firmware GOP framebuffer before freeing the
    // sysmem slab so the display controller does not DMA from poisoned
    // pages during the tail of PnP stop.
    //
    if (Adapter->Vop2.Initialized && Adapter->Gop.Valid
        && Adapter->Vop2.ScanoutLayer < WINMALI_VOP2_LAYER_COUNT)
    {
        ULONG virPx = (Adapter->Gop.Width != 0u) ? Adapter->Gop.Width : 1920u;
        (VOID)WinMaliVop2SetScanoutPa(
            &Adapter->Vop2,
            (ULONG)(Adapter->Gop.PhysBase.QuadPart & 0xFFFFFFFFull),
            virPx);
    }

    WINMALI_TRACE(
        "VOP2: freeing scanout slab va=%p phys=0x%llx bytes=%Iu",
        Adapter->ScanoutSegmentVa,
        (ULONGLONG)Adapter->ScanoutSegmentPhys.QuadPart,
        Adapter->ScanoutSegmentBytes);

    MmFreeContiguousMemory(Adapter->ScanoutSegmentVa);
    Adapter->ScanoutSegmentVa            = NULL;
    Adapter->ScanoutSegmentPhys.QuadPart = 0;
    Adapter->ScanoutSegmentBytes         = 0;
}
