/*
 * Copyright © 2026 WinMali project
 * SPDX-License-Identifier: MIT
 *
 * Phase 1 WDDM-allocation bridge across the C <-> C++ boundary.
 *
 * The escape ABI lives in the C pan_kmod backend (winmali_kmod.c); the dxgk
 * allocation callbacks (pfnAllocateCb / pfnLockCb / pfnDeallocateCb) live in
 * the C++ d3d10umd frontend (Device). This tiny, dependency-free header
 * (no gallium, no pan_kmod, no windows.h) lets the frontend install a hook
 * so winmali_kmod_bo_alloc backs every BO with a REAL kernel-tracked dxgk
 * allocation instead of an escape-private BoCreate.
 *
 * It sits in rk3588-GPU/Shared/ because that dir is already on the include
 * path of BOTH the C++ frontend (d3d10umd) and the C backend (pan_kmod).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pan_kmod_dev;
struct pipe_screen;

/* When installed, winmali_kmod_bo_alloc routes through these. NULL = escape
 * BoCreate (baseline). Any per-alloc failure falls back to escape BoCreate,
 * so a bug in this path can never brick boot. */
struct winmali_wddm_alloc_ops {
   void *cookie;

   /* Phase 3 UMD half: submit a render command stream through the dxgk
    * render context (pfnRenderCb) instead of the escape GroupSubmit. The CS
    * itself lives at stream_va in the UMD's escape VM (vm_id); this tags a
    * dxgk DMA buffer's private data with WINMALI_CS_SUBMIT_DESC so the KMD's
    * DxgkDdiSubmitCommandVirtual runs it on the CSG. Returns 0 on success.
    * Opt-in: panfrost only calls this when WINMALI_DXGK_SUBMIT is set;
    * otherwise the working escape GroupSubmit path is used. */
   int (*render)(void *cookie, uint32_t vm_id, uint64_t stream_va,
                 uint32_t stream_size);

   /* Phase 2 UMD half: map/unmap a dxgk allocation into the GPU virtual address
    * space via pfnMapGpuVirtualAddressCb / pfnFreeGpuVirtualAddressCb, retiring
    * the escape VmBind for BOs that are real dxgk allocations. map_va requests
    * *inout_va (panfrost's chosen VA) as the base and returns the dxgk-assigned
    * VA in *inout_va (must match for panfrost's bookkeeping to hold). The map is
    * synchronised on the device's paging queue before returning. Opt-in:
    * panfrost only calls these when WINMALI_DXGK_ADDR is set AND the BO is
    * dxgk-backed (wddm_kmt_handle != 0); otherwise escape VmBind is used.
    * Returns 0 on success, non-zero (errno) on failure. */
   int (*map_va)(void *cookie, uint32_t kmt_handle, uint64_t offset_bytes,
                 uint64_t size, uint64_t *inout_va);
   void (*unmap_va)(void *cookie, uint64_t va, uint64_t size);

   /* Create `size` bytes of opaque backing as a real dxgk allocation, made
    * resident and CPU-mapped for its lifetime (LockCb). *out_kmt_handle = the
    * D3DKMT allocation handle; *out_cpu = the persistent CPU VA (so the BO
    * needs no escape BoMapCpu). Returns 0 on success, non-zero (errno) fail. */
   int (*alloc)(void *cookie, uint64_t size, uint32_t *out_kmt_handle,
                void **out_cpu);
   /* Release an allocation previously returned by alloc(). */
   void (*free)(void *cookie, uint32_t kmt_handle);

   /* Phase 4 R1 (opt-in WINMALI_DXGK_FENCE): dxgk monitored fences replacing
    * escape SyncObj (0x82/0x83/0x84/0x91) on the fully-dxgk path. A monitored
    * fence is a timeline (create with initial value 0, signal/wait a value),
    * matching the WinMali timeline syncobj semantics mesa uses. Per-process, so
    * no cross-process handle confusion with escape-syncobj processes (DWM). When
    * these are installed, winmali_kmod's syncobj helpers route through them
    * instead of the escape SyncObj ops. Returns 0 on success. */
   int (*fence_create)(void *cookie, uint32_t *out_handle);
   void (*fence_destroy)(void *cookie, uint32_t handle);
   int (*fence_signal)(void *cookie, uint32_t handle, uint64_t value);
   int (*fence_wait)(void *cookie, uint32_t handle, uint64_t value);
};

/* C backend setter — impl in panfrost/lib/kmod/winmali/winmali_kmod.c. */
void winmali_kmod_set_wddm_alloc_ops(struct pan_kmod_dev *dev,
                                     const struct winmali_wddm_alloc_ops *ops);

/* gallium-screen convenience — impl in panfrost drivers/panfrost/pan_resource.c
 * (which can reach pan_device(screen)->kmod.dev). Lets the d3d10umd C++
 * frontend register the hook with only a pipe_screen, no panfrost internals. */
void panfrost_winmali_set_wddm_alloc_ops(struct pipe_screen *screen,
                                         const struct winmali_wddm_alloc_ops *ops);

#ifdef __cplusplus
} /* extern "C" */
#endif
