#pragma once

#include "WinMaliCommon.h"

typedef enum _WINMALI_PANKMOD_ESCAPE_OPCODE {
    WinMaliPanKmodOp_Invalid = 0,
    /* Panfrost JM submit uses the main escape enum: WinMaliEscapeOp_PanfrostSubmit (3). */
    /* Future: BO_CREATE, MMAP, WAIT_BO as separate opcodes if not folded into WDDM DDIs?  */
} WINMALI_PANKMOD_ESCAPE_OPCODE;
