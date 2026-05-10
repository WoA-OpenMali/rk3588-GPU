#pragma once

#include <ntddk.h>
#include <TraceLoggingProvider.h>
#include <evntrace.h>

#define WINMALI_TRACE_PROVIDER_NAME          "WinMali-KMD"
#define WINMALI_TRACE_PROVIDER_GUID_STRING   "{B4A0F5C4-7D1E-4E6A-9C44-2B9C6F3A0E10}"

TRACELOGGING_DECLARE_PROVIDER(g_WinMaliTraceProvider);

NTSTATUS WinMaliTraceRegister(VOID);
VOID     WinMaliTraceUnregister(VOID);
VOID WinMaliTraceFormatted_(
    _In_ UCHAR  Level,       // TRACE_LEVEL_INFORMATION / WARNING / ERROR / VERBOSE
    _In_ PCSTR  Function,
    _In_ PCSTR  Format,
    ...);

#define WINMALI_TRACE(fmt, ...)  \
    WinMaliTraceFormatted_(TRACE_LEVEL_INFORMATION, __FUNCTION__, (fmt), ##__VA_ARGS__)
#define WINMALI_WARN(fmt, ...)   \
    WinMaliTraceFormatted_(TRACE_LEVEL_WARNING,     __FUNCTION__, (fmt), ##__VA_ARGS__)
#define WINMALI_ERROR(fmt, ...)  \
    WinMaliTraceFormatted_(TRACE_LEVEL_ERROR,       __FUNCTION__, (fmt), ##__VA_ARGS__)
#define WINMALI_ENTER()          \
    WinMaliTraceFormatted_(TRACE_LEVEL_VERBOSE,     __FUNCTION__, "ENTER")
