#pragma once

#define VOP2_DISP_POOL_TAG      '2poV'  // "Vop2"

typedef enum _VOP2_DISP_CONNECTOR_ID {
    Vop2DispConnHdmi0  = 0,   // VP0 default 
    Vop2DispConnHdmi1  = 1,
    Vop2DispConnDp0    = 2,   // edp0 block, USB-C DP or eDP panel
    Vop2DispConnDp1    = 3,   // edp1 block
    Vop2DispConn_Max   = 4
} VOP2_DISP_CONNECTOR_ID;

// Short kind used by the host tool to render a symbolic name.
typedef enum _VOP2_DISP_CONNECTOR_KIND {
    Vop2DispConnKindHdmi = 0,
    Vop2DispConnKindDp   = 1,
} VOP2_DISP_CONNECTOR_KIND;

#define VOP2_DISP_PROBE_UNMAPPED      0
#define VOP2_DISP_PROBE_MAPPED        1
#define VOP2_DISP_PROBE_ALIVE         2   // read returned a non-trivial value
#define VOP2_DISP_ESCAPE_MAGIC    0x32506F56UL  // "VoP2"

typedef enum _VOP2_DISP_ESCAPE_OPCODE {
    Vop2DispEscapeOp_Invalid         = 0,
    Vop2DispEscapeOp_GetDiagnostics  = 1,  // -> VOP2_DISP_ESCAPE_DIAG_OUT
    Vop2DispEscapeOp_GetMmioSnapshot = 2,  // -> VOP2_DISP_ESCAPE_MMIO_OUT
    Vop2DispEscapeOp_GetVidPnState   = 3,  // -> VOP2_DISP_ESCAPE_VIDPN_OUT
    Vop2DispEscapeOp_Max
} VOP2_DISP_ESCAPE_OPCODE;

typedef struct _VOP2_DISP_ESCAPE_HEADER {
    unsigned long Magic;           // = VOP2_DISP_ESCAPE_MAGIC
    unsigned long Opcode;          // VOP2_DISP_ESCAPE_OPCODE
    unsigned long Version;         // currently 1
    unsigned long Reserved;
} VOP2_DISP_ESCAPE_HEADER;

#define VOP2_DISP_ESCAPE_VERSION  2

typedef struct _VOP2_DISP_MMIO_REGION_INFO {
    unsigned long long PhysBase;
    unsigned long long PhysSize;
    unsigned long      Mapped;       // 1 = MmMapIoSpaceEx succeeded
    unsigned long      Index;        // VOP2_DISP_MMIO_INDEX
} VOP2_DISP_MMIO_REGION_INFO;

typedef struct _VOP2_DISP_CONNECTOR_INFO {
    unsigned long      Id;                  // VOP2_DISP_CONNECTOR_ID
    unsigned long      Kind;                // VOP2_DISP_CONNECTOR_KIND
    unsigned long      MmioIndex;           // which MMIO window backs it
    unsigned long      ProbeState;          // VOP2_DISP_PROBE_*
    unsigned long      ProbeWord0;          // raw 32-bit read at offset 0
    unsigned long      HpdIrqVector;        // 0 if not discovered
    unsigned long      HpdIrqLevel;
    unsigned long      Connected;           // best-effort (Phase 1: always 1)
    unsigned long      IsPrimary;           // 1 if this is the GOP target
    unsigned long      Reserved[3];
} VOP2_DISP_CONNECTOR_INFO;
