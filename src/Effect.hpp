#pragma once

// th07 Effect structure. The layout is heavily rewritten from th06:
//   - Effect grew from 0x17c to 0x2d8 bytes (AnmVm is much larger, and a
//     trailing singly-linked "next" pointer was added for the new
//     per-frame draw buckets).
//   - The AnmVm occupies the leading 0x1b8 bytes of the structure
//     (color @ +0x1b8, flags @ +0x1c0, pos @ +0x1c8, the active anm
//     script index @ +0x1d8 as a u16).
//
// All offsets below were recovered from the th07.exe disassembly by reading
// the absolute byte displacements used by EffectManager's update / draw /
// spawn callbacks (see EffectManager.cpp for the cross-references).

#include "ZunColor.hpp"
#include "ZunTimer.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>

namespace th07
{
enum EffectCallbackResult
{
    // Returned by an effect's per-frame update callback (or by its one-shot
    // init callback). For an update callback, STOP releases the effect
    // (inUseFlag := 0), DONE keeps it alive. For an init callback, any
    // non-zero return refuses the spawn.
    EFFECT_CALLBACK_RESULT_STOP = 0,
    EFFECT_CALLBACK_RESULT_DONE = 1
};

struct Effect;

// Per-frame update callback. Stored in the effect at +0x2c8 and invoked once
// per frame by OnUpdate. The init half of an EffectInfo pair (see g_Effects)
// uses the same signature and runs once at spawn time.
typedef i32 (__fastcall *EffectUpdateCallback)(Effect *);

// 0x2d8-byte effect slot.
//
// Field names mirror th06 where the semantics survived the rewrite; new th07
// fields keep the ZUN-style unk_XXX naming using their byte offset within the
// structure so the mapping back to the binary stays auditable.
struct Effect
{
    // AnmVm front-end. The th07 AnmVm is 0x1b8 bytes; its color sits at
    // +0x1b8 (ZunColor), flags at +0x1c0, pos at +0x1c8, and the active
    // anm script index at +0x1d8 (u16). We treat the leading region as a
    // byte buffer because the full th07 AnmVm layout is owned by the
    // AnmManager module.
    u8 vm[0x1b8];
    ZunColor color;            // +0x1b8  (AnmVm tail)
    i32 vmPadding;             // +0x1bc
    u32 flags;                 // +0x1c0  (AnmVm flags; bit 12 = "additive")
    u8 vmFlagsPad[0x1c8 - 0x1c0 - 0x4];  // pad to align vmPos @ 0x1c8
    D3DXVECTOR3 vmPos;         // +0x1c8  (AnmVm pos, copied from pos1 in OnDraw)
    u8 vmPad2[0x1d8 - 0x1c8 - 0xc];
    u16 anmScriptIndex;        // +0x1d8
    u8 pad1[0x24c - 0x1da];
    D3DXVECTOR3 pos1;          // +0x24c  current world position (th06 pos1)
    D3DXVECTOR3 unk_258;       // +0x258  initial velocity
                               //         (th07 SpawnParticlesWithVelocity writes here)
    D3DXVECTOR3 velocity;      // +0x264  (th06 unk_11c)
    D3DXVECTOR3 acceleration;  // +0x270  (th06 unk_128)
    u8 pad3[0xc];  // 0x27c..0x287 (align position @ 0x288)
    D3DXVECTOR3 position;      // +0x288  anchor (th06 position)
    D3DXVECTOR3 pos2;          // +0x294  direction / axis (th06 pos2)
    D3DXQUATERNION quaternion; // +0x2a0  (th06 quaternion)
    f32 unk_2b0;               // +0x2b0  (th06 unk_15c, scale radius)
    f32 angleRelated;          // +0x2b4  (th06 angleRelated)
    ZunTimer timer;            // +0x2b8  (0xc bytes, th07 ZunTimer layout)
    i32 unk_2c4;               // +0x2c4  (th06 unk_170)
    EffectUpdateCallback updateCallback; // +0x2c8
    i8 inUseFlag;              // +0x2cc
    i8 effectId;               // +0x2cd
    i8 unk_2ce;                // +0x2ce  (th06 unk_17a, "fade-out requested")
    i8 unk_2cf;                // +0x2cf  (th06 unk_17b, fade frame counter)
    i8 drawListBucket;         // +0x2d0  0..3 -> which draw list (set at spawn)
    u8 pad4[3];                // +0x2d1
    struct Effect *next;       // +0x2d4  next link for per-frame draw lists
};
ZUN_ASSERT_SIZE(Effect, 0x2d8);

// Static descriptor for each particle kind: anm script + (update, init)
// callbacks. The init callback runs once at spawn (it can refuse the effect
// by returning non-zero); the update callback runs every frame.
struct EffectInfo
{
    i32 anmIdx;
    EffectUpdateCallback updateCallback;
    EffectUpdateCallback initCallback;
};
ZUN_ASSERT_SIZE(EffectInfo, 0xc);
}; // namespace th07
