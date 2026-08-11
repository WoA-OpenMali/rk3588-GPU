/*
 * WinMaliVersion.h
 *
 * Single source for the KMD file version, consumed by:
 *   - WinMaliKmd.rc  (VERSIONINFO FILEVERSION on WinMaliKmd.sys)
 *   - WinMaliDriver.c (DriverEntry banner, so kd shows which binary
 *     is loaded; __DATE__/__TIME__ are unavailable - the 26100 WDK
 *     compiles deterministic, so those macros don't expand)
 *
 * RULE: must stay equal to <Inf><TimeStamp> in WinMaliKmd.vcxproj
 * (the stamped INF DriverVer). dxgkrnl 26100 bugchecks device start
 * on any .sys-vs-INF version mismatch
 * (DpiFdoValidateKmdAndPnpVersionMatch, brk #0xF000). Bump all
 * together, forever.
 */
#pragma once

#define WINMALI_KMD_VERSION      1,0,0,85
#define WINMALI_KMD_VERSION_STR  "1.0.0.85"
