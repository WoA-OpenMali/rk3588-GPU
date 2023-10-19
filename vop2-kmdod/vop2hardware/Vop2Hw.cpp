
#include "../Vop2Kmd.hpp"

NTSTATUS
NTAPI
DisplaySetCrtcInfo (
  OUT DRM_DISPLAY_MODE                     *Mode,
  IN  UINT32                               AdjustFlags
  )
{
  if ((Mode == NULL) || ((Mode->Type & DRM_MODE_TYPE_CRTC_C) == DRM_MODE_TYPE_BUILTIN))
    return 1;

  if (Mode->Flags & DRM_MODE_FLAG_DBLCLK)
    Mode->CrtcClock = 2 * Mode->Clock;
  else
    Mode->CrtcClock = Mode->Clock;
  Mode->CrtcHDisplay = Mode->HDisplay;
  Mode->CrtcHSyncStart = Mode->HSyncStart;
  Mode->CrtcHSyncEnd = Mode->HSyncEnd;
  Mode->CrtcHTotal = Mode->HTotal;
  Mode->CrtcHSkew = Mode->HSkew;
  Mode->CrtcVDisplay = Mode->VDisplay;
  Mode->CrtcVSyncStart = Mode->VSyncStart;
  Mode->CrtcVSyncEnd = Mode->VSyncEnd;
  Mode->CrtcVTotal = Mode->VTotal;

  if (Mode->Flags & DRM_MODE_FLAG_INTERLACE) {
    if (AdjustFlags & CRTC_INTERLACE_HALVE_V) {
      Mode->CrtcVDisplay /= 2;
      Mode->CrtcVSyncStart /= 2;
      Mode->CrtcVSyncEnd /= 2;
      Mode->CrtcVTotal /= 2;
    }
  }

  if (!(AdjustFlags & CRTC_NO_DBLSCAN)) {
    if (Mode->Flags & DRM_MODE_FLAG_DBLSCAN) {
      Mode->CrtcVDisplay *= 2;
      Mode->CrtcVSyncStart *= 2;
      Mode->CrtcVSyncEnd *= 2;
      Mode->CrtcVTotal *= 2;
    }
  }

  if (!(AdjustFlags & CRTC_NO_VSCAN)) {
    if (Mode->VScan > 1) {
      Mode->CrtcVDisplay *= Mode->VScan;
      Mode->CrtcVSyncStart *= Mode->VScan;
      Mode->CrtcVSyncEnd *= Mode->VScan;
      Mode->CrtcVTotal *= Mode->VScan;
    }
  }

  Mode->CrtcVBlankStart = min(Mode->CrtcVSyncStart, Mode->CrtcVDisplay);
  Mode->CrtcVBlankEnd = max(Mode->CrtcVSyncEnd, Mode->CrtcVTotal);
  Mode->CrtcHBlankStart = min(Mode->CrtcHSyncStart, Mode->CrtcHDisplay);
  Mode->CrtcHBblankEnd = max(Mode->CrtcHSyncEnd, Mode->CrtcHTotal);

  return STATUS_SUCCESS;
}

VOP2_VP_PLANE_MASK mVpPlaneMaskRK3588[VOP2_VP_MAX][VOP2_VP_MAX] = {
  { /* one display policy */
    {/* main display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART0,
      .AttachedLayersNr = 8,
      .AttachedLayers = {
        ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0,
        ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1,
        ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART2,
        ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART3
      },
    },
    {/* second display */},
    {/* third  display */},
    {/* fourth display */},
  },

  { /* two display policy */
    {/* main display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART0,
      .AttachedLayersNr = 4,
      .AttachedLayers = {
        ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0,
        ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1
      },
    },

    {/* second display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART2,
      .AttachedLayersNr = 4,
      .AttachedLayers = {
        ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART2,
        ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART3
      },
    },
    {/* third  display */},
    {/* fourth display */},
  },

  { /* three display policy */
    {/* main display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART0,
      .AttachedLayersNr = 3,
      .AttachedLayers = {
        ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0,
        ROCKCHIP_VOP2_CLUSTER1
      },
    },

    {/* second display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART1,
      .AttachedLayersNr = 3,
      .AttachedLayers = {
        ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART1,
        ROCKCHIP_VOP2_CLUSTER3
      },
    },

    {/* third  display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART2,
      .AttachedLayersNr = 2,
      .AttachedLayers = { ROCKCHIP_VOP2_ESMART2, ROCKCHIP_VOP2_ESMART3 },
    },

    {/* fourth display */},
  },

  { /* four display policy */
    {/* main display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART0,
      .AttachedLayersNr = 2,
      .AttachedLayers = { ROCKCHIP_VOP2_CLUSTER0, ROCKCHIP_VOP2_ESMART0 },
    },

    {/* second display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART1,
      .AttachedLayersNr = 2,
      .AttachedLayers = { ROCKCHIP_VOP2_CLUSTER1, ROCKCHIP_VOP2_ESMART1 },
    },

    {/* third  display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART2,
      .AttachedLayersNr = 2,
      .AttachedLayers = { ROCKCHIP_VOP2_CLUSTER2, ROCKCHIP_VOP2_ESMART2 },
    },

    {/* fourth display */
      .PrimaryPlaneId = ROCKCHIP_VOP2_ESMART3,
      .AttachedLayersNr = 2,
      .AttachedLayers = { ROCKCHIP_VOP2_CLUSTER3, ROCKCHIP_VOP2_ESMART3 },
    },
  },
};
VOP2_VP_DATA mVpDataRK3588[4] = {
  {
    .Feature = VOP_FEATURE_OUTPUT_10BIT,
    .MaxDclk = 600000,
    .PreScanMaxDly = 42,
    .MaxOutput = {7680, 4320},
  },
  {
    .Feature = VOP_FEATURE_OUTPUT_10BIT,
    .MaxDclk = 600000,
    .PreScanMaxDly = 40,
    .MaxOutput = {4096, 2304},
  },
  {
    .Feature = VOP_FEATURE_OUTPUT_10BIT,
    .MaxDclk = 600000,
    .PreScanMaxDly = 52,
    .MaxOutput = {4096, 2304},
  },
  {
    .Feature = 0,
    .MaxDclk = 200000,
    .PreScanMaxDly = 52,
    .MaxOutput = {1920, 1080},
  },
};

static VOP2_POWER_DOMAIN_DATA mCluster0PdDataRK3588 = {
  .PdEnShift = RK3588_CLUSTER0_PD_EN_SHIFT,
  .PdStatusShift = RK3588_CLUSTER0_PD_STATUS_SHIFT,
  .PmuStatusShift = RK3588_PD_CLUSTER0_PWR_STAT_SHIFI,
  .BisrEnStatusShift = RK3588_PD_CLUSTER0_REPAIR_EN_SHIFT,
};

static VOP2_POWER_DOMAIN_DATA mCluster1PdDataRK3588 = {
  .IsParentNeeded = TRUE,
  .PdEnShift = RK3588_CLUSTER1_PD_EN_SHIFT,
  .PdStatusShift = RK3588_CLUSTER1_PD_STATUS_SHIFT,
  .PmuStatusShift = RK3588_PD_CLUSTER1_PWR_STAT_SHIFI,
  .BisrEnStatusShift = RK3588_PD_CLUSTER1_REPAIR_EN_SHIFT,
  .ParentPhyID = ROCKCHIP_VOP2_CLUSTER0,
};

static VOP2_POWER_DOMAIN_DATA mCluster2PdDataRK3588 = {
  .IsParentNeeded = TRUE,
  .PdEnShift = RK3588_CLUSTER2_PD_EN_SHIFT,
  .PdStatusShift = RK3588_CLUSTER2_PD_STATUS_SHIFT,
  .PmuStatusShift = RK3588_PD_CLUSTER2_PWR_STAT_SHIFI,
  .BisrEnStatusShift = RK3588_PD_CLUSTER2_REPAIR_EN_SHIFT,
  .ParentPhyID = ROCKCHIP_VOP2_CLUSTER0,
};

static VOP2_POWER_DOMAIN_DATA mCluster3PdDataRK3588 = {
  .IsParentNeeded = TRUE,
  .PdEnShift = RK3588_CLUSTER3_PD_EN_SHIFT,
  .PdStatusShift = RK3588_CLUSTER3_PD_STATUS_SHIFT,
  .PmuStatusShift = RK3588_PD_CLUSTER3_PWR_STAT_SHIFI,
  .BisrEnStatusShift = RK3588_PD_CLUSTER3_REPAIR_EN_SHIFT,
  .ParentPhyID = ROCKCHIP_VOP2_CLUSTER0,
};

static VOP2_POWER_DOMAIN_DATA mEsmartPdDataRK3588 = {
  .PdEnShift = RK3588_ESMART_PD_EN_SHIFT,
  .PdStatusShift = RK3588_ESMART_PD_STATUS_SHIFT,
  .PmuStatusShift = RK3588_PD_ESMART_PWR_STAT_SHIFI,
  .BisrEnStatusShift = RK3588_PD_ESMART_REPAIR_EN_SHIFT,
};


VOP2_WIN_DATA mWinDataRK3588[8] = {
  {
    .Type = CLUSTER_LAYER,
    .Name = (CHAR*)"Cluster0",
    .PhysID = ROCKCHIP_VOP2_CLUSTER0,
    .WinSelPortOffset = 0,
    .LayerSelWinID = 0,
    .RegOffset = 0,
    .PdData = &mCluster0PdDataRK3588,
  },

  {
    .Type = CLUSTER_LAYER,
    .Name = (CHAR*)"Cluster1",
    .PhysID = ROCKCHIP_VOP2_CLUSTER1,
    .WinSelPortOffset = 1,
    .LayerSelWinID = 1,
    .RegOffset = 0x200,
    .PdData = &mCluster1PdDataRK3588,
  },

  {
    .Type = CLUSTER_LAYER,
    .Name = (CHAR*)"Cluster2",
    .PhysID = ROCKCHIP_VOP2_CLUSTER2,
    .WinSelPortOffset = 2,
    .LayerSelWinID = 4,
    .RegOffset = 0x400,
    .PdData = &mCluster2PdDataRK3588,
  },

  {
    .Type = CLUSTER_LAYER,
    .Name = (CHAR*)"Cluster3",
    .PhysID = ROCKCHIP_VOP2_CLUSTER3,
    .WinSelPortOffset = 3,
    .LayerSelWinID = 5,
    .RegOffset = 0x600,
    .PdData = &mCluster3PdDataRK3588,
  },

  {
    .Type = ESMART_LAYER,
    .Name = (CHAR*)"Esmart0",
    .PhysID = ROCKCHIP_VOP2_ESMART0,
    .WinSelPortOffset = 4,
    .LayerSelWinID = 2,
    .RegOffset = 0,
    .PdData = &mEsmartPdDataRK3588,
  },

  {
    .Type = ESMART_LAYER,
    .Name = (CHAR*)"Esmart1",
    .PhysID = ROCKCHIP_VOP2_ESMART1,
    .WinSelPortOffset = 5,
    .LayerSelWinID = 3,
    .RegOffset = 0x200,
    .PdData = &mEsmartPdDataRK3588,
  },

  {
    .Type = ESMART_LAYER,
    .Name = (CHAR*)"Esmart2",
    .PhysID = ROCKCHIP_VOP2_ESMART2,
    .WinSelPortOffset = 6,
    .LayerSelWinID = 6,
    .RegOffset = 0x400,
    .PdData = &mEsmartPdDataRK3588,
  },

  {
    .Type = ESMART_LAYER,
    .Name = (CHAR*)"Esmart3",
    .PhysID = ROCKCHIP_VOP2_ESMART3,
    .WinSelPortOffset = 7,
    .LayerSelWinID = 7,
    .RegOffset = 0x600,
    .PdData = &mEsmartPdDataRK3588,
  },
};

VOP2_DATA mVop2RK3588 = {
  .Version = VOP_VERSION_RK3588,
  .VpData = mVpDataRK3588,
  .WinData = mWinDataRK3588,
/*
  .plane_table = rk3588_plane_table,
*/
  .PlaneMask = mVpPlaneMaskRK3588[0],
  .NrVps = 4,
  .NrLayers = 8,
  .NrMixers = 7,
  .NrGammas = 4,
  .NrDscs = 2,
};

/* Driver calls ******************************************************************/

#pragma warning(disable : 4996)
#pragma warning(default : 4302)
#pragma warning(default : 4311)
UINT32 mRegsBackup[RK3568_MAX_REG] = {0};

VOID
Vop2MaskWrite (
  IN  ULONG_PTR                                 Address,
  IN  UINT32                                Offset,
  IN  UINT32                                Mask,
  IN  UINT32                                Shift,
  IN  UINT32                                Value,
  IN  BOOLEAN                               WriteMask
  )
{
  UINT32 CachedVal;

  if (!Mask)
    return;

  if (WriteMask) {
    Value = ((Value & Mask) << Shift) | (Mask << (Shift + 16));
  } else {
    CachedVal = mRegsBackup[Offset >> 2];

    Value = (CachedVal & ~(Mask << Shift)) | ((Value & Mask) << Shift);
    mRegsBackup[Offset >> 2] = Value;
  }

  WRITE_PORT_ULONG((PULONG)Address + Offset, Value);
}

VOID
Vop2Writel (
  IN ULONG_PTR                                 Address,
  IN UINT32                                Offset,
  IN UINT32                                Value
)
{
  WRITE_PORT_ULONG((PULONG)Address + Offset, Value);
  mRegsBackup[Offset >> 2] = Value;
}

VOID
Vop2GrfWrite (
  IN  ULONG_PTR                                 Address,
  IN  UINT32                                Offset,
  IN  UINT32                                Mask,
  IN  UINT32                                Shift,
  IN  UINT32                                Value
  )
{
  UINT32 TempVal = 0;

  TempVal = (Value << Shift) | (Mask << (Shift + 16));
  WRITE_PORT_ULONG((PULONG)Address + Offset, TempVal);
}

UINT32 GenericHWeight32 (
  IN  UINT32                                 W
  )
{
  UINT32 Res = (W & 0x55555555) + ((W >> 1) & 0x55555555);
  Res = (Res & 0x33333333) + ((Res >> 2) & 0x33333333);
  Res = (Res & 0x0F0F0F0F) + ((Res >> 4) & 0x0F0F0F0F);
  Res = (Res & 0x00FF00FF) + ((Res >> 8) & 0x00FF00FF);
  return (Res & 0x0000FFFF) + ((Res >> 16) & 0x0000FFFF);
}

INT32 FFS(int x)
{
  int r = 1;

  if (!x)
    return 0;
  if (!(x & 0xffff)) {
    x >>= 16;
    r += 16;
  }
  if (!(x & 0xff)) {
    x >>= 8;
    r += 8;
  }
  if (!(x & 0xf)) {
    x >>= 4;
    r += 4;
  }
  if (!(x & 3)) {
    x >>= 2;
    r += 2;
  }
  if (!(x & 1))
    r += 1;
  return r;
}
PVOID PubRegisterAddress;
#if 0
VOID
Vop2DumpRegisters (
   DISPLAY_STATE                        *DisplayState,
   VOP2_WIN_DATA                        *WinData
  )
{
  CRTC_STATE *CrtcState = &DisplayState->CrtcState;
  VOP2HW *Vop2 = (VOP2HW*)CrtcState->Private;
  UINT32 VPOffset = CrtcState->CrtcID * 0x100;
  UINT32 WinOffset = 0;
  UINT8 PrimaryPlaneID = 0;
  INT32 i = 0;
  PVOID Reg = PubRegisterAddress;

  /* sys registers */
  DPRINT1(("SYS:"));
  for (i = 0; i < 0x100; i += 4) {
    if (i % 0x10 == 0) {
      DPRINT1(("\n"));
      DPRINT1(("%x:", (ULONG_PTR)Reg + i));
    }

    DPRINT1((" %08x", READ_PORT_ULONG((PULONG)Reg + i)));
  }
  DPRINT1(("\n"));
  DPRINT1(("\n"));

  /* ovl registers */
  Reg = (PVOID)((ULONG_PTR)PubRegisterAddress + RK3568_OVL_CTRL);
  DPRINT1(( "OVL:"));
  for (i = 0; i < 0x100; i += 4) {
    if (i % 0x10 == 0) {
      DPRINT1 (("\n"));
      DPRINT1 (("%x:", (ULONG_PTR)Reg + i));
    }

    DPRINT1((" %08x", READ_PORT_ULONG((PULONG)Reg + i)));
  }
  DPRINT1(("\n"));
  DPRINT1(("\n"));

  /* hdr registers */
  Reg = (PVOID)((ULONG_PTR)PubRegisterAddress + 0x2000);
  DPRINT1(("HDR:"));
  for (i = 0; i < 0x40; i += 4) {
    if (i % 0x10 == 0) {
      DPRINT1(("\n"));
      DPRINT1(("%x:", (ULONG_PTR)Reg + i));
    }

    DPRINT1((" %08x", READ_PORT_ULONG((PULONG)Reg + i)));
  }
  DPRINT1(("\n"));
  DPRINT1(("\n"));

  /* vp registers */
  Reg = (PVOID)((ULONG_PTR)PubRegisterAddress + RK3568_VP0_DSP_CTRL + VPOffset);
  DPRINT1(("VP%d:", CrtcState->CrtcID));
  for (i = 0; i < 0x100; i += 4) {
    if (i % 0x10 == 0) {
      DPRINT1(("\n"));
      DPRINT1(("%x:", (ULONG_PTR)Reg + i));
    }

    DPRINT1((" %08x", READ_PORT_ULONG((PULONG)Reg + i)));
  }
  DPRINT1(("\n"));
  DPRINT1(("\n"));

  /* plane registers */
  if (WinData) {
    WinOffset = WinData->RegOffset;
    if (WinData->Type == CLUSTER_LAYER)
        Reg = (PVOID)((ULONG_PTR)PubRegisterAddress + RK3568_CLUSTER0_WIN0_CTRL0 + WinOffset);
    else
        Reg = (PVOID)((ULONG_PTR)PubRegisterAddress + RK3568_ESMART0_CTRL0 + WinOffset);
    PrimaryPlaneID = Vop2->VpPlaneMask[CrtcState->CrtcID].PrimaryPlaneId;

    DPRINT1(("%a:", GetPlaneName(PrimaryPlaneID)));
    for (i = 0; i < 0x100; i += 4) {
      if (i % 0x10 == 0) {
        DPRINT1 (("\n"));
        DPRINT1 (("%x:", (ULONG_PTR)Reg + i));
      }

      DPRINT1((" %08x", READ_PORT_ULONG((PULONG)Reg + i)));
    }
    DPRINT1(("\n"));
    DPRINT1(("\n"));
  }
}
#endif

VOP2HW *RockchipVop2;
NTSTATUS
HwVop2Init(PVOID RegisterAddress)
{
    DbgPrint("HwVop2Init: Enter: VA: %X\n", RegisterAddress);
    DPRINT1("test\n");
    PubRegisterAddress = RegisterAddress;
    RockchipVop2 = (VOP2HW*)ExAllocatePoolWithTag(NonPagedPool, sizeof(VOP2HW), VOP2TAG);
    RockchipVop2->BaseAddress = RegisterAddress;
    RockchipVop2->Version = mVop2RK3588.Version;
    RockchipVop2->Data = &mVop2RK3588;
    RockchipVop2->GlobalInit = FALSE;
    memset(RockchipVop2->VpPlaneMask, 0, sizeof(VOP2_VP_PLANE_MASK) * VOP2_VP_MAX);
    return 0;
}