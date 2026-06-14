#define D3DXMatrixIdentity D3DXMatrixIdentity_DontUseHeaderDecl
// AnmVm module for th07 (Perfect Cherry Blossom).
//
// This translation unit implements the four standalone AnmVm helper
// routines that th07.exe emits. Every address, calling convention and
// field access was recovered from th07.exe via Ghidra:
//
//   D3DXMatrixIdentity          FUN_00401000   __fastcall (matrix in ECX)
//   AnmVm::Initialize           FUN_004010f0   __fastcall (this in ECX)
//   AnmVm::AnmVm                FUN_00401170   __fastcall (this in ECX)
//   AnmVm::ResetInterpTimers    FUN_004011b0   __fastcall (this in ECX)
//
// The matrix pointer and the AnmVm `this` pointer both arrive in ECX
// (verified from each function's prologue: ECX is spilled to a stack slot
// and [EBP+8] is never read). d3dx8math.h declares D3DXMatrixIdentity as
// __cdecl, so the macro at the very top of this file (and AnmVm.hpp)
// shadows that prototype while the headers are included; we undef it
// afterwards and declare our own __fastcall version, which makes the
// ECX-argument call site in Initialize emit the exact same `MOV ECX,this+0xf8`
// + `CALL D3DXMatrixIdentity` sequence the binary has.
//
// The AnmVm struct is intentionally trivial (see AnmVm.hpp): all embedded
// timers are AnmVmTimer (a plain {i32,f32,i32}), so MSVC does not emit a
// member-constructor `??_H` helper for AnmVm::AnmVm - matching the binary,
// which constructs the struct by hand (memset + ResetInterpTimers).

#include "AnmVm.hpp"

#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3d8.h>
#include <d3dx8math.h>

#undef D3DXMatrixIdentity

#include <string.h>

// ---------------------------------------------------------------------------
// D3DXMatrixIdentity  (FUN_00401000)
//
// Writes the 4x4 identity matrix by assigning each element in turn. The
// assignment order below mirrors the disassembly exactly:
//
//   0x38(_43)=0 0x34(_42)=0 0x30(_41)=0 0x2c(_34)=0 0x24(_32)=0 0x20(_31)=0
//   0x1c(_24)=0 0x18(_23)=0 0x10(_21)=0 0x0c(_14)=0 0x08(_13)=0 0x04(_12)=0
//   0x3c(_44)=1 0x28(_33)=1 0x14(_22)=1 0x00(_11)=1
//
// The off-diagonal zeros are written from the highest offset down, then the
// four diagonal ones from _44 down to _11. MSVC 7.0 with /Od emits each
// assignment as a single `MOV dword [reg+disp], imm`, so matching the source
// order reproduces the byte stream verbatim. The matrix pointer is taken in
// ECX (__fastcall, single argument).
// ---------------------------------------------------------------------------
D3DXMATRIX *__fastcall D3DXMatrixIdentity(D3DXMATRIX *pOut)
{
    pOut->_43 = 0.0f;
    pOut->_42 = 0.0f;
    pOut->_41 = 0.0f;
    pOut->_34 = 0.0f;
    pOut->_32 = 0.0f;
    pOut->_31 = 0.0f;
    pOut->_24 = 0.0f;
    pOut->_23 = 0.0f;
    pOut->_21 = 0.0f;
    pOut->_14 = 0.0f;
    pOut->_13 = 0.0f;
    pOut->_12 = 0.0f;
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
// Resets the 12 timers AnmVm owns to the idle state
//   { previous = -999, subFrame = 0, current = 0 }.
//
// The disassembly uses a fixed local-variable layout: two explicit pointer
// locals for the single timers (currentTimeInScript @ +0x30 and the second
// timer @ +0x3c), then two count-down while loops each carrying its own
// count, stride (=0xc) and pointer local. The stride is stored as an int
// and the loop pointer is a byte pointer so `ptr += stride` adds the raw
// byte offset. `#pragma var_order` pins the exact stack slots so MSVC's
// allocator does not reorder them.
//
// Stack slots (highest -> lowest, per var_order convention):
//   timerPtr1, timerPtr2, loop1Count, loop1Stride, loop1Ptr,
//   loop2Count, loop2Stride, loop2Ptr
//   <this spilled at EBP-0x24 by the prologue>
// ---------------------------------------------------------------------------
#pragma var_order(timerPtr1, timerPtr2, loop1Count, loop1Stride, loop1Ptr, \
                 loop2Count, loop2Stride, loop2Ptr)
AnmVm *AnmVm::ResetInterpTimers()
{
    AnmVmTimer *timerPtr1;
    AnmVmTimer *timerPtr2;
    i32 loop1Count;
    i32 loop1Stride;
    u8 *loop1Ptr;
    i32 loop2Count;
    i32 loop2Stride;
    u8 *loop2Ptr;

    // +0x30 currentTimeInScript
    timerPtr1 = &this->currentTimeInScript;
    timerPtr1->current = 0;
    timerPtr1->previous = -999;
    timerPtr1->subFrame = 0;

    // +0x3c unkTimer_3c
    timerPtr2 = &this->unkTimer_3c;
    timerPtr2->current = 0;
    timerPtr2->previous = -999;
    timerPtr2->subFrame = 0;

    // +0x48 interpDuration[5]
    loop1Count = 5;
    loop1Stride = 0xc;
    loop1Ptr = (u8 *)&this->interpDuration[0];
    while (--loop1Count >= 0)
    {
        ((AnmVmTimer *)loop1Ptr)->current = 0;
        ((AnmVmTimer *)loop1Ptr)->previous = -999;
        ((AnmVmTimer *)loop1Ptr)->subFrame = 0;
        loop1Ptr += loop1Stride;
    }

    // +0x84 interpElapsed[5]
    loop2Count = 5;
    loop2Stride = 0xc;
    loop2Ptr = (u8 *)&this->interpElapsed[0];
    while (--loop2Count >= 0)
    {
        ((AnmVmTimer *)loop2Ptr)->current = 0;
        ((AnmVmTimer *)loop2Ptr)->previous = -999;
        ((AnmVmTimer *)loop2Ptr)->subFrame = 0;
        loop2Ptr += loop2Stride;
    }

    return this;
}

// ---------------------------------------------------------------------------
// AnmVm::AnmVm  (FUN_00401170)
//
//   1. ResetInterpTimers()                 (CALL FUN_004011b0, ECX=this)
//   2. memset(this, 0, 0x24c)              (REP STOSD, ECX=0x93 dwords)
//   3. this->activeSpriteIndex = -1        (MOV word [this+0x1d4], 0xffff)
//
// The timer writes from step 1 are immediately overwritten by the memset in
// step 2; preserving this apparent redundancy is required for objdiff.
//
// The binary's prologue reserves 0x24 bytes of frame and spills `this` to
// [EBP-0x24]. The compiler does this even though only one local is live, so
// we add eight unused pad locals and pin the order with #pragma var_order to
// force MSVC into the same over-allocation.
// ---------------------------------------------------------------------------
#pragma var_order(pad0, pad1, pad2, pad3, pad4, pad5, pad6, pad7)
AnmVm::AnmVm()
{
    i32 pad0;
    i32 pad1;
    i32 pad2;
    i32 pad3;
    i32 pad4;
    i32 pad5;
    i32 pad6;
    i32 pad7;

    this->ResetInterpTimers();
    memset(this, 0, sizeof(AnmVm));
    this->activeSpriteIndex = -1;
}

// ---------------------------------------------------------------------------
// AnmVm::Initialize  (FUN_004010f0)
//
//   1. memset(this, 0, 0x1c8)              (REP STOSD, ECX=0x72 dwords)
//   2. scaleX = 1.0f                       ([this+0x18])
//   3. scaleY = 1.0f                       ([this+0x1c])
//   4. color  = 0xffffffff                 ([this+0x1b8])
//   5. D3DXMatrixIdentity(&matrix)         (ECX = this+0xf8; CALL FUN_00401000)
//   6. flags = ANM_VM_INITIAL_FLAGS        (word [this+0x1c0] = 7)
//   7. currentTimeInScript reset via a pointer local:
//        timerPtr = &this->currentTimeInScript;
//        timerPtr->current   = 0           ([timer+0x08])
//        timerPtr->previous  = -999        ([timer+0x00])
//        timerPtr->subFrame  = 0           ([timer+0x04])
//
// The disassembly pulls the timer pointer into [EBP-0x4]; we mirror that
// with a single local + #pragma var_order so the frame size (0x8) and slot
// assignment match. `this` is spilled by the prologue to [EBP-0x8].
// ---------------------------------------------------------------------------
#pragma var_order(timerPtr)
void AnmVm::Initialize()
{
    AnmVmTimer *timerPtr;

    memset(this, 0, 0x1c8);
    this->scaleX = 1.0f;
    this->scaleY = 1.0f;
    this->color = 0xffffffff;
    D3DXMatrixIdentity(&this->matrix);
    this->flags = ANM_VM_INITIAL_FLAGS;
    timerPtr = &this->currentTimeInScript;
    timerPtr->current = 0;
    timerPtr->previous = -999;
    timerPtr->subFrame = 0;
    return;
}
}; // namespace th07
