/* This file made up of a few BSD licensed headers from rockchip*/

/** @file

  Copyright (c) 2022 Rockchip Electronics Co. Ltd.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#define __ROUND_MASK(x, y) ((__typeof__(x))((y)-1))
#define ROUNDUP(x, y) ((((x)-1) | __ROUND_MASK(x, y))+1)
#define ROUNDDOWN(x, y) ((x) & ~__ROUND_MASK(x, y))
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define INLINE static inline

#define BIT(x)                (1 << x)

#define EDID_SIZE             128

typedef enum {
  ROCKCHIP_FMT_ARGB8888 = 0,
  ROCKCHIP_FMT_RGB888,
  ROCKCHIP_FMT_RGB565,
  ROCKCHIP_FMT_YUV420SP = 4,
  ROCKCHIP_FMT_YUV422SP,
  ROCKCHIP_FMT_YUV444SP,
} DATA_FORMAT;

#define ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE    BIT(0)
#define ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE      BIT(1)
#define ROCKCHIP_OUTPUT_DATA_SWAP                       BIT(2)
#define ROCKCHIP_OUTPUT_MIPI_DS_MODE                    BIT(3)

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888          0
#define ROCKCHIP_OUT_MODE_BT1120        0
#define ROCKCHIP_OUT_MODE_P666          1
#define ROCKCHIP_OUT_MODE_P565          2
#define ROCKCHIP_OUT_MODE_BT656         5
#define ROCKCHIP_OUT_MODE_S888          8
#define ROCKCHIP_OUT_MODE_S888_DUMMY    12
#define ROCKCHIP_OUT_MODE_YUV420        14
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA          15

#define VOP_OUTPUT_IF_RGB               BIT(0)
#define VOP_OUTPUT_IF_BT1120            BIT(1)
#define VOP_OUTPUT_IF_BT656             BIT(2)
#define VOP_OUTPUT_IF_LVDS0             BIT(3)
#define VOP_OUTPUT_IF_LVDS1             BIT(4)
#define VOP_OUTPUT_IF_MIPI0             BIT(5)
#define VOP_OUTPUT_IF_MIPI1             BIT(6)
#define VOP_OUTPUT_IF_eDP0              BIT(7)
#define VOP_OUTPUT_IF_eDP1              BIT(8)
#define VOP_OUTPUT_IF_DP0               BIT(9)
#define VOP_OUTPUT_IF_DP1               BIT(10)
#define VOP_OUTPUT_IF_HDMI0             BIT(11)
#define VOP_OUTPUT_IF_HDMI1             BIT(12)


#define DRM_DISPLAY_INFO_LEN	32
#define DRM_CONNECTOR_NAME_LEN	32
#define DRM_DISPLAY_MODE_LEN	32
#define DRM_PROP_NAME_LEN	32

#define DRM_MODE_TYPE_BUILTIN	(1<<0)
#define DRM_MODE_TYPE_CLOCK_C	((1<<1) | DRM_MODE_TYPE_BUILTIN)
#define DRM_MODE_TYPE_CRTC_C	((1<<2) | DRM_MODE_TYPE_BUILTIN)
#define DRM_MODE_TYPE_PREFERRED	(1<<3)
#define DRM_MODE_TYPE_DEFAULT	(1<<4)
#define DRM_MODE_TYPE_USERDEF	(1<<5)
#define DRM_MODE_TYPE_DRIVER	(1<<6)

/* Video mode flags */
/* bit compatible with the xorg definitions. */
#define DRM_MODE_FLAG_PHSYNC			(1 << 0)
#define DRM_MODE_FLAG_NHSYNC			(1 << 1)
#define DRM_MODE_FLAG_PVSYNC			(1 << 2)
#define DRM_MODE_FLAG_NVSYNC			(1 << 3)
#define DRM_MODE_FLAG_INTERLACE			(1 << 4)
#define DRM_MODE_FLAG_DBLSCAN			(1 << 5)
#define DRM_MODE_FLAG_CSYNC			(1 << 6)
#define DRM_MODE_FLAG_PCSYNC			(1 << 7)
#define DRM_MODE_FLAG_NCSYNC			(1 << 8)
#define DRM_MODE_FLAG_HSKEW			(1 << 9) /* hskew provided */
#define DRM_MODE_FLAG_BCAST			(1 << 10)
#define DRM_MODE_FLAG_PIXMUX			(1 << 11)
#define DRM_MODE_FLAG_DBLCLK			(1 << 12)
#define DRM_MODE_FLAG_CLKDIV2			(1 << 13)
#define DRM_MODE_FLAG_PPIXDATA			BIT(31)

/* Panel Mirror control */
#define DRM_MODE_FLAG_XMIRROR			(1<<28)
#define DRM_MODE_FLAG_YMIRROR			(1<<29)
#define DRM_MODE_FLAG_XYMIRROR			(DRM_MODE_FLAG_XMIRROR | DRM_MODE_FLAG_YMIRROR)

/* Picture aspect ratio options */
#define DRM_MODE_PICTURE_ASPECT_NONE		0
#define DRM_MODE_PICTURE_ASPECT_4_3		1
#define DRM_MODE_PICTURE_ASPECT_16_9		2
#define DRM_MODE_PICTURE_ASPECT_64_27		3
#define DRM_MODE_PICTURE_ASPECT_256_135		4

/* Aspect ratio flag bitmask (4 bits 22:19) */
#define DRM_MODE_FLAG_PIC_AR_MASK		(0x0F << 19)
#define  DRM_MODE_FLAG_PIC_AR_NONE \
			(DRM_MODE_PICTURE_ASPECT_NONE << 19)
#define  DRM_MODE_FLAG_PIC_AR_4_3 \
			(DRM_MODE_PICTURE_ASPECT_4_3 << 19)
#define  DRM_MODE_FLAG_PIC_AR_16_9 \
			(DRM_MODE_PICTURE_ASPECT_16_9 << 19)
#define  DRM_MODE_FLAG_PIC_AR_64_27 \
			(DRM_MODE_PICTURE_ASPECT_64_27 << 19)
#define  DRM_MODE_FLAG_PIC_AR_256_135 \
			(DRM_MODE_PICTURE_ASPECT_256_135 << 19)

#define DRM_MODE_CONNECTOR_Unknown	0
#define DRM_MODE_CONNECTOR_VGA		1
#define DRM_MODE_CONNECTOR_DVII		2
#define DRM_MODE_CONNECTOR_DVID		3
#define DRM_MODE_CONNECTOR_DVIA		4
#define DRM_MODE_CONNECTOR_Composite	5
#define DRM_MODE_CONNECTOR_SVIDEO	6
#define DRM_MODE_CONNECTOR_LVDS		7
#define DRM_MODE_CONNECTOR_Component	8
#define DRM_MODE_CONNECTOR_9PinDIN	9
#define DRM_MODE_CONNECTOR_DisplayPort	10
#define DRM_MODE_CONNECTOR_HDMIA	11
#define DRM_MODE_CONNECTOR_HDMIB	12
#define DRM_MODE_CONNECTOR_TV		13
#define DRM_MODE_CONNECTOR_eDP		14
#define DRM_MODE_CONNECTOR_VIRTUA	15
#define DRM_MODE_CONNECTOR_DSI		16
#define DRM_MODE_CONNECTOR_DPI		17

#define DRM_EDID_PT_HSYNC_POSITIVE (1 << 1)
#define DRM_EDID_PT_VSYNC_POSITIVE (1 << 2)
#define DRM_EDID_PT_SEPARATE_SYNC  (3 << 3)
#define DRM_EDID_PT_STEREO         (1 << 5)
#define DRM_EDID_PT_INTERLACED     (1 << 7)

#define VOP_VERSION(major, minor)	((major) << 8 | (minor))
#define VOP_MAJOR(version)		((version) >> 8)
#define VOP_MINOR(version)		((version) & 0xFF)

#define VOP_VERSION_RK3568		VOP_VERSION(0x40, 0x15)
#define VOP_VERSION_RK3588		VOP_VERSION(0x40, 0x17)

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888		0
#define ROCKCHIP_OUT_MODE_BT1120	0
#define ROCKCHIP_OUT_MODE_P666		1
#define ROCKCHIP_OUT_MODE_P565		2
#define ROCKCHIP_OUT_MODE_BT656		5
#define ROCKCHIP_OUT_MODE_S888		8
#define ROCKCHIP_OUT_MODE_S888_DUMMY	12
#define ROCKCHIP_OUT_MODE_YUV420	14
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA		15

#define VOP_OUTPUT_IF_RGB		BIT(0)
#define VOP_OUTPUT_IF_BT1120		BIT(1)
#define VOP_OUTPUT_IF_BT656		BIT(2)
#define VOP_OUTPUT_IF_LVDS0		BIT(3)
#define VOP_OUTPUT_IF_LVDS1		BIT(4)
#define VOP_OUTPUT_IF_MIPI0		BIT(5)
#define VOP_OUTPUT_IF_MIPI1		BIT(6)
#define VOP_OUTPUT_IF_eDP0		BIT(7)
#define VOP_OUTPUT_IF_eDP1		BIT(8)
#define VOP_OUTPUT_IF_DP0		BIT(9)
#define VOP_OUTPUT_IF_DP1		BIT(10)
#define VOP_OUTPUT_IF_HDMI0		BIT(11)
#define VOP_OUTPUT_IF_HDMI1		BIT(12)

#define VOP_OUTPUT_IF_NUMS		13

#define VOP2_LAYER_MAX			8

#define VOP_FEATURE_OUTPUT_10BIT	BIT(0)

/* KHz */
#define VOP2_MAX_DCLK_RATE			600000

#define CRTC_INTERLACE_HALVE_V	(1 << 0) /* halve V values for interlacing */
#define CRTC_STEREO_DOUBLE	(1 << 1) /* adjust timings for stereo modes */
#define CRTC_NO_DBLSCAN		(1 << 2) /* don't adjust doublescan */
#define CRTC_NO_VSCAN		(1 << 3) /* don't adjust doublescan */
#define CRTC_STEREO_DOUBLE_ONLY	(CRTC_STEREO_DOUBLE | CRTC_NO_DBLSCAN | \
				 CRTC_NO_VSCAN)

#define DRM_MODE_FLAG_3D_MAX	DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF
typedef struct {
  UINT32                      Mode;
  UINT32                      Offset;
  UINT32                      Width;
  UINT32                      Height;
  UINT32                      Bpp;
  PUCHAR                      Memory;
  BOOLEAN                     YMirror;
} LOGO_INFO;

typedef struct {
  UINT32                      Width;
  UINT32                      Height;
} VOP_RECT;

typedef struct {
  UINT32                      LeftMargin;
  UINT32                      RightMargin;
  UINT32                      TopMargin;
  UINT32                      BottomMargin;
} OVER_SCAN;

typedef struct {
  /* Proposed mode values */
  UINT32                      Clock;		/* in kHz */
  UINT32                      HDisplay;
  UINT32                      HSyncStart;
  UINT32                      HSyncEnd;
  UINT32                      HTotal;
  UINT32                      VDisplay;
  UINT32                      VSyncStart;
  UINT32                      VSyncEnd;
  UINT32                      VTotal;
  UINT32                      VRefresh;
  UINT32                      VScan;
  UINT32                      Flags;
  UINT32                      PictureAspectRatio;
  UINT32                      HSkew;
  UINT32                      Type;
  /* Actual mode we give to hw */
  UINT32                      CrtcClock;         /* in KHz */
  UINT32                      CrtcHDisplay;
  UINT32                      CrtcHBlankStart;
  UINT32                      CrtcHBblankEnd;
  UINT32                      CrtcHSyncStart;
  UINT32                      CrtcHSyncEnd;
  UINT32                      CrtcHTotal;
  UINT32                      CrtcHSkew;
  UINT32                      CrtcVDisplay;
  UINT32                      CrtcVBlankStart;
  UINT32                      CrtcVBlankEnd;
  UINT32                      CrtcVSyncStart;
  UINT32                      CrtcVSyncEnd;
  UINT32                      CrtcVTotal;
  BOOLEAN                     Invalid;
} DRM_DISPLAY_MODE;

typedef struct {
  UINT16 Brightness;
  UINT16 Contrast;
  UINT16 Saturation;
  UINT16 Hue;
} BASE_BCSH_INFO;

typedef struct {
  INT8 DispHeadFlag[6];
  /* struct base2_screen_info screen_info[4]; --- todo */
  BASE_BCSH_INFO BCSHInfo;
  /* struct base_overscan overscan_info; --- todo */
  /* struct base2_gamma_lut_data gamma_lut_data; --- todo */
  /* struct base2_cubic_lut_data cubic_lut_data; --- todo */
  /* struct framebuffer_info framebuffer_info; --- todo */
  UINT32 Reserved[244];
  UINT32 CRC;
} BASE2_DISP_INFO;

typedef struct {
  VOID                        *Connector;
  OVER_SCAN                   OverScan;
  DRM_DISPLAY_MODE            DisplayMode;
  BASE2_DISP_INFO             *DispInfo; /* disp_info from baseparameter 2.0 */
  UINT8                       EDID[EDID_SIZE * 4];
  UINT32                      BusFormat;
  UINT32                      OutputMode;
  UINT32                      Type;
  UINT32                      OutputInterface;
  UINT32                      OutputFlags;
  UINT32                      ColorSpace;
  UINT32                      BPC;

  /**
   * @hold_mode: enabled when it's:
   * (1) mcu hold mode
   * (2) mipi dsi cmd mode
   * (3) edp psr mode
   */
  BOOLEAN                     hold_mode;
} CONNECTOR_STATE;

typedef struct {
  VOID                        *Crtc;
  VOID                        *Private;
  UINT32                      CrtcID;
  UINT32                      Format;
  UINT32                      YMirror;
  UINT32                      RBSwap;
  UINT32                      XVirtual;
  UINT32                      SrcX;
  UINT32                      SrcY;
  UINT32                      SrcW;
  UINT32                      SrcH;
  UINT32                      CrtcX;
  UINT32                      CrtcY;
  UINT32                      CrtcW;
  UINT32                      CrtcH;
  UINT32                      Feature;
  UINT32                      DMAAddress;
  BOOLEAN                     YUVOverlay;
  VOP_RECT                    MaxOutput;
} CRTC_STATE;

typedef struct {
  LIST_ENTRY                  ListHead;
  CRTC_STATE                  CrtcState;
  CONNECTOR_STATE             ConnectorState;

  UINT32                      ModeNumber;
  INT32                       VpsConfigModeID;

  VOID                        *MemoryBase;
  UINT32                      MemorySize;

  BOOLEAN                     IsInit;
  BOOLEAN                     IsEnable;

  BOOLEAN                     IsForceOutput;
  UINT32                      ForceOutputFormat;
} DISPLAY_STATE;

typedef enum {
  CSC_BT601L,
  CSC_BT709L,
  CSC_BT601F,
  CSC_BT2020,
} VOP2_CSC_FORMAT;

typedef enum {
	HSYNC_POSITIVE = 0,
	VSYNC_POSITIVE = 1,
	DEN_NEGATIVE   = 2,
	DCLK_INVERT    = 3
} VOP2_POL;

typedef enum {
  BCSH_OUT_MODE_BLACK,
  BCSH_OUT_MODE_BLUE,
  BCSH_OUT_MODE_COLOR_BAR,
  BCSH_OUT_MODE_NORMAL_VIDEO,
} VOP2_BCSH_OUT_MODE;

typedef enum {
  VOP2_VP0,
  VOP2_VP1,
  VOP2_VP2,
  VOP2_VP3,
  VOP2_VP_MAX,
} VOP2_VIDEO_PORTS_ID;

typedef enum {
  ROCKCHIP_VOP2_CLUSTER0 = 0,
  ROCKCHIP_VOP2_CLUSTER1,
  ROCKCHIP_VOP2_ESMART0,
  ROCKCHIP_VOP2_ESMART1,
  ROCKCHIP_VOP2_SMART0,
  ROCKCHIP_VOP2_SMART1,
  ROCKCHIP_VOP2_CLUSTER2,
  ROCKCHIP_VOP2_CLUSTER3,
  ROCKCHIP_VOP2_ESMART2,
  ROCKCHIP_VOP2_ESMART3,
  ROCKCHIP_VOP2_LAYER_MAX,
} VOP2_LAYER_PHY_ID;

typedef enum {
  CLUSTER_LAYER = 0,
  ESMART_LAYER = 1,
  SMART_LAYER = 2,
} VOP2_LAYER_TYPE;

typedef enum {
  VOP2_SCALE_UP_NRST_NBOR,
  VOP2_SCALE_UP_BIL,
  VOP2_SCALE_UP_BIC,
} VOP2_SCALE_UP_MODE;

typedef enum {
  VOP2_SCALE_DOWN_NRST_NBOR,
  VOP2_SCALE_DOWN_BIL,
  VOP2_SCALE_DOWN_AVG,
} VOP2_SCALE_DOWN_MODE;

typedef enum {
  SCALE_NONE = 0x0,
  SCALE_UP   = 0x1,
  SCALE_DOWN = 0x2,
} SCALE_MODE;

typedef struct {
  BOOLEAN                     IsParentNeeded;
  CHAR                       PdEnShift;
  CHAR                       PdStatusShift;
  CHAR                       PmuStatusShift;
  CHAR                       BisrEnStatusShift;
  CHAR                       ParentPhyID;
} VOP2_POWER_DOMAIN_DATA;

typedef struct {
  VOP2_LAYER_TYPE             Type;
  CHAR                       *Name;
  UINT8                       PhysID;
  UINT8                       WinSelPortOffset;
  UINT8                       LayerSelWinID;
  UINT32                      RegOffset;
  VOP2_POWER_DOMAIN_DATA      *PdData;
} VOP2_WIN_DATA;

typedef struct {
  UINT32                      Feature;
  UINT32                      MaxDclk;
  UINT8                       PreScanMaxDly;
  VOP_RECT                    MaxOutput;
} VOP2_VP_DATA;

typedef struct {
  UINT8                       PrimaryPlaneId;                 /* use this win to show logo */
  UINT8                       AttachedLayersNr;               /* number layers attach to this vp */
  UINT8                       AttachedLayers[VOP2_LAYER_MAX]; /* the layers attached to this vp */
  UINT32                      PlaneMask;
  INT32                       CursorPlaneID;
} VOP2_VP_PLANE_MASK;

typedef struct {
  UINT32                      Version;
  VOP2_VP_DATA                *VpData;
  VOP2_WIN_DATA               *WinData;
  /*
  struct vop2_plane_table *plane_table;
  */
  VOP2_VP_PLANE_MASK          *PlaneMask;

  UINT8                       NrVps;
  UINT8                       NrLayers;
  UINT8                       NrMixers;
  UINT8                       NrGammas;
  UINT8                       NrDscs;
  UINT32                      RegLen;
} VOP2_DATA;

typedef struct {
  PVOID                      BaseAddress;
  UINT32                      Version;
  BOOLEAN                     GlobalInit;
  VOP2_DATA                   *Data;
  VOP2_VP_PLANE_MASK          VpPlaneMask[VOP2_VP_MAX];
} VOP2HW;

typedef struct {
  BOOLEAN                        Enable;
  UINT8                          BgOvlDly;
  UINT32                         PlaneMask;
  UINT32                         PrimaryPlane;
  INT32                          OutputType;
  INT32                          CursorPlane;
} VPS_CONFIG;

