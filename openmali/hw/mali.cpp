#include "../openmali.hpp"

MaliGpu::MaliGpu(PDEVICE_OBJECT     PhysicalDeviceObject)
{
    //TODO: On setup event, find exact GPU details
}

MaliGpu::~MaliGpu()
{

}

VOID
MaliGpu::IrqHandler(UINT32 Status)
{
	if (Status & GPU_IRQ_FAULT) {
		UINT32 fault_status = CsfReadReg(GPU_FAULT_STATUS);
		UINT64 address = ((UINT64)CsfReadReg(GPU_FAULT_ADDR_HI) << 32) |
			      CsfReadReg(GPU_FAULT_ADDR_LO);

		DbgPrint("GPU Fault 0x%08x (%s) at 0x%016llx\n",
			 fault_status, panthor_exception_name(ptdev, fault_status & 0xFF),
			 address);
	}
	if (Status & GPU_IRQ_PROTM_FAULT)
		DbgPrint("GPU Fault in protected mode\n");

}

NTSTATUS
MaliGpu::StartDevice(IN_PDXGK_START_INFO     DxgkStartInfo,
                     IN_PDXGKRNL_INTERFACE   DxgkInterface,
                     OUT_PULONG              NumberOfVideoPresentSources,
                     OUT_PULONG              NumberOfChildren)
{
    NTSTATUS Status;
    MaliDbgPrint("MaliGpu::StartDevice Entry\n");
    LocDxgkStartInfo =  *DxgkStartInfo;
    LocDxgkInterface =  *DxgkInterface;

    /* Grab the device information - We don't need to care if it's ACPI or not with this function */
    Status = LocDxgkInterface.DxgkCbGetDeviceInformation(LocDxgkInterface.DeviceHandle,
                                                         &LocDeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        MaliDbgPrint("DxgkCbGetDeviceInformation failed with Status: 0x%X\n", Status);
        return Status;
    }

    CM_PARTIAL_RESOURCE_LIST       *ResourceList = &LocDeviceInfo.TranslatedResourceList->List->PartialResourceList;
    //TODO: We're assuming there's one reg block - THat's how it is in our EDK build/device tree
    CM_PARTIAL_RESOURCE_DESCRIPTOR *Resource = &ResourceList->PartialDescriptors[0];
    Status = LocDxgkInterface.DxgkCbMapMemory(LocDxgkInterface.DeviceHandle,
                                              Resource->u.Memory.Start,
                                              Resource->u.Memory.Length,
                                              FALSE,
                                              FALSE,
                                              MmNonCached,
                                              &RegsVaAddr);
    if (!NT_SUCCESS(Status))
    {
        MaliDbgPrint("DxgkCbMapMemory failed with Status: 0x%X\n", Status);
        return Status;
    }
    MaliDbgPrint("MaliGpu::StartDevice Mapped register PA: %X to VA: %X\n", Resource->u.Memory.Start, RegsVaAddr);
    //TODO: Let's get the device ID - for now it doesn't matter since G610 is the first Windows mali driver public

    CsfGrabinfo();
    Status =  MaliSpinupGpu();
    MaliDbgPrint("MaliGpu::StartDevice Exit\n");
    return Status;
}