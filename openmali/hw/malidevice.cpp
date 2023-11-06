#include "../openmali.hpp"

NTSTATUS
MaliGpu::MaliSpinupGpu()
{
    MaliDbgPrint("MaliGpu::MaliSpinupGpu: Entry\n");
    //Disable interrupts
    //generic startup <--
    //Firmware handle
    //MMU
    MaliDbgBreakPoint();
    return 1;
}