#define D3DXMatrixIdentity D3DXMatrixIdentity_DontUseHeaderDecl
// AnmVm module for th07 (Perfect Cherry Blossom).
//
// This translation unit implements the four AnmVm helper functions that
// the binary emits as standalone routines (the rest of AnmVm's behaviour
// lives in AnmManager). Each function's address, calling convention and
// exact field accesses were recovered from th07.exe via Ghidra:
//
//   AnmVm::AnmVm                FUN_00401170   __fastcall (this in ECX)
//   AnmVm::Initialize           FUN_004010f0   __fastcall (this in ECX)
//   AnmVm::ResetInterpTimers    FUN_004011b0   __fastcall (this in ECX)
//   D3DXMatrixIdentity          FUN_00401000   __fastcall (matrix in ECX)
//
// Note on D3DXMatrixIdentity: the th07 binary passes the matrix pointer
// in ECX (verified from FUN_004010f0 / FUN_0044e8e0 call sites and from
// FUN_00401000's prologue which saves ECX into [EBP-4] and never reads
// [EBP+8]). This differs from th06 where the same symbol is __cdecl.
// To emit the matching ECX-arg call we declare our own __fastcall
// D3DXMatrixIdentity here; the d3dx8math.h prototype is shadowed via a
// macro so there is no conflicting declaration.
//
// The struct layout is documented field-by-field in AnmVm.hpp; this file
// only touches the fields the binary touches, in the same order.

#include "AnmVm.hpp"

#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3d8.h>
#include <d3dx8math.h>

// Restore the real D3DXMatrixIdentity name (the macro at the top of this
// file shadowed the header's __cdecl declaration during inclusion).
#undef D3DXMatrixIdentity

#include <string.h>

// ---------------------------------------------------------------------------
// D3DXMatrixIdentity  (FUN_00401000)
//
// Statically-linked identity-matrix helper. The binary writes the 4x4
// identity directly via individual MOVs of 0.0f and 1.0f:
//   [ECX+0x00] = 1.0f, [ECX+0x14] = 1.0f, [ECX+0x28] = 1.0f, [ECX+0x3c] = 1.0f
//   every other element = 0.0f
//
// We assign the 16 matrix elements individually in the exact same order
// (12 zeros followed by the 4 diagonal ones) so MSVC /Od emits the same
// flat sequence of MOVs the binary has. The matrix pointer arrives in
// ECX (matches the binary).
// ---------------------------------------------------------------------------
D3DXMATRIX *__fastcall D3DXMatrixIdentity(D3DXMATRIX *pOut)
{
    pOut->_12 = 0.0f;
    pOut->_13 = 0.0f;
    pOut->_14 = 0.0f;
    pOut->_21 = 0.0f;
    pOut->_23 = 0.0f;
    pOut->_24 = 0.0f;
    pOut->_31 = 0.0f;
    pOut->_32 = 0.0f;
    pOut->_34 = 0.0f;
    pOut->_41 = 0.0f;
    pOut->_42 = 0.0f;
    pOut->_43 = 0.0f;
    pOut->_44 = 1.0f;
    pOut->_33 = 1.0f;
    pOut->_22 = 1.0f;
    pOut->_11 = 1.0f;
    return pOut;
}

namespace th07
{
// ---------------------------------------------------------------------------
// AnmVm::ResetInterpTimers  (FUN_004011b0)
//
// Resets the 12 ZunTimers AnmVm owns to the idle state
//   { previous = -999, subFrame = 0, current = 0 }.
//
// Binary layout (verified from the disassembly of FUN_004011b0):
//   +0x30  currentTimeInScript            (single reset)
//   +0x3c  unkTimer_3c                    (single reset)
//   +0x48  interpDuration[0..4]           (loop, 5 timers, stride 0xc)
//   +0x84  interpElapsed[0..4]            (loop, 5 timers, stride 0xc)
//
// The two 5-timer loops are emitted by MSVC as classic count-down
// while loops; we mirror that with explicit count-down loops so the
// branch shape matches.
// ---------------------------------------------------------------------------
#pragma optimize("s", on)
void AnmVm::ResetInterpTimers()
{
    ZunTimer *t;

    // +0x30 currentTimeInScript
    this->currentTimeInScript.current = 0;
    this->currentTimeInScript.previous = -999;
    this->currentTimeInScript.subFrame = 0;

    // +0x3c unkTimer_3c
    this->unkTimer_3c.current = 0;
    this->unkTimer_3c.previous = -999;
    this->unkTimer_3c.subFrame = 0;

    // +0x48 interpDuration[5]
    {
        i32 remaining = 5;
        t = &this->interpDuration[0];
        while (--remaining >= 0)
        {
            t->current = 0;
            t->previous = -999;
            t->subFrame = 0;
            t += 1;
        }
    }

    // +0x84 interpElapsed[5]
    {
        i32 remaining = 5;
        t = &this->interpElapsed[0];
        while (--remaining >= 0)
        {
            t->current = 0;
            t->previous = -999;
            t->subFrame = 0;
            t += 1;
        }
    }

    return;
}
#pragma optimize("s", off)

// ---------------------------------------------------------------------------
// AnmVm::AnmVm  (FUN_00401170)
//
//   1. ResetInterpTimers()       (FUN_004011b0)
//   2. memset(this, 0, 0x24c)    (REP STOSD, ECX=0x93 dwords)
//   3. this->activeSpriteIndex = -1   (MOV word [this+0x1d4], 0xffff)
//
// Note that the timer reset is overwritten by the immediately-following
// memset; that is the exact behaviour the binary emits and must be
// preserved for objdiff.
// ---------------------------------------------------------------------------
#pragma optimize("s", on)
AnmVm::AnmVm()
{
    this->ResetInterpTimers();
    memset(this, 0, sizeof(AnmVm));
    this->activeSpriteIndex = -1;
}
#pragma optimize("s", off)

// ---------------------------------------------------------------------------
// AnmVm::Initialize  (FUN_004010f0)
//
//   1. memset(this, 0, 0x1c8)               (REP STOSD, ECX=0x72 dwords)
//   2. this->scaleX = 1.0f                  ([this+0x18])
//   3. this->scaleY = 1.0f                  ([this+0x1c])
//   4. this->color = 0xffffffff             ([this+0x1b8], D3DCOLOR white)
//   5. D3DXMatrixIdentity(&this->matrix)    ([this+0xf8], FUN_00401000)
//   6. this->flags = ANM_VM_INITIAL_FLAGS   ([this+0x1c0], value 7)
//   7. currentTimeInScript reset:
//        current = 0                        ([this+0x38])
//        previous = -999                    ([this+0x30])
//        subFrame = 0                       ([this+0x34])
// ---------------------------------------------------------------------------
#pragma optimize("s", on)
void AnmVm::Initialize()
{
    memset(this, 0, 0x1c8);
    this->scaleX = 1.0f;
    this->scaleY = 1.0f;
    this->color = D3DCOLOR_RGBA(0xff, 0xff, 0xff, 0xff);
    D3DXMatrixIdentity(&this->matrix);
    this->flags = ANM_VM_INITIAL_FLAGS;
    this->currentTimeInScript.current = 0;
    this->currentTimeInScript.previous = -999;
    this->currentTimeInScript.subFrame = 0;
    return;
}
#pragma optimize("s", off)
}; // namespace th07
