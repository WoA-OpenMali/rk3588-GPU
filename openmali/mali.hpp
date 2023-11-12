class MaliGpu
{
private:
    //LOCAL HW
    NTSTATUS MaliSpinupGpu(); 
public:

    PDEVICE_OBJECT     PDO;
    DXGK_DEVICE_INFO    LocDeviceInfo;
    DXGK_START_INFO    LocDxgkStartInfo;
    DXGKRNL_INTERFACE   LocDxgkInterface;
    HW_GPU_INFO       LocGpuInfo;
    PVOID RegsVaAddr;
    MaliGpu(PDEVICE_OBJECT     PhysicalDeviceObject);
    ~MaliGpu();
    VOID IrqHandler(UINT32 Status);
    //DXGKRNL
    NTSTATUS StartDevice(IN_PDXGK_START_INFO     DxgkStartInfo,
                         IN_PDXGKRNL_INTERFACE   DxgkInterface,
                         OUT_PULONG              NumberOfVideoPresentSources,
                         OUT_PULONG              NumberOfChildren);




    /* CSF Mali GPU -> I could swap this into a seperate class... */
    VOID CsfGrabinfo()
    VOID CsfWriteReg(UINT32 RegAddr, UINT32 RegData);
    UINT32 CsfReadReg(UINT32 RegAddr);
};

