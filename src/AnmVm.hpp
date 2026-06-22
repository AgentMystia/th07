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
//   bit  0  Visible           (DrawInner early-bail)
//   bit  1  InScope/Active    (DrawInner early-bail)
//   bit  2  RotMatrixDirty    (DrawInner rebuilds scratch from rotation)
//   bit  3  ScaleDirty        (DrawInner rebakes scaleX/Y into scratch diag)
//   bit  4  UsePosOffset
//   bit  7  FlipMode          (selects altPosOffset @ 0x230 vs posOffset @ 0x1c8;
//                              also toggles scale-X vs scale-Y blend-mode logic)
//   bit  8  BlendMode0        (2-bit field with bit 9; 0/1/2/3 blend modes)
//   bit  9  BlendMode1
//   bit 11  ZWriteDisable
//   bit 12  PosTimeFlag       (case 0x1f sets/clears)
//   bit 13  IsStopped
//   bit 14  UnkFlag_0x4000    (case 0x20 sets/clears)
//   bit 15  ScriptResetGate   (case 0x18 clears bit 0 to halt script)
// Also a high bit at 0x2000 (case 0x15 sets to mark "script interrupt armed").
#define AnmVmFlags_Visible (1 << 0)
#define AnmVmFlags_1 (1 << 1)
#define AnmVmFlags_PosDirty (1 << 2)
#define AnmVmFlags_ScaleDirty (1 << 3)
#define AnmVmFlags_UsePosOffset (1 << 4)
#define AnmVmFlags_FlipMode (1 << 7)
#define AnmVmFlags_BlendMode0 (1 << 8)
#define AnmVmFlags_BlendMode1 (1 << 9)
#define AnmVmFlags_ZWriteDisable (1 << 11)
#define AnmVmFlags_PosTime (1 << 12)
#define AnmVmFlags_IsStopped (1 << 13)
#define AnmVmFlags_Unk4000 (1 << 14)

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
    // +0xc0..0xc4 easingModes[5] (one byte per easing channel; 0=linear,
    // 1=quad, 2=cubic, 3=quart, 4=inv-quad, 5=inv-cubic, 6=inv-quart).
    // Written by cases 0x12/0x13/0x14 (channel 0), 0x21 (channel 0 reset),
    // 0x22 (channel 1), 0x23 (channel 2), 0x24 (channel 3), 0x25 (channel 4),
    // 0x10 (channel 2 interrupt-time). Read by the post-switch LAB_004538e2
    // per-channel loop (FUN_00450d60) at `*(u8*)(this + ch + 0xc0)`.
    u8 easingModes[5]; // +0xc0..+0xc5
    u8 unk_c5[0xf8 - 0xc5]; // +0xc5..+0xf8 opaque (timer scratch / angle acc)
    // +0xf8 world matrix (4x4). FUN_004010f0 does
    // `ECX = this+0xf8; CALL D3DXMatrixIdentity`.
    D3DXMATRIX matrix;
    // +0x138 scratch world-matrix copy (4x4). SetActiveSprite copies the 16
    // dwords from +0xf8 here and then bakes the per-axis scale into the
    // diagonal (m[0][0] *= scaleX at +0x138, m[1][1] *= scaleY at +0x14c).
    // DrawInner snapshots this for SetTransform(D3DTS_WORLD).
    D3DXMATRIX scratchMatrix;
    // +0x178 texture matrix (4x4). SetActiveSprite primes m[0][0] (+0x178)
    // and m[1][1] (+0x18c) with the per-sprite uvScale; DrawInner snapshots
    // this for SetTransform(D3DTS_TEXTURE0) and overwrites m[2][0]/m[2][1]
    // with the current uv scroll offset.
    D3DXMATRIX textureMatrix;
    // +0x1b8 color (D3DCOLOR). FUN_004010f0 MOV [this+0x1b8],0xffffffff.
    ZunColor color;
    // +0x1bc alternate colour (D3DCOLOR). Selected over `color` by
    // dword-flags bit 16 in SetRenderStateForVm / DrawInner.
    ZunColor altColor;
    // +0x1c0 flags (u16). FUN_004010f0 MOV word [this+0x1c0],7.
    // The 4 bytes +0x1c0..+0x1c3 are also read/written as a single dword
    // (treated as f32 bits) by ExecuteScript's bit-twiddling cases.
    u16 flags;                       // +0x1c0
    // +0x1c2 byte written by case 0x1a (MOV word [param_2+0x1c4]); this byte
    // at +0x1c4 (== param_2[0x71] as short, byte offset 0x1c4) is a per-VM
    // layer/priority id. +0x1c6 is the interrupt-target script id (i16,
    // written/read by the case-0x15 path and the top-of-function search loop).
    u8 unk1c2[0x1c4 - 0x1c2];        // +0x1c2..+0x1c4
    i16 layerId;                     // +0x1c4 (case 0x1a)
    i16 interruptTargetId;           // +0x1c6 (case 0x15 / loop head)
    // +0x1c8 positionOffset (3 f32 xyz). Written by cases 0xd/0x14/0x21/0x24
    // (rotation/position ops) and consumed by DrawInner. Case 7 writes the
    // FlipMode=0 variant here.
    f32 positionOffsetX;             // +0x1c8
    f32 positionOffsetY;             // +0x1cc
    f32 positionOffsetZ;             // +0x1d0
    // +0x1d4 activeSpriteIndex (i16). FUN_00401170 MOV word [this+0x1d4],0xffff.
    i16 activeSpriteIndex;           // +0x1d4
    // +0x1d6 baseSpriteIndex (i16). Read by AnmManager::SetActiveSprite and
    // ExecuteScript case 4 (sprite-offset addend).
    i16 baseSpriteIndex;             // +0x1d6
    // +0x1d8 anmFileIndex (i16). Written by AnmManager::ExecuteAnmIdx and read
    // by case 4 (param_2[0x76] = byte offset 0x1d8, used as spriteIndices idx).
    i16 anmFileIndex;                // +0x1d8
    // +0x1da..0x1dc raw gap (2 bytes) so the script pointer is 4-aligned.
    u8 unk1da[0x1dc - 0x1da];        // +0x1da..+0x1dc
    // +0x1dc beginningOfScript (pointer). Set by SetAndExecuteScript.
    AnmRawInstr *beginningOfScript;  // +0x1dc
    // +0x1e0 currentInstruction (pointer). Set by SetAndExecuteScript; the
    // switch in ExecuteScript reads it as `psVar1 = (short*)param_2[0x78]`.
    AnmRawInstr *currentInstruction; // +0x1e0
    // +0x1e4 sprite (pointer to the active AnmLoadedSprite).
    AnmLoadedSprite *sprite;         // +0x1e4
    // +0x1e8..+0x200 easing-channel-0 start xyz (cases 0x14/0x21 prime these
    // from the VM's current rotation; the per-frame loop interpolates between
    // these and the +0x1f4 end triple).
    f32 easeCh0StartX;               // +0x1e8
    f32 easeCh0StartY;               // +0x1ec
    f32 easeCh0StartZ;               // +0x1f0
    // +0x1f4..+0x200 easing-channel-0 end xyz.
    f32 easeCh0EndX;                 // +0x1f4
    f32 easeCh0EndY;                 // +0x1f8
    f32 easeCh0EndZ;                 // +0x1fc
    // +0x200..+0x20c easing-channel-3 start xyz (position channel; case 0x24).
    f32 easeCh3StartX;               // +0x200
    f32 easeCh3StartY;               // +0x204
    f32 easeCh3StartZ;               // +0x208
    // +0x20c..+0x218 easing-channel-3 end xyz.
    f32 easeCh3EndX;                 // +0x20c
    f32 easeCh3EndY;                 // +0x210
    f32 easeCh3EndZ;                 // +0x214
    // +0x218..+0x224 easing-channel-4 start xy + end xy (scale channel;
    // cases 0x1e/0x25). 2D because scale has no Z component.
    f32 easeCh4StartX;               // +0x218
    f32 easeCh4StartY;               // +0x21c
    f32 easeCh4EndX;                 // +0x220
    f32 easeCh4EndY;                 // +0x224
    // +0x228..+0x230 per-channel-1/2 byte mirrors (cases 0x22/0x23 copy colour
    // bytes / mode bytes here for the colour-cycling channels). Layout:
    //   +0x228 byte (case 0x22 mirrors vm->color byte 0 / case 1 writes here)
    //   +0x229 byte (case 0x22 mirrors vm->color byte 1)
    //   +0x22a byte (case 0x22 sets to a mode byte)
    //   +0x22b byte (cases 0x10/0x23 mirror vm->color byte 3 = alpha)
    //   +0x22c byte (case 0x22)
    //   +0x22d byte (case 0x22)
    //   +0x22e byte (cases 0x10/0x22/0x23 = interrupt mode)
    //   +0x22f byte (cases 0x10/0x22/0x23 = interrupt mode)
    u8 unk_228[0x230 - 0x228];       // +0x228..+0x230
    // +0x230..+0x23c altPosOffset (3 f32 xyz). Used when FlipMode flag is set;
    // case 7's FlipMode branch writes these instead of +0x1c8.
    f32 altPosOffsetX;               // +0x230
    f32 altPosOffsetY;               // +0x234
    f32 altPosOffsetZ;               // +0x238
    // +0x23c..0x24c trailing state. Case 4 sets this to the script-frame time
    // at sprite activation (param_2[0x8f] = param_2[0xe]).
    f32 spriteActivationTime;        // +0x23c
    u8 unk_240[0x24c - 0x240];       // +0x240..+0x24c trailing pad
};
ZUN_ASSERT_SIZE(AnmVm, 0x24c);
}; // namespace th07
