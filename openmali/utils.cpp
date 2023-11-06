/*
 * PROJECT:     OpenMali Render only display driver
 * PURPOSE:     Misc Utilities
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include "openmali.hpp"

/* Respectfully, No. I Wont use ExAllocatePool2 or whatever */
#pragma warning(disable : 4996)

void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, MALITAG);
   return pObject;
}

void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;
    
    void* pObject = ExAllocatePoolWithTag(PoolType, Size, MALITAG);

    return pObject;
}

void __cdecl operator delete(void* pObject)
{

    if (pObject != NULL)
    {
        ExFreePool(pObject);
    }
}

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
