/** @file
 *
 * RK3588 VOP2 (Video Output Processor v2) low-level interface for the
 * WinMaliKmd driver. This is the hardware that the boot firmware uses to
 * scan out the GOP framebuffer, and the hardware we eventually need to
 * own ourselves so we can publish a real, sysmem-backed, DirectFlip-capable
 * primary segment to dxgk (instead of pretending the BIOS-reserved GOP
 * scan-out range is a dxgk segment, which Win11 26100 silently refuses to
 * accept under GpuMmu - see WinMaliBuildSegmentList_).
 *
 * Phase 2 (this header) just enumerates the MMIO regions, maps them, and
 * provides a register-read helper plus a state-dump for diagnostics.
 * Phase 2b adds reset / clock / IRQ bring-up; phase 2c adds mode-set;
 * phase 2d wires SetVidPnSourceAddress -> VOP2 VP-A scanout-base register.
 *
 * Address map and register naming all come from the Rockchip RK3588 VOP2
 * documentation, mirrored in three places already in this repository:
 *   1) Reference/edk2-rk3588/.../Library/Vop2Regs.h     (UEFI driver headers)
 *   2) Reference/edk2-rk3588/.../Vop2Dxe/Vop2Dxe.{h,c}  (UEFI driver impl)
 *   3) rk3588-BasicDisplay/rk3588_vop2_regs.hxx         (existing WDDM
 *                                                        display-only driver)
 *
 * For consistency with (3) we use the same hard-coded RK3588 base addresses
 * rather than discovering them via ACPI: the WDDM PDO that dxgkrnl hands us
 * is the GPU node (Mali, RKCP6100), not the VOP2 node, so an ACPI walk to
 * find VOP2 (_HID = "RKCP5650") is the only alternative and adds complexity
 * without buying anything for a fixed-platform driver.
 */

#ifndef _WINMALI_VOP2_H_
#define _WINMALI_VOP2_H_

#include <ntddk.h>

//
// MMIO regions, copied verbatim from rk3588-BasicDisplay/rk3588_vop2_regs.hxx
// and cross-checked against vop2.asl (_HID="RKCP5650" _CRS).
//
// VOP2 itself is at 0xFDD90000 / 64 KiB (the ASL has 0x4200 + a 4 KiB gamma
// LUT block at +0x5000; we map the full 64 KiB so all VP0..VP3, cluster,
// esmart and DSC sub-blocks are reachable via offset arithmetic without a
// separate map for the LUT).
//
// VOP_GRF (system-side glue regs that route the VP outputs to the HDMI / DP
// PHYs and gate their clocks) lives at 0xFD5A4000 / 4 KiB.
// VO1_GRF (HDMI sync polarity, dual-link enable) lives at 0xFD5A8000 / 4 KiB.
// SYS_GRF (top-level chip glue, HDMI tx0/tx1 enable bits) lives at
//   0xFD58C000 / 4 KiB.
//
#define WINMALI_VOP2_REG_BASE       0xFDD90000ULL
#define WINMALI_VOP2_REG_SIZE       0x10000u
#define WINMALI_VOP2_VOP_GRF_BASE   0xFD5A4000ULL
#define WINMALI_VOP2_VOP_GRF_SIZE   0x1000u
#define WINMALI_VOP2_VO1_GRF_BASE   0xFD5A8000ULL
#define WINMALI_VOP2_VO1_GRF_SIZE   0x1000u
#define WINMALI_VOP2_SYS_GRF_BASE   0xFD58C000ULL
#define WINMALI_VOP2_SYS_GRF_SIZE   0x1000u

//
// Global control registers.
//
// REG_CFG_DONE @ +0x000 is the "commit shadow state at next vsync" pulse
// for every VP. Layout: bit N = "commit VP N's shadow regs", bit 15 =
// CFG_DONE_EN arming bit (MUST be set in every write or the request is
// ignored). RK3588 keeps the same RK3568 layout.
//
#define WINMALI_VOP2_REG_CFG_DONE       0x0000u
#define WINMALI_VOP2_CFG_DONE_EN_BIT    (1u << 15)

//
// Identification register (RK3568_VERSION_INFO at +0x004). The expected
// value for RK3588 is 0x4017xxxx ("major 0x40, minor 0x17"); the lower 16
// bits encode the IP revision (we observed 0x6786). 0xFFFFFFFF means MMIO
// didn't decode (region not powered / wrong base); 0x00000000 means
// powered but reset asserted.
//
#define WINMALI_VOP2_REG_VERSION_INFO   0x0004u

//
// Per-VP interrupt control registers. Stride is 0x10 between VPs:
//   VP0: EN=0xA0  CLR=0xA4  STATUS=0xA8
//   VP1: EN=0xB0  CLR=0xB4  STATUS=0xB8
//   VP2: EN=0xC0  CLR=0xC4  STATUS=0xC8
//   VP3: EN=0xD0  CLR=0xD4  STATUS=0xD8
// The Vop2VpIntBase_() helper in WinMaliVop2.c computes the right base
// from a VP id. VSYNC interrupt bit is typically bit 1 in INT_STATUS
// (FS_FIELD on RK3588), but we don't enable IRQs in phase 2b - the dump
// is purely diagnostic so we can see what's already pending / ticking.
//
#define WINMALI_VOP2_VP_INT_BASE         0x00A0u
#define WINMALI_VOP2_VP_INT_STRIDE       0x0010u
#define WINMALI_VOP2_VP_INT_EN_OFFSET    0x00u
#define WINMALI_VOP2_VP_INT_CLR_OFFSET   0x04u
#define WINMALI_VOP2_VP_INT_STATUS_OFFSET 0x08u

//
// Display interface enable / mux register (RK3568_DSP_IF_EN at +0x028).
// Bit 3 = HDMI0 enable, bit 4 = HDMI1 enable, bits 16..17 = HDMI0 VP source
// mux (0..3 -> VP0..VP3), bits 18..19 = HDMI1 VP source mux. UEFI sets these
// when it lights HDMI; reading the register tells us which VP UEFI picked.
//
#define WINMALI_VOP2_REG_DSP_IF_EN      0x0028u

//
// Per-VP DSP control / timing registers. Offsets follow the Rockchip pattern
// VP0=0xC00, VP1=0xD00, VP2=0xE00 (and VP3 on RK3588 at +0x100 stride). The
// timing block (HTOTAL/HACT_ST_END, VTOTAL/VACT_ST_END) starts at +0x48 from
// the per-VP base and tells us exactly which mode UEFI is scanning.
//
#define WINMALI_VOP2_VP0_BASE           0x0C00u
#define WINMALI_VOP2_VP1_BASE           0x0D00u
#define WINMALI_VOP2_VP2_BASE           0x0E00u
#define WINMALI_VOP2_VP3_BASE           0x0F00u
#define WINMALI_VOP2_VP_DSP_CTRL_OFFSET     0x000u  // STANDBY / OUT_MODE / ...
#define WINMALI_VOP2_VP_DSP_HTOTAL_OFFSET   0x048u  // HTOTAL_HS_END
#define WINMALI_VOP2_VP_DSP_HACT_OFFSET     0x04Cu  // HACT_ST_END
#define WINMALI_VOP2_VP_DSP_VTOTAL_OFFSET   0x050u  // VTOTAL_VS_END
#define WINMALI_VOP2_VP_DSP_VACT_OFFSET     0x054u  // VACT_ST_END

#define WINMALI_VOP2_VP_DSP_CTRL_STANDBY_BIT    (1u << 31)

//
// Layer (window) register map. RK3588 has 8 layers split between two
// blocks - 4 Cluster layers (AFBC-capable, base 0x1000) and 4 Esmart
// layers (linear-only, base 0x1800). Stride is 0x200 within each block.
// The Esmart REGION0_* offsets are exactly +0x10..+0x28 from the layer's
// CTRL0 (same as RK3568); Cluster WIN0_* are +0x10..+0x28 from CTRL0 plus
// AFBC sub-block at +0x54..+0x6C.
//
// VOP2 layer routing happens in two registers:
//   RK3568_OVL_PORT_SEL  @ +0x608   - for each WinSelPortOffset slot in
//                                     [0..7], 2 bits at [16 + slot*2] pick
//                                     which VP (0..3) consumes its output.
//   RK3568_OVL_LAYER_SEL @ +0x604   - for each Z-order slot in [0..7],
//                                     4 bits pick which WinID provides it.
//
// LayerSelWinID values (per RK3588 wWinData table):
//   Cluster0=0  Cluster1=1  Esmart0=2  Esmart1=3
//   Cluster2=4  Cluster3=5  Esmart2=6  Esmart3=7
//
#define WINMALI_VOP2_REG_OVL_LAYER_SEL  0x0604u
#define WINMALI_VOP2_REG_OVL_PORT_SEL   0x0608u
#define WINMALI_VOP2_OVL_PORT_SEL_SHIFT_BASE   16u  // per-layer 2-bit fields start here
#define WINMALI_VOP2_OVL_PORT_SEL_MASK         0x3u

#define WINMALI_VOP2_CLUSTER_BASE       0x1000u  // Cluster0 layer regs
#define WINMALI_VOP2_ESMART_BASE        0x1800u  // Esmart0 layer regs
#define WINMALI_VOP2_LAYER_STRIDE       0x0200u  // between consecutive layers

// Cluster WIN0_* offsets (relative to "Cluster N base")
#define WINMALI_VOP2_CLUSTER_WIN0_CTRL0_OFFSET      0x000u
#define WINMALI_VOP2_CLUSTER_WIN0_YRGB_MST_OFFSET   0x010u
#define WINMALI_VOP2_CLUSTER_WIN0_VIR_OFFSET        0x018u  // pitch (bytes)
#define WINMALI_VOP2_CLUSTER_WIN0_ACT_INFO_OFFSET   0x020u  // src w/h-1
#define WINMALI_VOP2_CLUSTER_WIN0_DSP_INFO_OFFSET   0x024u  // dst w/h-1
#define WINMALI_VOP2_CLUSTER_WIN0_DSP_ST_OFFSET     0x028u  // dst x/y
#define WINMALI_VOP2_CLUSTER_WIN0_AFBCD_HDR_OFFSET  0x058u  // AFBC hdr base

// Esmart REGION0_* offsets (relative to "Esmart N base")
#define WINMALI_VOP2_ESMART_R0_CTRL_OFFSET      0x010u
#define WINMALI_VOP2_ESMART_R0_YRGB_MST_OFFSET  0x014u  // linear scan-out PA
#define WINMALI_VOP2_ESMART_R0_VIR_OFFSET       0x01Cu  // pitch (bytes)
#define WINMALI_VOP2_ESMART_R0_ACT_INFO_OFFSET  0x020u  // src w/h-1
#define WINMALI_VOP2_ESMART_R0_DSP_INFO_OFFSET  0x024u  // dst w/h-1
#define WINMALI_VOP2_ESMART_R0_DSP_ST_OFFSET    0x028u  // dst x/y

// WIN_EN bit lives in the layer's CTRL0/REGION0_CTRL @ bit 0.
#define WINMALI_VOP2_WIN_EN_BIT             (1u << 0)
// WIN_FORMAT field in CTRL @ bits 5..1 (RK3568_ESMART0_REGION0_CTRL).
#define WINMALI_VOP2_WIN_FORMAT_SHIFT       1u
#define WINMALI_VOP2_WIN_FORMAT_MASK        0x1Fu

//
// Logical layer identifier (matches Vop2Dxe.h VOP2_LAYER_PHY_ID for
// RK3588). Doubles as an index into WINMALI_VOP2.Layers[].
//
typedef enum _WINMALI_VOP2_LAYER_ID {
    WINMALI_VOP2_LAYER_CLUSTER0 = 0,
    WINMALI_VOP2_LAYER_CLUSTER1,
    WINMALI_VOP2_LAYER_ESMART0,
    WINMALI_VOP2_LAYER_ESMART1,
    WINMALI_VOP2_LAYER_CLUSTER2,
    WINMALI_VOP2_LAYER_CLUSTER3,
    WINMALI_VOP2_LAYER_ESMART2,
    WINMALI_VOP2_LAYER_ESMART3,
    WINMALI_VOP2_LAYER_COUNT
} WINMALI_VOP2_LAYER_ID;

typedef enum _WINMALI_VOP2_LAYER_TYPE {
    WINMALI_VOP2_LAYER_TYPE_CLUSTER = 0,
    WINMALI_VOP2_LAYER_TYPE_ESMART  = 1
} WINMALI_VOP2_LAYER_TYPE;

//
// Per-layer snapshot. Captured by WinMaliVop2DumpLayers; mainly used to
// (a) confirm the layer-id <-> VP mapping UEFI programmed and (b) record
// the live YRGB_MST scan-out PA so phase 2c knows which YRGB_MST register
// to rewrite when dxgk hands us a new presentation buffer.
//
typedef struct _WINMALI_VOP2_LAYER_STATE {
    WINMALI_VOP2_LAYER_ID    Id;
    WINMALI_VOP2_LAYER_TYPE  Type;
    PCSTR                    Name;
    ULONG                    RegBase;       // offset within Vop2Regs
    ULONG                    Ctrl;
    ULONG                    YrgbMst;       // scan-out base PA
    ULONG                    Vir;           // pitch (bytes)
    ULONG                    ActInfo;
    ULONG                    DspInfo;
    ULONG                    DspSt;
    ULONG                    AfbcdHdrPtr;   // 0 for Esmart (linear-only)
    UINT32                   VpRouting;     // 0..3 = VP id this layer feeds
    BOOLEAN                  Enabled;       // CTRL[0] WIN_EN
} WINMALI_VOP2_LAYER_STATE, *PWINMALI_VOP2_LAYER_STATE;

//
// Adapter-attached state. Lives inside WINMALI_ADAPTER (embedded, not a
// pointer, no extra pool allocation). All MMIO pointers are NULL until
// WinMaliVop2Initialize succeeds and become NULL again after Shutdown so
// callers can guard with `if (Vop2->Vop2Regs != NULL)`.
//
typedef struct _WINMALI_VOP2 {
    PVOID    Vop2Regs;        // 0xFDD90000 / 64 KiB - main VOP2 block
    PVOID    VopGrfRegs;      // 0xFD5A4000 /  4 KiB - VOP-GRF (HDMI tx enable)
    PVOID    Vo1GrfRegs;      // 0xFD5A8000 /  4 KiB - VO1-GRF (HDMI sync pol)
    PVOID    SysGrfRegs;      // 0xFD58C000 /  4 KiB - SYS-GRF
    BOOLEAN  Initialized;     // TRUE if all four maps succeeded

    //
    // Snapshot captured at WinMaliVop2DumpState time. Used both for
    // diagnostics (logged at bring-up) and as the source-of-truth modeline
    // we'll feed back into VidPn cofunc enumeration when phase 2c hooks
    // CommitVidPn / SetTimingsFromVidPn.
    //
    UINT32   VersionInfo;     // RK3568_VERSION_INFO @ +0x004
    UINT32   DspIfEn;         // RK3568_DSP_IF_EN    @ +0x028
    UINT32   OvlLayerSel;     // RK3568_OVL_LAYER_SEL @ +0x604 (Z-order)
    UINT32   OvlPortSel;      // RK3568_OVL_PORT_SEL  @ +0x608 (layer->VP)
    UINT32   ActiveVpId;      // VP currently driving HDMI 0 (0..3)
    UINT32   ActiveDspCtrl;   // VPx_DSP_CTRL value
    UINT32   ActiveHTotalHsEnd;
    UINT32   ActiveHActStEnd;
    UINT32   ActiveVTotalVsEnd;
    UINT32   ActiveVActStEnd;
    UINT32   ActiveHActive;   // pixels (decoded from HACT_ST_END)
    UINT32   ActiveVActive;   // lines  (decoded from VACT_ST_END)

    //
    // Per-layer snapshot. Layers[i].Id == i. Captured by DumpLayers.
    //
    WINMALI_VOP2_LAYER_STATE  Layers[WINMALI_VOP2_LAYER_COUNT];

    //
    // The single layer we've identified as "the one scanning out the
    // current GOP framebuffer to the active VP". Set by DumpLayers as the
    // enabled layer whose VpRouting matches ActiveVpId and whose YrgbMst
    // is non-zero. WINMALI_VOP2_LAYER_COUNT == invalid / not-yet-resolved.
    //
    WINMALI_VOP2_LAYER_ID     ScanoutLayer;
    ULONG                     ScanoutYrgbMst;  // cached for sanity-check
} WINMALI_VOP2, *PWINMALI_VOP2;

//
// Initialise the VOP2 sub-block: map all four MMIO regions and capture a
// state snapshot via WinMaliVop2DumpState. Idempotent: a second call with
// Vop2->Initialized==TRUE returns STATUS_SUCCESS without re-mapping.
//
// Failure to map any individual region is logged and treated as soft -
// we leave the corresponding pointer NULL and return STATUS_SUCCESS so
// the GPU bring-up still completes. The display path can then probe
// `Vop2->Initialized` to see whether VOP2 is actually usable on this run.
//
NTSTATUS
WinMaliVop2Initialize(_Inout_ PWINMALI_VOP2 Vop2);

//
// Tear down the VOP2 sub-block. Safe to call even if Initialize was never
// called or partially failed (NULL-tolerant on each region pointer).
//
VOID
WinMaliVop2Shutdown(_Inout_ PWINMALI_VOP2 Vop2);

//
// Read VP0..VP3 control + timings, decode the active modeline, and emit
// it through WINMALI_TRACE so the boot log shows exactly what UEFI handed
// us. Sets the Active* fields on the Vop2 struct as a side effect. Also
// invokes WinMaliVop2DumpLayers internally so a single call gives us a
// full picture of UEFI's VOP2 programming.
//
// Safe at PASSIVE_LEVEL only (does I/O register reads). Caller is
// responsible for not invoking it from ISR / DPC.
//
VOID
WinMaliVop2DumpState(_Inout_ PWINMALI_VOP2 Vop2);

//
// Walk all 8 RK3588 layers (Cluster0..3, Esmart0..3), read their CTRL/
// YRGB_MST/VIR/ACT_INFO/DSP_INFO/DSP_ST registers + the OVL_PORT_SEL
// per-layer mux bits, log a one-line summary per layer, and populate
// Vop2->Layers[].Vop2->ScanoutLayer is set to the single enabled layer
// whose VpRouting matches ActiveVpId; ScanoutYrgbMst is the live PA.
// Read-only - never writes a VOP2 register.
//
VOID
WinMaliVop2DumpLayers(_Inout_ PWINMALI_VOP2 Vop2);

//
// Rewrite the scan-out base PA of the currently-active layer (the one
// identified by Vop2->ScanoutLayer) and pulse REG_CFG_DONE for the
// owning VP so VOP2 latches the new address on the next vsync edge.
//
//   YrgbMst   - new GPU/CPU physical address of the surface to scan out.
//               Must be 4-byte aligned (Esmart) or 16-byte aligned (Cluster).
//   VirPixels - pitch of the surface in PIXELS (not bytes); VOP2 multiplies
//               by the format's bpp internally. Pass 0 to leave VIR alone.
//
// Returns:
//   STATUS_SUCCESS              register write accepted (vsync will latch)
//   STATUS_DEVICE_NOT_READY     Vop2 not initialised or no scan-out layer
//   STATUS_INVALID_PARAMETER    YrgbMst is 0 or violates alignment
//
// SAFETY: this is the ONLY function in this module that writes to VOP2.
// Callers MUST own (or coordinate with) the VOP2 scan-out path so we
// don't race CommitVidPn / mode-set. Until phase 2c connects vsync, the
// call site is expected to be SetVidPnSourceAddress at PASSIVE_LEVEL.
//
NTSTATUS
WinMaliVop2SetScanoutPa(
    _Inout_ PWINMALI_VOP2 Vop2,
    _In_    UINT32        YrgbMst,
    _In_    UINT32        VirPixels);

#endif // _WINMALI_VOP2_H_
