/*
 * WinMaliAlloc.c - allocation lifecycle DDIs.
 *
 * Scope: a flat "everything lives in segment 1 (sysmem)" model. dxgk
 * places allocations into the contiguous DmaSegment we reported in
 * QUERYSEGMENT3/4. Tiling, swizzling, eviction-to-aperture and HW-queue
 * sharing are out of scope until UMD is exercising real D3D11 paths.
 *
 * UMD passes a per-allocation private blob (WINMALI_ALLOC_PRIV) that
 * carries width/height/pitch/format/usage. Standard allocations (shared
 * primary / shadow / staging / GDI) get the blob synthesized by
 * GetStandardAllocationDriverData here.
 */

#include "WinMaliKmd.h"
#include "WinMaliAlloc.h"

#define WINMALI_KMD_ALLOC_MAGIC      'AllW'
#define WINMALI_ALLOC_PRIV_MAGIC     'PaWM'
#define WINMALI_ALLOC_PRIV_VERSION   1u

/* Default pitch alignment for linear surfaces. Mali utiles want 64-byte
   row alignment for AFBC/tile layouts; D3D11 promises 256 for staging.
   64 is the conservative floor that satisfies both. */
#define WINMALI_PITCH_ALIGN          64u

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

static UINT
WinMaliBppForFormat_(_In_ D3DDDIFORMAT Format)
{
    switch (Format) {
    case D3DDDIFMT_A8R8G8B8:
    case D3DDDIFMT_X8R8G8B8:
    case D3DDDIFMT_A8B8G8R8:
    case D3DDDIFMT_X8B8G8R8:
        return 4;
    case D3DDDIFMT_R5G6B5:
    case D3DDDIFMT_X1R5G5B5:
    case D3DDDIFMT_A1R5G5B5:
    case D3DDDIFMT_A4R4G4B4:
        return 2;
    case D3DDDIFMT_R8G8B8:
        return 3;
    case D3DDDIFMT_A8:
    case D3DDDIFMT_L8:
        return 1;
    default:
        /* Unknown format: assume 32bpp. Caller can override via private data. */
        return 4;
    }
}

static UINT
WinMaliAlignUp_(UINT v, UINT a)
{
    return (a == 0) ? v : (((v + a - 1) / a) * a);
}

static SIZE_T
WinMaliAlignUpSz_(SIZE_T v, SIZE_T a)
{
    return (a == 0) ? v : (((v + a - 1) / a) * a);
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiCreateAllocation                                                  */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_CREATEALLOCATION)
NTSTATUS
APIENTRY
WinMaliKmdCreateAllocation(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_CREATEALLOCATION pCreateAllocation)
{
    PWINMALI_ADAPTER  adapter;
    UINT              i;

    if (pCreateAllocation == NULL || pCreateAllocation->pAllocationInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    for (i = 0; i < pCreateAllocation->NumAllocations; ++i) {
        DXGK_ALLOCATIONINFO*    info = &pCreateAllocation->pAllocationInfo[i];
        PWINMALI_ALLOC_PRIV     priv = (PWINMALI_ALLOC_PRIV)info->pPrivateDriverData;
        PWINMALI_KMD_ALLOCATION ka;
        SIZE_T                  bytes;
        UINT                    pitch;
        UINT                    bpp;

        if (priv == NULL ||
            info->PrivateDriverDataSize < sizeof(WINMALI_ALLOC_PRIV) ||
            priv->Magic != WINMALI_ALLOC_PRIV_MAGIC) {
            /* No private data: treat as a raw buffer of `Alignment` bytes
               rounded to PAGE_SIZE. UMD shouldn't normally hit this path
               but we want to keep dxgk's own internal allocations working. */
            bpp   = 4;
            pitch = 0;
            if (info->Alignment != 0) {
                bytes = WinMaliAlignUpSz_(info->Alignment, PAGE_SIZE);
            } else if (priv != NULL && priv->Size != 0) {
                bytes = WinMaliAlignUpSz_(priv->Size, PAGE_SIZE);
            } else {
                bytes = PAGE_SIZE;
            }
        } else {
            bpp   = WinMaliBppForFormat_((D3DDDIFORMAT)priv->Format);
            pitch = WinMaliAlignUp_(priv->Pitch != 0 ? priv->Pitch : priv->Width * bpp,
                                    WINMALI_PITCH_ALIGN);
            bytes = (SIZE_T)pitch * (priv->Height != 0 ? priv->Height : 1);
            if (priv->Size > bytes) {
                bytes = priv->Size;
            }
            bytes = WinMaliAlignUpSz_(bytes, PAGE_SIZE);
        }

        ka = (PWINMALI_KMD_ALLOCATION)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*ka), WINMALI_POOL_TAG);
        if (ka == NULL) {
            /* Roll back any allocations we already filled. */
            while (i-- > 0) {
                PWINMALI_KMD_ALLOCATION prev =
                    (PWINMALI_KMD_ALLOCATION)pCreateAllocation->pAllocationInfo[i].hAllocation;
                if (prev != NULL && prev->Magic == WINMALI_KMD_ALLOC_MAGIC) {
                    prev->Magic = 0;
                    ExFreePoolWithTag(prev, WINMALI_POOL_TAG);
                    pCreateAllocation->pAllocationInfo[i].hAllocation = NULL;
                }
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(ka, sizeof(*ka));
        ka->Magic        = WINMALI_KMD_ALLOC_MAGIC;
        ka->Adapter      = adapter;
        ka->hResource    = pCreateAllocation->hResource;
        ka->Size         = bytes;
        ka->Alignment    = info->Alignment != 0 ? info->Alignment : PAGE_SIZE;
        if (priv != NULL && priv->Magic == WINMALI_ALLOC_PRIV_MAGIC) {
            ka->Width  = priv->Width;
            ka->Height = priv->Height;
            ka->Pitch  = pitch;
            ka->Format = priv->Format;
            ka->Usage  = priv->Usage;
        } else {
            ka->Width = ka->Height = ka->Pitch = 0;
            ka->Format = D3DDDIFMT_UNKNOWN;
            ka->Usage = 0;
        }

        /* Tell dxgk where this lives and what kind of memory it is. */
        info->Size                     = ka->Size;
        info->PitchAlignedSize         = ka->Size;
        info->Alignment                = ka->Alignment;
        info->PreferredSegment.Value   = 0;
        info->PreferredSegment.SegmentId0 = WINMALI_SEGMENT_ID_SYSMEM;
        info->SupportedReadSegmentSet  = (1u << (WINMALI_SEGMENT_ID_SYSMEM - 1));
        info->SupportedWriteSegmentSet = (1u << (WINMALI_SEGMENT_ID_SYSMEM - 1));
        info->EvictionSegmentSet       = 0;
        info->PhysicalAdapterIndex     = 0;
        info->Flags.Value              = 0;
        info->Flags.CpuVisible         = 1;
        info->Flags.Cached             = 0;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        info->Flags.AccessedPhysically = 1; /* GpuMmu maps host PA directly */
#endif
        info->AllocationPriority       = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        info->hAllocation              = (HANDLE)ka;

        WINMALI_TRACE("CreateAllocation[%u]: ka=%p sz=0x%Ix px=%ux%u@%u pitch=%u fmt=%u usage=0x%x",
                      i, ka, ka->Size, ka->Width, ka->Height, ka->Alignment,
                      ka->Pitch, ka->Format, ka->Usage);
    }

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiDestroyAllocation                                                  */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_DESTROYALLOCATION)
NTSTATUS
APIENTRY
WinMaliKmdDestroyAllocation(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_DESTROYALLOCATION pDestroyAllocation)
{
    UINT i;

    UNREFERENCED_PARAMETER(hAdapter);
    if (pDestroyAllocation == NULL || pDestroyAllocation->pAllocationList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    for (i = 0; i < pDestroyAllocation->NumAllocations; ++i) {
        PWINMALI_KMD_ALLOCATION ka =
            (PWINMALI_KMD_ALLOCATION)pDestroyAllocation->pAllocationList[i];

        if (ka == NULL) {
            continue;
        }
        if (ka->Magic != WINMALI_KMD_ALLOC_MAGIC) {
            WINMALI_WARN("DestroyAllocation[%u]: ka=%p bad magic 0x%x", i, ka, ka->Magic);
            continue;
        }
        if (ka->OpenCount != 0) {
            WINMALI_WARN("DestroyAllocation[%u]: ka=%p leaks %ld opens",
                         i, ka, ka->OpenCount);
        }
        WINMALI_TRACE("DestroyAllocation[%u]: ka=%p sz=0x%Ix", i, ka, ka->Size);
        ka->Magic = 0;
        ExFreePoolWithTag(ka, WINMALI_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiOpenAllocation / DxgkDdiCloseAllocation                            */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_OPENALLOCATIONINFO)
NTSTATUS
APIENTRY
WinMaliKmdOpenAllocation(
    IN_CONST_HANDLE                  hDevice,
    IN_CONST_PDXGKARG_OPENALLOCATION pOpenAllocation)
{
    UINT i;

    UNREFERENCED_PARAMETER(hDevice);  /* per-device tracking deferred (system device can also open) */

    if (pOpenAllocation == NULL || pOpenAllocation->pOpenAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pOpenAllocation->NumAllocations; ++i) {
        DXGK_OPENALLOCATIONINFO* oi = &pOpenAllocation->pOpenAllocation[i];

        /* Flat model: device-specific handle is just the kmd alloc ptr we
           returned from CreateAllocation. dxgk gave us hAllocation (its
           D3DKMT_HANDLE), but the actual per-device cookie we hand back
           is the WINMALI_KMD_ALLOCATION* via hDeviceSpecificAllocation. */
        if (oi->hDeviceSpecificAllocation == NULL) {
            /* dxgk fills hAllocation (its handle) on input; we look up
               our struct via the private blob, which UMD attaches. If
               UMD attached the kmd pointer in pPrivateDriverData, take
               it; otherwise leave hDeviceSpecificAllocation = NULL and
               dxgk will route through our hAllocation cookie. */
            if (oi->pPrivateDriverData != NULL &&
                oi->PrivateDriverDataSize >= sizeof(PWINMALI_KMD_ALLOCATION)) {
                PWINMALI_KMD_ALLOCATION ka =
                    *(PWINMALI_KMD_ALLOCATION*)oi->pPrivateDriverData;
                if (ka != NULL && ka->Magic == WINMALI_KMD_ALLOC_MAGIC) {
                    InterlockedIncrement(&ka->OpenCount);
                    oi->hDeviceSpecificAllocation = (HANDLE)ka;
                }
            }
        } else {
            PWINMALI_KMD_ALLOCATION ka =
                (PWINMALI_KMD_ALLOCATION)oi->hDeviceSpecificAllocation;
            if (ka != NULL && ka->Magic == WINMALI_KMD_ALLOC_MAGIC) {
                InterlockedIncrement(&ka->OpenCount);
            }
        }
    }
    return STATUS_SUCCESS;
}

_Function_class_(DXGKDDI_CLOSEALLOCATION)
NTSTATUS
APIENTRY
WinMaliKmdCloseAllocation(
    IN_CONST_HANDLE                   hDevice,
    IN_CONST_PDXGKARG_CLOSEALLOCATION pCloseAllocation)
{
    UINT i;

    UNREFERENCED_PARAMETER(hDevice);
    if (pCloseAllocation == NULL || pCloseAllocation->pOpenHandleList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    for (i = 0; i < pCloseAllocation->NumAllocations; ++i) {
        PWINMALI_KMD_ALLOCATION ka =
            (PWINMALI_KMD_ALLOCATION)pCloseAllocation->pOpenHandleList[i];
        if (ka != NULL && ka->Magic == WINMALI_KMD_ALLOC_MAGIC) {
            InterlockedDecrement(&ka->OpenCount);
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiDescribeAllocation                                                 */
/* ------------------------------------------------------------------------ */

_Function_class_(DXGKDDI_DESCRIBEALLOCATION)
NTSTATUS
APIENTRY
WinMaliKmdDescribeAllocation(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_DESCRIBEALLOCATION pDescribeAllocation)
{
    PWINMALI_KMD_ALLOCATION ka;

    UNREFERENCED_PARAMETER(hAdapter);
    if (pDescribeAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ka = (PWINMALI_KMD_ALLOCATION)pDescribeAllocation->hAllocation;
    if (ka == NULL || ka->Magic != WINMALI_KMD_ALLOC_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }

    pDescribeAllocation->Width  = ka->Width;
    pDescribeAllocation->Height = ka->Height;
    pDescribeAllocation->Format = (D3DDDIFORMAT)ka->Format;
    pDescribeAllocation->MultisampleMethod.NumSamples = 1;
    pDescribeAllocation->MultisampleMethod.NumQualityLevels = 0;
    pDescribeAllocation->RefreshRate.Numerator   = 60000;
    pDescribeAllocation->RefreshRate.Denominator = 1000;
    pDescribeAllocation->PrivateDriverFormatAttribute = 0;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    pDescribeAllocation->Flags.Value = 0;
    pDescribeAllocation->Rotation    = D3DDDI_ROTATION_IDENTITY;
#endif
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* DxgkDdiGetStandardAllocationDriverData                                    */
/* ------------------------------------------------------------------------ */

static VOID
WinMaliFillStdAllocPriv_(
    _Out_ WINMALI_ALLOC_PRIV* priv,
    UINT width, UINT height, D3DDDIFORMAT fmt, UINT pitch, UINT usage)
{
    RtlZeroMemory(priv, sizeof(*priv));
    priv->Magic   = WINMALI_ALLOC_PRIV_MAGIC;
    priv->Version = WINMALI_ALLOC_PRIV_VERSION;
    priv->Width   = width;
    priv->Height  = height;
    priv->Pitch   = pitch;
    priv->Format  = (UINT)fmt;
    priv->Usage   = usage;
    priv->Flags   = 0;
    priv->Size    = (SIZE_T)pitch * height;
}

_Function_class_(DXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA)
NTSTATUS
APIENTRY
WinMaliKmdGetStandardAllocationDriverData(
    IN_CONST_HANDLE                                hAdapter,
    INOUT_PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pArgs)
{
    UINT width = 0, height = 0, pitch = 0, usage = 0, bpp = 4;
    D3DDDIFORMAT fmt = D3DDDIFMT_A8R8G8B8;

    UNREFERENCED_PARAMETER(hAdapter);
    if (pArgs == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (pArgs->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE:
        if (pArgs->pCreateSharedPrimarySurfaceData != NULL) {
            width  = pArgs->pCreateSharedPrimarySurfaceData->Width;
            height = pArgs->pCreateSharedPrimarySurfaceData->Height;
            fmt    = pArgs->pCreateSharedPrimarySurfaceData->Format;
        }
        usage = WINMALI_ALLOC_USAGE_PRIMARY | WINMALI_ALLOC_USAGE_RENDER_TARGET;
        break;

    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
        if (pArgs->pCreateShadowSurfaceData != NULL) {
            width  = pArgs->pCreateShadowSurfaceData->Width;
            height = pArgs->pCreateShadowSurfaceData->Height;
            fmt    = pArgs->pCreateShadowSurfaceData->Format;
        }
        usage = WINMALI_ALLOC_USAGE_STAGING;
        break;

    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
        if (pArgs->pCreateStagingSurfaceData != NULL) {
            width  = pArgs->pCreateStagingSurfaceData->Width;
            height = pArgs->pCreateStagingSurfaceData->Height;
            fmt    = D3DDDIFMT_X8R8G8B8; /* per WDK spec */
        }
        usage = WINMALI_ALLOC_USAGE_STAGING;
        break;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    case D3DKMDT_STANDARDALLOCATION_GDISURFACE:
        if (pArgs->pCreateGdiSurfaceData != NULL) {
            width  = pArgs->pCreateGdiSurfaceData->Width;
            height = pArgs->pCreateGdiSurfaceData->Height;
            fmt    = pArgs->pCreateGdiSurfaceData->Format;
        }
        usage = WINMALI_ALLOC_USAGE_RENDER_TARGET;
        break;
#endif

    default:
        WINMALI_WARN("GetStandardAllocationDriverData: unknown type %d",
                     pArgs->StandardAllocationType);
        return STATUS_INVALID_PARAMETER;
    }

    bpp   = WinMaliBppForFormat_(fmt);
    pitch = WinMaliAlignUp_(width * bpp, WINMALI_PITCH_ALIGN);

    /* Two-pass query pattern: dxgk first calls with pAllocationPrivateDriverData
       = NULL just to learn the size, then again with a real buffer. */
    if (pArgs->pAllocationPrivateDriverData == NULL) {
        pArgs->AllocationPrivateDriverDataSize = sizeof(WINMALI_ALLOC_PRIV);
    } else {
        if (pArgs->AllocationPrivateDriverDataSize < sizeof(WINMALI_ALLOC_PRIV)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        WinMaliFillStdAllocPriv_(
            (WINMALI_ALLOC_PRIV*)pArgs->pAllocationPrivateDriverData,
            width, height, fmt, pitch, usage);
        pArgs->AllocationPrivateDriverDataSize = sizeof(WINMALI_ALLOC_PRIV);
    }

    /* Resource-level data: we don't pack anything extra at the resource
       level today. Return zero size on probe; ignore content on fill. */
    pArgs->ResourcePrivateDriverDataSize = 0;

    /* Patch the per-surface "pitch" back into shadow/staging/gdi structs
       so UMD knows the row stride to use. */
    switch (pArgs->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
        if (pArgs->pCreateShadowSurfaceData != NULL) {
            pArgs->pCreateShadowSurfaceData->Pitch = pitch;
        }
        break;
    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
        if (pArgs->pCreateStagingSurfaceData != NULL) {
            pArgs->pCreateStagingSurfaceData->Pitch = pitch;
        }
        break;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    case D3DKMDT_STANDARDALLOCATION_GDISURFACE:
        if (pArgs->pCreateGdiSurfaceData != NULL) {
            pArgs->pCreateGdiSurfaceData->Pitch = pitch;
        }
        break;
#endif
    default:
        break;
    }

    WINMALI_TRACE("GetStdAllocDriverData: type=%d wh=%ux%u fmt=%u pitch=%u sz=0x%Ix",
                  pArgs->StandardAllocationType, width, height, fmt, pitch,
                  (SIZE_T)pitch * height);
    return STATUS_SUCCESS;
}
