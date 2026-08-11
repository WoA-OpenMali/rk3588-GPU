/*
 * WinMaliSerial.c - polled-TX writer for the RK3588 debug UART.
 *
 * UART2 @ 0xFEB50000 is a Synopsys DW-APB 8250-compatible with 32-bit
 * register stride (rk3588s.dtsi: reg-shift=2, reg-io-width=4; base
 * confirmed by edk2-rk3588 RK3588.h UART2_BASE). UEFI programs it as
 * the boot console; we reprogram the divisor to a deterministic
 * 115200-8n1 at init (uclk 24 MHz -> divisor 13) so the dev-box reader
 * never has to guess the baud UEFI left behind.
 *
 * Everything is bounded polling on LSR.THRE - safe at any IRQL, never
 * hangs if the UART clock is gated (characters drop instead). A raw
 * interlocked flag serializes writers because DIRQL callers (our ISR
 * traces) can't take normal spinlocks.
 */

#define WINMALI_LOG_NO_REDIRECT   /* this file talks to the REAL DbgPrint */
#include "WinMaliLog.h"

#include <ntstrsafe.h>

#define WINMALI_UART_PHYS   0xFEB50000ULL
#define WINMALI_UART_BYTES  0x100

/* Register indices (offset / 4). */
#define UART_THR   0    /* TX holding (DLAB=0)          */
#define UART_DLL   0    /* divisor low  (DLAB=1)        */
#define UART_DLH   1    /* divisor high (DLAB=1)        */
#define UART_FCR   2    /* FIFO control (write)         */
#define UART_LCR   3    /* line control                 */
#define UART_LSR   5    /* line status                  */
#define UART_USR   31   /* DW-APB busy status           */

#define LSR_THRE   0x20u
#define LSR_TEMT   0x40u
#define USR_BUSY   0x01u

/* 24 MHz uclk / (16 * 115200) = 13.02 -> 13 (115385 baud, 0.16% off) */
#define UART_DIVISOR_115200  13u

static volatile ULONG* g_UartRegs;
static LONG            g_UartLock;

/* Each poll is an uncached APB MMIO read (~0.5-1us on RK3588), so the
   spin bound is a real-time bound: 2000 polls ~= 1-2 ms, comfortably
   past the ~87us a THRE cycle takes at 115200 but short enough that a
   wedged/clock-gated UART costs at most a couple ms - after which the
   caller must DROP the rest of the line, not keep waiting per-char.
   (The old 100k bound was ~50-100 ms PER CHARACTER when stuck: with
   the DPC watchdog regkeys disabled that presents as the whole system
   stalling instead of a bugcheck.) */
static BOOLEAN
UartSpinWhile_(_In_ ULONG RegIndex, _In_ ULONG Mask, _In_ BOOLEAN WaitSet)
{
    ULONG spins;
    for (spins = 0; spins < 2000u; ++spins) {
        ULONG v = g_UartRegs[RegIndex];
        if (WaitSet ? ((v & Mask) != 0) : ((v & Mask) == 0)) {
            return TRUE;
        }
    }
    return FALSE;   /* bounded: caller drops output rather than hanging */
}

NTSTATUS
WinMaliSerialInit(VOID)
{
#if !WINMALI_LOG_TO_UART
    /* WinDbg/kd mode: leave the UART entirely alone (no MMIO map, no line
       reprogram). g_UartRegs stays NULL so WinMaliSerialWrite no-ops. */
    return STATUS_SUCCESS;
#else
    PHYSICAL_ADDRESS pa;

    if (g_UartRegs != NULL) {
        return STATUS_SUCCESS;
    }

    pa.QuadPart = (LONGLONG)WINMALI_UART_PHYS;
    g_UartRegs = (volatile ULONG*)MmMapIoSpaceEx(
        pa, WINMALI_UART_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (g_UartRegs == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Only reprogram the line when no kernel debugger owns the UART -
       kd would lose sync mid-boot otherwise. */
    if (!KD_DEBUGGER_ENABLED) {
        UartSpinWhile_(UART_LSR, LSR_TEMT, TRUE);   /* drain TX        */
        UartSpinWhile_(UART_USR, USR_BUSY, FALSE);  /* DW-APB LCR rule */
        g_UartRegs[UART_LCR] = 0x83;                /* DLAB + 8n1      */
        g_UartRegs[UART_DLL] = UART_DIVISOR_115200;
        g_UartRegs[UART_DLH] = 0;
        g_UartRegs[UART_LCR] = 0x03;                /* 8n1             */
        g_UartRegs[UART_FCR] = 0x07;                /* FIFO en + clear */
    }
    return STATUS_SUCCESS;
#endif
}

VOID
WinMaliSerialTeardown(VOID)
{
    if (g_UartRegs != NULL) {
        MmUnmapIoSpace((PVOID)g_UartRegs, WINMALI_UART_BYTES);
        g_UartRegs = NULL;
    }
}

VOID
WinMaliSerialWrite(_In_z_ const char* Text)
{
    const char* p;

    if (g_UartRegs == NULL || Text == NULL) {
        return;
    }
    if (KD_DEBUGGER_ENABLED) {
        return;     /* kd owns the wire; DbgPrint reaches it instead */
    }

    /* Interlocked flag, not a spinlock: ISR-level (DIRQL) callers are
       legal here and normal spinlocks are not. BOUNDED acquisition -
       if the holder is a preempted PASSIVE thread and we're spinning
       at DISPATCH/DIRQL on the same CPU it can never release, so an
       unbounded spin is a livelock. Losing a log line beats hanging. */
    {
        ULONG tries;
        for (tries = 0; ; ++tries) {
            if (InterlockedCompareExchange(&g_UartLock, 1, 0) == 0) {
                break;
            }
            if (tries >= 500000u) {
                return;             /* drop the line */
            }
            YieldProcessor();
        }
    }

    for (p = Text; *p != '\0'; ++p) {
        if (*p == '\n') {
            if (!UartSpinWhile_(UART_LSR, LSR_THRE, TRUE)) {
                break;              /* UART wedged: drop rest of line */
            }
            g_UartRegs[UART_THR] = (ULONG)'\r';
        }
        if (!UartSpinWhile_(UART_LSR, LSR_THRE, TRUE)) {
            break;
        }
        g_UartRegs[UART_THR] = (ULONG)(UCHAR)*p;
    }

    InterlockedExchange(&g_UartLock, 0);
}

ULONG
WinMaliLogPrint(_In_z_ _Printf_format_string_ const char* Format, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, Format);
    if (!NT_SUCCESS(RtlStringCbVPrintfA(buf, sizeof(buf), Format, ap))) {
        buf[sizeof(buf) - 1] = '\0';   /* truncated is fine */
    }
    va_end(ap);

    DbgPrint("%s", buf);      /* real DbgPrint - kd path when attached */
    WinMaliSerialWrite(buf);  /* UART path when not */
    return 0;
}
