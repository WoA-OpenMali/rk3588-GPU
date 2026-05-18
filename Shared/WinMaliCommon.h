/*
 * PROJECT:     WinMaliCommon.h
 * LICENSE:
 * PURPOSE:     Implementation of the pool headers
 * COPYRIGHT:   Justin Miller <justin.miller@reactos.org>
 */

/*++
    Types shared between the WinMali kernel-mode miniport (KMD) and the
    user-mode D3D11 driver (UMD). Anything that crosses the KMD/UMD
    boundary - escape codes, private QueryAdapterInfo payloads, version
    handshake - lives here.

    This header is intentionally minimal in the skeleton: it carries the
    pool tag, the version handshake fields, and a place to grow the
    escape ABI. Real driver state stays on each side.

    The file is compiled under both ntddk.h (KMD) and windows.h (UMD),
    so use only fixed-width integer types and avoid anything that pulls
    in IRQL annotations.

--*/
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINMALI_POOL_TAG    'MniW'      /* 'WniM' little-endian: shows as "WinM" in pooltrack */

/*
 * Driver/UMD handshake. Bumped whenever the shared ABI below changes
 * shape; mismatch is fatal at OpenAdapter time.
 */
#define WINMALI_ABI_MAJOR   0
#define WINMALI_ABI_MINOR   1

/*
 * QueryAdapterInfo private-payload magic. The UMD asks the KMD for a
 * WINMALI_ADAPTER_INFO via DXGKQAITYPE_UMDRIVERPRIVATE; the KMD fills
 * Magic so the UMD can sanity-check it received our payload (and not
 * an older driver's).
 */
typedef struct _WINMALI_ADAPTER_INFO {
    uint32_t Magic;
#define WINMALI_ADAPTER_INFO_MAGIC  0x57494E4D  /* 'WINM' */
    uint16_t AbiMajor;
    uint16_t AbiMinor;
} WINMALI_ADAPTER_INFO;

#ifdef __cplusplus
} /* extern "C" */
#endif
