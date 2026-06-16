#pragma once

// th07 EffectManager.
//
// Compared to th06, ZUN substantially rewrote this module:
//   - The effect pool was reshaped: 409 Effect slots (vs. th06's 513),
//     with SpawnParticles round-robin limited to the first 400 and OnUpdate
//     walking all 408 live slots (the 409th is the sentinel returned when
//     the pool is exhausted, matching th06's "effects[512]" sentinel).
//   - Effects are now threaded through four singly-linked draw buckets
//     rebuilt every frame by OnUpdate, instead of th06's flat array scan
//     in OnDraw. The bucket is selected from a one-byte field set at spawn
//     time (Effect::drawListBucket) plus an AnmVm flag bit for the bucket-0
//     split.
//   - Each EffectInfo gained a second "init" callback (run once at spawn).
//   - The g_Effects table grew from 20 entries (th06) to 34 (th07), adding
//     Cherry-system / graze / spell-card background particle kinds.
//   - OnUpdate now also reports a frame counter modulo 300 == 100 fast-path
//     that re-arms a supervisor tick (returning CHAIN_CALLBACK_RESULT_...).
//
// All addresses in this module come from th07.exe (see the per-function
// cross-references in EffectManager.cpp).

#include "Chain.hpp"
#include "Effect.hpp"
#include "ZunColor.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>

namespace th07
{
// Effect kinds. th07 ships 34 entries in g_Effects; the names below follow
// th06's ParticleEffects enum for the kinds that survived, plus TH07_*
// placeholders for the new ones (their semantic names live with the
// Supervisor / Cherry modules and are deliberately left generic here).
enum ParticleEffects
{
    PARTICLE_EFFECT_UNK_0,
    PARTICLE_EFFECT_UNK_1,
    PARTICLE_EFFECT_UNK_2,
    PARTICLE_EFFECT_RANDOM_SPLASH_BIG, // idx 3
    PARTICLE_EFFECT_4,
    PARTICLE_EFFECT_5,
    PARTICLE_EFFECT_6,
    PARTICLE_EFFECT_7,
    PARTICLE_EFFECT_8,
    PARTICLE_EFFECT_9,
    PARTICLE_EFFECT_10,
    PARTICLE_EFFECT_11,
    PARTICLE_EFFECT_UNK_12,
    PARTICLE_EFFECT_UPDATE_CALLBACK_4, // idx 13
    PARTICLE_EFFECT_UPDATE_CALLBACK_4_B, // idx 14
    PARTICLE_EFFECT_UPDATE_CALLBACK_4_C, // idx 15
    PARTICLE_EFFECT_SPELLCARD_BACKGROUND, // idx 16
    PARTICLE_EFFECT_ATTRACT,           // idx 17
    PARTICLE_EFFECT_ATTRACT_SLOW,      // idx 18
    PARTICLE_EFFECT_TH07_19,           // idx 19  (cb1 = SpawnParticleAt-self)
    PARTICLE_EFFECT_TH07_20,           // idx 20
    PARTICLE_EFFECT_TH07_21,           // idx 21
    PARTICLE_EFFECT_TH07_22,           // idx 22
    PARTICLE_EFFECT_TH07_23,           // idx 23
    PARTICLE_EFFECT_TH07_24,           // idx 24
    PARTICLE_EFFECT_TH07_25,           // idx 25
    PARTICLE_EFFECT_TH07_26,           // idx 26
    PARTICLE_EFFECT_TH07_27,           // idx 27
    PARTICLE_EFFECT_TH07_28,           // idx 28
    PARTICLE_EFFECT_TH07_29,           // idx 29
    PARTICLE_EFFECT_TH07_30,           // idx 30
    PARTICLE_EFFECT_TH07_31,           // idx 31
    PARTICLE_EFFECT_TH07_32,           // idx 32
    PARTICLE_EFFECT_TH07_33,           // idx 33  (cb1 = EffectCallbackAttract)
    PARTICLE_EFFECT_COUNT,
};

// Total Effect slot count in EffectManager::effects. 409 = 408 live + 1
// sentinel. (th06 had 512 live + 1 sentinel.)
#define TH07_EFFECT_COUNT 409
// OnUpdate scans this many slots per frame (= 0x198).
#define TH07_EFFECT_UPDATE_SCAN_COUNT 408
// SpawnParticles round-robins over the first this many slots (= 0x190).
#define TH07_EFFECT_SPAWN_SCAN_COUNT 400

struct EffectManager
{
    i32 nextIndex;       // +0x00 round-robin cursor for SpawnParticles (0..399)
    i32 pad0;            // +0x04
    i32 activeEffects;   // +0x08 recounted every frame by OnUpdate
    u8 pad1[0x1c - 0x0c];
    Effect effects[TH07_EFFECT_COUNT]; // +0x1c (409 slots; [408] is sentinel)

    // Draw buckets. Each bucket is a singly-linked list whose head lives in
    // sentinelN.next (i.e. the byte at sentinelOffset + 0x2d4); OnUpdate
    // rebuilds the lists every frame and OnDraw walks them.
    //
    //   bucket 0 -> drawListBucket == 0 AND (flags & 0x10) == 0
    //   bucket 1 -> drawListBucket == 1 OR == 3            (additive, screen-offset)
    //   bucket 2 -> drawListBucket == 2                    (plain Draw3, no offset)
    //   bucket 3 -> drawListBucket == other                (additive, screen-offset)
    //   bucket 4 -> drawListBucket == 0 AND (flags & 0x10) != 0  (additive, screen-offset)
    Effect sentinel1;        // +0x48b34 (head lives at +0x48e08)
    Effect sentinel2;        // +0x48e0c (head lives at +0x490e0)
    Effect sentinel3;        // +0x490e4 (head lives at +0x493b8)
    Effect sentinel4;        // +0x493bc (head lives at +0x49690)
    Effect *drawListTails[4]; // +0x49694 (bucket0..3 tail pointers)
    i32 frameCounter;        // +0x496a4 incremented every OnUpdate

    EffectManager();

    static ZunResult RegisterChain();
    static void CutChain();
    static ChainCallbackResult __fastcall OnUpdate(EffectManager *mgr);
    static ChainCallbackResult __fastcall OnDraw(EffectManager *mgr);
    static ZunResult __fastcall AddedCallback(EffectManager *mgr);
    static ZunResult __fastcall DeletedCallback(EffectManager *mgr);

    static i32 __fastcall EffectCallbackRandomSplashInit(Effect *effect);
    static i32 __fastcall EffectCallbackRandomSplashBigInit(Effect *effect);
    static i32 __fastcall EffectCallbackAttractInit(Effect *effect);
    static i32 __fastcall EffectUpdateCallback4Init(Effect *effect);

    static i32 __fastcall EffectCallbackStill(Effect *effect);
    static i32 __fastcall EffectUpdateCallback4(Effect *effect);
    static i32 __fastcall EffectCallbackAttract(Effect *effect);
    static i32 __fastcall EffectCallbackAttractSlow(Effect *effect);

    void Reset();

    Effect *__fastcall SpawnParticles(i32 effectIdx, D3DXVECTOR3 *pos, i32 count, ZunColor color);
    Effect *__fastcall SpawnParticlesWithVelocity(i32 effectIdx, D3DXVECTOR3 *pos, D3DXVECTOR3 *velocity,
                                                  i32 count, ZunColor color);
    Effect *__fastcall SpawnParticleAt(i32 effectIdx, D3DXVECTOR3 *pos, i32 slot, i32 unused, ZunColor color);
};
ZUN_ASSERT_SIZE(EffectManager, 0x496a8);

DIFFABLE_EXTERN(EffectManager, g_EffectManager);

// Adjacent externals owned by other modules but written by AddedCallback /
// the stage loader. They sit right after g_EffectManager in .bss.
//   g_EffectAnmFileCount (0x01348020)  number of eff0X.anm files for the stage
//   g_EffectAnmBaseIdx   (0x01348024)  ordinal base added to per-stage eff ids
extern i32 g_EffectAnmFileCount;
extern i32 g_EffectAnmBaseIdx;

// g_Effects: 34 EffectInfo descriptors. Defined in EffectManager.cpp.
DIFFABLE_EXTERN_ARRAY(EffectInfo, PARTICLE_EFFECT_COUNT, g_Effects);

// ---------- Constants for AnmManager / Supervisor integration ---------------
// These live in modules not yet landed (AnmManager, Supervisor, GameManager,
// ChainPriorities). Forward-declared here as raw constants / externs so this
// translation unit compiles standalone; once the owning modules land they
// should replace these.

// ANM file id for the stage's primary eff0X.anm (== 0x11 in th07's anm file
// enum). The +1/+2/+3 ids are the auxiliary eff04b/eff06/eff07/eff08 slots.
#define ANM_FILE_EFFECTS 0x11
// Base offset added to the eff0X.anm's anm script indices (== 0x2dc).
#define ANM_OFFSET_EFFECTS 0x2dc

// Chain priorities (see ChainPriorities.hpp once landed).
#define TH_CHAIN_PRIO_CALC_EFFECTMANAGER 0xb
#define TH_CHAIN_PRIO_DRAW_EFFECTMANAGER 0x9

// Screen draw offset applied to additive effect buckets in OnDraw. Lives in
// Supervisor .data at 0x0062f864 / 0x0062f868.
extern f32 g_EffectDrawOffsetX;
extern f32 g_EffectDrawOffsetY;
}; // namespace th07
