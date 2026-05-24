#pragma once

#include <wdm.h> 

// ---------------------------------------------------------------------------
// Program 0: NOP
// ---------------------------------------------------------------------------
static const UCHAR WinMaliShaderNop[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00
};

// ---------------------------------------------------------------------------
// Program 1: MOV.i32 r1, r2
// ---------------------------------------------------------------------------
// Source: MOV.i32 r1, r2
// Purpose: Exercise register-file read+write. Shader writes nothing to
//          memory; observable only via a post-run register readback or
//          when followed by a STORE.
// Cross-check: assembler-cases.txt line 1.
static const UCHAR WinMaliShaderMovR1R2[] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0xc1, 0x91, 0x00
};

// ---------------------------------------------------------------------------
// Program 2: IADD.u32 r0, r1, r2
// ---------------------------------------------------------------------------
// Source: IADD.u32 r0, r1, r2
// Purpose: Integer ALU path is alive. Computes r0 = r1 + r2.
// Cross-check: none (not in golden corpus) - produced locally by asm.py
// and byte-verified by you on 2026-04-20.
static const UCHAR WinMaliShaderIaddR0R1R2[] = {
    0x01, 0x02, 0x00, 0x00, 0x00, 0xc0, 0xa0, 0x00
};

// ---------------------------------------------------------------------------
// Program 3: STORE_CONSTANT (2 instructions, 16 bytes)
// ---------------------------------------------------------------------------
// Source:
//   IADD.u32 r4, r1, 0x7060504
//   STORE.i32.slot0.end @r4, r0^, offset:0
//
// Purpose: First program that produces an observable side effect.
//          Writes the constant (r1 + 0x7060504) to memory at the address
//          in r0 (low 32 bits of output-buffer GPU VA). With r1 seeded
//          to zero via the uniform file, the buffer should contain
//          0x07060504 after completion.
//
// NOTE on the immediate: Valhall inline u32 immediates are sign-extended
// from 28 bits; 0xDEADBEEF / 0xCAFEBABE are illegal inline - use
// IADD_IMM.i32 (program 4 style) for those. 0x07060504 is the canonical
// test pattern used throughout Mesa's test corpus.
static const UCHAR WinMaliShaderStoreConstant[] = {
    // IADD.u32 r4, r1, 0x7060504
    0x01, 0xc9, 0x00, 0x00, 0x00, 0xc4, 0xa0, 0x00,
    // STORE.i32.slot0.end @r4, r0^, offset:0
    0x40, 0x00, 0x00, 0x18, 0x02, 0x44, 0x61, 0x78
};

// ---------------------------------------------------------------------------
// Program 5: KERNEL_COMPUTE_STORE_CONST (2 instructions, 16 bytes)
// ---------------------------------------------------------------------------
// Source:
//   IADD_IMM.i32 r4, 0x0, #0x07060504
//   STORE.i32.slot0.end @r4, ^r48, offset:0
//
// Purpose: First program that runs as a *real* compute dispatch via a CSF
//          RUN_COMPUTE (opcode 4), not as a sub-stream of the trampoline.
//          Does not assume *any* register starts at zero — the value comes
//          from a 28-bit signed inline immediate (panvk's canonical pattern).
//          The output address comes from r48:r49, which is preloaded from
//          the FAU table when SPD.Preload bit 0 is set (R48..R49 vec2).
//
// Cross-check: round-trip-assembled with mesa/src/panfrost/compiler/valhall/
//              asm.py against the kernel test harness's source file
//              kernel_compute_store_const.va; bytes verified manually.
//
//   Byte 0..7 :  IADD_IMM.i32 r4, 0x0, #0x07060504
//   Byte 8..15:  STORE.i32.slot0.end @r4, ^r48, offset:0
//
// Why r48 (and not r0..r3)?
//   On Valhall, the "Preload" field of the Shader Program Descriptor
//   (v10.xml Preload struct) only addresses R48..R63: bits 0..6 control
//   vec2 preloads from FAU (bit 0 = R48-R49, ... bit 6 = R60-R61), and
//   bits 7..15 enable individual sysval registers (R55..R63). There is no
//   FAU-to-R0 path on Valhall; r0..r47 are scratch and start undefined.
//   So an output buffer address must arrive via the FAU in r48-r49.
//
// Expected runtime behavior:
//   1. Caller allocates a 4 KiB output buffer at GPU VA O.
//   2. Caller writes [O_lo, O_hi] (vec2 = 8 bytes) into the FAU table.
//   3. Caller programs SPD with stage=Compute, binary=this_shader_va,
//      preload.r48_r49 = 1.
//   4. Caller emits CSF: MOVE48 the SRT/FAU/SPD/TSD bases into the
//      register selects (SRT=r0, FAU=r8, SPD=r16, TSD=r24), MOVE32 the
//      workgroup config (r33..r39 = packed_size, offX..offZ, sizeX..sizeZ),
//      then RUN_COMPUTE.x_axis #1.
//   5. The shader writes 0x07060504 to *(uint32_t*)O. Verifiable from CPU.
//
static const UCHAR WinMaliShaderKernelComputeStoreConst[] = {
    // IADD_IMM.i32 r4, 0x0, #0x07060504
    0xC0, 0x04, 0x05, 0x06, 0x07, 0xC4, 0x10, 0x01,
    // STORE.i32.slot0.end @r4, ^r48, offset:0
    0x70, 0x00, 0x00, 0x18, 0x02, 0x44, 0x61, 0x78
};

// ---------------------------------------------------------------------------
// Program 4: LOAD_ADD_STORE (3 instructions, 24 bytes)
// ---------------------------------------------------------------------------
// Source:
//   LOAD.i32.unsigned.slot0.wait0 @r4, r0^, offset:0   ; r4 = *(u32*)r0
//   IADD_IMM.i32                  r4, r4^, #0x1        ; r4 = r4 + 1
//   STORE.i32.slot0.end           @r4, r2^, offset:0   ; *(u32*)r2 = r4
//
// Purpose: End-to-end memory round-trip. Seeds the harness's input
//          buffer with any value V, expects output buffer to hold V+1.
//          This is the first test that actually *requires* the MMU to
//          be programmed correctly for two distinct GPU VAs (input and
//          output). It is therefore the L5 canary test.
//
// NOTE on IADD_IMM vs IADD: IADD takes a sign-extended 28-bit inline
// immediate; tiny values like +1 are still legal there but the
// canonical Mesa form for arbitrary constants is IADD_IMM.i32 which
// encodes the full 32 bits in a second word.
static const UCHAR WinMaliShaderLoadAddStore[] = {
    // LOAD.i32.unsigned.slot0.wait0 @r4, r0^, offset:0
    0x40, 0x00, 0x00, 0x18, 0x82, 0x84, 0x60, 0x08,
    // IADD_IMM.i32 r4, r4^, #0x1
    0x44, 0x01, 0x00, 0x00, 0x00, 0xc4, 0x10, 0x01,
    // STORE.i32.slot0.end @r4, r2^, offset:0
    0x42, 0x00, 0x00, 0x18, 0x02, 0x44, 0x61, 0x78
};

