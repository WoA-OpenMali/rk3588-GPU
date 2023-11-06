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
    PVOID RegsVaAddr;
    MaliGpu(PDEVICE_OBJECT     PhysicalDeviceObject);
    ~MaliGpu();

    //DXGKRNL
    NTSTATUS StartDevice(IN_PDXGK_START_INFO     DxgkStartInfo,
                         IN_PDXGKRNL_INTERFACE   DxgkInterface,
                         OUT_PULONG              NumberOfVideoPresentSources,
                         OUT_PULONG              NumberOfChildren);

};

