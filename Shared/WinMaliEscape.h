/*
 * WinMaliEscape.h - WinMali KMD <-> UMD escape ABI.
 *
 * The mesa-side UMD/ICDs (D3D11 UMD, OpenGL ICD, Vulkan ICD) and the
 * Windows kernel-mode miniport speak this ABI over D3DKMT_ESCAPE with
 * Type=D3DKMT_ESCAPE_DRIVERPRIVATE.
 *
 * Target hardware: Mali-G610 MP4 (Odin) on Rockchip RK3588. Valhall
 * arch v10, CSF-based, MCU-mediated submission. The ABI is intentionally
 * CSF-only - older Mali generations (Midgard, Bifrost, Valhall v9 JM)
 * are out of scope. The opcode numbering and payload struct layout
 * mirror the upstream Linux panthor DRM uAPI verbatim, so the kernel-
 * mode driver can reuse panthor's allocation / scheduling / queue-group
 * implementation without ABI translation.
 *
 * Wire format
 * -----------
 *
 *   +----------------------------------------+   offset 0
 *   |  WINMALI_ESCAPE_HEADER                 |   magic + version + op + size + status
 *   +----------------------------------------+   sizeof(WINMALI_ESCAPE_HEADER)
 *   |  Op-specific argument struct (in/out)  |   layout-compatible with drm_panthor_*
 *   +----------------------------------------+
 *   |  Variable-length tail data             |   only for ops with arrays
 *   +----------------------------------------+
 *
 * Variable-length tails (sync op arrays, vm_bind op arrays, queue
 * submit arrays, dev_query result buffer) follow the fixed payload at
 * byte offsets recorded in the payload's array/pointer fields. All
 * offsets are bytes from the start of pPrivateDriverData. User-mode
 * pointers are NEVER passed across the escape - the kernel-mode driver
 * does not share the calling process's address space at the IRQL the
 * escape arrives.
 *
 * Versioning
 * ----------
 *
 * WINMALI_ABI_MAJOR  - bumped for incompatible changes (renumbering an
 *                      opcode, repacking a struct, removing a field).
 *                      KMD rejects a mismatch at OpenDevice time.
 * WINMALI_ABI_MINOR  - bumped for purely additive changes (new
 *                      opcodes, new optional flags). The KMD MUST
 *                      return -EOPNOTSUPP for opcodes it doesn't
 *                      implement; UMDs tolerate that.
 *
 * Compilation
 * -----------
 *
 * Compiled under both ntddk.h (kernel-mode driver) and windows.h
 * (mesa-side UMD/ICDs). The KM toolset on VS 2017 v141 has no
 * <stdint.h> on its include path, so we map uint*_t onto the Windows
 * UINTn / INTn types when we are in kernel mode.
 */
#pragma once

#if defined(_KERNEL_MODE) || defined(_NTDDK_) || defined(_NTIFS_)
typedef UINT8   uint8_t;
typedef UINT16  uint16_t;
typedef UINT32  uint32_t;
typedef UINT64  uint64_t;
typedef INT8    int8_t;
typedef INT16   int16_t;
typedef INT32   int32_t;
typedef INT64   int64_t;
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Version / magic                                                            */
/* -------------------------------------------------------------------------- */

/* 'WMEs' little-endian. Sentinel for "this escape buffer is ours".           */
#define WINMALI_ESCAPE_MAGIC    0x73654D57u

/* Bumped when an opcode is renumbered, a struct repacked, or a field         */
/* removed. KMD rejects mismatches at OpenDevice handshake.                   */
#define WINMALI_ABI_MAJOR       2

/* Bumped for additive changes. KMD returns -EOPNOTSUPP for unknown ops.       */
/*                                                                              */
/*  v2.1 - Added BoDestroy, GetFlushId, timeline sync objects.                 */
#define WINMALI_ABI_MINOR       1

/* KMD pool tag. Appears as "WinM" in !pooltrack / Driver Verifier output.    */
#define WINMALI_POOL_TAG        'MniW'

/* Caller-side: the mesa pan_kmod backend opens this sentinel "fd" once it     */
/* has bound to a Windows D3DKMT adapter, so the rest of pan_kmod treats it    */
/* like a normal DRM file descriptor.                                          */
#define WINMALI_FD_SENTINEL     (-442)

/* -------------------------------------------------------------------------- */
/*  Opcodes                                                                    */
/*                                                                             */
/*  Values 0..15 mirror enum drm_panthor_ioctl_id from upstream panthor_drm.h  */
/*  exactly; the kernel-mode driver can re-use panthor's ioctl handlers by     */
/*  switching on the same numeric range. Values >= 0x80 are WinMali-specific - */
/*  things the Linux panthor driver doesn't need (adapter handshake, DXGI     */
/*  swap-chain backbuffer import).                                             */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_ESCAPE_OP {
    /* Mirrors of drm_panthor_ioctl_id (do not renumber). */
    WinMaliEscapeOp_DevQuery            = 0,
    WinMaliEscapeOp_VmCreate            = 1,
    WinMaliEscapeOp_VmDestroy           = 2,
    WinMaliEscapeOp_VmBind              = 3,
    WinMaliEscapeOp_VmGetState          = 4,
    WinMaliEscapeOp_BoCreate            = 5,
    WinMaliEscapeOp_BoMmapOffset        = 6,
    WinMaliEscapeOp_GroupCreate         = 7,
    WinMaliEscapeOp_GroupDestroy        = 8,
    WinMaliEscapeOp_GroupSubmit         = 9,
    WinMaliEscapeOp_GroupGetState       = 10,
    WinMaliEscapeOp_TilerHeapCreate     = 11,
    WinMaliEscapeOp_TilerHeapDestroy    = 12,
    WinMaliEscapeOp_BoSetLabel          = 13,
    /* Op 14: panthor uses the generic DRM_GEM_CLOSE ioctl to drop a BO    */
    /* handle. Windows has no equivalent generic-DRM space, so we expose   */
    /* an explicit BoDestroy here.                                         */
    WinMaliEscapeOp_BoDestroy           = 14,
    WinMaliEscapeOp_BoSync              = 15,   /* panthor's id 15 */
    WinMaliEscapeOp_BoQueryInfo         = 16,   /* panthor's id 16 */

    /* WinMali-specific (panthor doesn't have these). */
    WinMaliEscapeOp_OpenDevice          = 0x80,
    WinMaliEscapeOp_BoFromAllocation    = 0x81,
    WinMaliEscapeOp_SyncObjCreate       = 0x82,
    WinMaliEscapeOp_SyncObjDestroy      = 0x83,
    WinMaliEscapeOp_SyncObjWait         = 0x84,
    /* panthor exposes its latest-flush-id register via a 4-byte page the */
    /* user-space mmaps. On Windows we surface it through a tiny escape.  */
    WinMaliEscapeOp_GetFlushId          = 0x85,
    WinMaliEscapeOp_PresentToHdc        = 0x90,
} WINMALI_ESCAPE_OP;

/* -------------------------------------------------------------------------- */
/*  Header                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Common header at offset 0 of every escape buffer.
 *
 * KMD dispatch:
 *   - Magic mismatch              -> InvalidPayload, return STATUS_SUCCESS.
 *   - AbiMajor != WINMALI_ABI_MAJOR -> AbiMismatch, return STATUS_SUCCESS.
 *   - Unknown Op                  -> -EOPNOTSUPP in Status, return STATUS_SUCCESS.
 *   - All other errors            -> the relevant -errno in Status.
 *
 * STATUS_SUCCESS at the D3DKMT_ESCAPE NTSTATUS level means "we recognized
 * the buffer and wrote a meaningful Status". The UMD reads errno from
 * Status, which is the same convention pan_kmod_ioctl uses against
 * libdrm (returns -errno on failure).
 */
typedef struct _WINMALI_ESCAPE_HEADER {
    uint32_t Magic;         /* WINMALI_ESCAPE_MAGIC */
    uint16_t AbiMajor;      /* WINMALI_ABI_MAJOR */
    uint16_t AbiMinor;      /* WINMALI_ABI_MINOR */
    uint32_t Op;            /* WINMALI_ESCAPE_OP value */
    uint32_t PayloadSize;   /* total bytes including header, args, tail */
    int32_t  Status;        /* out: 0 on success, -errno otherwise */
    uint32_t Reserved[4];   /* MBZ; reserved for trace IDs, etc. */
} WINMALI_ESCAPE_HEADER;

/* All variable-length tails are 8-byte aligned. KMD rejects misaligned
 * offsets as -EINVAL.                                                        */
#define WINMALI_TAIL_ALIGN  8u

#define WINMALI_ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))

/* -------------------------------------------------------------------------- */
/*  Op 0x80 - OpenDevice (handshake)                                           */
/* -------------------------------------------------------------------------- */

/*
 * First escape after the mesa-side pan_kmod_dev_create opens the adapter via
 * D3DKMTOpenAdapterFromLuid. Probes whether this is a WinMali adapter (other
 * adapters return STATUS_INVALID_PARAMETER from D3DKMT_ESCAPE so the magic
 * never matters); validates the ABI major.
 *
 * KMD fills the response fields. UmdAbiMinor lets the KMD know what the
 * caller knows; the KMD reports its own minor in KmdAbiMinor. A UMD with a
 * minor newer than the KMD's must NOT assume ops above the KMD's minor are
 * implemented; the KMD returns -EOPNOTSUPP for those.
 */
typedef struct _WINMALI_OPEN_DEVICE {
    /* in */
    uint16_t UmdAbiMajor;       /* must equal WINMALI_ABI_MAJOR */
    uint16_t UmdAbiMinor;
    uint32_t Pad0;
    /* out */
    uint16_t KmdAbiMajor;
    uint16_t KmdAbiMinor;
    uint32_t KmdBuildId;        /* opaque; useful for "are these compatible" */
    char     KmdName[32];       /* nul-terminated, e.g. "WinMali-rk3588" */
} WINMALI_OPEN_DEVICE;

/* -------------------------------------------------------------------------- */
/*  Op 0 - DevQuery                                                            */
/*                                                                             */
/*  Layout-compatible with drm_panthor_dev_query. The 'pointer' field is a    */
/*  byte offset within pPrivateDriverData (not a userspace pointer); the KMD  */
/*  copies query_type-specific data at that offset.                            */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_DEV_QUERY_TYPE {
    WinMaliDevQuery_GpuInfo                 = 0,
    WinMaliDevQuery_CsifInfo                = 1,
    WinMaliDevQuery_TimestampInfo           = 2,
    WinMaliDevQuery_GroupPrioritiesInfo     = 3,
} WINMALI_DEV_QUERY_TYPE;

typedef struct _WINMALI_DEV_QUERY {
    uint32_t Type;          /* WINMALI_DEV_QUERY_TYPE */
    uint32_t Size;          /* in: output buffer capacity; out: actual size */
    uint64_t PointerOffset; /* byte offset within escape buffer to output */
} WINMALI_DEV_QUERY;

/*
 * Body returned by WinMaliDevQuery_GpuInfo. Layout-compatible with
 * drm_panthor_gpu_info. KMD reads physical registers (GPU_ID, MMU_FEATURES,
 * etc.) at StartDevice and caches the result.
 *
 * G610 expected values:
 *   GpuId       0xa007____ (product id 0xa007, revision in low 16 bits)
 *   ArchMajor   0xa (extracted via (GpuId >> 28) & 0xf)
 *   CsfId       set; CSF-based hardware
 *   ShaderPresent  populated cores bitmask (G610 MP4 = 0xf)
 *   MmuFeatures    low 8 bits = VA bits (G610: 48)
 */
typedef struct _WINMALI_GPU_INFO {
    uint32_t GpuId;
    uint32_t Pad0;
    uint32_t CsfId;
    uint32_t L2Features;
    uint32_t TilerFeatures;
    uint32_t MemFeatures;
    uint32_t MmuFeatures;
    uint32_t ThreadFeatures;
    uint32_t MaxThreads;
    uint32_t ThreadMaxWorkgroupSize;
    uint32_t ThreadMaxBarrierSize;
    uint32_t CoherencyFeatures;
    uint32_t TextureFeatures[4];
    uint32_t AsPresent;
    uint64_t ShaderPresent;
    uint64_t L2Present;
    uint64_t TilerPresent;
    uint32_t CorePresent;       /* not used on v10; MBZ */
    uint32_t Pad1;
    uint32_t Padding[19];
} WINMALI_GPU_INFO;

/* Layout-compatible with drm_panthor_csif_info. */
typedef struct _WINMALI_CSIF_INFO {
    uint32_t CsGroupCount;
    uint32_t CsCount;
    uint32_t ScoreboardSlotCount;
    uint32_t SyncWaitSlotCount;
    uint32_t CsRegCount;
} WINMALI_CSIF_INFO;

/* Layout-compatible with drm_panthor_timestamp_info. */
typedef struct _WINMALI_TIMESTAMP_INFO {
    uint64_t TimestampFrequency;
    uint64_t CurrentTimestamp;
    uint64_t TimestampOffset;
} WINMALI_TIMESTAMP_INFO;

/* Layout-compatible with drm_panthor_group_priorities_info. */
typedef struct _WINMALI_GROUP_PRIORITIES_INFO {
    uint8_t AllowedMask;
    uint8_t Pad[3];
} WINMALI_GROUP_PRIORITIES_INFO;

/* -------------------------------------------------------------------------- */
/*  Op 1 - VmCreate / Op 2 - VmDestroy                                         */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_VM_CREATE {
    uint32_t Flags;         /* MBZ in v2.0 */
    uint32_t Id;            /* out */
    uint64_t UserVaRange;   /* in: requested; out: granted. 0 = KMD picks. */
} WINMALI_VM_CREATE;

typedef struct _WINMALI_VM_DESTROY {
    uint32_t Id;
    uint32_t Pad;
} WINMALI_VM_DESTROY;

/* -------------------------------------------------------------------------- */
/*  Op 3 - VmBind                                                              */
/*                                                                             */
/*  Variable-length tail at OpsOffset is a WINMALI_VM_BIND_OP array. Each      */
/*  op MAY have an attached sync-op array at its own SyncsOffset. Sync ops     */
/*  carry SyncObj handles allocated via WinMaliEscapeOp_SyncObjCreate.        */
/* -------------------------------------------------------------------------- */

/* Bit ranges chosen to match drm_panthor_vm_bind_op_flags so a KMD that's */
/* internally a panthor port doesn't have to remap. */
#define WINMALI_VM_BIND_OP_MAP_READONLY     (1u <<  0)
#define WINMALI_VM_BIND_OP_MAP_NOEXEC       (1u <<  1)
#define WINMALI_VM_BIND_OP_MAP_UNCACHED     (1u <<  2)

#define WINMALI_VM_BIND_OP_TYPE_MASK        (0xfu << 28)
#define WINMALI_VM_BIND_OP_TYPE_MAP         (0u << 28)
#define WINMALI_VM_BIND_OP_TYPE_UNMAP       (1u << 28)
#define WINMALI_VM_BIND_OP_TYPE_SYNC_ONLY   (2u << 28)

#define WINMALI_VM_BIND_FLAG_ASYNC          (1u <<  0)

#define WINMALI_SYNC_OP_HANDLE_TYPE_MASK       0xffu
#define WINMALI_SYNC_OP_HANDLE_TYPE_SYNCOBJ    0u
#define WINMALI_SYNC_OP_HANDLE_TYPE_TIMELINE   1u
#define WINMALI_SYNC_OP_WAIT                   (0u << 31)
#define WINMALI_SYNC_OP_SIGNAL                 (1u << 31)

typedef struct _WINMALI_SYNC_OP {
    uint32_t Flags;
    uint32_t Handle;
    uint64_t TimelineValue;
} WINMALI_SYNC_OP;

typedef struct _WINMALI_VM_BIND_OP {
    uint32_t Flags;
    uint32_t BoHandle;          /* MBZ for UNMAP / SYNC_ONLY */
    uint64_t BoOffset;          /* MBZ for UNMAP / SYNC_ONLY */
    uint64_t Va;                /* MBZ for SYNC_ONLY */
    uint64_t Size;              /* MBZ for SYNC_ONLY */
    /* Tail: WINMALI_SYNC_OP[SyncsCount] at SyncsOffset (only ASYNC). */
    uint32_t SyncsCount;
    uint32_t SyncsOffset;       /* byte offset within escape buffer */
} WINMALI_VM_BIND_OP;

typedef struct _WINMALI_VM_BIND {
    uint32_t VmId;
    uint32_t Flags;             /* WINMALI_VM_BIND_FLAG_* */
    /* Tail: WINMALI_VM_BIND_OP[OpsCount] at OpsOffset. */
    uint32_t OpsCount;
    uint32_t OpsOffset;
} WINMALI_VM_BIND;

/* -------------------------------------------------------------------------- */
/*  Op 4 - VmGetState                                                          */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_VM_STATE {
    WinMaliVmState_Usable      = 0,
    WinMaliVmState_Unusable    = 1,
} WINMALI_VM_STATE;

typedef struct _WINMALI_VM_GET_STATE {
    uint32_t VmId;
    uint32_t State;     /* WINMALI_VM_STATE */
} WINMALI_VM_GET_STATE;

/* -------------------------------------------------------------------------- */
/*  Op 5 - BoCreate / Op 6 - BoMmapOffset                                      */
/* -------------------------------------------------------------------------- */

/* Layout-compatible with enum drm_panthor_bo_flags. */
#define WINMALI_BO_FLAG_NO_MMAP             (1u << 0)
#define WINMALI_BO_FLAG_NEEDS_INVALIDATION  (1u << 1)
#define WINMALI_BO_FLAG_CACHED              (1u << 2)
#define WINMALI_BO_FLAG_IS_HEAP_CHUNK       (1u << 3)

typedef struct _WINMALI_BO_CREATE {
    uint64_t Size;          /* in: requested; KMD rounds to page granule */
    uint32_t Flags;
    uint32_t ExclusiveVmId; /* 0 = shareable across VMs */
    /* out */
    uint32_t Handle;
    uint32_t Pad;
} WINMALI_BO_CREATE;

typedef struct _WINMALI_BO_MMAP_OFFSET {
    uint32_t Handle;        /* in */
    uint32_t Pad;
    /* out */
    uint64_t Offset;        /* opaque pgoff_t-shaped value; mesa never */
                            /* mmap()s on Windows - the offset is passed back */
                            /* to WinMali host-copy / BoSync helpers verbatim. */
} WINMALI_BO_MMAP_OFFSET;

/* -------------------------------------------------------------------------- */
/*  Op 13 - BoSetLabel                                                         */
/* -------------------------------------------------------------------------- */

#define WINMALI_BO_LABEL_MAX    4096

typedef struct _WINMALI_BO_SET_LABEL {
    uint32_t Handle;
    uint32_t LabelLen;
    uint32_t LabelOffset;   /* byte offset within escape buffer */
    uint32_t Pad;
} WINMALI_BO_SET_LABEL;

/* -------------------------------------------------------------------------- */
/*  Op 15 - BoSync (CPU<->device cache maintenance)                           */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_BO_SYNC_OP_TYPE {
    WinMaliBoSyncForCpu     = 0,    /* invalidate caches before CPU read */
    WinMaliBoSyncForDevice  = 1,    /* flush caches before GPU access */
} WINMALI_BO_SYNC_OP_TYPE;

typedef struct _WINMALI_BO_SYNC_OP {
    uint32_t Handle;
    uint32_t Type;          /* WINMALI_BO_SYNC_OP_TYPE */
    uint64_t Offset;
    uint64_t Size;
} WINMALI_BO_SYNC_OP;

typedef struct _WINMALI_BO_SYNC {
    uint32_t OpsCount;
    uint32_t OpsOffset;     /* WINMALI_BO_SYNC_OP[OpsCount] in escape buffer */
} WINMALI_BO_SYNC;

/* -------------------------------------------------------------------------- */
/*  Op 16 - BoQueryInfo                                                        */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_BO_QUERY_INFO {
    uint32_t Handle;
    uint32_t Pad;
    /* out */
    uint64_t Size;          /* allocated size (after page-granule round-up) */
    uint32_t Flags;
    uint32_t ExclusiveVmId;
} WINMALI_BO_QUERY_INFO;

/* -------------------------------------------------------------------------- */
/*  Op 14 - BoDestroy                                                          */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_BO_DESTROY {
    uint32_t Handle;
    uint32_t Pad;
} WINMALI_BO_DESTROY;

/* -------------------------------------------------------------------------- */
/*  Op 7 - GroupCreate / Op 8 - GroupDestroy                                   */
/*                                                                             */
/*  Layout-compatible with drm_panthor_group_create / -_destroy. CSF queue    */
/*  groups bind a set of command streams to a VM and a tiler heap.            */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_GROUP_PRIORITY {
    WinMaliGroupPriority_Low       = 0,
    WinMaliGroupPriority_Medium    = 1,
    WinMaliGroupPriority_High      = 2,
    WinMaliGroupPriority_Realtime  = 3,
} WINMALI_GROUP_PRIORITY;

typedef struct _WINMALI_QUEUE_CREATE {
    uint8_t  Priority;      /* 0..15; lower is higher priority */
    uint8_t  Pad[3];
    uint32_t RingbufSize;   /* per-queue ring buffer size in bytes (page-aligned) */
} WINMALI_QUEUE_CREATE;

typedef struct _WINMALI_GROUP_CREATE {
    /* in: queue array */
    uint32_t QueuesCount;
    uint32_t QueuesOffset;  /* WINMALI_QUEUE_CREATE[QueuesCount] */
    /* in: scheduling parameters */
    uint8_t  MaxComputeCores;
    uint8_t  MaxFragmentCores;
    uint8_t  MaxTilerCores;
    uint8_t  Priority;      /* WINMALI_GROUP_PRIORITY */
    uint32_t Pad;
    uint64_t ComputeCoreMask;
    uint64_t FragmentCoreMask;
    uint64_t TilerCoreMask;
    /* in: bindings */
    uint32_t VmId;
    /* out */
    uint32_t GroupHandle;
} WINMALI_GROUP_CREATE;

typedef struct _WINMALI_GROUP_DESTROY {
    uint32_t GroupHandle;
    uint32_t Pad;
} WINMALI_GROUP_DESTROY;

/* -------------------------------------------------------------------------- */
/*  Op 9 - GroupSubmit                                                         */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_QUEUE_SUBMIT {
    uint32_t QueueIndex;        /* 0..QueuesCount-1 from GroupCreate */
    uint32_t StreamSize;        /* bytes of CS opcodes to consume */
    uint64_t StreamAddr;        /* GPU VA in the group's VM */
    /* tails: WINMALI_SYNC_OP[*] */
    uint32_t SyncsCount;
    uint32_t SyncsOffset;
    uint32_t LatestFlushIdSlot; /* index into LatestFlushIds at the GroupSubmit level */
    uint32_t Pad;
} WINMALI_QUEUE_SUBMIT;

typedef struct _WINMALI_GROUP_SUBMIT {
    uint32_t GroupHandle;
    /* tails: WINMALI_QUEUE_SUBMIT[QueueSubmitsCount] + per-submit sync arrays */
    uint32_t QueueSubmitsCount;
    uint32_t QueueSubmitsOffset;
    uint32_t LatestFlushIdsCount;
    uint32_t LatestFlushIdsOffset;  /* uint32_t[] of in-order MCU flush ids */
    uint32_t Pad;
} WINMALI_GROUP_SUBMIT;

/* -------------------------------------------------------------------------- */
/*  Op 10 - GroupGetState                                                      */
/* -------------------------------------------------------------------------- */

#define WINMALI_GROUP_STATE_TIMEDOUT        (1u << 0)
#define WINMALI_GROUP_STATE_FATAL_FAULT     (1u << 1)
#define WINMALI_GROUP_STATE_INNOCENT        (1u << 2)

typedef struct _WINMALI_GROUP_GET_STATE {
    uint32_t GroupHandle;
    uint32_t StateFlags;        /* out: WINMALI_GROUP_STATE_* */
    uint32_t FatalQueues;       /* out: bitmask of queues that faulted */
    uint32_t Pad;
} WINMALI_GROUP_GET_STATE;

/* -------------------------------------------------------------------------- */
/*  Op 11 - TilerHeapCreate / Op 12 - TilerHeapDestroy                         */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_TILER_HEAP_CREATE {
    uint32_t VmId;
    uint32_t ChunkSize;         /* bytes per chunk; KMD page-aligns */
    uint32_t InitialChunkCount; /* must be >= 1 */
    uint32_t MaxChunks;         /* growth cap; 0 = unbounded */
    uint32_t TargetInFlightChunks;
    /* out */
    uint32_t HeapHandle;
    uint64_t FirstChunkGpuVa;   /* in the VM */
} WINMALI_TILER_HEAP_CREATE;

typedef struct _WINMALI_TILER_HEAP_DESTROY {
    uint32_t HeapHandle;
    uint32_t Pad;
} WINMALI_TILER_HEAP_DESTROY;

/* -------------------------------------------------------------------------- */
/*  Ops 0x82..0x84 - Sync objects                                              */
/*                                                                             */
/*  Required because Windows has no drmSyncobj equivalent. KMD-backed         */
/*  KEVENTs or KMUTEXes. The handles produced here are what queue submits     */
/*  reference in their sync arrays.                                            */
/* -------------------------------------------------------------------------- */

/* Make a sync object timeline-capable (mirrors drmSyncobjCreate's          */
/* WAIT_FOR_TIMELINE behaviour). Without this flag the handle is binary -    */
/* one signalled/unsignalled state, point arguments to WAIT and SYNC_OP are */
/* ignored.                                                                  */
#define WINMALI_SYNC_OBJ_FLAG_TIMELINE   (1u << 0)

typedef struct _WINMALI_SYNC_OBJ_CREATE {
    uint32_t Flags;         /* WINMALI_SYNC_OBJ_FLAG_* */
    uint32_t InitialState;  /* 0 = unsignalled, 1 = signalled (binary only) */
    /* out */
    uint32_t Handle;
    uint32_t Pad;
} WINMALI_SYNC_OBJ_CREATE;

typedef struct _WINMALI_SYNC_OBJ_DESTROY {
    uint32_t Handle;
    uint32_t Pad;
} WINMALI_SYNC_OBJ_DESTROY;

/* WAIT_FLAG bits: ALL = wait for every handle to reach its point. The       */
/* default is "any one of them suffices" (i.e. ANY). FOR_SUBMIT means        */
/* "tolerate handles that haven't been submitted yet, treat unstarted as     */
/* pending instead of error".                                                */
#define WINMALI_SYNC_OBJ_WAIT_FLAG_ALL          (1u << 0)
#define WINMALI_SYNC_OBJ_WAIT_FLAG_FOR_SUBMIT   (1u << 1)

typedef struct _WINMALI_SYNC_OBJ_WAIT {
    uint32_t Flags;         /* WINMALI_SYNC_OBJ_WAIT_FLAG_* */
    uint32_t Count;
    uint32_t HandlesOffset;     /* uint32_t[Count] */
    /* Optional tail of uint64_t[Count] timeline points. Zero means         */
    /* "binary wait" (or "any positive point" for timeline handles).        */
    uint32_t PointsOffset;
    int64_t  TimeoutNs;     /* 0 = poll, <0 = wait forever */
} WINMALI_SYNC_OBJ_WAIT;

/* -------------------------------------------------------------------------- */
/*  Op 0x85 - GetFlushId                                                       */
/*                                                                             */
/*  Reads the MCU's LATEST_FLUSH_ID register, used by CSF to scope cache      */
/*  flushes across submissions. panthor exposes this via a 4 KiB mmap of the  */
/*  register page; we do an escape here instead. Cheap enough to call per     */
/*  GroupSubmit.                                                               */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_GET_FLUSH_ID {
    uint32_t FlushId;       /* out */
    uint32_t Pad;
} WINMALI_GET_FLUSH_ID;

/* -------------------------------------------------------------------------- */
/*  Op 0x81 - BoFromAllocation                                                 */
/*                                                                             */
/*  DXGI swap-chain backbuffers are D3DKMTCreateAllocation handles that DWM   */
/*  composites from. Escape-managed BOs are invisible to DWM. This op wraps   */
/*  an existing D3DKMT allocation as a panthor BO so panvk / panfrost can     */
/*  render into a DWM-visible surface.                                         */
/* -------------------------------------------------------------------------- */

typedef struct _WINMALI_BO_FROM_ALLOCATION {
    uint32_t AllocationHandle;  /* D3DKMT_HANDLE */
    uint32_t Flags;             /* WINMALI_BO_FLAG_* (subset valid here) */
    uint32_t VmId;              /* 0 = create unmapped */
    uint32_t Pad;
    /* out */
    uint32_t BoHandle;
    uint32_t Pad1;
    uint64_t Size;
    uint64_t GpuVa;             /* 0 if not mapped */
} WINMALI_BO_FROM_ALLOCATION;

/*
 * Magic + private data the D3D11 UMD packs into D3DDDI_ALLOCATIONINFO::
 * pPrivateDriverData when calling pfnAllocateCb for a backbuffer. The KMD's
 * DxgkDdiCreateAllocation reads this and allocates Size bytes of physical
 * backing. Magic = "WMAl".
 */
#define WINMALI_ALLOCATION_PRIVATE_MAGIC    0x6C414D57u

typedef struct _WINMALI_ALLOCATION_PRIVATE {
    uint32_t Magic;
    uint32_t Flags;             /* WINMALI_BO_FLAG_* applied at BoFromAllocation */
    uint64_t Size;
    uint32_t Width;             /* informational */
    uint32_t Height;
    uint32_t Format;            /* DXGI_FORMAT, informational */
    uint32_t Stride;            /* bytes/row, informational */
} WINMALI_ALLOCATION_PRIVATE;

/* -------------------------------------------------------------------------- */
/*  Op 0x90 - PresentToHdc                                                     */
/*                                                                             */
/*  GL SwapBuffers fast path: KMD waits for the source BO's last submission,  */
/*  CPU-maps it (it owns the pages already), and SetDIBitsToDevice's to the   */
/*  supplied HDC. CPU blit, slow but no DXGI involvement.                     */
/* -------------------------------------------------------------------------- */

typedef enum _WINMALI_PRESENT_FORMAT {
    WinMaliPresentFormat_Bgra8888  = 1,
    WinMaliPresentFormat_Rgba8888  = 2,
    WinMaliPresentFormat_Rgb565    = 3,
} WINMALI_PRESENT_FORMAT;

typedef struct _WINMALI_PRESENT_TO_HDC {
    uint64_t Hdc;           /* HDC cast to uint64_t (PVOID-shaped) */
    uint32_t BoHandle;
    uint32_t Format;        /* WINMALI_PRESENT_FORMAT */
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;        /* bytes/row */
    uint32_t DstX;
    uint32_t DstY;
    uint32_t Pad;
} WINMALI_PRESENT_TO_HDC;

#ifdef __cplusplus
} /* extern "C" */
#endif
