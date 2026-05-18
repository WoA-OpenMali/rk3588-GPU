/**
 * @file WinMaliEscape.h
 * @brief WinMali KMD <-> UMD escape ABI — authoritative contract.
 *
 * The mesa-side UMD/ICD (OpenGL ICD, D3D11 UMD, Vulkan ICD) defines
 * this surface; the KMD is implemented to match. Every opcode below
 * is shipped through `D3DKMT_ESCAPE` with
 * `Type=D3DKMT_ESCAPE_DRIVERPRIVATE` and `pPrivateDriverData` pointing
 * at a #WINMALI_ESCAPE_HEADER followed by op-specific input/output
 * payloads (in-place — the buffer is r/w).
 *
 * @par Wire format
 *
 * @verbatim
 *  +------------------------------------+   offset 0
 *  |  WINMALI_ESCAPE_HEADER             |
 *  +------------------------------------+   sizeof(header)
 *  |  Op-specific payload (in / out)    |
 *  +------------------------------------+
 *  |  Variable-length tail at *Offset   |   (some ops only)
 *  +------------------------------------+
 * @endverbatim
 *
 * Variable-length tails (handle arrays, vm-bind op arrays, etc.) follow
 * the fixed payload at the offsets documented per-op. All offsets are
 * **byte offsets** from the start of `pPrivateDriverData`; UM pointers
 * are NEVER passed across the escape because the KMD cannot follow
 * them in the context of the calling application.
 *
 * @par Compilation
 *
 * Compiled under both `ntddk.h` (KMD) and `windows.h` (UMD): only
 * fixed-width integer types, no IRQL annotations, no Windows handles
 * baked in.
 *
 * @par Version handshake
 *
 * - #WINMALI_ABI_MAJOR bumped for breaking changes (KMD must reject
 *   unequal value at #WinMaliEscapeOp_Handshake time).
 * - #WINMALI_ABI_MINOR bumped for additive changes (new opcodes, new
 *   optional fields). Older minor is OK; the KMD MUST return
 *   #WinMaliEscapeStatus_UnknownOp for opcodes it doesn't implement.
 *
 * @par Backward-compat carriage
 *
 * `WINMALI_ADAPTER_INFO` / `DXGKQAITYPE_UMDRIVERPRIVATE` handshake
 * remains in `WinMaliCommon.h`. `WinMaliCommon.h` is otherwise legacy
 * and should not be included by new code — include this file instead.
 *
 * @par See also
 *
 * - `mesa/src/panfrost/lib/kmod/winmali/wm_adapter.{h,c}` — UMD-side
 *   typed C API that emits each op. Use it as a worked reference for
 *   the byte layouts the KMD will see.
 * - `docs/KMD_IMPLEMENTATION.md` — KMD author's guide, opcode by
 *   opcode.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/** @name ABI version                                                 */
/** @{ */

/**
 * @brief Major ABI version. Mismatch is fatal at Handshake time.
 *
 * Bumped for any incompatible change to opcode numbers, status codes,
 * or struct layouts. UMD with unequal major must abort cleanly; KMD
 * must respond with #WinMaliEscapeStatus_AbiMismatch from
 * #WinMaliEscapeOp_Handshake.
 *
 * @par History
 * - 1.0 — Initial: Handshake, GetParams, BO/VM/Submit/SyncObj
 * - 1.1 — Added: PresentToHdc, BoFromAllocation (Present-path placeholders)
 */
#define WINMALI_ABI_MAJOR   1

/**
 * @brief Minor ABI version. Bumped for purely additive changes.
 *
 * UMD with a newer minor than KMD must NOT assume ops above the KMD's
 * minor are implemented; KMD returns #WinMaliEscapeStatus_UnknownOp
 * for opcodes it doesn't implement. KMD reports its own minor in
 * #WINMALI_HANDSHAKE_OUT::KmdAbiMinor.
 */
#define WINMALI_ABI_MINOR   1

/**
 * @brief Magic header marker — letters `WMEs` packed little-endian.
 *
 * Reject the escape if #WINMALI_ESCAPE_HEADER::Magic does not equal
 * this constant; that's a wrong-driver / bit-rot signal.
 */
#define WINMALI_ESCAPE_MAGIC    0x73654D57u

/**
 * @brief Pool tag carried by KMD allocations made on behalf of escapes.
 *
 * `'WniM'` in little-endian, appears as `"WinM"` in `!pooltrack` /
 * Driver Verifier output.
 */
#define WINMALI_ESCAPE_POOL_TAG 'MniW'

/** @} */

/* ================================================================== */
/** @name Escape opcodes                                              */
/** @{ */

/**
 * @brief Escape opcode enumeration.
 *
 * Each value is a stable identifier for a request shape — both ABI
 * major and minor bumps are required to renumber an existing opcode.
 * New opcodes go at the next free slot in the appropriate group.
 */
typedef enum _WINMALI_ESCAPE_OP {
    /**
     * @brief Handshake / discovery. First escape made on a newly
     *        opened `D3DKMT_HANDLE_ADAPTER`.
     *
     * KMD validates magic + major, fills out the handshake payload,
     * sets `Status`. Used by `wm_escape_open()` (mesa-side) to find
     * the WinMali adapter among the system's WDDM adapters.
     *
     * @see WINMALI_HANDSHAKE_OUT
     */
    WinMaliEscapeOp_Handshake       = 0x0001,

    /**
     * @brief Bulk param query.
     *
     * Equivalent to running `DRM_PANFROST_GET_PARAM` for every member
     * of `enum drm_panfrost_param` in one round-trip.
     *
     * @see WINMALI_PARAMS_OUT
     */
    WinMaliEscapeOp_GetParams       = 0x0002,

    /**
     * @name BO lifecycle (0x0010..0x0016)
     *
     * Mirror of DRM panfrost BO ops: `DRM_PANFROST_CREATE_BO` /
     * `-MMAP_BO` / `-GET_BO_OFFSET` / `-WAIT_BO` / `-MADVISE`.
     * @{
     */
    WinMaliEscapeOp_BoCreate        = 0x0010,   ///< Allocate a BO.   @see WINMALI_BO_CREATE
    WinMaliEscapeOp_BoDestroy       = 0x0011,   ///< Free a BO.       @see WINMALI_BO_DESTROY_IN
    WinMaliEscapeOp_BoMapCpu        = 0x0012,   ///< Map into UM VA.  @see WINMALI_BO_MAP_CPU
    WinMaliEscapeOp_BoUnmapCpu      = 0x0013,   ///< Unmap UM mapping. @see WINMALI_BO_UNMAP_CPU_IN
    WinMaliEscapeOp_BoGetOffset     = 0x0014,   ///< Query GPU VA.    @see WINMALI_BO_GET_OFFSET
    WinMaliEscapeOp_BoWait          = 0x0015,   ///< Wait on last submit. @see WINMALI_BO_WAIT_IN
    WinMaliEscapeOp_BoMadvise       = 0x0016,   ///< Hint about backing. @see WINMALI_BO_MADVISE
    /** @} */

    /**
     * @name VM lifecycle (0x0020..0x0022)
     *
     * Per-process GPU address space. On v4-v7 (JM) there's typically
     * one MMU AS slot per device shared across processes via
     * fast-slot switch; on v9+ (CSF, including G610) each VM is its
     * own MMU context. KMD hides the difference.
     * @{
     */
    WinMaliEscapeOp_VmCreate        = 0x0020,   ///< Allocate AS slot.  @see WINMALI_VM_CREATE
    WinMaliEscapeOp_VmDestroy       = 0x0021,   ///< Release AS slot.   @see WINMALI_VM_DESTROY_IN
    WinMaliEscapeOp_VmBind          = 0x0022,   ///< Batched map/unmap. @see WINMALI_VM_BIND_IN
    /** @} */

    /**
     * @name Job submission (0x0030..0x0031)
     *
     * Two flavours; UMD picks based on probed arch via
     * #WINMALI_PARAMS_OUT::GpuProdId.
     * @{
     */
    WinMaliEscapeOp_SubmitJm        = 0x0030,   ///< `drm_panfrost_submit` shape (v4-v7). @see WINMALI_SUBMIT_JM_IN
    WinMaliEscapeOp_SubmitCsf       = 0x0031,   ///< CSF queue submit (v9+, G610).        @see WINMALI_SUBMIT_CSF_IN
    /** @} */

    /**
     * @name Sync objects (0x0040..0x0044)
     *
     * DRM-syncobj-equivalent fences. Currently binary; timeline
     * support is reserved for a future minor bump.
     * @{
     */
    WinMaliEscapeOp_SyncObjCreate   = 0x0040,   ///< Allocate syncobj.  @see WINMALI_SYNCOBJ_CREATE
    WinMaliEscapeOp_SyncObjDestroy  = 0x0041,   ///< Free syncobj.      @see WINMALI_SYNCOBJ_DESTROY_IN
    WinMaliEscapeOp_SyncObjWait     = 0x0042,   ///< Wait on syncobj(s). @see WINMALI_SYNCOBJ_WAIT_IN
    WinMaliEscapeOp_SyncObjSignal   = 0x0043,   ///< Signal syncobj.    @see WINMALI_SYNCOBJ_SIGNAL_IN
    WinMaliEscapeOp_SyncObjReset    = 0x0044,   ///< Reset to unsignaled. @see WINMALI_SYNCOBJ_RESET_IN
    /** @} */

    /**
     * @name Present path (0x0050..0x0051, ABI v1.1)
     *
     * Two complementary opcodes closing the loop from rendered BO to
     * pixels on screen. KMD implementations are deliverables of
     * Phases 6 and 7 in `docs/MESA_BUILD_STATUS.md`.
     *
     * - #WinMaliEscapeOp_PresentToHdc — sync + copy a panfrost-
     *   rendered BO out to a window (HDC) via the KMD. Used by the
     *   OpenGL ICD's `flush_frontbuffer` callback for `SwapBuffers`.
     *   First-cut path; slow (CPU-side blit) but no DXGI/DWM needed.
     * - #WinMaliEscapeOp_BoFromAllocation — wrap an existing
     *   `D3DKMT_HANDLE` allocation (typically a DXGI swap-chain
     *   backbuffer) as a panfrost-usable BO. Required for D3D11
     *   windowed Present to interop with the desktop compositor.
     * @{
     */
    WinMaliEscapeOp_PresentToHdc    = 0x0050,   ///< @see WINMALI_PRESENT_TO_HDC_IN
    WinMaliEscapeOp_BoFromAllocation = 0x0051,  ///< @see WINMALI_BO_FROM_ALLOCATION
    /** @} */

    /**
     * @brief Reserved sentinel.
     *
     * KMD must return #WinMaliEscapeStatus_UnknownOp for opcodes
     * >= this value unless extending this header.
     */
    WinMaliEscapeOp_Max,
} WINMALI_ESCAPE_OP;

/** @} */

/* ================================================================== */
/** @name Status codes                                                */
/** @{ */

/**
 * @brief Result code written to #WINMALI_ESCAPE_HEADER::Status by the KMD.
 *
 * Values are stable across ABI minor bumps. New codes may be added at
 * the next free slot; UMD treats unknown codes as a generic failure.
 */
typedef enum _WINMALI_ESCAPE_STATUS {
    WinMaliEscapeStatus_Success         = 0,    ///< All good.
    WinMaliEscapeStatus_InvalidPayload  = 1,    ///< Magic / size / version off.
    WinMaliEscapeStatus_UnknownOp       = 2,    ///< Opcode not implemented.
    WinMaliEscapeStatus_AbiMismatch     = 3,    ///< AbiMajor != WINMALI_ABI_MAJOR.
    WinMaliEscapeStatus_NoMemory        = 4,    ///< KMD allocation failed.
    WinMaliEscapeStatus_InvalidHandle   = 5,    ///< BO/VM/SyncObj handle bad or wrong process.
    WinMaliEscapeStatus_Busy            = 6,    ///< Would block (BoWait/SyncObjWait timeout == 0).
    WinMaliEscapeStatus_NotSupported    = 7,    ///< Op valid but not applicable on this HW/arch.
    WinMaliEscapeStatus_DeviceLost      = 8,    ///< GPU reset / TDR in flight.
    WinMaliEscapeStatus_Timeout         = 9,    ///< Wait expired with TimeoutNs > 0.
    WinMaliEscapeStatus_PermissionDenied = 10,  ///< Caller doesn't own the resource.
} WINMALI_ESCAPE_STATUS;

/** @} */

/* ================================================================== */
/** @name Common header                                               */
/** @{ */

/**
 * @brief Common header at offset 0 of every escape buffer.
 *
 * @par KMD dispatch sequence
 *
 *  1. Reject if `Magic` != #WINMALI_ESCAPE_MAGIC → #WinMaliEscapeStatus_InvalidPayload.
 *  2. Reject if `AbiMajor` != #WINMALI_ABI_MAJOR → #WinMaliEscapeStatus_AbiMismatch.
 *  3. Dispatch on `Op`. Unknown → #WinMaliEscapeStatus_UnknownOp.
 *  4. Validate `PayloadSize` matches the op's expected size (or
 *     suffices for an in/out op with variable-length tail).
 *  5. Update `Status` before returning the escape.
 *  6. NEVER touch UM pointers; tails are at **byte offsets** within
 *     `pPrivateDriverData`.
 *
 * @note `Reserved[2]` must be zero; reserved for adding a 64-bit
 *       request ID later if we ever need correlation tracing.
 */
typedef struct _WINMALI_ESCAPE_HEADER {
    uint32_t Magic;         ///< Must equal #WINMALI_ESCAPE_MAGIC.
    uint16_t AbiMajor;      ///< Must equal #WINMALI_ABI_MAJOR.
    uint16_t AbiMinor;      ///< Informational; KMD reports its own in Handshake.
    uint32_t Op;            ///< A #WINMALI_ESCAPE_OP value.
    uint32_t PayloadSize;   ///< Total buffer bytes (header + payload + tail).
    uint32_t Status;        ///< Out: #WINMALI_ESCAPE_STATUS. Written by KMD.
    uint32_t Reserved[2];   ///< Must be zero.
} WINMALI_ESCAPE_HEADER;

/** @} */

/* ================================================================== */
/** @name Op 0x0001 — Handshake                                       */
/** @{ */

/**
 * @brief Output payload for #WinMaliEscapeOp_Handshake.
 *
 * Input is the header only; KMD inspects `header.AbiMajor` and fills
 * this struct.
 *
 * @par KMD behavior
 *
 * Handshake must succeed before any other op is accepted on the
 * adapter handle. KMD remembers per-adapter that handshake completed;
 * further escapes on the same adapter skip the check.
 *
 * KMD fills `KmdName` with a null-terminated string identifying the
 * driver (e.g. "WinMali-rk3588"). `KmdBuildId` is opaque to the UMD
 * but useful for "are these binaries compatible" diagnostics.
 *
 * @see WinMaliEscapeOp_Handshake
 */
typedef struct _WINMALI_HANDSHAKE_OUT {
    uint16_t KmdAbiMajor;   ///< Must equal #WINMALI_ABI_MAJOR (else KMD returns AbiMismatch).
    uint16_t KmdAbiMinor;   ///< KMD's own minor — UMD tolerates older minor.
    uint32_t KmdBuildId;    ///< Opaque build identifier (e.g. git SHA truncated).
    char     KmdName[32];   ///< Null-terminated driver name, e.g. "WinMali-rk3588".
} WINMALI_HANDSHAKE_OUT;

/** @} */

/* ================================================================== */
/** @name Op 0x0002 — GetParams                                       */
/** @{ */

/**
 * @brief Output payload for #WinMaliEscapeOp_GetParams.
 *
 * Input is the header only.
 *
 * @par KMD behavior
 *
 * Read the GPU's register block and populate every field. Equivalent
 * of running `DRM_PANFROST_GET_PARAM` for each member of
 * `enum drm_panfrost_param` in one round-trip. KMD MUST zero-fill any
 * field it can't determine; mesa's `pan_kmod` treats 0 as "unknown"
 * for most.
 *
 * @par Field mapping (DRM equivalent)
 *
 * | Field                  | DRM_PANFROST_PARAM_                       |
 * |------------------------|-------------------------------------------|
 * | `GpuProdId`            | `GPU_PROD_ID`                             |
 * | `GpuRevision`          | `GPU_REVISION`                            |
 * | `ShaderPresent`        | `SHADER_PRESENT`                          |
 * | `TilerPresent`         | `TILER_PRESENT`                           |
 * | `L2Present`            | `L2_PRESENT`                              |
 * | `StackPresent`         | `STACK_PRESENT`                           |
 * | `AsPresent`            | `AS_PRESENT`                              |
 * | `JsPresent`            | `JS_PRESENT`                              |
 * | `L2Features`           | `L2_FEATURES`                             |
 * | `CoreFeatures`         | `CORE_FEATURES`                           |
 * | `TilerFeatures`        | `TILER_FEATURES`                          |
 * | `MemFeatures`          | `MEM_FEATURES`                            |
 * | `MmuFeatures`          | `MMU_FEATURES`                            |
 * | `ThreadFeatures`       | `THREAD_FEATURES`                         |
 * | `MaxThreads`           | `MAX_THREADS`                             |
 * | `ThreadMaxWorkgroupSz` | `THREAD_MAX_WORKGROUP_SZ`                 |
 * | `ThreadMaxBarrierSz`   | `THREAD_MAX_BARRIER_SZ`                   |
 * | `CoherencyFeatures`    | `COHERENCY_FEATURES`                      |
 * | `TextureFeatures[N]`   | `TEXTURE_FEATURES{0..3}`                  |
 * | `JsFeatures[N]`        | `JS_FEATURES{0..15}`                      |
 * | `NrCoreGroups`         | `NR_CORE_GROUPS`                          |
 * | `ThreadTlsAlloc`       | `THREAD_TLS_ALLOC`                        |
 * | `AfbcFeatures`         | `AFBC_FEATURES`                           |
 *
 * @note `GpuProdId` is the critical field — mesa's panfrost driver
 *       matches it against the model table in
 *       `mesa/src/panfrost/lib/pan_props.c` to decide which arch-v*
 *       cmdstream init to dispatch.
 */
typedef struct _WINMALI_PARAMS_OUT {
    uint32_t GpuProdId;             ///< 16-bit GPU_ID product code (G610 expects 0xa867 family).
    uint32_t GpuRevision;           ///< GPU_ID revision bits.
    uint64_t ShaderPresent;         ///< Bitmask of populated shader cores.
    uint64_t TilerPresent;          ///< Bitmask of populated tiler units.
    uint64_t L2Present;             ///< Bitmask of populated L2 slices.
    uint64_t StackPresent;          ///< Bitmask of populated stack tiles.
    uint32_t AsPresent;             ///< MMU address-space slots present.
    uint32_t JsPresent;             ///< Job-slot count (v4-v7; 0 on v9+).
    uint32_t L2Features;            ///< L2 cache properties.
    uint32_t CoreFeatures;          ///< Shader core feature bits.
    uint32_t TilerFeatures;         ///< Tiler properties.
    uint32_t MemFeatures;           ///< Memory-system feature bits.
    uint32_t MmuFeatures;           ///< MMU feature bits; low 8 bits = VA bits.
    uint32_t ThreadFeatures;        ///< Per-thread resource caps.
    uint32_t MaxThreads;            ///< Max threads per core.
    uint32_t ThreadMaxWorkgroupSz;  ///< Max workgroup size (compute).
    uint32_t ThreadMaxBarrierSz;    ///< Max barrier size.
    uint32_t CoherencyFeatures;     ///< Cache-coherency support bits.
    uint32_t TextureFeatures[4];    ///< Per-stage texture format support.
    uint32_t JsFeatures[16];        ///< Per-job-slot capability bits (v4-v7).
    uint32_t NrCoreGroups;          ///< Core group count.
    uint32_t ThreadTlsAlloc;        ///< TLS allocation granularity.
    uint32_t AfbcFeatures;          ///< AFBC support bits (v6+).
    uint32_t Reserved[8];           ///< Must be zero; reserved for future params.
} WINMALI_PARAMS_OUT;

/** @} */

/* ================================================================== */
/** @name BO flags                                                    */
/** @{ */

#define WINMALI_BO_FLAG_NOEXEC          0x00000001u ///< Page tables get the no-exec bit.
#define WINMALI_BO_FLAG_HEAP            0x00000002u ///< Growable BO (v9+ tiler heap).
#define WINMALI_BO_FLAG_NO_MMAP         0x00000004u ///< GPU-only; never CPU-mappable.
#define WINMALI_BO_FLAG_GPU_UNCACHED    0x00000008u ///< Map GPU-uncached at VmBind.

/** @} */

/* ================================================================== */
/** @name Op 0x0010 — BoCreate                                        */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoCreate.
 */
typedef struct _WINMALI_BO_CREATE_IN {
    uint64_t Size;          ///< Bytes; KMD rounds up to page granule.
    uint32_t Flags;         ///< `WINMALI_BO_FLAG_*` bitmask.
    uint32_t VmHandle;      ///< 0 = create unmapped; else map into this VM with auto-VA.
} WINMALI_BO_CREATE_IN;

/**
 * @brief Output for #WinMaliEscapeOp_BoCreate.
 */
typedef struct _WINMALI_BO_CREATE_OUT {
    uint32_t BoHandle;      ///< Nonzero KMD-owned handle. Opaque to UMD.
    uint32_t Pad;
    uint64_t GpuVa;         ///< 0 if unmapped, else GPU VA. MUST be nonzero on success when mapped.
    uint64_t ActualSize;    ///< Size after page-granule round-up.
} WINMALI_BO_CREATE_OUT;

/**
 * @brief Combined in/out payload for #WinMaliEscapeOp_BoCreate.
 *
 * @par KMD behavior
 *
 *  - Allocates physical pages (size rounded up to KMD's page granule).
 *  - Allocates a KMD-internal BO handle (32-bit, nonzero, fresh per BO).
 *  - When `In.VmHandle != 0`, maps the BO into that VM at an auto-picked
 *    GPU VA and returns it in `Out.GpuVa`. When `In.VmHandle == 0` the
 *    BO is unmapped; caller must use #WinMaliEscapeOp_VmBind later.
 *  - `WINMALI_BO_FLAG_NOEXEC` ⇒ MMU PTEs get the no-exec bit.
 *  - `WINMALI_BO_FLAG_HEAP` ⇒ growable BO (mesa expects this for the
 *    tiler heap on v9+; treat as a sparsely-backed region).
 *  - `WINMALI_BO_FLAG_NO_MMAP` ⇒ never CPU-mappable (#WinMaliEscapeOp_BoMapCpu
 *    returns #WinMaliEscapeStatus_NotSupported).
 *
 * @note `Out.GpuVa == 0` is reserved as "unmapped"; a successful map
 *       must never return 0.
 */
typedef struct _WINMALI_BO_CREATE {
    WINMALI_BO_CREATE_IN  In;
    WINMALI_BO_CREATE_OUT Out;
} WINMALI_BO_CREATE;

/** @} */

/* ================================================================== */
/** @name Op 0x0011 — BoDestroy                                       */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoDestroy.
 *
 * @par KMD behavior
 *
 * Tears down the BO. Implicitly unmaps from any VM it was bound to.
 * Returns #WinMaliEscapeStatus_InvalidHandle if the BO doesn't exist
 * or doesn't belong to this adapter's process.
 */
typedef struct _WINMALI_BO_DESTROY_IN {
    uint32_t BoHandle;
    uint32_t Pad;
} WINMALI_BO_DESTROY_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0012 — BoMapCpu                                        */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoMapCpu.
 */
typedef struct _WINMALI_BO_MAP_CPU_IN {
    uint32_t BoHandle;
    uint32_t Pad;
} WINMALI_BO_MAP_CPU_IN;

/**
 * @brief Output for #WinMaliEscapeOp_BoMapCpu.
 */
typedef struct _WINMALI_BO_MAP_CPU_OUT {
    uint64_t CpuAddress;    ///< UM virtual address (cast from `PVOID`).
} WINMALI_BO_MAP_CPU_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_BoMapCpu.
 *
 * @par KMD behavior
 *
 *  - Maps the BO's backing pages into the calling process at any UM-VA.
 *  - Returns the resulting UM-VA as a uint64_t in `Out.CpuAddress`.
 *  - Returns #WinMaliEscapeStatus_InvalidHandle for unknown BO,
 *    #WinMaliEscapeStatus_NotSupported when the BO was created with
 *    #WINMALI_BO_FLAG_NO_MMAP.
 *
 * @par Lifetime
 *
 * The mapping is valid until #WinMaliEscapeOp_BoUnmapCpu OR
 * #WinMaliEscapeOp_BoDestroy. KMD MUST tear it down on process exit
 * even if UMD didn't unmap (handle the `EPROCESS` goes-away path in
 * `DxgkDdiDestroyProcess`).
 */
typedef struct _WINMALI_BO_MAP_CPU {
    WINMALI_BO_MAP_CPU_IN  In;
    WINMALI_BO_MAP_CPU_OUT Out;
} WINMALI_BO_MAP_CPU;

/** @} */

/* ================================================================== */
/** @name Op 0x0013 — BoUnmapCpu                                      */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoUnmapCpu.
 */
typedef struct _WINMALI_BO_UNMAP_CPU_IN {
    uint32_t BoHandle;
    uint32_t Pad;
    uint64_t CpuAddress;    ///< Must match what #WinMaliEscapeOp_BoMapCpu returned.
} WINMALI_BO_UNMAP_CPU_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0014 — BoGetOffset                                     */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoGetOffset.
 */
typedef struct _WINMALI_BO_GET_OFFSET_IN {
    uint32_t BoHandle;
    uint32_t VmHandle;      ///< The VM whose mapping is queried.
} WINMALI_BO_GET_OFFSET_IN;

/**
 * @brief Output for #WinMaliEscapeOp_BoGetOffset.
 */
typedef struct _WINMALI_BO_GET_OFFSET_OUT {
    uint64_t GpuVa;
} WINMALI_BO_GET_OFFSET_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_BoGetOffset.
 *
 * Returns the current GPU VA of a BO. Valid only after the BO has been
 * mapped into a VM (either at #WinMaliEscapeOp_BoCreate time via
 * `VmHandle`, or via #WinMaliEscapeOp_VmBind). Returns
 * #WinMaliEscapeStatus_InvalidHandle if not mapped.
 */
typedef struct _WINMALI_BO_GET_OFFSET {
    WINMALI_BO_GET_OFFSET_IN  In;
    WINMALI_BO_GET_OFFSET_OUT Out;
} WINMALI_BO_GET_OFFSET;

/** @} */

/* ================================================================== */
/** @name Op 0x0015 — BoWait                                          */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoWait.
 *
 * Waits until the last submission referencing the BO completes.
 *
 * @par Timeout semantics
 *
 *  - `TimeoutNs == 0`  ⇒ non-blocking poll; returns
 *    #WinMaliEscapeStatus_Busy if still in use.
 *  - `TimeoutNs < 0`   ⇒ wait forever.
 *  - Otherwise relative nanoseconds; returns
 *    #WinMaliEscapeStatus_Timeout on expiry.
 */
typedef struct _WINMALI_BO_WAIT_IN {
    uint32_t BoHandle;
    uint32_t Pad;
    int64_t  TimeoutNs;
} WINMALI_BO_WAIT_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0016 — BoMadvise                                       */
/** @{ */

#define WINMALI_BO_MADV_WILLNEED  0     ///< Re-acquire backing pages if dropped.
#define WINMALI_BO_MADV_DONTNEED  1     ///< Pages may be reclaimed under memory pressure.

/**
 * @brief Input for #WinMaliEscapeOp_BoMadvise.
 */
typedef struct _WINMALI_BO_MADVISE_IN {
    uint32_t BoHandle;
    uint32_t Advice;        ///< `WINMALI_BO_MADV_*`.
} WINMALI_BO_MADVISE_IN;

/**
 * @brief Output for #WinMaliEscapeOp_BoMadvise.
 */
typedef struct _WINMALI_BO_MADVISE_OUT {
    uint32_t Retained;      ///< For WILLNEED: 1 if backing still present, 0 if it was reaped.
    uint32_t Pad;
} WINMALI_BO_MADVISE_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_BoMadvise.
 */
typedef struct _WINMALI_BO_MADVISE {
    WINMALI_BO_MADVISE_IN  In;
    WINMALI_BO_MADVISE_OUT Out;
} WINMALI_BO_MADVISE;

/** @} */

/* ================================================================== */
/** @name VM flags                                                    */
/** @{ */

#define WINMALI_VM_FLAG_AUTO_VA     0x00000001u     ///< Allow VmBind without an explicit VA.

/** @} */

/* ================================================================== */
/** @name Op 0x0020 — VmCreate                                        */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_VmCreate.
 */
typedef struct _WINMALI_VM_CREATE_IN {
    uint32_t Flags;     ///< `WINMALI_VM_FLAG_*`.
    uint32_t Pad;
    uint64_t VaStart;   ///< 0 = KMD picks arch default.
    uint64_t VaRange;   ///< 0 = KMD picks arch default.
} WINMALI_VM_CREATE_IN;

/**
 * @brief Output for #WinMaliEscapeOp_VmCreate.
 */
typedef struct _WINMALI_VM_CREATE_OUT {
    uint32_t VmHandle;      ///< Nonzero KMD-owned handle.
    uint32_t Pad;
    uint64_t UserVaStart;   ///< KMD-decided usable VA range start.
    uint64_t UserVaRange;   ///< KMD-decided usable VA range size in bytes.
} WINMALI_VM_CREATE_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_VmCreate.
 *
 * Creates a per-process GPU address space.
 *
 * @par KMD behavior
 *
 * Allocates an MMU address-space slot. On v4-v7 there's typically one
 * AS slot shared device-wide with per-process VM differentiated by
 * fast-slot switch; on v9+ each VM is its own MMU context. KMD hides
 * the difference behind this op.
 *
 * If `In.VaStart`/`In.VaRange` are both zero, KMD picks its arch
 * default — typically `0x2000000 .. 4GB` for 32-bit, or the full
 * 48-bit space minus reserved zones for v9+.
 *
 * `WINMALI_VM_FLAG_AUTO_VA` lets the UMD pass
 * #WINMALI_VM_BIND_FLAG_AUTO_VA in subsequent #WinMaliEscapeOp_VmBind
 * ops; without it, every bind must supply an explicit GPU VA.
 */
typedef struct _WINMALI_VM_CREATE {
    WINMALI_VM_CREATE_IN  In;
    WINMALI_VM_CREATE_OUT Out;
} WINMALI_VM_CREATE;

/** @} */

/* ================================================================== */
/** @name Op 0x0021 — VmDestroy                                       */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_VmDestroy.
 */
typedef struct _WINMALI_VM_DESTROY_IN {
    uint32_t VmHandle;
    uint32_t Pad;
} WINMALI_VM_DESTROY_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0022 — VmBind                                          */
/** @{ */

#define WINMALI_VM_BIND_OP_MAP      1   ///< Map BO range into VM.
#define WINMALI_VM_BIND_OP_UNMAP    2   ///< Unmap VA range from VM.

#define WINMALI_VM_BIND_FLAG_AUTO_VA    0x00000001u   ///< KMD picks `GpuVa`; writes result back in-place.

#define WINMALI_VM_BIND_MODE_IMMEDIATE  1   ///< Effective before escape returns.
#define WINMALI_VM_BIND_MODE_DEFERRED   2   ///< Tied to next submission's completion fence.

/**
 * @brief One element of the variable-length VmBind op array.
 *
 * Array placed at byte offset `WINMALI_VM_BIND_IN::OpsOffset` from the
 * start of `pPrivateDriverData`.
 */
typedef struct _WINMALI_VM_BIND_OP {
    uint32_t Op;            ///< `WINMALI_VM_BIND_OP_MAP` or `_UNMAP`.
    uint32_t Flags;         ///< `WINMALI_VM_BIND_FLAG_*`.
    uint32_t BoHandle;      ///< BO to map (ignored for UNMAP if `VaSize` covers a synthetic range).
    uint32_t Pad0;
    uint64_t BoOffset;      ///< Page-aligned byte offset into the BO.
    uint64_t GpuVa;         ///< In/out: AUTO_VA writes the chosen VA back here.
    uint64_t VaSize;        ///< Bytes to map.
} WINMALI_VM_BIND_OP;

/**
 * @brief Fixed-size portion of the #WinMaliEscapeOp_VmBind payload.
 *
 * @par Wire format
 *
 * @verbatim
 *  [WINMALI_ESCAPE_HEADER]
 *  [WINMALI_VM_BIND_IN]
 *  ...padding to OpsOffset...
 *  [WINMALI_VM_BIND_OP × OpCount]
 * @endverbatim
 *
 * Total escape buffer size = `OpsOffset + OpCount * sizeof(WINMALI_VM_BIND_OP)`.
 *
 * @par KMD behavior
 *
 *  - Validate `OpsOffset` is at least `sizeof(header) + sizeof(WINMALI_VM_BIND_IN)`
 *    and aligned to #WINMALI_TAIL_ALIGN.
 *  - Validate `OpsOffset + OpCount * sizeof(WINMALI_VM_BIND_OP) ==
 *    header.PayloadSize`.
 *  - For each op:
 *    - `_MAP`: program the MMU PT to point at `BoHandle`'s pages
 *      starting at `BoOffset` for `VaSize` bytes, mapped at `GpuVa`.
 *      If `Flags & AUTO_VA`, allocate a VA in the VM's free range and
 *      write it back into `GpuVa` (in-place).
 *    - `_UNMAP`: clear PT entries covering `[GpuVa, GpuVa + VaSize)`.
 *
 * @par Mode semantics
 *
 *  - `IMMEDIATE`: KMD MAY block on outstanding GPU work using the
 *    mappings being changed. Used by mesa for synchronous binds.
 *  - `DEFERRED`: KMD applies the binds when the next submission on
 *    the VM completes; returns immediately. Used by mesa for
 *    low-latency binds during command-buffer building. Implementation
 *    detail mirrors `pan_kmod_vm_op_mode`.
 */
typedef struct _WINMALI_VM_BIND_IN {
    uint32_t VmHandle;
    uint32_t Mode;          ///< `WINMALI_VM_BIND_MODE_*`.
    uint32_t OpCount;
    uint32_t OpsOffset;     ///< Byte offset from start of `pPrivateDriverData`.
} WINMALI_VM_BIND_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0030 — SubmitJm (JM hardware, v4-v7)                   */
/** @{ */

#define WINMALI_JD_REQ_FS  (1u << 0)    ///< Fragment shader submit (mirrors `PANFROST_JD_REQ_FS`).

/**
 * @brief Input for #WinMaliEscapeOp_SubmitJm.
 *
 * Mirrors `struct drm_panfrost_submit` with two changes for
 * cross-boundary safety:
 *  - `drm_panfrost_submit::bo_handles` (UM pointer) becomes
 *    #BoHandlesOffset (byte offset within `pPrivateDriverData`).
 *  - `drm_panfrost_submit::in_syncs` becomes #InSyncsOffset.
 *
 * @par Wire format
 *
 * @verbatim
 *  [WINMALI_ESCAPE_HEADER]
 *  [WINMALI_SUBMIT_JM_IN]
 *  ...padding to BoHandlesOffset...
 *  [uint32_t × BoHandleCount]
 *  ...padding to InSyncsOffset...
 *  [uint32_t × InSyncCount]
 * @endverbatim
 *
 * @par KMD behavior
 *
 *  - Validate every entry in the BO handle array belongs to this
 *    adapter+process. Reject with InvalidHandle if any don't.
 *  - Walk the JM job descriptor chain starting at `Jc` (a GPU VA in
 *    `VmHandle` — caller's responsibility to have bound it).
 *  - Submit to the appropriate JS slot per `Requirements`.
 *  - Return Success immediately on enqueue; completion observed via
 *    `OutSync` syncobj or #WinMaliEscapeOp_BoWait.
 *
 * @note Returns #WinMaliEscapeStatus_NotSupported on v9+/CSF hardware.
 *       UMD should choose #WinMaliEscapeOp_SubmitCsf when
 *       #WINMALI_PARAMS_OUT::GpuProdId indicates v9+ (G610 included).
 */
typedef struct _WINMALI_SUBMIT_JM_IN {
    uint32_t VmHandle;
    uint32_t Pad;
    uint64_t Jc;                ///< GPU VA of job descriptor chain head.
    uint32_t Requirements;      ///< `WINMALI_JD_REQ_*`.
    uint32_t BoHandleCount;
    uint32_t BoHandlesOffset;   ///< Byte offset to `uint32_t[BoHandleCount]`.
    uint32_t InSyncCount;
    uint32_t InSyncsOffset;     ///< Byte offset to `uint32_t[InSyncCount]`.
    uint32_t OutSync;           ///< Syncobj handle to signal on completion; 0 = none.
    uint32_t Pad1;
} WINMALI_SUBMIT_JM_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0031 — SubmitCsf (CSF hardware, v9+ / G610)            */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_SubmitCsf.
 *
 * Submits a CSF (Command Stream Frontend) ring-buffer batch. v9+
 * hardware (G610 included) has 32-byte command-stream entries
 * scheduled by the MCU. UMD writes the entries into the queue ring,
 * then asks the KMD to nudge the MCU to start consuming.
 *
 * @par Ownership
 *
 * **KMD owns:**
 *  - The MCU firmware boot and queue-group lifecycle.
 *  - The queue groups (each = up to 8 streams; group bound to a VM at
 *    create-time).
 *  - The ring buffers (BO-backed, MMU-mapped into the group's VM).
 *
 * **UMD owns:**
 *  - The 32-byte CS opcodes that fill the ring (mesa generates these
 *    via the CSF compile pipeline).
 *  - The protected-mode and exclusion-zone bits in each command.
 *
 * @par KMD behavior
 *
 *  - Validate the queue-group exists and belongs to the calling process.
 *  - Read `RingBufferWriteOffsetNew`, compare to the group's stored
 *    write offset; range-check it against the ring BO's size.
 *  - Call the MCU "doorbell" path to make it pick up the new tail.
 *  - Arrange for `OutSync` (if nonzero) to fire when the MCU acks
 *    completion of the last enqueued command (typically a
 *    FENCE_WAIT/FENCE_SIGNAL pair in the cmdstream).
 *
 * @note Queue groups + ring BOs are created via the not-yet-defined
 *       `CsfGroupCreate` op (held back from v1 since the day-one mesa
 *       use case doesn't need it). When that op lands, group lifetime +
 *       ring geometry move into KMD state and #WinMaliEscapeOp_SubmitCsf
 *       just nudges. For v1, `GroupHandle` MUST be a handle returned by
 *       `CsfGroupCreate`; UMD passes the BO handle of the ring through
 *       that op, not here.
 */
typedef struct _WINMALI_SUBMIT_CSF_IN {
    uint32_t GroupHandle;
    uint32_t QueueIndex;                ///< 0..7 within the group.
    uint64_t RingBufferWriteOffsetNew;
    uint32_t InSyncCount;
    uint32_t InSyncsOffset;             ///< Byte offset to `uint32_t[InSyncCount]`.
    uint32_t OutSync;                   ///< Syncobj handle to signal on completion; 0 = none.
    uint32_t Reserved;
} WINMALI_SUBMIT_CSF_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0040..0x0044 — Sync objects                            */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_SyncObjCreate.
 *
 * Lightweight syncobj — semantically equivalent to DRM syncobj.
 * KMD-tracked binary timeline; full timeline support is reserved for
 * a future minor bump.
 */
typedef struct _WINMALI_SYNCOBJ_CREATE_IN {
    uint32_t InitiallySignaled;     ///< 0 = unsignaled, nonzero = signaled.
    uint32_t Pad;
} WINMALI_SYNCOBJ_CREATE_IN;

/**
 * @brief Output for #WinMaliEscapeOp_SyncObjCreate.
 */
typedef struct _WINMALI_SYNCOBJ_CREATE_OUT {
    uint32_t SyncObjHandle;
    uint32_t Pad;
} WINMALI_SYNCOBJ_CREATE_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_SyncObjCreate.
 */
typedef struct _WINMALI_SYNCOBJ_CREATE {
    WINMALI_SYNCOBJ_CREATE_IN  In;
    WINMALI_SYNCOBJ_CREATE_OUT Out;
} WINMALI_SYNCOBJ_CREATE;

/**
 * @brief Input for #WinMaliEscapeOp_SyncObjDestroy.
 */
typedef struct _WINMALI_SYNCOBJ_DESTROY_IN {
    uint32_t SyncObjHandle;
    uint32_t Pad;
} WINMALI_SYNCOBJ_DESTROY_IN;

#define WINMALI_SYNCOBJ_WAIT_FLAG_ALL    0x00000001u   ///< Wait for ALL handles; otherwise ANY.

/**
 * @brief Input for #WinMaliEscapeOp_SyncObjWait.
 *
 * Tail at #HandlesOffset is a `uint32_t[HandleCount]` of syncobj handles.
 */
typedef struct _WINMALI_SYNCOBJ_WAIT_IN {
    uint32_t Flags;             ///< `WINMALI_SYNCOBJ_WAIT_FLAG_*`.
    uint32_t HandleCount;
    uint32_t HandlesOffset;     ///< Byte offset to `uint32_t[HandleCount]`.
    uint32_t Pad;
    int64_t  TimeoutNs;         ///< 0 = poll, <0 = forever, else relative ns.
} WINMALI_SYNCOBJ_WAIT_IN;

/**
 * @brief Input for #WinMaliEscapeOp_SyncObjSignal.
 */
typedef struct _WINMALI_SYNCOBJ_SIGNAL_IN {
    uint32_t SyncObjHandle;
    uint32_t Pad;
} WINMALI_SYNCOBJ_SIGNAL_IN;

/**
 * @brief Input for #WinMaliEscapeOp_SyncObjReset.
 */
typedef struct _WINMALI_SYNCOBJ_RESET_IN {
    uint32_t SyncObjHandle;
    uint32_t Pad;
} WINMALI_SYNCOBJ_RESET_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0050 — PresentToHdc (ABI v1.1)                         */
/** @{ */

#define WINMALI_PRESENT_FORMAT_BGRA8888  1   ///< DIB-native; no swizzle at copy.
#define WINMALI_PRESENT_FORMAT_RGBA8888  2   ///< Swap R/B at copy time.
#define WINMALI_PRESENT_FORMAT_RGB565    3

/**
 * @brief Input for #WinMaliEscapeOp_PresentToHdc.
 *
 * GL SwapBuffers fast path. UMD has rendered into a panfrost BO and
 * wants the pixels visible in a window.
 *
 * @par KMD behavior
 *
 *  1. Wait for the BO's last submission to complete (implicit
 *     #WinMaliEscapeOp_BoWait).
 *  2. Read back the pixels via the BO's CPU-mappable backing
 *     (KMD-side, no UM round-trip needed since KMD already owns the
 *     pages).
 *  3. Translate the source pixels per `Format` (typically just a
 *     BGRA / RGBA swizzle decision at copy time; identity for
 *     #WINMALI_PRESENT_FORMAT_BGRA8888).
 *  4. `KeStackAttachProcess` to the calling process so the HDC is
 *     valid in this context, then `SetDIBitsToDevice` to the supplied
 *     HDC.
 *
 * @par Why route through the KMD
 *
 * KMD already has the BO mapped in kernel memory (it owns the pages),
 * so this skips the UM round-trip plus section view plus map. The
 * "shippable" version may also let the KMD do a scanout-style copy
 * if/when VOP2 integration lands. For first cut, plain
 * `SetDIBitsToDevice` is fine.
 *
 * @par Sync semantics
 *
 * KMD MUST complete the wait+blit before returning. Asynchronous
 * Present (queued via the GPU scheduler) is a later extension and
 * would use a separate opcode if/when needed.
 *
 * @note Tiled / AFBC surfaces aren't directly presentable — panfrost
 *       renders to linear for swap targets, KMD treats `Stride` as
 *       bytes-per-row of a linear surface.
 */
typedef struct _WINMALI_PRESENT_TO_HDC_IN {
    uint64_t Hdc;           ///< UM HDC handle (PVOID cast to uint64_t — 64-bit on ARM64).
    uint32_t BoHandle;      ///< Source BO. Must be CPU-mappable (not NO_MMAP).
    uint32_t Format;        ///< `WINMALI_PRESENT_FORMAT_*`.
    uint32_t Width;         ///< Pixel width.
    uint32_t Height;        ///< Pixel height.
    uint32_t Stride;        ///< Bytes per row in the BO.
    uint32_t DstX;          ///< Top-left X in the HDC.
    uint32_t DstY;          ///< Top-left Y in the HDC.
    uint32_t Pad;
} WINMALI_PRESENT_TO_HDC_IN;

/** @} */

/* ================================================================== */
/** @name Op 0x0051 — BoFromAllocation (ABI v1.1)                     */
/** @{ */

/**
 * @brief Input for #WinMaliEscapeOp_BoFromAllocation.
 */
typedef struct _WINMALI_BO_FROM_ALLOCATION_IN {
    uint32_t AllocationHandle;  ///< `D3DKMT_HANDLE` (`uint32_t`-shaped).
    uint32_t Flags;             ///< Subset of `WINMALI_BO_FLAG_*` (NOEXEC, GPU_UNCACHED).
    uint32_t VmHandle;          ///< 0 = create unmapped; else map at create time.
    uint32_t Pad;
} WINMALI_BO_FROM_ALLOCATION_IN;

/**
 * @brief Output for #WinMaliEscapeOp_BoFromAllocation.
 */
typedef struct _WINMALI_BO_FROM_ALLOCATION_OUT {
    uint32_t BoHandle;
    uint32_t Pad;
    uint64_t GpuVa;             ///< 0 if unmapped, else GPU virtual address.
    uint64_t Size;              ///< Bytes (from the underlying D3DKMT allocation).
} WINMALI_BO_FROM_ALLOCATION_OUT;

/**
 * @brief Combined in/out for #WinMaliEscapeOp_BoFromAllocation.
 *
 * Wraps an existing D3DKMT allocation handle (typically a DXGI
 * swap-chain backbuffer) as a panfrost-addressable BO. After this op,
 * the returned `BoHandle` behaves identically to a BoCreate'd BO —
 * #WinMaliEscapeOp_VmBind / -Submit* / -BoWait all work against it.
 *
 * @par Why this op exists
 *
 * The seam between canonical WDDM allocation surface
 * (`D3DKMTCreateAllocation`, owned by the D3D11 runtime / DXGI / DWM)
 * and the escape-driven panfrost path. The desktop compositor can
 * only read pixels from kernel-tracked allocations — escape-managed
 * BOs are invisible to it. This op lets panfrost render *into* an
 * OS-tracked allocation that DWM composites.
 *
 * @par KMD behavior
 *
 *  - Resolve the `AllocationHandle` (the calling process opened it
 *    via `D3DKMTCreateAllocation2` or `D3DKMTOpenResource*`).
 *  - Pin its backing pages into the panfrost VM for the duration of
 *    the WinMali BO.
 *  - If `In.VmHandle != 0`, map at an auto-picked GPU VA in that VM
 *    and return it in `Out.GpuVa`.
 *
 * @par Lifetime
 *
 * The WinMali BO references the D3DKMT allocation. On
 * #WinMaliEscapeOp_BoDestroy, KMD releases the panfrost-side mapping
 * but does NOT destroy the underlying D3DKMT allocation — the D3D
 * runtime owns its lifetime.
 *
 * @note `WINMALI_BO_FLAG_HEAP` / `NO_MMAP` / `ALLOC_ON_FAULT` are
 *       invalid here — the allocation's backing is the OS's
 *       responsibility, not ours.
 */
typedef struct _WINMALI_BO_FROM_ALLOCATION {
    WINMALI_BO_FROM_ALLOCATION_IN  In;
    WINMALI_BO_FROM_ALLOCATION_OUT Out;
} WINMALI_BO_FROM_ALLOCATION;

/** @} */

/* ================================================================== */
/** @name Helpers for UMD packing code                                */
/** @{ */

/**
 * @brief Align `x` up to a multiple of `a`. `a` must be a power of two.
 */
#define WINMALI_ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))

/**
 * @brief Standard alignment for variable-length tail arrays inside
 *        the escape buffer.
 *
 * KMD validates that UMD-supplied tail offsets (`OpsOffset`,
 * `BoHandlesOffset`, `InSyncsOffset`, `HandlesOffset`) are aligned to
 * this. Reject as #WinMaliEscapeStatus_InvalidPayload if not.
 */
#define WINMALI_TAIL_ALIGN  8u

/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif
