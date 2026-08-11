/*
 * WinMaliLog.h - route every driver print to BOTH DbgPrint and the
 * RK3588 debug UART (UART2 @ 0xFEB50000, the same wire kd uses).
 *
 * Why: with bcdedit /debug on the box stalls at boot-debugger breaks
 * unless kd is attached and pumping 'g' - useless for unattended
 * iteration. With /debug OFF, DbgPrint output goes nowhere. This shim
 * lets us run debug-OFF and still stream [WinMali] logs over the UART,
 * captured on the dev box with a plain serial reader at 115200.
 *
 * Ownership rule: while a kernel debugger IS attached it owns the UART
 * (kd protocol frames); WinMaliSerialWrite checks KD_DEBUGGER_ENABLED
 * and stays silent then - DbgPrint already reaches kd in that mode.
 * Same binary works in both worlds.
 *
 * The `#define DbgPrint WinMaliLogPrint` below captures every existing
 * call site (macros, raw calls, generated stubs) - WinMaliSerial.c
 * opts out with WINMALI_LOG_NO_REDIRECT to reach the real DbgPrint.
 */
#pragma once

#include <ntddk.h>

/* MASTER SWITCH.
 *   1 = mirror every DbgPrint to UART2 (headless capture, debug OFF).
 *   0 = plain DbgPrint only - classic WinDbg/kd flow; the UART is never
 *       touched (WinMaliSerialInit no-ops), the wire belongs to kd.
 * Flip + rebuild; nothing else changes. */
#define WINMALI_LOG_TO_UART 0

/* Map the UART MMIO + program 115200-8n1 (only when no debugger owns
   it). Call once, early in DriverEntry, before the first print.
   No-op when WINMALI_LOG_TO_UART == 0. */
NTSTATUS WinMaliSerialInit(VOID);
VOID     WinMaliSerialTeardown(VOID);

/* Raw text writer: polled TX, bounded spins, any IRQL. */
VOID     WinMaliSerialWrite(_In_z_ const char* Text);

/* printf-shim: formats once, forwards to real DbgPrint + the UART. */
ULONG    WinMaliLogPrint(_In_z_ _Printf_format_string_ const char* Format, ...);

#if WINMALI_LOG_TO_UART && !defined(WINMALI_LOG_NO_REDIRECT)
#undef  DbgPrint
#define DbgPrint WinMaliLogPrint
#endif
