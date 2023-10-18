class VOP2
{
private:
    DEVICE_OBJECT* DriverPDO;
    /* These are your DxgKrnl services */
    DXGKRNL_INTERFACE DxgkInterface;
    /* Startup info passed via a DDI */
    DXGK_START_INFO StartInfo;
    DXGK_DEVICE_INFO DeviceInfo;

public:
    BOOLEAN IsActive;

    VOP2(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~VOP2();

    /* DDIs: */
    NTSTATUS StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren);

};