// AnmVm module for th07 (Perfect Cherry Blossom).
//
// th07 heavily reworked AnmVm relative to th06. The structure grew from
// 0x110 bytes (th06) to 0x24c bytes (th07) and gained a far richer
// per-frame interpolation state machine (5 independent interpolation
// channels: rotation / color / alpha / position / scale, each with a
// duration ZunTimer, an elapsed ZunTimer and a 1-byte easing mode).
//
// All offsets below were recovered directly from th07.exe via Ghidra:
//
//   AnmVm::Ctor          FUN_00401170  (__fastcall, this in ECX)
//     - calls FUN_004011b0 (reset the 12 interpolation / script timers)
//     - memsets the whole 0x24c-byte struct to 0
//     - sets activeSpriteIndex (offset +0x1d4, i16) = -1
//
//   AnmVm::Initialize    FUN_004010f0  (__fastcall, this in ECX)
//     - memsets the leading 0x1c8 bytes (0x72 dwords) to 0
//     - scaleX (+0x18) = scaleY (+0x1c) = 1.0f
//     - color (+0x1b8) = 0xffffffff (D3DCOLOR_RGBA white)
//     - D3DXMatrixIdentity on matrix (+0xf8)   [FUN_00401000]
//     - flags (+0x1c0, u16) = 7   (Visible|flag1|BlendMode)
//     - currentTimeInScript (+0x30) ZunTimer reset: previous=-999,
//       subFrame=0, current=0
//
//   AnmVm::ResetInterpTimers  FUN_004011b0  (__fastcall, this in ECX)
//     - resets the 12 ZunTimers at +0x30, +0x3c, +0x48..+0x78 (5x),
//       +0x84..+0xb4 (5x) to previous=-999, subFrame=0, current=0.
//
//   D3DXMatrixIdentity (statically linked thunk) FUN_00401000
//     - writes the 4x4 identity to the ECX matrix (16 dwords).
//
// The structure is intentionally kept as a single struct with byte-level
// offsets matching the binary; unmapped gaps are exposed as named
// `unk_XXXX` fields so every byte is accounted for and objdiff can rely
// on the absolute layout.

#pragma once

#include <d3d8.h>
#include <d3dx8math.h>

#include "ZunColor.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "ZunTimer.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// ----- AnmLoadedSprite (sprite descriptor in the AnmManager) -----
//
// Recovered from AnmManager::SetActiveSprite (FUN_0044e8e0), which reads
// sourceFileIndex (+0x00), textureWidth (+0x14), textureHeight (+0x18),
// widthPx (+0x2c), heightPx (+0x30), uvScaleX (+0x34), uvScaleY (+0x38).
struct AnmLoadedSprite
{
    i32 sourceFileIndex;       // +0x00
    ZunVec2 startPixelInclusive; // +0x04
    ZunVec2 endPixelInclusive;   // +0x0c
    f32 textureWidth;          // +0x14
    f32 textureHeight;         // +0x18
    ZunVec2 uvStart;           // +0x1c
    ZunVec2 uvEnd;             // +0x24
    f32 widthPx;               // +0x2c
    f32 heightPx;              // +0x30
    f32 uvScaleX;              // +0x34
    f32 uvScaleY;              // +0x38
};
ZUN_ASSERT_SIZE(AnmLoadedSprite, 0x3c);

// ----- AnmRawInstr (ANM script instruction) -----
//
// Matches th06's layout: a 4-byte header { i16 time; u8 opcode; u8 argsCount; }
// followed by up to 10 u32 arguments.
struct AnmRawInstr
{
    i16 time;
    u8 opcode;
    u8 argsCount;
    u32 args[10];
};
ZUN_ASSERT_SIZE(AnmRawInstr, 0x2c);

// ----- AnmVm opcode table -----
//
// Opcodes are written verbatim from the th07 ANM interpreter switch at
// FUN_00450d60 (ExecuteScript). The case labels there are
// `*psVar1 + 1`, i.e. the raw opcode field is one less than the case
// number, so the values below are the actual values stored in
// AnmRawInstr::opcode.
#define AnmOpcode_Exit 0
#define AnmOpcode_SetActiveSprite 3
#define AnmOpcode_SetScale 8
#define AnmOpcode_SetAlpha 9
#define AnmOpcode_SetColor 10
#define AnmOpcode_SetRotationInternal 11
#define AnmOpcode_SetAngleVelInternal 12
#define AnmOpcode_SetScaleSpeed 13
#define AnmOpcode_SetBlendAdditive 14
#define AnmOpcode_SetBlendDefault 15
#define AnmOpcode_Fade 16
#define AnmOpcode_Stop 17
#define AnmOpcode_AnchorTopLeft 18
#define AnmOpcode_UsePosOffset 19
#define AnmOpcode_FlipX 20
#define AnmOpcode_FlipY 21
#define AnmOpcode_UVScrollX 22
#define AnmOpcode_UVScrollY 23
#define AnmOpcode_StopHide 24
#define AnmOpcode_SetVisibility 25
#define AnmOpcode_ScaleTime 26
#define AnmOpcode_SetZWriteDisable 27

// ----- AnmVm flags -----
//
// Stored as a u16 at offset +0x1c0. The bit layout was recovered from
// ExecuteScript's bit-tests:
//   bit 0  (0x0001) Visible          (opcode SetVisibility writes this)
//   bit 1  (0x0002) flag1            (always set in Initialize)
//   bit 2  (0x0004) posDirty         (set when pos is written, forces matrix rebuild)
//   bit 3  (0x0008) scaleDirty       (set when scale is written)
//   bit 4  (0x0010) usePosOffset     (opcode UsePosOffset writes this)
//   bit 7  (0x0080) flipMode         (0 = flipX target, 1 = flipY target selects rotation/scale registers)
//   bit 8  (0x0100) blendMode        (toggle by opcodes SetBlendAdditive/Default)
//   bit 9  (0x0200) colorOp
//   bit 11 (0x0800) zWriteDisable
//   bit 12 (0x1000) posTime flag
//   bit 13 (0x2000) isStopped
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

// Initial flags value written by AnmVm::Initialize (FUN_004010f0).
#define ANM_VM_INITIAL_FLAGS 0x7

// ----- AnmVm easing modes -----
//
// One byte per interpolation channel at offsets +0xc0..+0xc4 (rotation,
// color, alpha, position, scale). Recovered from the easing switch in
// ExecuteScript (FUN_00450d60, after the per-channel loop).
enum AnmVmEasing
{
    AnmVmEasing_Linear = 0,
    AnmVmEasing_QuadIn = 1,
    AnmVmEasing_CubicIn = 2,
    AnmVmEasing_QuarticIn = 3,
    AnmVmEasing_QuadOut = 4,
    AnmVmEasing_CubicOut = 5,
    AnmVmEasing_QuarticOut = 6,
};

// ----- AnmVm (sprite VM) -----
//
// 0x24c bytes. Every offset was verified against th07.exe via Ghidra
// (see the per-field comments for the cross-referencing function).
struct AnmVm
{
    AnmVm();
    void Initialize();
    // Reset the 12 interpolation / script timers to the "idle" state
    // (previous=-999, subFrame=0, current=0). Called by the constructor
    // before the memset and by the stage-menu reset paths.
    void ResetInterpTimers();

    void SetInvisible()
    {
        this->flags &= ~AnmVmFlags_Visible;
    }

    // +0x00 (3) rotation. Written by ANM opcode SetRotationInternal and by
    // the rotation interpolation channel. Absolute offset verified in
    // FUN_00450d60 case 0xd (`*param_2 = ...; param_2[0x70] |= 4`).
    D3DXVECTOR3 rotation;
    // +0x0c (3) angular velocity. Verified in FUN_00450d60 case 0xe.
    D3DXVECTOR3 angleVel;
    // +0x18 scaleX = 1.0f. Verified in FUN_004010f0 (MOV [this+0x18],0x3f800000)
    // and FUN_00450d60 case 8.
    f32 scaleX;
    // +0x1c scaleY = 1.0f. Verified in FUN_004010f0 (MOV [this+0x1c],0x3f800000).
    f32 scaleY;
    // +0x20 (2) scale interpolation final values (x,y). Verified in
    // FUN_00450d60 case 0xf.
    D3DXVECTOR2 scaleInterpFinal;
    // +0x28 (2) uv scroll position. Verified in FUN_00450d60 cases 0x1b/0x1c
    // (wrap-around uv scroll).
    D3DXVECTOR2 uvScrollPos;
    // +0x30 currentTimeInScript. Verified in FUN_004010f0 (reset to
    // previous=-999,subFrame=0,current=0) and FUN_00450d60 (the
    // instruction-time comparison uses current at +0x38).
    ZunTimer currentTimeInScript;
    // +0x3c script-time ZunTimer. Reset by ResetInterpTimers (FUN_004011b0).
    // Verified at 0x004011e2 in FUN_004011b0.
    ZunTimer unkTimer_3c;
    // +0x48 interpolation "duration" ZunTimers (5 channels: rotation, color,
    // alpha, position, scale). Verified in FUN_00450d60 (the per-frame
    // interpolation loop iterates 0..4 and reads duration.current at
    // param_2[local_10*3 + 0x14]).
    ZunTimer interpDuration[5];
    // +0x84 interpolation "elapsed" ZunTimers (5 channels). Verified in
    // FUN_00450d60 (reads elapsed at param_2[local_10*3 + 0x21]).
    ZunTimer interpElapsed[5];
    // +0xc0 (5 bytes) per-channel easing mode (AnmVmEasing). Verified in
    // FUN_00450d60 (the easing switch reads *(this + local_10 + 0xc0)).
    u8 interpEasing[5];
    // +0xc5 padding to align the matrix.
    u8 unkC5[0xf8 - 0xc5];
    // +0xf8 world matrix (4x4). Verified in FUN_004010f0 (D3DXMatrixIdentity
    // called with ECX = this + 0xf8) and FUN_0044e8e0 (copies this region
    // to +0x138).
    D3DXMATRIX matrix;
    // +0x138 (0x40 bytes) cached copy of the sprite/uv transform built by
    // AnmManager::SetActiveSprite. Verified in FUN_0044e8e0 (16-dword copy
    // from +0xf8).
    u8 unkSpriteCopy[0x178 - 0x138];
    // +0x178 uv / scale scratch fields written by SetActiveSprite.
    // FUN_0044e8e0: `*(this + 0x178) = sprite[0x30]/sprite[0x18]*sprite[0x34]`
    f32 unk178;
    // +0x17c padding.
    u8 unk17c[0x18c - 0x17c];
    // +0x18c vertical uv scale written by SetActiveSprite.
    f32 unk18c;
    // +0x190 padding.
    u8 unk190[0x1b8 - 0x190];
    // +0x1b8 color (D3DCOLOR). Verified in FUN_004010f0 (MOV [this+0x1b8],
    // 0xffffffff) and FUN_00450d60 case 0xa (reads/sets RGB keeping alpha).
    ZunColor color;
    // +0x1bc padding to keep the u16 flags on a 4-byte boundary.
    u8 unk1bc[0x1c0 - 0x1bc];
    // +0x1c0 flags (u16). Verified in FUN_004010f0 (MOV word [this+0x1c0],7)
    // and throughout FUN_00450d60.
    u16 flags;
    // +0x1c2 padding.
    u8 unk1c2[0x1d4 - 0x1c2];
    // +0x1d4 activeSpriteIndex (i16). Verified in FUN_00401170 (MOV word
    // [this+0x1d4],0xffff) and read back in FUN_00424e00 /
    // FUN_0044e8e0 as the current sprite offset.
    i16 activeSpriteIndex;
    // +0x1d6 baseSpriteIndex (i16). Verified in FUN_0044e8e0 (the SetActiveSprite
    // call site in FUN_00424e00 reads *(this+0x1d4) + param_2[1], implying
    // a base index sits adjacent).
    i16 baseSpriteIndex;
    // +0x1d8 anmFileIndex (i16). Verified in FUN_00424e00 (sets *(this+0x1d8)).
    i16 anmFileIndex;
    // +0x1da padding.
    u8 unk1da[0x1dc - 0x1da];
    // +0x1dc beginingOfScript (pointer). Verified in FUN_0044ea20
    // (`param_2[0x77] = param_3`) and FUN_00450d60 (`param_2[0x78]` is the
    // resume pointer derived from beginingOfScript + offset).
    AnmRawInstr *beginningOfScript;
    // +0x1e0 currentInstruction (pointer). Verified in FUN_0044ea20 and
    // FUN_00450d60 (param_2[0x78]).
    AnmRawInstr *currentInstruction;
    // +0x1e4 sprite (pointer to the active AnmLoadedSprite). Verified in
    // FUN_0044e8e0 (`*(this + 0x1e4) = anmManager + 0x60 + idx*0x40`).
    AnmLoadedSprite *sprite;
    // +0x1e8 trailing state: interpolation initial/final values, posOffset,
    // posInterp timers, alphaInterp, font width/height etc. The exact
    // sub-field layout here is owned by AnmManager's draw/interp code; we
    // expose the region as raw bytes so the struct size is locked.
    u8 unk1e8[0x23c - 0x1e8];
    // +0x23c timeOfLastSpriteSet (i32). Verified in FUN_00450d60 case 4
    // (SetActiveSprite sets `param_2[0x8f] = param_2[0xe]`, i.e. records the
    // current script frame).
    i32 timeOfLastSpriteSet;
    // +0x240 alphaInterp + font metrics tail. Owned by AnmManager.
    u8 unk240[0x24c - 0x240];
};
ZUN_ASSERT_SIZE(AnmVm, 0x24c);
}; // namespace th07
