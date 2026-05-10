/*++

Module Name:

    WinMaliDiag.h

Abstract:

    Kernel-mode GPU diagnostic harness. Consumes hand-assembled Valhall
    shader blobs from Shaders/winmali_shader_programs.h and runs them
    against the Mali GPU, validating observable side effects against
    known-good values. No dependency on Mesa, D3D, or any UMD.

    Levels (designed to be called bottom-up during bring-up):

      L0   WinMaliDiagRunSanity       - pure SW: decoder, header consistency
      L1   WinMaliDiagRunMmioProbe    - read GPU_ID / CSF_ID
      L2   WinMaliDiagRunMcuSmoke     - (later) MCU boot check
      L3   WinMaliDiagRunMmuSmoke     - (later) one page in, one page out
      L4   WinMaliDiagRunNopDispatch  - push NOP shader, wait for done
      L5   WinMaliDiagRunStoreConst   - dispatch STORE_CONSTANT, read back
      L6   WinMaliDiagRunLoadAddStore - full round-trip

    Every level is safe to call even when lower levels haven't been
    implemented - they return STATUS_NOT_IMPLEMENTED. This lets the
    driver bring-up one level at a time without breaking test runners.

--*/

#pragma once

#include <ntddk.h>
#include "..\..\Shaders\winmali_shader_programs.h"

struct _WINMALI_ADAPTER;

// L0: pure software sanity (always available, no hardware access).
NTSTATUS WinMaliDiagRunSanity(_In_ struct _WINMALI_ADAPTER* Adapter);

// L1: probe identity via MMIO. Returns STATUS_DEVICE_NOT_READY if the
// BAR is not mapped yet.
NTSTATUS WinMaliDiagRunMmioProbe(_In_ struct _WINMALI_ADAPTER* Adapter);

// L4..L6: actual shader dispatches. All stubbed to NOT_IMPLEMENTED here.
NTSTATUS WinMaliDiagRunNopDispatch   (_In_ struct _WINMALI_ADAPTER* Adapter);
NTSTATUS WinMaliDiagRunStoreConstant (_In_ struct _WINMALI_ADAPTER* Adapter);
NTSTATUS WinMaliDiagRunLoadAddStore  (_In_ struct _WINMALI_ADAPTER* Adapter, _In_ ULONG InputValue);

// Runs every level and logs pass/fail. Calls this at the end of
// StartDevice once enough plumbing exists, or on demand via an IOCTL.
NTSTATUS WinMaliDiagRunAll(_In_ struct _WINMALI_ADAPTER* Adapter);
