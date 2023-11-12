#include "../../../openmali.hpp"

VOID
MaliGpu::CsfWriteReg(UINT32 RegAddr, UINT32 RegData)
{
    // Write the value to the register at the specified offset
    *((volatile UINT32 *)(RegsVaAddr + RegAddr)) = RegData;
}

UINT32
MaliGpu::CsfReadReg(UINT32 RegAddr)
{
    UINT32 RegValue = 0;

    // Read the register value at the specified offset
    RegValue = *((volatile UINT32 *)((ULONG_PTR)RegsVaAddr + RegAddr));

    return RegValue;
}

VOID
MaliGpu::CsfGrabinfo()
{
    LocGpuInfo.gpu_id = CsfReadReg(GPU_ID);
	LocGpuInfo.csf_id = CsfReadReg(GPU_CSF_ID);
	LocGpuInfo.gpu_rev = CsfReadReg(GPU_REVID);
	LocGpuInfo.l2_features = CsfReadReg(GPU_L2_FEATURES);
	LocGpuInfo.tiler_features = CsfReadReg(GPU_TILER_FEATURES);
	LocGpuInfo.mem_features = CsfReadReg(GPU_MEM_FEATURES);
	LocGpuInfo.mmu_features = CsfReadReg(GPU_MMU_FEATURES);
	LocGpuInfo.thread_features = CsfReadReg(GPU_THREAD_FEATURES);
	LocGpuInfo.max_threads = CsfReadReg(GPU_THREAD_MAX_THREADS);
	LocGpuInfo.thread_max_workgroup_size = CsfReadReg(GPU_THREAD_MAX_WORKGROUP_SIZE);
	LocGpuInfo.thread_max_barrier_size = CsfReadReg(GPU_THREAD_MAX_BARRIER_SIZE);
	LocGpuInfo.coherency_features = CsfReadReg(GPU_COHERENCY_FEATURES);

    DbgPrint("Features: L2:%x Tiler:%x Mem:%x MMU:%x AS:%x\n",
		 LocGpuInfo.l2_features,
		 LocGpuInfo.tiler_features,
		 LocGpuInfo.mem_features,
		 LocGpuInfo.mmu_features,
		 LocGpuInfo.as_present);
}