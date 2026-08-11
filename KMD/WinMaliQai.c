/*
 * WinMaliQai.c - DxgkDdiQueryAdapterInfo.
 *
 * Real cap-walk implementation. With MMU + the 256 MiB sysmem segment
 * allocated at StartDevice, we now publish numbers dxgk can act on:
 *
 *   DRIVERCAPS                 -> render-only WDDM 3.2 + GpuMmu + VA
 *   WDDMDEVICECAPS             -> WDDMv3_2 echo (pre-StartDevice)
 *   PHYSICAL_MEMORY_CAPS       -> 2^PA_bits-1 from MMU_FEATURES (pre-Start)
 *   IOMMU_CAPS                 -> none (GPU has its own MMU, no SMMU)
 *   HARDWARERESERVEDRANGES(2)  -> zero ranges
 *   PHYSICALADAPTERCAPS        -> our DxgkHandle + GpuMmu, no paging node
 *   GPUMMUCAPS                 -> 30-bit VA, 2-level page table, NX/RO
 *   PAGETABLELEVELDESC         -> 9 idx bits per level, segment id 1
 *   QUERYSEGMENT3              -> 1 segment, sysmem-backed, CpuVisible
 *   QUERYSEGMENT4              -> same data, newer descriptor layout
 *   HISTORYBUFFERPRECISION     -> 64-bit
 *
 * Bounded copy-out (RtlZeroMemory dest first, copy min(outSz, sizeof)) is
 * used everywhere so kernels with smaller struct views than our 26100 WDK
 * can't smash stack/heap by writing past their buffer.
 *
 * Anything we haven't implemented yet falls through to STATUS_NOT_IMPLEMENTED
 * with a log line so kd shows exactly what dxgk still wants from us.
 */

#include "WinMaliKmd.h"

#define WM_QAI_OK(name)  DbgPrint("[WinMali]    QAI[%s] OK\n", name)

NTSTATUS APIENTRY
WinMaliKmdQueryAdapterInfo(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*    pQueryAdapterInfo)
{
    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* NODEPERFDATA/ADAPTERPERFDATA are polled ~once per second per node
       for as long as the adapter is up. With the serial mirror active,
       printing the entry line for them means dxgkrnl's telemetry worker
       synchronously drains a UART line at 115200 baud every poll -
       forever - which reads as periodic whole-system stalls. The case
       handlers below are already silent for these; the entry print must
       be too. */
    if (pQueryAdapterInfo->Type != DXGKQAITYPE_NODEPERFDATA &&
        pQueryAdapterInfo->Type != DXGKQAITYPE_ADAPTERPERFDATA) {
        DbgPrint("[WinMali]    QAI[%u] enter outsz=%u\n",
                 (unsigned)pQueryAdapterInfo->Type,
                 (unsigned)pQueryAdapterInfo->OutputDataSize);
    }

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_UMDRIVERPRIVATE: {
        /* The D3D runtime forwards KMTQAITYPE_UMDRIVERPRIVATE here while
           creating a device on the adapter, with a runtime-chosen buffer
           size. Our mesa d3d10umd-based UMD never reads this blob (the
           KMD<->UMD contract is the escape ABI, negotiated via
           WinMaliEscapeOp_OpenDevice) - but the QUERY must succeed for
           any size or device creation dies before the UMD even loads.
           Zero-filled = "no private data". */
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        DbgPrint("[WinMali]    QAI[UMDRIVERPRIVATE] OK size=%u (zero blob)\n",
                 (unsigned)pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS* caps  = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;
        SIZE_T           outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T           lim   = (outSz < sizeof(*caps)) ? outSz : sizeof(*caps);

        if (caps == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(caps, outSz);

#define WM_SET(field, val)                                                       \
        do {                                                                     \
            if (FIELD_OFFSET(DXGK_DRIVERCAPS, field) + sizeof((caps)->field) <= lim) { \
                (caps)->field = (val);                                           \
            }                                                                    \
        } while (0)

        /* Win11 24H2 dxgkrnl refuses to register a new third-party
           adapter declaring WDDM 2.5 (event 549, AddAdapterFailed,
           STATUS_REVISION_MISMATCH). Must match init.Version in
           WinMaliDxgkInitFill.c (WDDM 3.2 on the 26100 WDK) and the
           WDDMDEVICECAPS echo below. */
        WM_SET(WDDMVersion, DXGKDDI_WDDMv3_2);
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, HighestAcceptableAddress) +
            sizeof(caps->HighestAcceptableAddress) <= lim) {
            caps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;
        }
        WM_SET(MaxAllocationListSlotId, 7);
        WM_SET(ApertureSegmentCommitLimit, 0);
        WM_SET(SupportNonVGA, TRUE);
        WM_SET(SupportSmoothRotation, TRUE);
        WM_SET(SupportPerEngineTDR, 1);
        WM_SET(SupportRuntimePowerManagement, FALSE);

        if (FIELD_OFFSET(DXGK_DRIVERCAPS, PresentationCaps) +
            sizeof(caps->PresentationCaps) <= lim) {
            caps->PresentationCaps.SupportSoftwareDeviceBitmaps = TRUE;
            caps->PresentationCaps.NoScreenToScreenBlt          = TRUE;
            caps->PresentationCaps.NoOverlapScreenBlt           = TRUE;
            caps->PresentationCaps.MaxTextureWidthShift         = 3;
            caps->PresentationCaps.MaxTextureHeightShift        = 3;
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, FlipCaps) +
            sizeof(caps->FlipCaps) <= lim) {
            /* Render-only adapter has no scanout/vsync, so it must not claim a
               flip capability (FlipOnVSyncMmIo is a display-DDI path via
               SetVidPnSourceAddress, which is NULL here). RosKmd only sets
               FlipCaps for display adapters. Leave it zero. */
            caps->FlipCaps.Value = 0;
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, SchedulingCaps) +
            sizeof(caps->SchedulingCaps) <= lim) {
            /* WDDM 3.2 / GpuMmu REQUIRES these scheduling caps - dxgkrnl
               rejects the adapter at start (queries DRIVERCAPS then tears the
               device down) if they're cleared. They are NOT optional, so we
               must actually IMPLEMENT the DDIs behind them: DxgkDdiPreempt
               Command + DXGK_INTERRUPT_DMA_PREEMPTED (async submit scheduler,
               WinMaliDdi.c) and DxgkDdiCancelCommand. Advertising them while
               stubbing the DDIs is what bugchecked dxgmms2 (0xD1 in the
               preemption path); the fix is the real implementation, not
               clearing the caps. */
            caps->SchedulingCaps.MultiEngineAware       = 1;
            caps->SchedulingCaps.VSyncPowerSaveAware    = 1;
            caps->SchedulingCaps.PreemptionAware        = 1;   /* WDDM 1.2+ */
            caps->SchedulingCaps.NoDmaPatching          = 1;   /* WDDM 1.2+ */
            caps->SchedulingCaps.CancelCommandAware     = 1;
            caps->SchedulingCaps.LowIrqlPreemptCommand  = 1;   /* WDDM 2.0+ */
            caps->SchedulingCaps.No64BitAtomics         = 1;   /* WDDM 2.0+ */
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, MemoryManagementCaps) +
            sizeof(caps->MemoryManagementCaps) <= lim) {
            caps->MemoryManagementCaps.CrossAdapterResource       = 1;
            caps->MemoryManagementCaps.CrossAdapterResourceTexture = 1;
            caps->MemoryManagementCaps.CrossAdapterResourceScanout = 1;
            caps->MemoryManagementCaps.SectionBackedPrimary       = 0;
            caps->MemoryManagementCaps.VirtualAddressingSupported = 1;
            caps->MemoryManagementCaps.GpuMmuSupported            = 1;
            caps->MemoryManagementCaps.OutOfOrderLock             = 1;
            caps->MemoryManagementCaps.NonCpuVisiblePrimary       = 1; 
            caps->MemoryManagementCaps.PagingNode                 = 1;  /* copy node 1 (matches PHYSICALADAPTERCAPS.PagingNodeIndex) */
            caps->MemoryManagementCaps.IoMmuSupported             = 0;
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, PreemptionCaps) +
            sizeof(caps->PreemptionCaps) <= lim) {
            /* DMA-BUFFER-boundary only: we cannot preempt mid-buffer (no
               hardware for primitive/dispatch-boundary save-restore). We run
               each DMA buffer to completion and honor a preempt request by
               reporting DMA_PREEMPTED at the next buffer boundary (see the
               async submit worker + DxgkDdiPreemptCommand). Claiming the finer
               PRIMITIVE/DISPATCH granularity here made the scheduler set up
               mid-buffer preemption state we never satisfied -> 0xD1 in
               dxgmms2's preemption path. This is the honest, implementable
               level. */
            caps->PreemptionCaps.GraphicsPreemptionGranularity =
                D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
            caps->PreemptionCaps.ComputePreemptionGranularity  =
                D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        }

        /* GPU VA span. Must enclose the per-process VA we accept in
           SetRootPageTable. Aligned with GPUMMUCAPS.VirtualAddressBitCount
           and PAGETABLELEVELDESC (4 levels x 9 idx bits + 12 page = 48 bits) -
           the native Mali LPAE 48-bit space. panfrost/mesa address the full
           range (tiler at 0x40000000, per-context BOs top-down near 2^48), so
           a smaller span makes MapGpuVirtualAddress reject every real VA. */
        WM_SET(InternalGpuVirtualAddressRangeStart,
               (D3DGPU_VIRTUAL_ADDRESS)0x0000000000010000ULL);
        WM_SET(InternalGpuVirtualAddressRangeEnd,
               (D3DGPU_VIRTUAL_ADDRESS)0x0000FFFFFFFFFFFFULL);

        if (FIELD_OFFSET(DXGK_DRIVERCAPS, GpuEngineTopology) +
            sizeof(caps->GpuEngineTopology) <= lim) {
            /* Two nodes (3D + COPY/paging), matching PHYSICALADAPTERCAPS
               NumExecutionNodes=2 and GetNodeMetadata. Reporting 1 here hid the
               copy node dxgk uses for cross-adapter present copies. */
            caps->GpuEngineTopology.NbAsymetricProcessingNodes = 2;
        }
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) +
            sizeof(caps->MiscCaps) <= lim) {
            caps->MiscCaps.SupportContextlessPresent = 1;
            caps->MiscCaps.Detachable                = 0;  /* not eGPU */
        }
#endif
#undef WM_SET
        WM_QAI_OK("DRIVERCAPS");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICALADAPTERCAPS: {
        DXGK_PHYSICALADAPTERCAPS phys;
        PWINMALI_ADAPTER         adapter;
        SIZE_T                   outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T                   copyLen;
        const DXGK_QUERYPHYSICALADAPTERCAPSIN* physIn;

        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (pQueryAdapterInfo->pInputData != NULL &&
            pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYPHYSICALADAPTERCAPSIN)) {
            physIn = (const DXGK_QUERYPHYSICALADAPTERCAPSIN*)pQueryAdapterInfo->pInputData;
            if (physIn->PhysicalAdapterIndex != 0) {
                return STATUS_INVALID_PARAMETER;
            }
        }
        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        if (adapter == NULL || adapter->DxgkHandle == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }

        RtlZeroMemory(&phys, sizeof(phys));
        /* Two nodes: 0 = 3D render engine, 1 = COPY/paging engine.
           dxgkrnl requires PagingNodeIndex to name a copy/DMA engine (not the
           3D node - =0 bails at StartDevice on Win11 ARM64) AND a node that
           actually exists (=NumExecutionNodes, out of range, materialises a
           PHANTOM paging node whose per-process scheduler state is left NULL
           -> 0xD1 in VidSchiProfilePerformanceTick when it is first
           scheduled). So node 1 is a real declared copy engine; GetNodeMetadata
           reports it, CreateContext accepts it, and its paging buffers are
           serviced CPU-side (BuildPagingBuffer). */
        phys.NumExecutionNodes         = 2;
        phys.PagingNodeIndex           = 1;   /* the real copy node */
        phys.DxgkPhysicalAdapterHandle = adapter->DxgkHandle;
        phys.Flags.Value               = 0;
        phys.Flags.GpuMmuSupported     = 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        phys.VPRPagingNode             = 1;   /* no VPR; keep off the phantom */
#endif

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(phys)) ? outSz : sizeof(phys);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &phys, copyLen);
        WM_QAI_OK("PHYSICALADAPTERCAPS");
        return STATUS_SUCCESS;
    }

    /* ------------------------------------------------------------------
       Pre-StartDevice caps. dxgkrnl 26100 issues WDDMDEVICECAPS(29),
       PHYSICAL_MEMORY_CAPS(34), IOMMU_CAPS(35) and
       HARDWARERESERVEDRANGES2(36) between AddDevice and StartDevice.
       PHYSICAL_MEMORY_CAPS is load-bearing: the WDDM 3.x System Memory
       Manager (dxgkrnl!DpiFdoCreateSysMmAdapter) gates adapter creation
       on it, and a zero HighestVisibleAddress means "no addressable
       memory" - dxgk then calls RemoveDevice WITHOUT EVER CALLING
       StartDevice (no 549 event; a related 549 shows {Not Enough
       Quota}). So these cannot ride the zero-fill default at the
       bottom of this switch.
       ------------------------------------------------------------------ */

    case DXGKQAITYPE_WDDMDEVICECAPS: {
        /* Must echo DRIVERCAPS.WDDMVersion exactly (header contract). */
        DXGK_WDDMDEVICECAPS* dc    = (DXGK_WDDMDEVICECAPS*)pQueryAdapterInfo->pOutputData;
        SIZE_T               outSz = pQueryAdapterInfo->OutputDataSize;

        if (dc == NULL || outSz < sizeof(*dc)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(dc, outSz);
        dc->WDDMVersion = DXGKDDI_WDDMv3_2;
        WM_QAI_OK("WDDMDEVICECAPS=WDDMv3_2");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICAL_MEMORY_CAPS: {
        /* Honest PA reach: HighestVisibleAddress = 2^PA_bits - 1, PA
           width from MMU_FEATURES[15:8]. This query lands before
           StartDevice maps MMIO, so the register snapshot may not
           exist yet - fall back to the Mali-G610 architectural value
           (PA = 40 bits) in that case. */
        DXGK_PHYSICAL_MEMORY_CAPS pmc;
        PWINMALI_ADAPTER          adapter;
        ULONG                     paBits = 0;
        SIZE_T                    outSz  = pQueryAdapterInfo->OutputDataSize;
        SIZE_T                    copyLen;

        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        if (adapter != NULL) {
            paBits = WINMALI_MMU_FEATURES_PA_BITS(adapter->Hw.MmuFeatures);
        }
        if (paBits == 0 || paBits > 48) {
            paBits = 40;    /* Mali-G610 MMU_FEATURES value; pre-MMIO fallback */
        }

        RtlZeroMemory(&pmc, sizeof(pmc));
        pmc.HighestVisibleAddress.QuadPart = (LONGLONG)((1ULL << paBits) - 1u);

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(pmc)) ? outSz : sizeof(pmc);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &pmc, copyLen);
        DbgPrint("[WinMali]    QAI[PHYSICAL_MEMORY_CAPS] OK pa_bits=%lu highest=0x%llx\n",
                 paBits, (unsigned long long)pmc.HighestVisibleAddress.QuadPart);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_IOMMU_CAPS: {
        /* All-zero is the honest answer: the RK3588's Mali sits on the
           SoC interconnect with its own GPU MMU; there is no Windows-
           managed IOMMU/SMMU in front of it (and we set
           MemoryManagementCaps.IoMmuSupported = 0 to match). */
        DXGK_IOMMU_CAPS iommu;
        SIZE_T          outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T          copyLen;

        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(&iommu, sizeof(iommu));

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(iommu)) ? outSz : sizeof(iommu);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &iommu, copyLen);
        WM_QAI_OK("IOMMU_CAPS=none");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_HARDWARERESERVEDRANGES:
    case DXGKQAITYPE_HARDWARERESERVEDRANGES2: {
        /* No hardware-reserved system RAM: firmware carve-outs on
           RK3588 are handled by UEFI/ACPI before Windows boots, and
           our segment is plain contiguous sysmem we allocate
           ourselves. NumRanges = 0; dxgk ignores pPhysicalRanges when
           the count is zero (don't zero the pointer - dxgk owns it). */
        DXGK_HARDWARERESERVEDRANGES* hrr =
            (DXGK_HARDWARERESERVEDRANGES*)pQueryAdapterInfo->pOutputData;

        if (hrr == NULL ||
            pQueryAdapterInfo->OutputDataSize < sizeof(*hrr)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        hrr->NumRanges = 0;
        WM_QAI_OK("HARDWARERESERVEDRANGES(2)=0");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUMMUCAPS: {
        DXGK_GPUMMUCAPS               caps;
        SIZE_T                        outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T                        copyLen;
        const DXGK_QUERYGPUMMUCAPSIN* in;

        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYGPUMMUCAPSIN)) {
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYGPUMMUCAPSIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(&caps, sizeof(caps));

        caps.PageTableUpdateMode                       = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        caps.ReadOnlyMemorySupported                   = 1;
        caps.NoExecuteMemorySupported                  = 1;
        caps.ZeroInPteSupported                        = 1;
        caps.VirtualAddressBitCount                    = 48;
        caps.PageTableLevelCount                       = 4;
        caps.ExplicitPageTableInvalidation             = 1;   /* we drive MMU AS_TLB invalidate registers */
        caps.CacheCoherentMemorySupported              = 1;
        caps.PageTableUpdateRequireAddressSpaceIdle    = 0;   /* live PT updates OK */
        caps.DualPteSupported                          = 0;   /* Mali LPAE has no dual-PT */
        caps.LeafPageTableSizeFor64KPagesInBytes       = 0;   /* no 64K pages */

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(caps)) ? outSz : sizeof(caps);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &caps, copyLen);
        WM_QAI_OK("GPUMMUCAPS");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN* in;
        DXGK_PAGE_TABLE_LEVEL_DESC            desc;
        SIZE_T                                outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T                                copyLen;
        const UINT                            kLevels  = 4;
        const UINT                            kIdxBits = 9;

        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYPAGETABLELEVELDESCIN)) {
            return STATUS_INVALID_PARAMETER;
        }
        in = (const DXGK_QUERYPAGETABLELEVELDESCIN*)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0 || in->LevelIndex >= kLevels) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlZeroMemory(&desc, sizeof(desc));
        desc.PageTableIndexBitCount             = kIdxBits;
        desc.PageTableSegmentId                 = WINMALI_SEGMENT_ID_SYSMEM;
        desc.PagingProcessPageTableSegmentId    = WINMALI_SEGMENT_ID_SYSMEM;
        desc.PageTableSizeInBytes               = (UINT)PAGE_SIZE;
        desc.PageTableAlignmentInBytes          = 0;  /* use segment page size */

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(desc)) ? outSz : sizeof(desc);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &desc, copyLen);
        DbgPrint("[WinMali]    QAI[PAGETABLELEVELDESC level=%u] OK\n", in->LevelIndex);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT3: {
        /* QUERYSEGMENT* is a TWO-PASS call:
             pass 1 (probe): pSegmentDescriptor == NULL. We set NbSegment +
                             PagingBuffer* fields and return SUCCESS. dxgk
                             then allocates space for descriptors.
             pass 2 (fill):  pSegmentDescriptor != NULL. We write the
                             descriptor array.
           Returning STATUS_INVALID_PARAMETER on a probe call trips a dxgk
           NT_ASSERT in chk builds (and silently corrupts state in fre). */
        DXGK_QUERYSEGMENTOUT3*   qo;
        DXGK_SEGMENTDESCRIPTOR3* segs;
        PWINMALI_ADAPTER         adapter;
        const UINT               kNumSegs = WINMALI_SEGMENT_COUNT;

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        if (adapter == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (adapter->DmaSegmentVa == NULL || adapter->DmaSegmentBytes == 0) {
            WINMALI_WARN("QUERYSEGMENT3: sysmem segment not allocated");
            return STATUS_DEVICE_NOT_READY;
        }
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3)) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo = (DXGK_QUERYSEGMENTOUT3*)pQueryAdapterInfo->pOutputData;
        qo->NbSegment                   = kNumSegs;

        qo->PagingBufferSegmentId       = WINMALI_SEGMENT_ID_SYSMEM;
        qo->PagingBufferSize            = 64 * 1024;
        qo->PagingBufferPrivateDataSize = 0;

        segs = qo->pSegmentDescriptor;
        if (segs != NULL) {
            RtlZeroMemory(segs, sizeof(DXGK_SEGMENTDESCRIPTOR3) * kNumSegs);
            segs[0].Flags.Value                     = 0;
            segs[0].Flags.Aperture                  = 1;
            segs[0].Flags.CacheCoherent             = 1;
            segs[0].Flags.CpuVisible                = 1;
            segs[0].Flags.ApplicationTarget         = 1;
            segs[0].Flags.NonLocalBudgetGroup       = 1;
            segs[0].Flags.LocalBudgetGroup          = 0;
            segs[0].BaseAddress.QuadPart            = WINMALI_SYSMEM_GPU_BASE;
            segs[0].CpuTranslatedAddress.QuadPart   = 0;   /* Aperture: dxgk pins pages on demand */
            segs[0].Size                            = adapter->DmaSegmentBytes;
            segs[0].NbOfBanks                       = 0;
            segs[0].pBankRangeTable                 = NULL;
            segs[0].CommitLimit                     = adapter->DmaSegmentBytes;
            segs[0].SystemMemoryEndAddress          = 0;
            DbgPrint("[WinMali]    QAI[QUERYSEGMENT3 fill] OK (1 aperture seg, %lu MiB)\n",
                     (ULONG)(adapter->DmaSegmentBytes >> 20));
        } else {
            DbgPrint("[WinMali]    QAI[QUERYSEGMENT3 probe] NbSegment=%u\n", kNumSegs);
        }
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    case DXGKQAITYPE_QUERYSEGMENT4: {
        /* Same two-pass dance as QUERYSEGMENT3 - probe first, fill second. */
        DXGK_QUERYSEGMENTOUT4*   qo;
        DXGK_SEGMENTDESCRIPTOR4* segs;
        PWINMALI_ADAPTER         adapter;
        const UINT               kNumSegs = WINMALI_SEGMENT_COUNT;

        adapter = WinMaliAdapterFromDxgkHandle((PVOID)hAdapter);
        if (adapter == NULL || adapter->DmaSegmentVa == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (pQueryAdapterInfo->pInputData != NULL &&
            pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYSEGMENTIN4)) {
            const DXGK_QUERYSEGMENTIN4* sin =
                (const DXGK_QUERYSEGMENTIN4*)pQueryAdapterInfo->pInputData;
            if (sin->PhysicalAdapterIndex != 0) {
                return STATUS_INVALID_PARAMETER;
            }
        }
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4)) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        qo = (DXGK_QUERYSEGMENTOUT4*)pQueryAdapterInfo->pOutputData;
        qo->NbSegment                   = kNumSegs;
        qo->PagingBufferSegmentId       = WINMALI_SEGMENT_ID_SYSMEM;
        qo->PagingBufferSize            = 64 * 1024;
        qo->PagingBufferPrivateDataSize = 0;

        segs = (DXGK_SEGMENTDESCRIPTOR4*)qo->pSegmentDescriptor;  /* BYTE* in header */
        if (segs != NULL) {
            RtlZeroMemory(segs, sizeof(DXGK_SEGMENTDESCRIPTOR4) * kNumSegs);
            segs[0].Flags.Value                     = 0;
            segs[0].Flags.Aperture                  = 1;
            segs[0].Flags.CacheCoherent             = 1;
            segs[0].Flags.CpuVisible                = 1;
            segs[0].Flags.ApplicationTarget         = 1;
            segs[0].Flags.NonLocalBudgetGroup       = 1;
            segs[0].Flags.LocalBudgetGroup          = 0;
            segs[0].BaseAddress.QuadPart            = WINMALI_SYSMEM_GPU_BASE;
            segs[0].CpuTranslatedAddress.QuadPart   = 0;
            segs[0].Size                            = adapter->DmaSegmentBytes;
            segs[0].CommitLimit                     = adapter->DmaSegmentBytes;
            segs[0].SystemMemoryEndAddress          = 0;
            segs[0].NumInvalidMemoryRanges          = 0;
            DbgPrint("[WinMali]    QAI[QUERYSEGMENT4 fill] OK (1 aperture seg, %lu MiB)\n",
                     (ULONG)(adapter->DmaSegmentBytes >> 20));
        } else {
            DbgPrint("[WinMali]    QAI[QUERYSEGMENT4 probe] NbSegment=%u\n", kNumSegs);
        }
        return STATUS_SUCCESS;
    }
#endif

    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        /* Report 64-bit fence precision so dxgk uses our wider monotonic
           counters and doesn't truncate to 32 bits. */
        UINT* prec = (UINT*)pQueryAdapterInfo->pOutputData;
        if (prec == NULL || pQueryAdapterInfo->OutputDataSize < sizeof(*prec)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *prec = 64;
        WM_QAI_OK("HISTORYBUFFERPRECISION=64");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
        /* Render-only - no display extension. */
        return STATUS_NOT_SUPPORTED;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    case DXGKQAITYPE_ADAPTERPERFDATA_CAPS: {
        /* dxgk asks this to learn power/thermal monitoring limits. We don't
           expose perf counters from the Mali MMIO yet; report zeros for
           bandwidth/fan and reasonable thermal headroom (TemperatureMax 105C,
           Warning 95C in deci-Celsius units, 1 = 0.1C). */
        DXGK_ADAPTER_PERFDATACAPS caps;
        SIZE_T outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T copyLen;

        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(&caps, sizeof(caps));
        caps.MaxMemoryBandwidth = 0;
        caps.MaxPCIEBandwidth   = 0;     /* SoC integrated - no PCI-E */
        caps.MaxFanRPM          = 0;     /* no fan / not exposed */
        caps.TemperatureMax     = 1050;  /* 105.0 C */
        caps.TemperatureWarning = 950;   /* 95.0 C */

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(caps)) ? outSz : sizeof(caps);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &caps, copyLen);
        WM_QAI_OK("ADAPTERPERFDATA_CAPS");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_NODEPERFDATA:
    case DXGKQAITYPE_ADAPTERPERFDATA: {
        /* Task Manager / dxgk telemetry polls these roughly once per
           second per node for as long as the adapter is up - seeing them
           loop in kd means the adapter STARTED and is being monitored.
           We don't read clocks/thermals from the Mali MMIO yet, so
           zero-fill (renders as 0 MHz / no sensor). Deliberately silent:
           a log line here floods the kd ring at steady state. */
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                      pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUVERSION: {
        DXGK_GPUVERSION ver;
        SIZE_T outSz = pQueryAdapterInfo->OutputDataSize;
        SIZE_T copyLen;

        if (pQueryAdapterInfo->pOutputData == NULL || outSz == 0) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(&ver, sizeof(ver));
        (VOID)RtlStringCbCopyW(ver.BiosVersion,
                               sizeof(ver.BiosVersion),
                               L"Mali CSF FW 1.1.0");
        (VOID)RtlStringCbCopyW(ver.GpuArchitecture,
                               sizeof(ver.GpuArchitecture),
                               L"Mali-G610 Valhall");
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(ver)) ? outSz : sizeof(ver);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &ver, copyLen);
        WM_QAI_OK("GPUVERSION");
        return STATUS_SUCCESS;
    }
#endif // WDDM 2.4+

    default:
        /* For *small* (<= 64 byte) unknown query types - typically future
           WDDM extensions probing for capabilities - return SUCCESS with a
           zero-filled output. NOT_IMPLEMENTED can be interpreted by newer
           dxgkrnl as "driver is broken"; SUCCESS-with-zeros means "I know
           the query but I don't claim that capability". We've seen this
           pattern matter for QAI[47] on Win11 26100 ARM64.
           For large unknown structs we still NOT_IMPLEMENTED to avoid
           false claims about complex caps. */
        if (pQueryAdapterInfo->pOutputData != NULL &&
            pQueryAdapterInfo->OutputDataSize > 0 &&
            pQueryAdapterInfo->OutputDataSize <= 64) {
            RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                          pQueryAdapterInfo->OutputDataSize);
            DbgPrint("[WinMali]    QAI[%u] UNKNOWN size=%u -> SUCCESS (zero-filled)\n",
                     (unsigned)pQueryAdapterInfo->Type,
                     (unsigned)pQueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;
        }
        DbgPrint("[WinMali]    QAI[%u] UNHANDLED size=%u -> NOT_IMPLEMENTED\n",
                 (unsigned)pQueryAdapterInfo->Type,
                 (unsigned)pQueryAdapterInfo->OutputDataSize);
        return STATUS_NOT_IMPLEMENTED;
    }
}
