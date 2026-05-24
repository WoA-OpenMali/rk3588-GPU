/*
 * WinMaliQai.c - DxgkDdiQueryAdapterInfo.
 *
 * Real cap-walk implementation. With MMU + the 256 MiB sysmem segment
 * allocated at StartDevice, we now publish numbers dxgk can act on:
 *
 *   DRIVERCAPS                 -> render-only WDDM 2 + GpuMmu + VA
 *   PHYSICALADAPTERCAPS        -> our DxgkHandle + GpuMmu, no paging node
 *   GPUMMUCAPS                 -> 30-bit VA, 2-level page table, NX/RO
 *   PAGETABLELEVELDESC         -> 9 idx bits per level, segment id 1
 *   QUERYSEGMENT3              -> 1 segment, sysmem-backed, CpuVisible
 *   QUERYSEGMENT4              -> same data, newer descriptor layout
 *   HISTORYBUFFERPRECISION     -> 64-bit
 *
 * Bounded copy-out (RtlZeroMemory dest first, copy min(outSz, sizeof)) is
 * used everywhere so kernels with smaller struct views than our 1809 WDK
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

    DbgPrint("[WinMali]    QAI[%u] enter outsz=%u\n",
             (unsigned)pQueryAdapterInfo->Type,
             (unsigned)pQueryAdapterInfo->OutputDataSize);

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_DRIVERCAPS: {
        /* Minimal honest DRIVERCAPS - only the caps we can actually back with
           working DDIs. Round 2's superset (matching NVIDIA) caused dxgk to
           bail at this stage: cap bits like NonCpuVisiblePrimary, NoDmaPatching,
           LowIrqlPreemptCommand assert capabilities that require working
           Patch/PreemptCommand/CPU-invisible-segment infrastructure that our
           render-only KMD doesn't yet have. dxgk verifies the claim and
           tears the adapter down when it doesn't pan out. */
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

        WM_SET(WDDMVersion, DXGKDDI_WDDMv2_5);
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, HighestAcceptableAddress) +
            sizeof(caps->HighestAcceptableAddress) <= lim) {
            caps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;
        }
        WM_SET(MaxAllocationListSlotId, 7);
        /* NVIDIA returns 0 from its base getOverrideSystemMemoryCommitLimit()
           (memoryCfgMgr.cpp:308) - matching them, not speculating. */
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
            caps->FlipCaps.FlipOnVSyncMmIo = TRUE;
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, SchedulingCaps) +
            sizeof(caps->SchedulingCaps) <= lim) {
            /* Match NVIDIA's nvlQuery.cpp:1808-1815, 2033 pattern - all
               unconditionally set on WDDM 2.0+. NVIDIA never sets
               HwQueuePacketCap, so we leave it 0. */
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
            /* nvlQuery.cpp:1818-1833, 1941, 2120-2129 */
            caps->MemoryManagementCaps.CrossAdapterResource       = 1;
            caps->MemoryManagementCaps.SectionBackedPrimary       = 0;
            caps->MemoryManagementCaps.VirtualAddressingSupported = 1;
            caps->MemoryManagementCaps.GpuMmuSupported            = 1;
            caps->MemoryManagementCaps.OutOfOrderLock             = 1;
            caps->MemoryManagementCaps.NonCpuVisiblePrimary       = 1;  /* NVIDIA sets for Win10 >=10523; we're 26100 */
            caps->MemoryManagementCaps.PagingNode                 = 0;
            caps->MemoryManagementCaps.IoMmuSupported             = 0;
        }
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, PreemptionCaps) +
            sizeof(caps->PreemptionCaps) <= lim) {
            caps->PreemptionCaps.GraphicsPreemptionGranularity =
                D3DKMDT_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY;
            caps->PreemptionCaps.ComputePreemptionGranularity  =
                D3DKMDT_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY;
        }

        /* GPU VA span. Must enclose the per-process VA we accept in
           SetRootPageTable. Aligned with GPUMMUCAPS.VirtualAddressBitCount
           and PAGETABLELEVELDESC (2 levels x 9 idx bits + 12 page = 30 bits). */
        WM_SET(InternalGpuVirtualAddressRangeStart,
               (D3DGPU_VIRTUAL_ADDRESS)0x0000000000010000ULL);
        WM_SET(InternalGpuVirtualAddressRangeEnd,
               (D3DGPU_VIRTUAL_ADDRESS)0x000000003FFFFFFFULL);

        if (FIELD_OFFSET(DXGK_DRIVERCAPS, GpuEngineTopology) +
            sizeof(caps->GpuEngineTopology) <= lim) {
            caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
        }
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
        if (FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) +
            sizeof(caps->MiscCaps) <= lim) {
            /* WDDM 2.4+ misc-caps bits. SupportContextlessPresent is set by
               NVIDIA for build >=16355 (we're on 26100); it indicates the
               driver can run presents without a per-context HW state. */
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
        phys.NumExecutionNodes         = 1;
        /* PagingNodeIndex == NumExecutionNodes is the documented sentinel
           for "no dedicated paging node" - appropriate when we have one node
           (3D) and CPU_VIRTUAL page-table updates. Setting it to 0 (the only
           real ordinal we have) makes dxgkrnl bail right after this DDI on
           Win11 ARM64 since node 0 is the 3D engine, not a copy/DMA engine. */
        phys.PagingNodeIndex           = phys.NumExecutionNodes;
        phys.DxgkPhysicalAdapterHandle = adapter->DxgkHandle;
        phys.Flags.Value               = 0;
        phys.Flags.GpuMmuSupported     = 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        phys.VPRPagingNode             = phys.NumExecutionNodes;  /* no VPR */
#endif

        RtlZeroMemory(pQueryAdapterInfo->pOutputData, outSz);
        copyLen = (outSz < sizeof(phys)) ? outSz : sizeof(phys);
        RtlCopyMemory(pQueryAdapterInfo->pOutputData, &phys, copyLen);
        WM_QAI_OK("PHYSICALADAPTERCAPS");
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

        /* Faithful port of NVIDIA's gpuMmuCaps (nvlQuery.cpp:1376-1443).
           Mali G610 has a full GPU-side MMU with LPAE-style page tables;
           the GpuMmu model maps cleanly. Adjusted for Mali specifics:
           - 30-bit GPU VA span (2 levels x 9 idx + 12 page = 30)
           - 2-level page tables (no PTE_V2)
           - No 64K page support (LPAE 4K leaves only)
           - No DualPte (Mali doesn't have NVIDIA's dual-PT layout)
           - Mali supports RO + NX in LPAE PTEs */
        RtlZeroMemory(&caps, sizeof(caps));
        caps.PageTableUpdateMode                       = DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
        caps.ReadOnlyMemorySupported                   = 1;
        caps.NoExecuteMemorySupported                  = 1;
        caps.ZeroInPteSupported                        = 1;
        caps.VirtualAddressBitCount                    = 30;
        caps.PageTableLevelCount                       = 2;
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
        const UINT                            kLevels  = 2;
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
        /* PagingBufferSegmentId must point at an Aperture-flagged segment
           (NVIDIA's nvlddmkm uses an Aperture+CacheCoherent sysmem segment for
           this purpose - see memoryCfgMgrWoI.cpp:1644-1655). Setting it to 0,
           or pointing at a non-Aperture Memory segment, makes dxgkrnl unload
           the adapter immediately after the system-context probe. */
        qo->PagingBufferSegmentId       = WINMALI_SEGMENT_ID_SYSMEM;
        qo->PagingBufferSize            = 64 * 1024;
        qo->PagingBufferPrivateDataSize = 0;

        segs = qo->pSegmentDescriptor;
        if (segs != NULL) {
            /* Aperture segment backed by our pre-allocated contig sysmem.
               Mirrors NVIDIA's cached-system-memory segment flags. */
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
        /* See QUERYSEGMENT3 above. PagingBufferSegmentId must reference an
           Aperture-flagged segment (NVIDIA pattern). */
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
