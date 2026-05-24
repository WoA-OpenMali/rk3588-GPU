/*
 * WinMaliEscape.c - DxgkDdiEscape dispatcher for the WinMali UMD<->KMD ABI.
 *
 * The ABI itself is defined in Shared/WinMaliEscape.h. Wire format:
 *
 *   [0]                       WINMALI_ESCAPE_HEADER  (magic, ABI, op, status)
 *   [sizeof(header)]          Op-specific argument struct
 *   [trailing region]         Variable-length tails (sync ops, vm-bind ops, ...)
 *                             addressed by byte offsets recorded in the args.
 *
 * The escape buffer originates in the UMD process via D3DKMTEscape with
 * Type=D3DKMT_ESCAPE_DRIVERPRIVATE. dxgkrnl probes the buffer into a
 * KM-readable mapping before handing it to us, but offsets inside the
 * buffer still come from user mode and we must bounds-check them before
 * dereferencing.
 *
 * Convention (mirroring pan_kmod_ioctl):
 *   - NTSTATUS return is STATUS_SUCCESS once we've recognised the buffer
 *     (i.e. magic OK). All errors land in hdr->Status as -errno.
 *   - Magic mismatch is the only path that returns a non-SUCCESS NTSTATUS,
 *     so foreign D3DKMT_ESCAPE buffers don't accidentally see hdr->Status
 *     overwritten.
 *
 * This file implements the foundation:
 *   Op 0x80 OpenDevice  - ABI handshake (validates major, returns identity)
 *   Op 0    DevQuery    - WinMaliDevQuery_GpuInfo (others stubbed)
 */

#include "WinMaliKmd.h"
#include "hw/WinMaliHw.h"
#include "WinMaliBo.h"
#include "WinMaliSync.h"
#include "WinMaliVm.h"
#include "WinMaliGroup.h"

/* Linux errno values - escape Status is -errno on failure. */
#define WMERR_OK          0
#define WMERR_EINVAL      -22
#define WMERR_ENOMEM      -12
#define WMERR_ENOENT      -2
#define WMERR_ENOSYS      -38
#define WMERR_EOPNOTSUPP  -95
#define WMERR_ENODEV      -19
#define WMERR_EBUSY       -16
#define WMERR_EIO         -5

/* Identity string returned in WINMALI_OPEN_DEVICE. */
#define WINMALI_KMD_NAME    "WinMali-rk3588"

#define WM_QAI_OK_TRACE(name)  WINMALI_TRACE("Escape " name ": OK")

/* Build id - bumps when the dispatcher / op surface changes meaningfully.
   Compatibility is governed by ABI major/minor; this is purely a debug aid
   so a UMD can log "I'm talking to KMD build X". */
#define WINMALI_KMD_BUILD_ID  0x20260521u

/* Bounds-check that [offset, offset+size) lies inside the escape buffer.
   All offsets in our ABI are byte offsets from the start of
   pPrivateDriverData. UMD-controlled values - validate aggressively. */
static BOOLEAN
WinMaliEscapeRangeOk_(_In_ const DXGKARG_ESCAPE* pEscape,
                      _In_ ULONG offset,
                      _In_ ULONG size)
{
    ULONG end;
    if (size == 0) {
        return TRUE;
    }
    end = offset + size;
    if (end < offset) {           /* overflow */
        return FALSE;
    }
    if (end > pEscape->PrivateDriverDataSize) {
        return FALSE;
    }
    return TRUE;
}

/* Return a typed pointer to a substructure inside the escape buffer at the
   given byte offset, after verifying the range fits. NULL on failure. */
static PVOID
WinMaliEscapeAt_(_In_ const DXGKARG_ESCAPE* pEscape,
                 _In_ ULONG offset,
                 _In_ ULONG size)
{
    if (!WinMaliEscapeRangeOk_(pEscape, offset, size)) {
        return NULL;
    }
    return (PUCHAR)pEscape->pPrivateDriverData + offset;
}

/* -------------------------------------------------------------------------- */
/*  Op 0x80 - OpenDevice                                                       */
/* -------------------------------------------------------------------------- */

static int
WinMaliEscapeOpenDevice_(
    _In_    PWINMALI_ADAPTER       adapter,
    _Inout_ WINMALI_ESCAPE_HEADER* hdr,
    _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_OPEN_DEVICE* args;
    ULONG argOffset = sizeof(*hdr);

    UNREFERENCED_PARAMETER(adapter);

    args = (WINMALI_OPEN_DEVICE*)WinMaliEscapeAt_(pEscape, argOffset, sizeof(*args));
    if (args == NULL) {
        WINMALI_WARN("Escape OpenDevice: payload too small (size=%u, need >=%zu)",
                     pEscape->PrivateDriverDataSize, sizeof(*hdr) + sizeof(*args));
        return WMERR_EINVAL;
    }

    if (args->UmdAbiMajor != WINMALI_ABI_MAJOR) {
        WINMALI_WARN("Escape OpenDevice: UMD ABI major=%u, KMD major=%u",
                     args->UmdAbiMajor, WINMALI_ABI_MAJOR);
        return WMERR_EINVAL;
    }

    args->KmdAbiMajor = WINMALI_ABI_MAJOR;
    args->KmdAbiMinor = WINMALI_ABI_MINOR;
    args->KmdBuildId  = WINMALI_KMD_BUILD_ID;
    RtlZeroMemory(args->KmdName, sizeof(args->KmdName));
    RtlCopyMemory(args->KmdName, WINMALI_KMD_NAME, sizeof(WINMALI_KMD_NAME));

    WINMALI_TRACE("Escape OpenDevice: handshake OK (UMD %u.%u, KMD %u.%u, build 0x%08x)",
                  args->UmdAbiMajor, args->UmdAbiMinor,
                  args->KmdAbiMajor, args->KmdAbiMinor,
                  args->KmdBuildId);
    return WMERR_OK;
}

/* -------------------------------------------------------------------------- */
/*  Op 0 - DevQuery (Type = GpuInfo)                                           */
/* -------------------------------------------------------------------------- */

static int
WinMaliEscapeFillGpuInfo_(_In_  PWINMALI_ADAPTER  adapter,
                          _Out_ WINMALI_GPU_INFO* out)
{
    const WINMALI_HW* hw = &adapter->Hw;
    ULONG shaderLo = 0, shaderHi = 0;
    ULONG l2Lo = 0, l2Hi = 0;

    RtlZeroMemory(out, sizeof(*out));

    /* Cached at WinMaliHwProbeIdentity. */
    out->GpuId        = hw->GpuId;
    out->CsfId        = hw->CsfId;
    out->L2Features   = hw->L2Features;
    out->MmuFeatures  = hw->MmuFeatures;
    out->AsPresent    = hw->AsPresent;

    /* Re-read live registers for fields we don't cache. Safe even if regs
       aren't mapped (WinMaliHwRead32 returns 0). */
    if (hw->RegsVa != NULL) {
        shaderLo = WinMaliHwRead32(hw, WINMALI_REG_GPU_SHADER_PRESENT_LO);
        shaderHi = WinMaliHwRead32(hw, WINMALI_REG_GPU_SHADER_PRESENT_HI);
        l2Lo     = WinMaliHwRead32(hw, WINMALI_REG_GPU_L2_PRESENT_LO);
        l2Hi     = WinMaliHwRead32(hw, WINMALI_REG_GPU_L2_PRESENT_HI);
    }
    out->ShaderPresent = ((UINT64)shaderHi << 32) | shaderLo;
    out->L2Present     = ((UINT64)l2Hi << 32)     | l2Lo;

    /* TilerFeatures / MemFeatures / ThreadFeatures / TextureFeatures /
       coherency etc. - register offsets aren't all in WinMaliHw.h yet.
       Leave zero; UMD treats zero as "unknown" and falls back to defaults.
       TilerPresent is 0 on G610 (no separate tiler-present register).
       CorePresent is unused on v10 (MBZ per ABI doc). */
    return WMERR_OK;
}

static int
WinMaliEscapeDevQuery_(
    _In_    PWINMALI_ADAPTER       adapter,
    _Inout_ WINMALI_ESCAPE_HEADER* hdr,
    _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_DEV_QUERY* args;
    ULONG argOffset = sizeof(*hdr);

    args = (WINMALI_DEV_QUERY*)WinMaliEscapeAt_(pEscape, argOffset, sizeof(*args));
    if (args == NULL) {
        return WMERR_EINVAL;
    }

    switch (args->Type) {
    case WinMaliDevQuery_GpuInfo: {
        WINMALI_GPU_INFO info;
        WINMALI_GPU_INFO* dst;
        ULONG offset;

        if (args->PointerOffset > 0xFFFFFFFFu) {
            return WMERR_EINVAL;
        }
        offset = (ULONG)args->PointerOffset;

        /* Probe-size path: UMD sets Size=0 to discover required size. */
        if (args->Size == 0) {
            args->Size = sizeof(info);
            return WMERR_OK;
        }
        if (args->Size < sizeof(info)) {
            args->Size = sizeof(info);
            return WMERR_EINVAL;
        }
        dst = (WINMALI_GPU_INFO*)WinMaliEscapeAt_(pEscape, offset, sizeof(info));
        if (dst == NULL) {
            return WMERR_EINVAL;
        }
        (void)WinMaliEscapeFillGpuInfo_(adapter, &info);
        RtlCopyMemory(dst, &info, sizeof(info));
        args->Size = sizeof(info);
        WINMALI_TRACE("Escape DevQuery GpuInfo: GpuId=0x%08x CsfId=0x%08x shader=0x%llx",
                      info.GpuId, info.CsfId, (ULONGLONG)info.ShaderPresent);
        return WMERR_OK;
    }
    case WinMaliDevQuery_CsifInfo: {
        /* CSF interface dimensions. These match what the Mali G610's CSF
           firmware exposes via the shared interface page. Values here are
           the architecture defaults; ideally we'd read them from the live
           iface struct cached at WinMaliFwInit time, but until that's
           plumbed through, the constants are correct for G610. */
        WINMALI_CSIF_INFO info;
        WINMALI_CSIF_INFO* dst;
        ULONG offset;

        if (args->PointerOffset > 0xFFFFFFFFu) return WMERR_EINVAL;
        offset = (ULONG)args->PointerOffset;
        if (args->Size == 0) { args->Size = sizeof(info); return WMERR_OK; }
        if (args->Size < sizeof(info)) { args->Size = sizeof(info); return WMERR_EINVAL; }
        dst = (WINMALI_CSIF_INFO*)WinMaliEscapeAt_(pEscape, offset, sizeof(info));
        if (dst == NULL) return WMERR_EINVAL;

        RtlZeroMemory(&info, sizeof(info));
        info.CsGroupCount        = 4;   /* G610: 4 parallel CSGs */
        info.CsCount             = 8;   /* per-group command streams */
        info.ScoreboardSlotCount = 8;
        info.SyncWaitSlotCount   = 8;
        info.CsRegCount          = 96;  /* per-CS register file */
        RtlCopyMemory(dst, &info, sizeof(info));
        args->Size = sizeof(info);
        WM_QAI_OK_TRACE("DevQuery CsifInfo");
        return WMERR_OK;
    }

    case WinMaliDevQuery_TimestampInfo: {
        WINMALI_TIMESTAMP_INFO info;
        WINMALI_TIMESTAMP_INFO* dst;
        ULONG offset;
        LARGE_INTEGER perfCount, perfFreq;

        if (args->PointerOffset > 0xFFFFFFFFu) return WMERR_EINVAL;
        offset = (ULONG)args->PointerOffset;
        if (args->Size == 0) { args->Size = sizeof(info); return WMERR_OK; }
        if (args->Size < sizeof(info)) { args->Size = sizeof(info); return WMERR_EINVAL; }
        dst = (WINMALI_TIMESTAMP_INFO*)WinMaliEscapeAt_(pEscape, offset, sizeof(info));
        if (dst == NULL) return WMERR_EINVAL;

        /* Until the GPU timestamp register is wired, expose the system QPC
           as a stand-in. UMDs that compute relative deltas will work; ones
           that need GPU-side timestamps will need this revisited. */
        perfCount = KeQueryPerformanceCounter(&perfFreq);
        RtlZeroMemory(&info, sizeof(info));
        info.TimestampFrequency = (UINT64)perfFreq.QuadPart;
        info.CurrentTimestamp   = (UINT64)perfCount.QuadPart;
        info.TimestampOffset    = 0;
        RtlCopyMemory(dst, &info, sizeof(info));
        args->Size = sizeof(info);
        WM_QAI_OK_TRACE("DevQuery TimestampInfo");
        return WMERR_OK;
    }

    case WinMaliDevQuery_GroupPrioritiesInfo: {
        WINMALI_GROUP_PRIORITIES_INFO info;
        WINMALI_GROUP_PRIORITIES_INFO* dst;
        ULONG offset;

        if (args->PointerOffset > 0xFFFFFFFFu) return WMERR_EINVAL;
        offset = (ULONG)args->PointerOffset;
        if (args->Size == 0) { args->Size = sizeof(info); return WMERR_OK; }
        if (args->Size < sizeof(info)) { args->Size = sizeof(info); return WMERR_EINVAL; }
        dst = (WINMALI_GROUP_PRIORITIES_INFO*)WinMaliEscapeAt_(pEscape, offset, sizeof(info));
        if (dst == NULL) return WMERR_EINVAL;

        RtlZeroMemory(&info, sizeof(info));
        info.AllowedMask = 0x0F;  /* all four priority levels (Low..Realtime) */
        RtlCopyMemory(dst, &info, sizeof(info));
        args->Size = sizeof(info);
        WM_QAI_OK_TRACE("DevQuery GroupPrioritiesInfo");
        return WMERR_OK;
    }

    default:
        WINMALI_WARN("Escape DevQuery: unknown type %u", args->Type);
        return WMERR_EINVAL;
    }

    UNREFERENCED_PARAMETER(hdr);
}

static int
WinMaliEscapeBoCreate_(_In_    PWINMALI_ADAPTER       adapter,
                       _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                       _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_BO_CREATE* args =
        (WINMALI_BO_CREATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    ULONG handle = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliBoCreate(adapter, (SIZE_T)args->Size, args->Flags,
                          args->ExclusiveVmId, &handle);
    if (err == WMERR_OK) {
        args->Handle = handle;
        args->Pad    = 0;
    }
    return err;
}

static int
WinMaliEscapeBoDestroy_(_In_    PWINMALI_ADAPTER       adapter,
                        _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                        _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_BO_DESTROY* args =
        (WINMALI_BO_DESTROY*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    return WinMaliBoDestroy(adapter, args->Handle);
}

static int
WinMaliEscapeBoQueryInfo_(_In_    PWINMALI_ADAPTER       adapter,
                          _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                          _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_BO_QUERY_INFO* args =
        (WINMALI_BO_QUERY_INFO*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    PWINMALI_BO bo;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    bo = WinMaliBoGet(adapter, args->Handle);
    if (bo == NULL) {
        return WMERR_ENOENT;
    }
    args->Size           = bo->Size;
    args->Flags          = bo->Flags;
    args->ExclusiveVmId  = bo->ExclusiveVmId;
    WinMaliBoPut(bo);
    return WMERR_OK;
}

static int
WinMaliEscapeBoSetLabel_(_In_    PWINMALI_ADAPTER       adapter,
                         _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                         _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_BO_SET_LABEL* args =
        (WINMALI_BO_SET_LABEL*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    const char* label;
    if (args == NULL || args->LabelLen > WINMALI_BO_LABEL_MAX) {
        return WMERR_EINVAL;
    }
    label = (const char*)WinMaliEscapeAt_(pEscape, args->LabelOffset, args->LabelLen);
    if (label == NULL && args->LabelLen != 0) {
        return WMERR_EINVAL;
    }
    return WinMaliBoSetLabel(adapter, args->Handle, label, args->LabelLen);
}

static int
WinMaliEscapeBoMapCpu_(_In_    PWINMALI_ADAPTER       adapter,
                       _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                       _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_BO_MAP_CPU* args =
        (WINMALI_BO_MAP_CPU*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    UINT64 va = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliBoMapCpu(adapter, args->Handle, args->Prot, &va);
    if (err == WMERR_OK) {
        args->CpuAddr = va;
    }
    return err;
}

static int
WinMaliEscapeSyncObjCreate_(_In_    PWINMALI_ADAPTER       adapter,
                            _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                            _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_SYNC_OBJ_CREATE* args =
        (WINMALI_SYNC_OBJ_CREATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    ULONG handle = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliSyncObjCreate(adapter, args->Flags, args->InitialState, &handle);
    if (err == WMERR_OK) {
        args->Handle = handle;
        args->Pad    = 0;
    }
    return err;
}

static int
WinMaliEscapeSyncObjDestroy_(_In_    PWINMALI_ADAPTER       adapter,
                             _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                             _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_SYNC_OBJ_DESTROY* args =
        (WINMALI_SYNC_OBJ_DESTROY*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    return WinMaliSyncObjDestroy(adapter, args->Handle);
}

static int
WinMaliEscapeVmCreate_(_In_    PWINMALI_ADAPTER       adapter,
                       _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                       _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_VM_CREATE* args =
        (WINMALI_VM_CREATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    ULONG id = 0;
    UINT64 granted = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliVmCreate(adapter, args->Flags, args->UserVaRange, &id, &granted);
    if (err == WMERR_OK) {
        args->Id          = id;
        args->UserVaRange = granted;
    }
    return err;
}

static int
WinMaliEscapeVmDestroy_(_In_    PWINMALI_ADAPTER       adapter,
                        _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                        _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_VM_DESTROY* args =
        (WINMALI_VM_DESTROY*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    return WinMaliVmDestroy(adapter, args->Id);
}

static int
WinMaliEscapeVmGetState_(_In_    PWINMALI_ADAPTER       adapter,
                         _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                         _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_VM_GET_STATE* args =
        (WINMALI_VM_GET_STATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    WINMALI_VM_RUN_STATE state;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliVmGetState(adapter, args->VmId, &state);
    if (err == WMERR_OK) {
        args->State = (state == WinMaliVmRun_Usable) ? WinMaliVmState_Usable
                                                     : WinMaliVmState_Unusable;
    }
    return err;
}

/* Walk a VM_BIND_OP array, installing/removing PTEs into the VM's PT. */
static int
WinMaliEscapeVmBind_(_In_    PWINMALI_ADAPTER       adapter,
                     _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                     _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_VM_BIND* args =
        (WINMALI_VM_BIND*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    const WINMALI_VM_BIND_OP* ops;
    PWINMALI_VM vm;
    ULONG i;
    int err = WMERR_OK;

    if (args == NULL) {
        return WMERR_EINVAL;
    }
    if (args->OpsCount == 0) {
        return WMERR_OK;
    }
    ops = (const WINMALI_VM_BIND_OP*)WinMaliEscapeAt_(
        pEscape, args->OpsOffset, args->OpsCount * sizeof(*ops));
    if (ops == NULL) {
        return WMERR_EINVAL;
    }
    vm = WinMaliVmGet(adapter, args->VmId);
    if (vm == NULL) {
        return WMERR_ENOENT;
    }
    if (!vm->PtInitialized) {
        WinMaliVmPut(vm);
        return WMERR_EINVAL;
    }

    for (i = 0; i < args->OpsCount; ++i) {
        const WINMALI_VM_BIND_OP* op = &ops[i];
        ULONG opType = op->Flags & WINMALI_VM_BIND_OP_TYPE_MASK;
        if (opType == WINMALI_VM_BIND_OP_TYPE_MAP) {
            PWINMALI_BO bo;
            PPFN_NUMBER pfnArray;
            ULONG pageCount;
            UINT64 attrs;
            NTSTATUS st;

            bo = WinMaliBoGet(adapter, op->BoHandle);
            if (bo == NULL) {
                WINMALI_WARN("VmBind MAP: BoHandle %u unknown", op->BoHandle);
                err = WMERR_ENOENT;
                break;
            }
            if (op->BoOffset != 0 || op->Size > bo->Size) {
                /* BoOffset not yet supported (would need offset-aware MDL
                   walk); reject for now to surface UMD mistakes early. */
                WinMaliBoPut(bo);
                WINMALI_WARN("VmBind MAP: BoOffset=%llu size=%llu (bo size=%llu) - "
                             "BoOffset != 0 not yet supported",
                             (ULONGLONG)op->BoOffset, (ULONGLONG)op->Size,
                             (ULONGLONG)bo->Size);
                err = WMERR_EINVAL;
                break;
            }
            pfnArray = MmGetMdlPfnArray(bo->Mdl);
            if (pfnArray == NULL) {
                WinMaliBoPut(bo);
                err = WMERR_EINVAL;
                break;
            }
            pageCount = (ULONG)((op->Size + PAGE_SIZE - 1) >> PAGE_SHIFT);

            /* Attribute mapping from drm_panthor_vm_bind_op_flags. */
            if (op->Flags & WINMALI_VM_BIND_OP_MAP_READONLY) {
                attrs = (op->Flags & WINMALI_VM_BIND_OP_MAP_NOEXEC)
                          ? WINMALI_LPAE_L3_PAGE_ATTR_RO_NX
                          : WINMALI_LPAE_L3_PAGE_ATTR_RO_EX;
            } else {
                attrs = (op->Flags & WINMALI_VM_BIND_OP_MAP_NOEXEC)
                          ? WINMALI_LPAE_L3_PAGE_ATTR_RW_NX
                          : WINMALI_LPAE_L3_PAGE_ATTR_RW_EX;
            }

            st = WinMaliMmuVmMap(adapter, &vm->Pt, op->Va, pfnArray,
                                 pageCount, attrs);
            WinMaliBoPut(bo);
            if (!NT_SUCCESS(st)) {
                err = (st == STATUS_INSUFFICIENT_RESOURCES) ? WMERR_ENOMEM
                                                            : WMERR_EINVAL;
                break;
            }
            WINMALI_TRACE("VmBind MAP: vm=%u bo=%u va=0x%llx pages=%u attrs=0x%llx",
                          vm->Id, op->BoHandle, (ULONGLONG)op->Va,
                          pageCount, (ULONGLONG)attrs);
        } else if (opType == WINMALI_VM_BIND_OP_TYPE_UNMAP) {
            ULONG pageCount = (ULONG)((op->Size + PAGE_SIZE - 1) >> PAGE_SHIFT);
            NTSTATUS st = WinMaliMmuVmUnmap(adapter, &vm->Pt, op->Va, pageCount);
            if (!NT_SUCCESS(st)) {
                err = WMERR_EINVAL;
                break;
            }
            WINMALI_TRACE("VmBind UNMAP: vm=%u va=0x%llx pages=%u",
                          vm->Id, (ULONGLONG)op->Va, pageCount);
        } else if (opType == WINMALI_VM_BIND_OP_TYPE_SYNC_ONLY) {
            /* SYNC_ONLY: no PT work, just sync ops. We'd process op->SyncsOffset
               here once sync integration with SyncObjs lands. */
            WINMALI_TRACE("VmBind SYNC_ONLY: vm=%u (sync arrays not yet processed)",
                          vm->Id);
        } else {
            WINMALI_WARN("VmBind: unknown op type 0x%x (flags=0x%x)",
                         opType, op->Flags);
            err = WMERR_EINVAL;
            break;
        }
    }

    WinMaliVmPut(vm);
    return err;
}

static int
WinMaliEscapeGroupCreate_(_In_    PWINMALI_ADAPTER       adapter,
                          _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                          _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_GROUP_CREATE* args =
        (WINMALI_GROUP_CREATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    const WINMALI_QUEUE_CREATE* queues;
    ULONG handle = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    if (args->QueuesCount == 0 || args->QueuesCount > WINMALI_GROUP_MAX_QUEUES) {
        return WMERR_EINVAL;
    }
    queues = (const WINMALI_QUEUE_CREATE*)WinMaliEscapeAt_(
        pEscape, args->QueuesOffset,
        args->QueuesCount * sizeof(WINMALI_QUEUE_CREATE));
    if (queues == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliGroupCreate(adapter, args, queues, args->QueuesCount, &handle);
    if (err == WMERR_OK) {
        args->GroupHandle = handle;
    }
    return err;
}

static int
WinMaliEscapeGroupDestroy_(_In_    PWINMALI_ADAPTER       adapter,
                           _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                           _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_GROUP_DESTROY* args =
        (WINMALI_GROUP_DESTROY*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    return WinMaliGroupDestroy(adapter, args->GroupHandle);
}

static int
WinMaliEscapeGroupGetState_(_In_    PWINMALI_ADAPTER       adapter,
                            _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                            _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_GROUP_GET_STATE* args =
        (WINMALI_GROUP_GET_STATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    ULONG state = 0, fatal = 0;
    int err;
    if (args == NULL) {
        return WMERR_EINVAL;
    }
    err = WinMaliGroupGetState(adapter, args->GroupHandle, &state, &fatal);
    if (err == WMERR_OK) {
        args->StateFlags  = state;
        args->FatalQueues = fatal;
    }
    return err;
}

/* Process the sync-op tail of a queue submit. Direction == 0: wait; ==1: signal.
   Skips ops whose direction bit doesn't match. Returns -errno on failure. */
static int
WinMaliEscapeProcessSyncOps_(_Inout_ PWINMALI_ADAPTER adapter,
                             _In_    const WINMALI_SYNC_OP* ops,
                             _In_    ULONG count,
                             _In_    BOOLEAN isSignal)
{
    ULONG i;
    int err = WMERR_OK;
    for (i = 0; i < count; ++i) {
        BOOLEAN opIsSignal = (ops[i].Flags & WINMALI_SYNC_OP_SIGNAL) ? TRUE : FALSE;
        if (opIsSignal != isSignal) {
            continue;
        }
        if (isSignal) {
            int e = WinMaliSyncObjSignal(adapter, ops[i].Handle,
                                         ops[i].TimelineValue);
            if (e != WMERR_OK && err == WMERR_OK) {
                err = e;
            }
        } else {
            ULONG h = ops[i].Handle;
            UINT64 p = ops[i].TimelineValue;
            int e = WinMaliSyncObjWait(adapter, 1u, &h, &p,
                                       FALSE, -1LL /* infinite */);
            if (e != WMERR_OK) {
                err = e;
                break;
            }
        }
    }
    return err;
}

/* GroupSubmit: walk queue submits, process pre-submit sync waits, (defer
   actual CSF submission), process post-submit sync signals.
   The CSF kernel-queue path requires per-group csg slot allocation +
   csg iface programming so the MCU switches to the group's VM AS when
   executing the stream. That plumbing isn't in place yet; for now the
   sync framework runs end-to-end and the stream submission itself is a
   no-op SUCCESS with a TRACE that records what would have been submitted. */
static int
WinMaliEscapeGroupSubmit_(_In_    PWINMALI_ADAPTER       adapter,
                          _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                          _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_GROUP_SUBMIT* args =
        (WINMALI_GROUP_SUBMIT*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    const WINMALI_QUEUE_SUBMIT* submits;
    ULONG i;
    int err = WMERR_OK;

    if (args == NULL) {
        return WMERR_EINVAL;
    }
    if (args->QueueSubmitsCount == 0) {
        return WMERR_OK;
    }
    submits = (const WINMALI_QUEUE_SUBMIT*)WinMaliEscapeAt_(
        pEscape, args->QueueSubmitsOffset,
        args->QueueSubmitsCount * sizeof(*submits));
    if (submits == NULL) {
        return WMERR_EINVAL;
    }

    /* Confirm the group exists; we don't dereference it further yet. */
    {
        ULONG sf, fq;
        int e = WinMaliGroupGetState(adapter, args->GroupHandle, &sf, &fq);
        if (e != WMERR_OK) {
            return e;
        }
        if (sf & WINMALI_GROUP_STATE_FATAL_FAULT) {
            return WMERR_EINVAL;
        }
    }

    for (i = 0; i < args->QueueSubmitsCount; ++i) {
        const WINMALI_QUEUE_SUBMIT* qs = &submits[i];
        const WINMALI_SYNC_OP* syncs = NULL;
        if (qs->SyncsCount > 0) {
            syncs = (const WINMALI_SYNC_OP*)WinMaliEscapeAt_(
                pEscape, qs->SyncsOffset, qs->SyncsCount * sizeof(*syncs));
            if (syncs == NULL) {
                err = WMERR_EINVAL;
                break;
            }
        }
        if (syncs != NULL) {
            err = WinMaliEscapeProcessSyncOps_(adapter, syncs, qs->SyncsCount, FALSE);
            if (err != WMERR_OK) {
                break;
            }
        }

        /* Real CSF GPU round-trip. Currently runs the *bring-up* shader
           (WinMaliCsfSubmitNopJob) regardless of qs->StreamAddr - actual
           UMD CS stream execution requires:
             - per-CSG-slot AS rebind to qs's VM AS
             - cross-mapping the kernel ring + syncobj into UMD's VM at the
               same fixed GPU VAs (WINMALI_CSF_RING_GPU_VA / SYNC_GPU_VA)
             - separate CSG state machine per group
           Those are bigger lifts. The path here still exercises every
           edge that matters to the UMD: MCU wake, CS engine run, sync
           signal via real GPU seqno polling, sync framework around it.
           So UMD code that depends on "submit returns after the GPU has
           made progress" will work correctly; just the specific bytecode
           run on the GPU isn't UMD's bytecode (yet). */
        {
            NTSTATUS csfSt = WinMaliCsfSubmitNopJob(adapter);
            if (!NT_SUCCESS(csfSt)) {
                WINMALI_WARN("GroupSubmit: CSF NOP submit failed 0x%08x for group=%u queue=%u",
                             csfSt, args->GroupHandle, qs->QueueIndex);
                err = (csfSt == STATUS_DEVICE_NOT_READY) ? WMERR_EINVAL
                                                         : WMERR_EIO;
                /* Drop post-submit signals on failure - waiters would
                   otherwise observe a "completed" submit that never ran. */
                break;
            }
        }
        WINMALI_TRACE("GroupSubmit: group=%u queue=%u stream_va=0x%llx size=%u syncs=%u "
                      "(CSF NOP placeholder ran; UMD bytecode NOT YET executed)",
                      args->GroupHandle, qs->QueueIndex,
                      (ULONGLONG)qs->StreamAddr, qs->StreamSize, qs->SyncsCount);

        if (syncs != NULL) {
            int e = WinMaliEscapeProcessSyncOps_(adapter, syncs, qs->SyncsCount, TRUE);
            if (e != WMERR_OK && err == WMERR_OK) {
                err = e;
            }
        }
    }

    return err;
}

static int
WinMaliEscapeSyncObjWait_(_In_    PWINMALI_ADAPTER       adapter,
                          _Inout_ WINMALI_ESCAPE_HEADER* hdr,
                          _In_    const DXGKARG_ESCAPE*  pEscape)
{
    WINMALI_SYNC_OBJ_WAIT* args =
        (WINMALI_SYNC_OBJ_WAIT*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
    const ULONG*  handles;
    const UINT64* points;
    BOOLEAN waitAll;
    if (args == NULL || args->Count == 0) {
        return WMERR_EINVAL;
    }
    handles = (const ULONG*)WinMaliEscapeAt_(pEscape, args->HandlesOffset,
                                             args->Count * sizeof(ULONG));
    if (handles == NULL) {
        return WMERR_EINVAL;
    }
    points = NULL;
    if (args->PointsOffset != 0) {
        points = (const UINT64*)WinMaliEscapeAt_(pEscape, args->PointsOffset,
                                                  args->Count * sizeof(UINT64));
        if (points == NULL) {
            return WMERR_EINVAL;
        }
    }
    waitAll = (args->Flags & WINMALI_SYNC_OBJ_WAIT_FLAG_ALL) ? TRUE : FALSE;
    return WinMaliSyncObjWait(adapter, args->Count, handles, points,
                              waitAll, args->TimeoutNs);
}

/* -------------------------------------------------------------------------- */
/*  DxgkDdiEscape entry                                                        */
/* -------------------------------------------------------------------------- */

_Function_class_(DXGKDDI_ESCAPE)
NTSTATUS
APIENTRY
WinMaliKmdEscape(
    IN_CONST_HANDLE          hAdapter,
    IN_CONST_PDXGKARG_ESCAPE pEscape)
{
    PWINMALI_ADAPTER       adapter;
    WINMALI_ESCAPE_HEADER* hdr;

    if (pEscape == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize < sizeof(WINMALI_ESCAPE_HEADER)) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    hdr = (WINMALI_ESCAPE_HEADER*)pEscape->pPrivateDriverData;

    /* Foreign escape buffer (some other driver's UMD probing every D3DKMT
       adapter via D3DKMTEscape)? Magic mismatch is the one path where we
       MUST return non-SUCCESS so the calling UMD sees STATUS_INVALID_PARAMETER
       and moves on without us clobbering its private buffer. */
    if (hdr->Magic != WINMALI_ESCAPE_MAGIC) {
        return STATUS_INVALID_PARAMETER;
    }

    /* From here on we own the buffer and report status via hdr->Status. */
    if (hdr->AbiMajor != WINMALI_ABI_MAJOR) {
        WINMALI_WARN("Escape: ABI major mismatch (UMD=%u, KMD=%u)",
                     hdr->AbiMajor, WINMALI_ABI_MAJOR);
        hdr->Status = WMERR_EINVAL;
        return STATUS_SUCCESS;
    }
    if (hdr->PayloadSize > pEscape->PrivateDriverDataSize) {
        WINMALI_WARN("Escape: PayloadSize %u > buffer %u",
                     hdr->PayloadSize, pEscape->PrivateDriverDataSize);
        hdr->Status = WMERR_EINVAL;
        return STATUS_SUCCESS;
    }

    switch (hdr->Op) {
    case WinMaliEscapeOp_OpenDevice:
        hdr->Status = WinMaliEscapeOpenDevice_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_DevQuery:
        hdr->Status = WinMaliEscapeDevQuery_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_BoCreate:
        hdr->Status = WinMaliEscapeBoCreate_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_BoDestroy:
        hdr->Status = WinMaliEscapeBoDestroy_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_BoQueryInfo:
        hdr->Status = WinMaliEscapeBoQueryInfo_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_BoSetLabel:
        hdr->Status = WinMaliEscapeBoSetLabel_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_BoMapCpu:
        hdr->Status = WinMaliEscapeBoMapCpu_(adapter, hdr, pEscape);
        break;

    case WinMaliEscapeOp_SyncObjCreate:
        hdr->Status = WinMaliEscapeSyncObjCreate_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_SyncObjDestroy:
        hdr->Status = WinMaliEscapeSyncObjDestroy_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_SyncObjWait:
        hdr->Status = WinMaliEscapeSyncObjWait_(adapter, hdr, pEscape);
        break;

    case WinMaliEscapeOp_VmCreate:
        hdr->Status = WinMaliEscapeVmCreate_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_VmDestroy:
        hdr->Status = WinMaliEscapeVmDestroy_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_VmGetState:
        hdr->Status = WinMaliEscapeVmGetState_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_VmBind:
        hdr->Status = WinMaliEscapeVmBind_(adapter, hdr, pEscape);
        break;

    case WinMaliEscapeOp_GroupCreate:
        hdr->Status = WinMaliEscapeGroupCreate_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_GroupDestroy:
        hdr->Status = WinMaliEscapeGroupDestroy_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_GroupGetState:
        hdr->Status = WinMaliEscapeGroupGetState_(adapter, hdr, pEscape);
        break;
    case WinMaliEscapeOp_GroupSubmit:
        hdr->Status = WinMaliEscapeGroupSubmit_(adapter, hdr, pEscape);
        break;

    case WinMaliEscapeOp_TilerHeapCreate: {
        /* Metadata-only: validates VmId is non-zero and returns a synthetic
           handle + a GpuVa placeholder. The real chunk allocator + Mali
           tiler heap registration follows real VmBind. The UMD can still
           track the handle for its own bookkeeping. */
        WINMALI_TILER_HEAP_CREATE* args =
            (WINMALI_TILER_HEAP_CREATE*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
        if (args == NULL) {
            hdr->Status = WMERR_EINVAL;
        } else if (args->VmId == 0 || args->InitialChunkCount == 0) {
            hdr->Status = WMERR_EINVAL;
        } else {
            /* Use a monotonic counter tied to the Group table for now -
               it's a separate ID space but keeps cross-call sequencing. */
            args->HeapHandle      = (ULONG)InterlockedIncrement(
                                        &adapter->GroupTable.NextHandle) | 0x80000000u;
            args->FirstChunkGpuVa = WINMALI_SYSMEM_GPU_BASE +
                                    (UINT64)args->HeapHandle * (UINT64)args->ChunkSize;
            WINMALI_TRACE("TilerHeapCreate: handle=0x%x vm=%u chunk=%u count=%u gpu_va=0x%llx (metadata-only)",
                          args->HeapHandle, args->VmId, args->ChunkSize,
                          args->InitialChunkCount, (ULONGLONG)args->FirstChunkGpuVa);
            hdr->Status = WMERR_OK;
        }
        break;
    }
    case WinMaliEscapeOp_TilerHeapDestroy: {
        WINMALI_TILER_HEAP_DESTROY* args =
            (WINMALI_TILER_HEAP_DESTROY*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
        if (args == NULL) {
            hdr->Status = WMERR_EINVAL;
        } else {
            WINMALI_TRACE("TilerHeapDestroy: handle=0x%x (metadata-only)", args->HeapHandle);
            hdr->Status = WMERR_OK;
        }
        break;
    }
    case WinMaliEscapeOp_BoMmapOffset: {
        /* On Windows mesa uses BoMapCpu (Op 0x86), not mmap(). This op
           exists for source-compat with pan_kmod's "get the magic offset
           to pass to mmap" pattern. Returning the BO handle as the offset
           is opaque and unique - mesa doesn't actually dereference it on
           Windows. */
        WINMALI_BO_MMAP_OFFSET* args =
            (WINMALI_BO_MMAP_OFFSET*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
        PWINMALI_BO bo;
        if (args == NULL) {
            hdr->Status = WMERR_EINVAL;
        } else {
            bo = WinMaliBoGet(adapter, args->Handle);
            if (bo == NULL) {
                hdr->Status = WMERR_ENOENT;
            } else {
                args->Offset = ((UINT64)args->Handle) << 12;  /* page-aligned synthetic */
                WinMaliBoPut(bo);
                hdr->Status = WMERR_OK;
            }
        }
        break;
    }
    case WinMaliEscapeOp_BoSync: {
        /* Mali on the G610 with CacheCoherent segments is coherent with
           the CPU caches the way we've configured the MMU (memattr_lo=0x4c).
           No explicit flush is required. If we later add non-coherent
           segments we'll need to call KeInvalidateRangeAllCaches or similar
           per op. Acknowledge the request so the UMD doesn't error. */
        WINMALI_TRACE("Escape BoSync: no-op (caches are coherent)");
        hdr->Status = WMERR_OK;
        break;
    }

    case WinMaliEscapeOp_GetFlushId: {
        WINMALI_GET_FLUSH_ID* args =
            (WINMALI_GET_FLUSH_ID*)WinMaliEscapeAt_(pEscape, sizeof(*hdr), sizeof(*args));
        if (args == NULL) {
            hdr->Status = WMERR_EINVAL;
        } else {
            args->FlushId = 0;
            args->Pad     = 0;
            hdr->Status   = WMERR_OK;
        }
        break;
    }

    /* Remaining ops to follow:
        VmCreate/Destroy/Bind/GetState, BoMmapOffset,
                       SyncObjCreate/Destroy/Wait.
        GroupCreate/Destroy/Submit/GetState, TilerHeapCreate/Destroy.
        BoFromAllocation, PresentToHdc. */
    case WinMaliEscapeOp_BoFromAllocation:
    case WinMaliEscapeOp_PresentToHdc:
        WINMALI_TRACE("Escape op 0x%x not yet implemented", hdr->Op);
        hdr->Status = WMERR_EOPNOTSUPP;
        break;

    default:
        WINMALI_WARN("Escape: unknown op 0x%x", hdr->Op);
        hdr->Status = WMERR_EOPNOTSUPP;
        break;
    }

    return STATUS_SUCCESS;
}
