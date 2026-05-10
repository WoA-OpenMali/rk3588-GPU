#include "WinMaliKmd.h"

// -------------------------------------------------------------------------
// Provider definition. GUID MUST match WinMaliTrace.h.
// -------------------------------------------------------------------------

TRACELOGGING_DEFINE_PROVIDER(
    g_WinMaliTraceProvider,
    WINMALI_TRACE_PROVIDER_NAME,
    // {B4A0F5C4-7D1E-4E6A-9C44-2B9C6F3A0E10}
    (0xB4A0F5C4, 0x7D1E, 0x4E6A, 0x9C, 0x44, 0x2B, 0x9C, 0x6F, 0x3A, 0x0E, 0x10));

NTSTATUS
WinMaliTraceRegister(VOID)
{
    DbgSetDebugFilterState(DPFLTR_IHVDRIVER_ID, 0xFFFFFFFF, TRUE);

    NTSTATUS status = TraceLoggingRegister(g_WinMaliTraceProvider);
    if (!NT_SUCCESS(status)) {
        // No ETW yet, so go straight to the debugger via the unfiltered
        // DbgPrint so we definitely see this.
        DbgPrint("[WinMali] TraceLoggingRegister failed 0x%08x\n", status);
    }
    return status;
}

VOID
WinMaliTraceUnregister(VOID)
{
    TraceLoggingUnregister(g_WinMaliTraceProvider);
}

// -------------------------------------------------------------------------
// Shared formatted emit. Writes to both ETW and to the kernel debugger.
// -------------------------------------------------------------------------

static PCSTR
WinMaliTraceLevelTag_(_In_ UCHAR Level)
{
    switch (Level) {
        case TRACE_LEVEL_ERROR:    return "ERR";
        case TRACE_LEVEL_WARNING:  return "WARN";
        case TRACE_LEVEL_VERBOSE:  return "VERB";
        case TRACE_LEVEL_INFORMATION:
        default:                   return "INFO";
    }
}

VOID
WinMaliTraceFormatted_(
    _In_ UCHAR  Level,
    _In_ PCSTR  Function,
    _In_ PCSTR  Format,
    ...)
{
    // Stack-resident format buffer. We never block, never allocate on this
    // path - the ISR calls us.
    CHAR     buffer[512];
    va_list  args;
    NTSTATUS status;
    SIZE_T   remaining = 0;

    va_start(args, Format);
    status = RtlStringCbVPrintfA(buffer, sizeof(buffer), Format, args);
    va_end(args);

    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW) {
        // Give up and emit something recognisable rather than nothing.
        RtlStringCbCopyA(buffer, sizeof(buffer), "<trace format error>");
    }

    // Ensure NUL-terminated; Rtl*VPrintfA already does, but be defensive.
    remaining = sizeof(buffer) - 1;
    buffer[remaining] = '\0';

    // TraceLoggingWrite bakes Level into the event metadata at compile
    // time, so we can't pass a runtime-selected constant - we have to
    // fan out to a small switch. Four sites, one per level.
    switch (Level) {
    case TRACE_LEVEL_ERROR:
        TraceLoggingWrite(g_WinMaliTraceProvider, "WinMaliTrace",
            TraceLoggingLevel(TRACE_LEVEL_ERROR),
            TraceLoggingString(Function, "function"),
            TraceLoggingString(buffer,   "message"));
        break;
    case TRACE_LEVEL_WARNING:
        TraceLoggingWrite(g_WinMaliTraceProvider, "WinMaliTrace",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingString(Function, "function"),
            TraceLoggingString(buffer,   "message"));
        break;
    case TRACE_LEVEL_VERBOSE:
        TraceLoggingWrite(g_WinMaliTraceProvider, "WinMaliTrace",
            TraceLoggingLevel(TRACE_LEVEL_VERBOSE),
            TraceLoggingString(Function, "function"),
            TraceLoggingString(buffer,   "message"));
        break;
    case TRACE_LEVEL_INFORMATION:
    default:
        TraceLoggingWrite(g_WinMaliTraceProvider, "WinMaliTrace",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingString(Function, "function"),
            TraceLoggingString(buffer,   "message"));
        break;
    }

    DbgPrint("[WinMali/%s] %s: %s\n",
             WinMaliTraceLevelTag_(Level), Function, buffer);
}
