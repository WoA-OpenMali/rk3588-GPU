/*
 * WinMaliKillSwitch.c - read HKLM\System\CCS\Services\WinMaliKmd\Parameters\Disabled
 * EARLY in DriverEntry, before we touch dxgk. Lets us shut the driver off via
 * `reg add ...` over SSH when a previous boot crashed, without having to pull
 * the WTG SSD.
 *
 * Designed to be the safest possible code path:
 *   - Stack-only locals, no globals touched.
 *   - Failure to read = return Disabled=FALSE (driver runs as normal).
 *   - All Zw* failures swallowed, only the registry contract matters.
 */

#include "WinMaliKmd.h"

#define WINMALI_KILLSWITCH_VALUE  L"Disabled"
#define WINMALI_PARAMS_SUFFIX     L"\\Parameters"

NTSTATUS
WinMaliArmKillSwitch(_In_ PUNICODE_STRING RegistryPath)
{
    WCHAR             pathBuf[512];
    UNICODE_STRING    paramsPath;
    UNICODE_STRING    valueName;
    OBJECT_ATTRIBUTES oa;
    HANDLE            hKey       = NULL;
    NTSTATUS          status;
    ULONG             one        = 1;
    ULONG             disposition = 0;

    if (RegistryPath == NULL || RegistryPath->Buffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (RegistryPath->Length + sizeof(WINMALI_PARAMS_SUFFIX) > sizeof(pathBuf)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(pathBuf, RegistryPath->Buffer, RegistryPath->Length);
    paramsPath.Buffer        = pathBuf;
    paramsPath.Length        = RegistryPath->Length;
    paramsPath.MaximumLength = sizeof(pathBuf);
    status = RtlAppendUnicodeToString(&paramsPath, WINMALI_PARAMS_SUFFIX);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    InitializeObjectAttributes(&oa, &paramsPath,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    status = ZwCreateKey(&hKey, KEY_SET_VALUE, &oa, 0, NULL,
                         REG_OPTION_NON_VOLATILE, &disposition);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&valueName, WINMALI_KILLSWITCH_VALUE);
    status = ZwSetValueKey(hKey, &valueName, 0, REG_DWORD,
                           &one, sizeof(one));
    ZwClose(hKey);
    return status;
}

/* Read a REG_DWORD from Services\WinMaliKmd\Parameters. Missing key/value
   (or any failure) leaves *Value at Default - callers pick fail-safe
   defaults. Same safety posture as the killswitch reader. */
NTSTATUS
WinMaliReadParamsDword(
    _In_  PUNICODE_STRING RegistryPath,
    _In_  PCWSTR          ValueName,
    _In_  ULONG           Default,
    _Out_ ULONG*          Value)
{
    WCHAR             pathBuf[512];
    UNICODE_STRING    paramsPath;
    UNICODE_STRING    valueName;
    OBJECT_ATTRIBUTES oa;
    HANDLE            hKey      = NULL;
    NTSTATUS          status;
    UCHAR             valBuf[64];
    ULONG             resultLen = 0;
    PKEY_VALUE_PARTIAL_INFORMATION pInfo;

    *Value = Default;

    if (RegistryPath == NULL || RegistryPath->Buffer == NULL) {
        return STATUS_SUCCESS;
    }
    if (RegistryPath->Length + sizeof(WINMALI_PARAMS_SUFFIX) > sizeof(pathBuf)) {
        return STATUS_SUCCESS;
    }
    RtlCopyMemory(pathBuf, RegistryPath->Buffer, RegistryPath->Length);
    paramsPath.Buffer        = pathBuf;
    paramsPath.Length        = RegistryPath->Length;
    paramsPath.MaximumLength = sizeof(pathBuf);
    status = RtlAppendUnicodeToString(&paramsPath, WINMALI_PARAMS_SUFFIX);
    if (!NT_SUCCESS(status)) {
        return STATUS_SUCCESS;
    }

    InitializeObjectAttributes(&oa, &paramsPath,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    status = ZwOpenKey(&hKey, KEY_READ, &oa);
    if (!NT_SUCCESS(status)) {
        return STATUS_SUCCESS;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation,
                             valBuf, sizeof(valBuf), &resultLen);
    if (NT_SUCCESS(status)) {
        pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)valBuf;
        if (pInfo->Type == REG_DWORD && pInfo->DataLength == sizeof(ULONG)) {
            RtlCopyMemory(Value, pInfo->Data, sizeof(ULONG));
        }
    }

    ZwClose(hKey);
    return STATUS_SUCCESS;
}

NTSTATUS
WinMaliReadKillSwitch(
    _In_  PUNICODE_STRING RegistryPath,
    _Out_ BOOLEAN*        Disabled)
{
    WCHAR             pathBuf[512];
    UNICODE_STRING    paramsPath;
    UNICODE_STRING    valueName;
    OBJECT_ATTRIBUTES oa;
    HANDLE            hKey       = NULL;
    NTSTATUS          status;
    UCHAR             valBuf[64];
    ULONG             resultLen  = 0;
    PKEY_VALUE_PARTIAL_INFORMATION pInfo;

    *Disabled = FALSE;

    if (RegistryPath == NULL || RegistryPath->Buffer == NULL) {
        return STATUS_SUCCESS;  /* Fail-open: treat as enabled. */
    }

    /* paramsPath = RegistryPath + "\\Parameters". Bounded copy. */
    if (RegistryPath->Length + sizeof(WINMALI_PARAMS_SUFFIX) > sizeof(pathBuf)) {
        return STATUS_SUCCESS;
    }
    RtlCopyMemory(pathBuf, RegistryPath->Buffer, RegistryPath->Length);
    paramsPath.Buffer        = pathBuf;
    paramsPath.Length        = RegistryPath->Length;
    paramsPath.MaximumLength = sizeof(pathBuf);
    status = RtlAppendUnicodeToString(&paramsPath, WINMALI_PARAMS_SUFFIX);
    if (!NT_SUCCESS(status)) {
        return STATUS_SUCCESS;
    }

    InitializeObjectAttributes(&oa, &paramsPath,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    status = ZwOpenKey(&hKey, KEY_READ, &oa);
    if (!NT_SUCCESS(status)) {
        /* No Parameters key = enabled (normal case). */
        return STATUS_SUCCESS;
    }

    RtlInitUnicodeString(&valueName, WINMALI_KILLSWITCH_VALUE);
    status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation,
                             valBuf, sizeof(valBuf), &resultLen);
    if (NT_SUCCESS(status)) {
        pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)valBuf;
        if (pInfo->Type == REG_DWORD && pInfo->DataLength == sizeof(ULONG)) {
            ULONG val;
            RtlCopyMemory(&val, pInfo->Data, sizeof(val));
            if (val != 0) {
                *Disabled = TRUE;
            }
        }
    }

    ZwClose(hKey);
    return STATUS_SUCCESS;
}
