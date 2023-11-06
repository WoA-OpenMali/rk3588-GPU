#pragma once

#ifndef __RELFILE__
#define __RELFILE__ __FILE__
#endif

ULONG
__cdecl
DbgPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...
);

NTSYSAPI
ULONG
__cdecl
DbgPrintEx(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...
);

__analysis_noreturn
NTSYSAPI
VOID
NTAPI
RtlAssert(
    _In_ PVOID FailedAssertion,
    _In_ PVOID FileName,
    _In_ ULONG LineNumber,
    _In_opt_z_ PCHAR Message
);


#ifndef ASSERT
#if DBG && !defined(NASSERT)
#define ASSERT(x) if (!(x)) { RtlAssert((PVOID)#x, (PVOID)__RELFILE__, __LINE__, (PCHAR)""); }
#else
#define ASSERT(x) ((VOID) 0)
#endif
#endif
#if 0
#define MaliDbgPrint(fmt, ...) do { \
        if (DbgPrint("(%s:%d) " fmt, __RELFILE__, __LINE__, ##__VA_ARGS__))  \
            DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)
#endif

#define MaliDbgPrint(fmt, ...) DbgPrint(fmt, __VA_ARGS__);                             


#define MaliDbgBreakPoint() DbgBreakPoint();
