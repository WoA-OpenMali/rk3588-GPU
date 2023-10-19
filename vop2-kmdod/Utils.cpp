#include "Vop2Kmd.hpp"

/* Respectfully, No. I Wont use ExAllocatePool2 or whatever */
#pragma warning(disable : 4996)
//
// New and delete operators
//

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, VOP2TAG);

#if DBG
    if (pObject != NULL)
    {
        RtlFillMemory(pObject, Size, 0xCD);
    }
#endif // DBG

    return pObject;
}

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;
    
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, VOP2TAG);

#if DBG
    if (pObject != NULL)
    {
        RtlFillMemory(pObject, Size, 0xCD);
    }
#endif // DBG

    return pObject;
}

void __cdecl operator delete(void* pObject)
{

    if (pObject != NULL)
    {
        ExFreePool(pObject);
    }
}

//
// size_t version is needed for VS2015(C++ 14).  
// 
void __cdecl operator delete(void* pObject, size_t s)
{

    UNREFERENCED_PARAMETER( s );

    ::operator delete( pObject );
}

void __cdecl operator delete[](void* pObject)
{

    if (pObject != NULL)
    {
        ExFreePool(pObject);
    }
}

void Vop2DebugPrint(CONST char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
    va_end(list);
}
