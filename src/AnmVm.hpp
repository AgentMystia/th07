// AnmVm module for th07 (Perfect Cherry Blossom).
//
// All field offsets below were recovered directly from th07.exe via Ghidra
// by reading the four standalone AnmVm routines the binary emits:
//
//   D3DXMatrixIdentity          FUN_00401000   __fastcall (matrix in ECX)
//   AnmVm::Initialize           FUN_004010f0   __fastcall (this in ECX)
//   AnmVm::AnmVm                FUN_00401170   __fastcall (this in ECX)
//   AnmVm::ResetInterpTimers    FUN_004011b0   __fastcall (this in ECX)
//
// plus the AnmManager call sites that poke individual AnmVm fields
// (FUN_0044e8e0 SetActiveSprite, FUN_0044ea20 SetAndExecuteScript, and the
// big ANM interpreter FUN_00450d60 ExecuteScript).
//
// IMPORTANT (objdiff): the AnmVm struct must remain a *trivial* C++ type.
// th06's AnmVm embeds ZunTimer objects, but ZunTimer has a non-trivial
// constructor. If we embed ZunTimer inside AnmVm here, MSVC 7.0 will emit
// an extra `??_H` (vector constructor iterator) helper for AnmVm::AnmVm and
// the produced .text will not match the original, which treats the whole
// struct as raw bytes (it memsets it and writes fields by hand). Therefore
// every embedded timer is exposed as a trivial AnmVmTimer (i32/f32/i32) so
// the compiler does not inject any member-constructor glue.

#pragma once

#define D3DXMatrixIdentity D3DXMatrixIdentity_DontUseHeaderDecl

#include <d3d8.h>
#include <d3dx8math.h>

#include "ZunColor.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#undef D3DXMatrixIdentity

namespace th07
{
// Trivial { previous, subFrame, current } triple matching ZunTimer's byte
// layout (see ZunTimer.hpp: previous @0, subFrame @4, current @8, size 0xc).
// Kept trivial (no constructor) on purpose - see the file header note: a
// non-trivial member here would make MSVC emit a ??_H vector-constructor
// helper for AnmVm::AnmVm, which the original binary does not have.
struct AnmVmTimer
{
    i32 previous;
    f32 subFrame;
    i32 current;
};
ZUN_ASSERT_SIZE(AnmVmTimer, 0xc);

// AnmLoadedSprite (sprite descriptor owned by AnmManager). Forward-declared;
// the full definition lives in AnmManager.hpp. We only need the pointer type
// for the AnmVm::sprite field.
struct AnmLoadedSprite;

// AnmRawInstr (ANM script instruction). Matches th06's layout:
//   { i16 time; u8 opcode; u8 argsCount; u32 args[10]; }
// Forward-declared here; the full definition lives in AnmManager.hpp.
struct AnmRawInstr;

// AnmVm flags (u16 at offset +0x1c0). Bit layout recovered from
// ExecuteScript (FUN_00450d60) bit-tests and the AnmManager call sites.
#define AnmVmFlags_Visible (1 << 0)
#define AnmVmFlags_1 (1 << 1)
#define AnmVmFlags_PosDirty (1 << 2)
#define AnmVmFlags_ScaleDirty (1 << 3)
#define AnmVmFlags_UsePosOffset (1 << 4)
#define AnmVmFlags_FlipMode (1 << 7)
#define AnmVmFlags_BlendMode (1 << 8)
#define AnmVmFlags_ColorOp (1 << 9)
#define AnmVmFlags_ZWriteDisable (1 << 11)
#define AnmVmFlags_PosTime (1 << 12)
#define AnmVmFlags_IsStopped (1 << 13)

// Initial flags value written by AnmVm::Initialize (FUN_004010f0,
// MOV word [this+0x1c0], 0x7).
#define ANM_VM_INITIAL_FLAGS 0x7

// AnmVm (sprite VM). sizeof = 0x24c, verified via the REP STOSD count in
// AnmVm::AnmVm (FUN_00401170, ECX = 0x93 dwords = 0x24c bytes). Every
// offset below was cross-checked against the disassembly of the four
// functions this module ships and the AnmManager field accesses.
struct AnmVm
{
    AnmVm();
    void Initialize();
    // Reset the 12 interpolation / script timers to the idle state
    // (previous=-999, subFrame=0, current=0). Called by the constructor
    // before the memset and by the stage-menu reset paths.
    //
    // The binary returns `this` in EAX (FUN_004011b0 ends with
    // `MOV EAX,[EBP-0x24]`), so we declare the return type as AnmVm* and
    // `return this;` to reproduce that epilogue exactly.
    AnmVm *ResetInterpTimers();

    // +0x00 (3 f32) rotation. Written by ANM opcode SetRotationInternal.
    D3DXVECTOR3 rotation; // 0x00..0x0c
    // +0x0c (3 f32) angular velocity. Written by ANM opcode SetAngleVel.
    D3DXVECTOR3 angleVel; // 0x0c..0x18
    // +0x18 scaleX = 1.0f (FUN_004010f0 MOV [this+0x18],0x3f800000).
    f32 scaleX;
    // +0x1c scaleY = 1.0f (FUN_004010f0 MOV [this+0x1c],0x3f800000).
    f32 scaleY;
    // +0x20 (2 f32) scale interpolation final xy.
    D3DXVECTOR2 scaleInterpFinal;
    // +0x28 (2 f32) uv scroll position.
    D3DXVECTOR2 uvScrollPos;
    // +0x30 currentTimeInScript. Reset to {previous=-999,subFrame=0,current=0}
    // by both Initialize and ResetInterpTimers.
    AnmVmTimer currentTimeInScript;
    // +0x3c second script-time timer (reset in ResetInterpTimers at
    // 0x004011e2, ADD ECX,0x3c).
    AnmVmTimer unkTimer_3c;
    // +0x48 interpolation "duration" timers (5 channels). Reset in the first
    // count-5 / stride-0xc loop of ResetInterpTimers (start = this+0x48).
    AnmVmTimer interpDuration[5];
    // +0x84 interpolation "elapsed" timers (5 channels). Reset in the second
    // count-5 / stride-0xc loop of ResetInterpTimers (start = this+0x84).
    AnmVmTimer interpElapsed[5];
    // +0xc0..0xf8 raw gap (per-channel easing bytes + alignment padding).
    // Owned by AnmManager; kept as bytes so the matrix lands at +0xf8.
    u8 unkC0[0xf8 - 0xc0];
    // +0xf8 world matrix (4x4). FUN_004010f0 does
    // `ECX = this+0xf8; CALL D3DXMatrixIdentity`.
    D3DXMATRIX matrix;
    // +0x138..0x1b8 raw gap (cached sprite/uv transform + scratch fields
    // written by AnmManager::SetActiveSprite). Owned by AnmManager.
    u8 unk138[0x1b8 - 0x138];
    // +0x1b8 color (D3DCOLOR). FUN_004010f0 MOV [this+0x1b8],0xffffffff.
    ZunColor color;
    // +0x1bc..0x1c0 raw gap so the u16 flags sits at +0x1c0.
    u8 unk1bc[0x1c0 - 0x1bc];
    // +0x1c0 flags (u16). FUN_004010f0 MOV word [this+0x1c0],7.
    u16 flags;
    // +0x1c2..0x1d4 raw gap.
    u8 unk1c2[0x1d4 - 0x1c2];
    // +0x1d4 activeSpriteIndex (i16). FUN_00401170 MOV word [this+0x1d4],0xffff.
    i16 activeSpriteIndex;
    // +0x1d6 baseSpriteIndex (i16). Read by AnmManager::SetActiveSprite.
    i16 baseSpriteIndex;
    // +0x1d8 anmFileIndex (i16). Written by AnmManager::ExecuteAnmIdx.
    i16 anmFileIndex;
    // +0x1da..0x1dc raw gap (2 bytes) so the script pointer is 4-aligned.
    u8 unk1da[0x1dc - 0x1da];
    // +0x1dc beginningOfScript (pointer). Set by SetAndExecuteScript
    // (vm->beginningOfScript = beginingOfScript).
    AnmRawInstr *beginningOfScript;
    // +0x1e0 currentInstruction (pointer). Set by SetAndExecuteScript.
    AnmRawInstr *currentInstruction;
    // +0x1e4 sprite (pointer to the active AnmLoadedSprite). Set by
    // SetActiveSprite (vm->sprite = this->sprites + spriteIdx).
    AnmLoadedSprite *sprite;
    // +0x1e8..0x24c trailing state (interpolation initial/final values,
    // posOffset, posInterp timers, alphaInterp, font width/height, ...).
    // Owned by AnmManager; exposed as raw bytes so sizeof stays locked at
    // 0x24c.
    u8 unk1e8[0x24c - 0x1e8];
};
ZUN_ASSERT_SIZE(AnmVm, 0x24c);
}; // namespace th07
