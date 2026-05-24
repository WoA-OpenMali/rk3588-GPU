/*
 * WinMaliAlloc.h - allocation lifecycle types + DDI prototypes.
 *
 * Shared between WinMaliAlloc.c (impl), WinMaliPaging.c (future, paging
 * buffer ops touch the same kmd allocation handles), and any future
 * present/render code that needs to read the alloc's GpuVa / Pitch /
 * dimensions.
 *
 * UMD ↔ KMD allocation private-data ABI is locked here. Any version
 * bump must be reflected in Mesa-modern's render-only winsys.
 */

#pragma once

#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>

/* ---------------- Allocation usage bits (UMD <-> KMD ABI) ---------------- */

#define WINMALI_ALLOC_USAGE_TEXTURE         0x00000001UL
#define WINMALI_ALLOC_USAGE_RENDER_TARGET   0x00000002UL
#define WINMALI_ALLOC_USAGE_DEPTH_STENCIL   0x00000004UL
#define WINMALI_ALLOC_USAGE_UAV             0x00000008UL
#define WINMALI_ALLOC_USAGE_VERTEX_BUFFER   0x00000010UL
#define WINMALI_ALLOC_USAGE_INDEX_BUFFER    0x00000020UL
#define WINMALI_ALLOC_USAGE_CONSTANT_BUFFER 0x00000040UL
#define WINMALI_ALLOC_USAGE_STAGING         0x00000080UL
#define WINMALI_ALLOC_USAGE_PRIMARY         0x00000100UL
#define WINMALI_ALLOC_USAGE_DMA_BUFFER      0x00000200UL

/* ---------------- WINMALI_ALLOC_PRIV - UMD-supplied per-alloc blob ------- */

/* Carried in DXGK_ALLOCATIONINFO::pPrivateDriverData. UMD packs one of
   these per allocation it wants us to create. KMD reads it during
   CreateAllocation to compute Size/Pitch/SegmentSet. */
typedef struct _WINMALI_ALLOC_PRIV {
    ULONG  Magic;        /* 'PaWM' */
    ULONG  Version;      /* 1 */
    UINT   Width;
    UINT   Height;
    UINT   Pitch;        /* bytes per row; 0 = compute as Width * bpp(Format) */
    UINT   Format;       /* D3DDDIFORMAT */
    UINT   Usage;        /* WINMALI_ALLOC_USAGE_* bits */
    UINT   Flags;        /* reserved */
    SIZE_T Size;         /* min total bytes; 0 = compute from pitch*height */
} WINMALI_ALLOC_PRIV, *PWINMALI_ALLOC_PRIV;

/* ---------------- WINMALI_KMD_ALLOCATION - per-alloc kernel state -------- */

typedef struct _WINMALI_KMD_ALLOCATION {
    ULONG               Magic;        /* 'AllW' */
    struct _WINMALI_ADAPTER* Adapter;
    HANDLE              hResource;    /* dxgk's hResource or NULL */
    SIZE_T              Size;         /* page-aligned bytes */
    UINT                Alignment;
    UINT                Width;
    UINT                Height;
    UINT                Pitch;        /* row stride in bytes */
    UINT                Format;       /* D3DDDIFORMAT */
    UINT                Usage;        /* WINMALI_ALLOC_USAGE_* */
    /* Residency state (filled by BuildPagingBuffer when the alloc gets a
       GPU VA / sysmem page set). Until then both are zero. */
    UINT64              GpuVa;
    PHYSICAL_ADDRESS    PhysicalBase;
    LONG                OpenCount;    /* per-device opens outstanding */
} WINMALI_KMD_ALLOCATION, *PWINMALI_KMD_ALLOCATION;

/* ---------------- DDI prototypes ---------------------------------------- */

NTSTATUS APIENTRY WinMaliKmdCreateAllocation(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_CREATEALLOCATION pCreateAllocation);

NTSTATUS APIENTRY WinMaliKmdDestroyAllocation(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_DESTROYALLOCATION pDestroyAllocation);

NTSTATUS APIENTRY WinMaliKmdOpenAllocation(
    IN_CONST_HANDLE                  hDevice,
    IN_CONST_PDXGKARG_OPENALLOCATION pOpenAllocation);

NTSTATUS APIENTRY WinMaliKmdCloseAllocation(
    IN_CONST_HANDLE                   hDevice,
    IN_CONST_PDXGKARG_CLOSEALLOCATION pCloseAllocation);

NTSTATUS APIENTRY WinMaliKmdDescribeAllocation(
    IN_CONST_HANDLE                   hAdapter,
    INOUT_PDXGKARG_DESCRIBEALLOCATION pDescribeAllocation);

NTSTATUS APIENTRY WinMaliKmdGetStandardAllocationDriverData(
    IN_CONST_HANDLE                                hAdapter,
    INOUT_PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pGetStandardAllocationDriverData);
