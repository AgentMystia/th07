// th07 EffectManager  see EffectManager.hpp for the high-level rewrite
// notes. Every address / constant in this file was read out of th07.exe via
// Ghidra; the per-function disassembly cross-references are noted inline.
//
// Several dependencies (AnmManager, Supervisor, GameManager, ChainPriorities,
// the full AnmVm layout) are not yet landed in this repo. They are forward-
// declared as extern / raw helpers below so this translation unit compiles
// standalone; once the owning modules land they should displace the stubs.

#include "EffectManager.hpp"

#include "Chain.hpp"
#include "Rng.hpp"
#include "ZunColor.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>
#include <math.h>
#include <string.h>

namespace th07
{
// ---------------------------------------------------------------------------
// Forward declarations for cross-module dependencies. These match the th07
// binary's calling conventions; bodies live in AnmManager / Supervisor /
// GameManager once those modules land.
// ---------------------------------------------------------------------------

// AnmManager. The th07 global is at 0x004b9e44. We only touch a few methods
// here: SetAndExecuteScriptIdx (sets the active script and runs its first
// interrupt), ExecuteScript (per-frame tick), Draw3 (additive draw),
// Draw3NoOffset (FUN_00450130, plain Draw3 with no screen offset) and
// LoadAnm / ReleaseAnm. The anm script-offset table lives at offset
// +0x28ef0 inside AnmManager (g_AnmManager->scriptOffsets[anmIdx]).
//
// All methods are __thiscall (this in ECX); we declare them as inline
// wrappers that go through the raw g_AnmManager global. When the real
// AnmManager class lands, delete these and call them directly.
struct AnmManager
{
    u8 pad[0x28ef0];
    void *scriptOffsets[1];
    // __thiscall methods (ECX=this=g_AnmManager). Member decls so MSVC emits
    // mov ecx,[g_AnmManager]; push args; call matching orig.
    void __fastcall SetAndExecuteScriptIdx(void *anmVm, i32 scriptOffset);
    i32 __fastcall ExecuteScript(void *anmVm);
    void __fastcall Draw3(void *anmVm);
    void __fastcall Draw3NoOffset(void *anmVm);
    void ReleaseAnm(i32 fileId);  // stack arg (not __fastcall)
};
extern AnmManager *g_AnmManager;

// Real AnmManager entrypoints (defined in AnmManager.cpp once that module
// lands). Signatures match the th07 binary:
//   SetAndExecuteScriptIdx  @ 0x0044ea20  __thiscall(this, AnmVm*, i32 scriptOffset)
//   ExecuteScript           @ 0x00450d60  __thiscall(this, AnmVm*) -> i32
//   Draw3                   @ 0x0044f9a0  __thiscall(this, AnmVm*)
//   Draw3NoOffset           @ 0x00450130  __thiscall(this, AnmVm*)
//   LoadAnm                 @ 0x0044df90  __fastcall(i32 fileId, char* path, i32 baseOffset) -> ZunResult
//   ReleaseAnm              @ 0x0044e4e0  __fastcall(i32 fileId)
//
// Until AnmManager lands, this translation unit references them as
// undefined externs (the build will link once AnmManager.cpp provides them).
void __fastcall Anm_SetAndExecuteScriptIdx(AnmManager *mgr, void *anmVm, i32 scriptOffset);
i32 __fastcall Anm_ExecuteScript(AnmManager *mgr, void *anmVm);
void __fastcall Anm_Draw3(AnmManager *mgr, void *anmVm);
void __fastcall Anm_Draw3NoOffset(AnmManager *mgr, void *anmVm);
ZunResult __fastcall Anm_LoadAnm(i32 fileId, const char *path, i32 baseOffset);
void __fastcall Anm_ReleaseAnm(i32 fileId);

// Supervisor. effectiveFramerateMultiplier lives at 0x00575ac8; TickTimer is
// the per-frame integrator used by ZunTimer::Tick; the "somePulseFlag"
// helper (FUN_00404fe0) gates OnUpdate's modulo-300 fast-exit.
extern f32 g_Supervisor_effectiveFramerateMultiplier;
extern "C" u8 g_EffectMgrPlayModeA;  // 0x575a8c playModeA
void __fastcall Supervisor_TickTimer(i32 *current, f32 *subFrame);
i32 __fastcall Supervisor_SomePulseFlag(void);

// GameManager. currentStage picks the eff0X.anm file in AddedCallback.
extern "C" i32 g_GameManager_currentStage;

// ---------------------------------------------------------------------------
// Globals owned by this module.
// ---------------------------------------------------------------------------

DIFFABLE_STATIC(EffectManager, g_EffectManager);

DIFFABLE_STATIC(ChainElem, g_EffectManagerCalcChain);
DIFFABLE_STATIC(ChainElem, g_EffectManagerDrawChain);

// g_EffectAnmFileCount / g_EffectAnmBaseIdx live just after g_EffectManager
// in .bss (0x01348020 / 0x01348024) and are also written by the stage
// loader (FUN_00429a4f). They are owned by Supervisor/FileSystem; we only
// reference them here (declared extern in the header).
//
// g_EffectDrawOffsetX / g_EffectDrawOffsetY live in Supervisor .data
// (0x0062f864 / 0x0062f868); same deal.

// g_Effects: 34 EffectInfo descriptors (vs. th06's 20). Recovered from
// th07.exe @ 0x0049efc0 (192 bytes for the first 16 entries) plus the
// continuation @ 0x0049f080. The 17,18 / 33 entries carry EffectCallbackAttract
// / AttractSlow as their update callback; 13..15 carry EffectUpdateCallback4.
//
// Each entry is { i32 anmIdx; EffectUpdateCallback update; EffectUpdateCallback init; }.
//
// Note: forward-declaring the not-yet-landed callbacks (0x0041aef0 etc.) as
// raw addresses via a cast. They are part of the Cherry / graze subsystem
// and will be named when those modules land.

// The g_Effects table references the static callback members unqualified;
// alias them to their qualified names so the table initializer resolves.
#define EffectCallbackStill             (EffectManager::EffectCallbackStill)
#define EffectCallbackRandomSplashInit  (EffectManager::EffectCallbackRandomSplashInit)
#define EffectCallbackRandomSplashBigInit (EffectManager::EffectCallbackRandomSplashBigInit)
#define EffectUpdateCallback4Init       (EffectManager::EffectUpdateCallback4Init)
#define EffectUpdateCallback4           (EffectManager::EffectUpdateCallback4)
#define EffectCallbackAttractInit       (EffectManager::EffectCallbackAttractInit)
#define EffectCallbackAttract           (EffectManager::EffectCallbackAttract)
#define EffectCallbackAttractSlow       (EffectManager::EffectCallbackAttractSlow)

DIFFABLE_STATIC_ARRAY_ASSIGN(EffectInfo, PARTICLE_EFFECT_COUNT, g_Effects) = {
    {0x2ab, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 0
    {0x2ac, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 1
    {0x2ad, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 2
    {0x2ae, EffectCallbackStill, EffectCallbackRandomSplashBigInit},              // 3
    {0x2b3, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 4
    {0x2b4, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 5
    {0x2b5, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 6
    {0x2b6, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 7
    {0x2b7, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 8
    {0x2b8, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 9
    {0x2b9, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 10
    {0x2ba, EffectCallbackStill, EffectCallbackRandomSplashInit},                 // 11
    {0x2bb, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 12
    {0x2bc, EffectUpdateCallback4, EffectUpdateCallback4Init},                    // 13
    {0x2bc, EffectUpdateCallback4, EffectUpdateCallback4Init},                    // 14
    {0x2bc, EffectUpdateCallback4, EffectUpdateCallback4Init},                    // 15
    {0x2dc, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 16  spellcard bg
    {0x2af, EffectCallbackAttract, EffectCallbackAttractInit},                    // 17
    {0x2b0, EffectCallbackAttractSlow, EffectCallbackAttractInit},                 // 18
    {0x2bd, (EffectUpdateCallback)0x0041c1b0, (EffectUpdateCallback)0},                              // 19  (spawn-self cb)
    {0x2bf, (EffectUpdateCallback)0x0041aef0, (EffectUpdateCallback)0x0041b0b0},   // 20
    {0x2c3, (EffectUpdateCallback)0, (EffectUpdateCallback)0},                                                          // 21
    {0x2c0, (EffectUpdateCallback)0x0041bfd0, (EffectUpdateCallback)0x0041bec0},   // 22
    {0x304, (EffectUpdateCallback)0x0041c100, (EffectUpdateCallback)0},                              // 23
    {0x2c2, (EffectUpdateCallback)0x0041abe0, (EffectUpdateCallback)0},                              // 24
    //{0x2da, (EffectUpdateCallback)0x0041c1b0, (EffectUpdateCallback)0},                              // 25
    //{0x2bf, (EffectUpdateCallback)0x0041aef0, (EffectUpdateCallback)0x0041b4a0},   // 26
    //{0x2bf, (EffectUpdateCallback)0x0041aef0, (EffectUpdateCallback)0x0041b770},   // 27
    //{0x2db, (EffectUpdateCallback)0x0041c1b0, (EffectUpdateCallback)0},                              // 28
    //{0x2b2, (EffectUpdateCallback)0x0041ad10, EffectCallbackAttractInit},          // 29
    //{0x2bf, (EffectUpdateCallback)0x0041aef0, (EffectUpdateCallback)0x0041b9f0},   // 30
    //{0x2bf, (EffectUpdateCallback)0x0041aef0, (EffectUpdateCallback)0x0041bc20},   // 31
    //{0x2c1, (EffectUpdateCallback)0x0041bfd0, (EffectUpdateCallback)0x0041bec0},   // 32
    //{0x2b1, EffectCallbackAttract, EffectCallbackAttractInit},                    // 33
};

#undef EffectCallbackStill
#undef EffectCallbackRandomSplashInit
#undef EffectCallbackRandomSplashBigInit
#undef EffectUpdateCallback4Init
#undef EffectUpdateCallback4
#undef EffectCallbackAttractInit
#undef EffectCallbackAttract
#undef EffectCallbackAttractSlow

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

EffectManager::EffectManager()
{
    this->Reset();
}

// Reset: memset(this, 0, sizeof). Implemented in th07 as a rep stosd over
// 0x125aa dwords (= 0x496a8 bytes = sizeof(EffectManager)). FUN_0041a350.
void EffectManager::Reset()
{
    memset(this, 0, sizeof(*this));
}

// ---------------------------------------------------------------------------
// Per-frame update callbacks. Each takes a single Effect* in ECX and returns
// EFFECT_CALLBACK_RESULT_DONE (1) to keep the effect alive, or
// EFFECT_CALLBACK_RESULT_STOP (0) to release it.
//
// th07 ditches th06's `g_Supervisor.effectiveFramerateMultiplier` scaling
// inside the per-frame callbacks  the integration is folded into the init
// callbacks now. The arithmetic matches th06 otherwise.
// ---------------------------------------------------------------------------

// EffectCallbackStill: linear motion + acceleration. th06's EffectCallbackStill
// is identical apart from the multiplier. FUN_0041a4f0.
i32 __fastcall EffectManager::EffectCallbackStill(Effect *effect)
{
    effect->pos1 += effect->velocity;
    effect->velocity += effect->acceleration;

    return EFFECT_CALLBACK_RESULT_DONE;
}

// EffectCallbackRandomSplashInit: random outward splash, /12 divisor / 19
// decel. Same constants as th06's EffectCallbackRandomSplash. FUN_0041a370.
//
// Unlike th06 (which gated the random init on `timer == 0 && HasTicked`),
// th07 runs this exactly once at spawn via the EffectInfo init callback.
// SpawnParticles refuses the effect only when the init callback returns
// NON-zero; returning STOP (0) here keeps the effect alive.
i32 __fastcall EffectManager::EffectCallbackRandomSplashInit(Effect *effect)
{
    effect->velocity.x = (g_Rng.GetRandomF32ZeroToOne() * 256.0f - 128.0f) / 12.0f;
    effect->velocity.y = (g_Rng.GetRandomF32ZeroToOne() * 256.0f - 128.0f) / 12.0f;
    effect->velocity.z = 0.0f;

    effect->acceleration = -effect->velocity / 19.0f;

    effect->velocity *= g_Supervisor_effectiveFramerateMultiplier;
    effect->acceleration *= g_Supervisor_effectiveFramerateMultiplier;

    return EFFECT_CALLBACK_RESULT_STOP;
}

// EffectCallbackRandomSplashBigInit: big splash, *4/33 divisor / 20 decel.
// FUN_0041a5a0.
i32 __fastcall EffectManager::EffectCallbackRandomSplashBigInit(Effect *effect)
{
    effect->velocity.x = (g_Rng.GetRandomF32ZeroToOne() * 256.0f - 128.0f) * 4.0f / 33.0f;
    effect->velocity.y = (g_Rng.GetRandomF32ZeroToOne() * 256.0f - 128.0f) * 4.0f / 33.0f;
    effect->velocity.z = 0.0f;

    effect->acceleration = -effect->velocity / 20.0f;

    effect->velocity *= g_Supervisor_effectiveFramerateMultiplier;
    effect->acceleration *= g_Supervisor_effectiveFramerateMultiplier;

    return EFFECT_CALLBACK_RESULT_STOP;
}

// EffectUpdateCallback4Init: marks the effect for the UpdateCallback4 fade-out
// path. FUN_0041a730. Sets drawListBucket = 2 so it lands in the plain-Draw3
// bucket.
i32 __fastcall EffectManager::EffectUpdateCallback4Init(Effect *effect)
{
    effect->drawListBucket = 2;
    return EFFECT_CALLBACK_RESULT_STOP;
}

// EffectCallbackAttractInit: pick a random direction (unit vector on the
// circle). FUN_0041aa60. Same idea as th06's EffectCallbackAttract /
// AttractSlow timer==0 branch, but hoisted out to a one-shot init callback.
//
// Note the inverted polarity vs th06: th07 draws the random angle from
// g_Rng first (to advance the seed deterministically), then overwrites the
// direction components with cos/sin of effect->angleRelated (which was set
// to a fresh random angle by the caller).
i32 __fastcall EffectManager::EffectCallbackAttractInit(Effect *effect)
{
    effect->position = effect->pos1;
    effect->position.z = 0.0f;

    g_Rng.GetRandomF32ZeroToOne();
    effect->pos2.x = cosf(effect->angleRelated);
    effect->pos2.y = sinf(effect->angleRelated);
    effect->pos2.z = 0.0f;

    return EFFECT_CALLBACK_RESULT_STOP;
}

// EffectCallbackAttract: 256 * (1 - t/60) * dir + anchor. FUN_0041aaf0.
// Matches th06's EffectCallbackAttract (which used /60.0f). th07 folds the
// timer's sub-frame into the parameter; th06 read timer.AsFramesFloat().
// rdata float constants. Defined as named globals here so MSVC emits
// fmul [const_addr]; generate_objdiff_objs.py maps them to the orig DAT_ names.
extern "C" const f32 g_EffectConst256 = 256.0f;   // orig DAT_00498a98
extern "C" const f32 g_EffectConst60 = 60.0f;     // orig DAT_00498a48
i32 __fastcall EffectManager::EffectCallbackAttract(Effect *effect)
{
    f32 t;
    f32 *base = (f32 *)((u8 *)effect + 0x2b8);

    t = g_EffectConst256 -
        ((f32)*(i32 *)((u8 *)base + 0x8) + *(f32 *)((u8 *)base + 0x4)) *
        g_EffectConst256 / g_EffectConst60;

    f32 *anchor = (f32 *)((u8 *)effect + 0x294);
    *(f32 *)((u8 *)effect + 0x248) = t * *(f32 *)((u8 *)anchor + 0x8) + *(f32 *)((u8 *)effect + 0x27c);
    *(f32 *)((u8 *)effect + 0x244) = t * *(f32 *)((u8 *)anchor + 0x4) + *(f32 *)((u8 *)effect + 0x278);
    *(f32 *)((u8 *)effect + 0x240) = t * *(f32 *)((u8 *)anchor + 0x0) + *(f32 *)((u8 *)effect + 0x274);
    *(i32 *)((u8 *)effect + 0x248 + 0x8) = 0;

    return EFFECT_CALLBACK_RESULT_DONE;
}

// EffectCallbackAttractSlow: same as Attract but /240 instead of /60.
// FUN_0041ac30. Matches th06's EffectCallbackAttractSlow.
extern "C" const f32 g_EffectConst240 = 240.0f;   // orig DAT_00498b50
i32 __fastcall EffectManager::EffectCallbackAttractSlow(Effect *effect)
{
    f32 t;
    f32 *base = (f32 *)((u8 *)effect + 0x2b8);

    t = g_EffectConst256 -
        ((f32)*(i32 *)((u8 *)base + 0x8) + *(f32 *)((u8 *)base + 0x4)) *
        g_EffectConst256 / g_EffectConst240;

    f32 *anchor = (f32 *)((u8 *)effect + 0x294);
    *(f32 *)((u8 *)effect + 0x248) = t * *(f32 *)((u8 *)anchor + 0x8) + *(f32 *)((u8 *)effect + 0x27c);
    *(f32 *)((u8 *)effect + 0x244) = t * *(f32 *)((u8 *)anchor + 0x4) + *(f32 *)((u8 *)effect + 0x278);
    *(f32 *)((u8 *)effect + 0x240) = t * *(f32 *)((u8 *)anchor + 0x0) + *(f32 *)((u8 *)effect + 0x274);

    return EFFECT_CALLBACK_RESULT_DONE;
}

// EffectUpdateCallback4: quaternion spin around the effect's pos2 axis with a
// fade-out after unk_2ce trips. FUN_0041a750. This is the th07 spelling of
// th06's EffectUpdateCallback4 (the math is identical; field offsets moved).
//
// var_order is required to match MSVC's stack layout for the local matrix
// and the per-axis cross products.
#pragma var_order(normalizedPos, verticalAngle, local_54, horizontalAngle, posOffset, alpha)
i32 __fastcall EffectManager::EffectUpdateCallback4(Effect *effect)
{
    D3DXVECTOR3 normalizedPos;
    f32 verticalAngle;
    D3DXMATRIX local_54;
    f32 horizontalAngle;
    D3DXVECTOR3 posOffset;
    f32 alpha;

    D3DXVec3Normalize(&normalizedPos, &effect->pos2);

    verticalAngle = sinf(effect->angleRelated);
    horizontalAngle = cosf(effect->angleRelated);

    effect->quaternion.x = normalizedPos.x * verticalAngle;
    effect->quaternion.y = normalizedPos.y * verticalAngle;
    effect->quaternion.z = normalizedPos.z * verticalAngle;
    effect->quaternion.w = horizontalAngle;
    D3DXMatrixRotationQuaternion(&local_54, &effect->quaternion);

    posOffset.x = normalizedPos.y * 1.0f - normalizedPos.z * 0.0f;
    posOffset.y = normalizedPos.z * 0.0f - normalizedPos.x * 1.0f;
    posOffset.z = normalizedPos.x * 0.0f - normalizedPos.y * 0.0f;

    if (D3DXVec3LengthSq(&posOffset) < 0)
    {
        normalizedPos = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        D3DXVec3Normalize(&posOffset, &posOffset);
    }

    posOffset *= effect->unk_2b0;
    D3DXVec3TransformCoord(&posOffset, &posOffset, &local_54);
    posOffset.z *= 6.0f;

    effect->pos1 = posOffset + effect->position;

    if (effect->unk_2ce)
    {
        effect->unk_2cf++;

        if (effect->unk_2cf >= 16)
        {
            return EFFECT_CALLBACK_RESULT_STOP;
        }

        alpha = 1.0f - (f32)effect->unk_2cf / 16.0f;
        effect->color = COLOR_SET_ALPHA3(effect->color, (i32)(alpha * 255.0f));

        // th07 writes the AnmVm's scaleY/scaleX directly via the vmPos
        // alias; the math is identical to th06's `vm.scaleY = 2.0f - alpha`.
        effect->vmPos.y = 2.0f - alpha;
        effect->vmPos.x = effect->vmPos.y;
    }

    return EFFECT_CALLBACK_RESULT_DONE;
}

// ---------------------------------------------------------------------------
// SpawnParticles and variants.
// ---------------------------------------------------------------------------

// SpawnParticles: round-robin over the first 400 effect slots, filling `count`
// of them. FUN_0041c1c0. __thiscall with 4 stack params (RET 0x10).
#pragma var_order(effect, idx, anmIdx, anmManager, timerPtr)
Effect *EffectManager::SpawnParticles(i32 effectIdx, D3DXVECTOR3 *pos, i32 count, ZunColor color)
{
    i32 idx;
    Effect *effect;
    i32 anmIdx;
    AnmManager *anmManager;
    ZunTimer *timerPtr;

    effect = &this->effects[this->nextIndex];
    for (idx = 0; idx < TH07_EFFECT_SPAWN_SCAN_COUNT; idx++)
    {
        this->nextIndex++;
        if (this->nextIndex >= TH07_EFFECT_SPAWN_SCAN_COUNT)
        {
            this->nextIndex = 0;
        }
        if (effect->inUseFlag)
        {
            if (this->nextIndex == 0)
            {
                effect = &this->effects[0];
            }
            else
            {
                effect++;
            }
            continue;
        }

        effect->drawListBucket = 0;
        effect->inUseFlag = 1;
        effect->effectId = (i8)effectIdx;
        effect->pos1 = *pos;

        anmIdx = g_Effects[effectIdx].anmIdx;
        anmManager = g_AnmManager;
        effect->anmScriptIndex = (u16)anmIdx;
        anmManager->SetAndExecuteScriptIdx(&effect->vm, (i32)anmManager->scriptOffsets[anmIdx]);

        effect->flags |= 0x1000;
        effect->color = color;
        effect->updateCallback = g_Effects[effectIdx].updateCallback;

        timerPtr = &effect->timer;
        timerPtr->InitializeForPopup();
        effect->unk_2ce = 0;
        effect->unk_2cf = 0;

        if (g_Effects[effectIdx].initCallback != NULL && g_Effects[effectIdx].initCallback(effect) != 0)
        {
            effect->inUseFlag = 0;
        }

        count--;
        if (count == 0)
            break;

        if (this->nextIndex == 0)
        {
            effect = &this->effects[0];
        }
        else
        {
            effect++;
        }
    }

    return idx >= TH07_EFFECT_SPAWN_SCAN_COUNT ? &this->effects[TH07_EFFECT_COUNT - 1] : effect;
}

// SpawnParticlesWithVelocity: like SpawnParticles but seeds the initial
// velocity at +0x258. FUN_0041c400. __thiscall with 5 stack params (RET 0x14).
#pragma var_order(effect, idx, anmIdx, anmManager, timerPtr)
Effect *EffectManager::SpawnParticlesWithVelocity(i32 effectIdx, D3DXVECTOR3 *pos, D3DXVECTOR3 *velocity,
                                                             i32 count, ZunColor color)
{
    i32 idx;
    Effect *effect;
    i32 anmIdx;
    AnmManager *anmManager;
    ZunTimer *timerPtr;

    effect = &this->effects[this->nextIndex];
    for (idx = 0; idx < TH07_EFFECT_SPAWN_SCAN_COUNT; idx++)
    {
        this->nextIndex++;
        if (this->nextIndex >= TH07_EFFECT_SPAWN_SCAN_COUNT)
        {
            this->nextIndex = 0;
        }
        if (effect->inUseFlag)
        {
            if (this->nextIndex == 0)
            {
                effect = &this->effects[0];
            }
            else
            {
                effect++;
            }
            continue;
        }

        effect->drawListBucket = 0;
        effect->inUseFlag = 1;
        effect->effectId = (i8)effectIdx;
        effect->pos1 = *pos;

        anmIdx = g_Effects[effectIdx].anmIdx;
        anmManager = g_AnmManager;
        effect->anmScriptIndex = (u16)anmIdx;
        anmManager->SetAndExecuteScriptIdx(&effect->vm, (i32)anmManager->scriptOffsets[anmIdx]);

        effect->color = color;
        effect->updateCallback = g_Effects[effectIdx].updateCallback;

        timerPtr = &effect->timer;
        timerPtr->InitializeForPopup();
        effect->unk_2ce = 0;
        effect->unk_2cf = 0;

        effect->unk_258 = *velocity;

        if (g_Effects[effectIdx].initCallback != NULL && g_Effects[effectIdx].initCallback(effect) != 0)
        {
            effect->inUseFlag = 0;
        }

        count--;
        if (count == 0)
            break;

        if (this->nextIndex == 0)
        {
            effect = &this->effects[0];
        }
        else
        {
            effect++;
        }
    }

    return idx >= TH07_EFFECT_SPAWN_SCAN_COUNT ? &this->effects[TH07_EFFECT_COUNT - 1] : effect;
}

// SpawnParticleAt: spawn a single effect at a specific slot (slot + 400).
// FUN_0041c610. __thiscall with 5 stack params (RET 0x14). `unused` (4th
// stack arg) is read by the binary but discarded.
#pragma var_order(effect, anmIdx, anmManager, timerPtr)
Effect *EffectManager::SpawnParticleAt(i32 effectIdx, D3DXVECTOR3 *pos, i32 slot, i32 unused,
                                                  ZunColor color)
{
    Effect *effect;
    i32 anmIdx;
    AnmManager *anmManager;
    ZunTimer *timerPtr;

    effect = &this->effects[slot + TH07_EFFECT_SPAWN_SCAN_COUNT];

    effect->drawListBucket = 0;
    effect->inUseFlag = 1;
    effect->effectId = (i8)effectIdx;
    effect->pos1 = *pos;

    anmIdx = g_Effects[effectIdx].anmIdx;
    anmManager = g_AnmManager;
    effect->anmScriptIndex = (u16)anmIdx;
    anmManager->SetAndExecuteScriptIdx(&effect->vm, (i32)anmManager->scriptOffsets[anmIdx]);

    effect->flags |= 0x1000;
    effect->color = color;
    effect->updateCallback = g_Effects[effectIdx].updateCallback;

    timerPtr = &effect->timer;
    timerPtr->InitializeForPopup();
    effect->unk_2ce = 0;
    effect->unk_2cf = 0;

    if (g_Effects[effectIdx].initCallback != NULL && g_Effects[effectIdx].initCallback(effect) != 0)
    {
        effect->inUseFlag = 0;
    }

    return effect;
}

// ---------------------------------------------------------------------------
// Chain callbacks.
// ---------------------------------------------------------------------------

// OnUpdate: walk all 408 live effect slots, run their per-frame update
// callback + AnmManager::ExecuteScript, and rebuild the four draw buckets.
// FUN_0041c790. __fastcall (mgr in ECX).
ChainCallbackResult __fastcall EffectManager::OnUpdate(EffectManager *mgr)
{
    Effect *effect;
    i32 idx;

    effect = &mgr->effects[0];
    mgr->activeEffects = 0;

    // Reset the four draw-bucket tails back to their sentinels and zero the
    // head pointers (sentinelN.next).
    mgr->drawListTails[0] = &mgr->sentinel1;
    mgr->drawListTails[1] = &mgr->sentinel2;
    mgr->drawListTails[2] = &mgr->sentinel3;
    mgr->drawListTails[3] = &mgr->sentinel4;
    mgr->sentinel1.next = NULL;
    mgr->sentinel2.next = NULL;
    mgr->sentinel3.next = NULL;
    mgr->sentinel4.next = NULL;

    for (idx = 0; idx < TH07_EFFECT_UPDATE_SCAN_COUNT; idx++, effect++)
    {
        if (effect->inUseFlag == 0)
        {
            continue;
        }

        mgr->activeEffects++;
        if (effect->updateCallback != NULL && effect->updateCallback(effect) != EFFECT_CALLBACK_RESULT_DONE)
        {
            effect->inUseFlag = 0;
            continue;
        }

        if (g_AnmManager->ExecuteScript(&effect->vm) != 0)
        {
            effect->inUseFlag = 0;
            continue;
        }

        // Copy the just-ticked timer sub-frame into the previous slot and
        // advance current. th07 folds this into a helper (FUN_0043958d) but
        // the visible effect is: timer.previous = timer.current; then Tick().
        effect->timer.previous = effect->timer.current;
        Supervisor_TickTimer(&effect->timer.current, &effect->timer.subFrame);

        effect->next = NULL;

        if (effect->drawListBucket == 1 || effect->drawListBucket == 3)
        {
            // bucket 1 (additive, screen-offset Draw3)
            mgr->drawListTails[1]->next = effect;
            mgr->drawListTails[1] = effect;
        }
        else if (effect->drawListBucket == 0)
        {
            if ((effect->flags & 0x10) == 0)
            {
                // bucket 0 (plain, drawn without a screen offset)
                mgr->drawListTails[0]->next = effect;
                mgr->drawListTails[0] = effect;
            }
            else
            {
                // bucket 4 (additive, screen-offset Draw3)
                mgr->drawListTails[3]->next = effect;
                mgr->drawListTails[3] = effect;
            }
        }
        else
        {
            // bucket 2 (plain Draw3, no offset)
            mgr->drawListTails[2]->next = effect;
            mgr->drawListTails[2] = effect;
        }
    }

    mgr->frameCounter++;
    if (mgr->frameCounter % 300 == 100 && Supervisor_SomePulseFlag() != 0)
    {
        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// OnDraw (FUN_0041cb80): walk bucket-1 draw list, alternate alpha coloring.
ChainCallbackResult __fastcall EffectManager::OnDraw(EffectManager *mgr)
{
    u8 *mgrBytes = (u8 *)mgr;
    Effect *cur = *(Effect **)(mgrBytes + 0x490e0);
    i32 alternator = 0;
    if (g_EffectMgrPlayModeA != 0)
    {
        while (cur != 0)
        {
            alternator = alternator + 1;
            if (!(g_EffectMgrPlayModeA == 1 && (alternator & 1) == 0))
            {
                if (*(i8 *)((u8 *)cur + 0x2cd) == 0x14)
                {
                    u32 r = (u32)(*(f32 *)(mgrBytes + 0xc) * (f32)*(u8 *)((u8 *)cur + 0x1ba));
                    if (r > 0xff) r = 0xff;
                    *(u8 *)((u8 *)cur + 0x1ba) = (u8)r;
                    u32 g = (u32)(*(f32 *)(mgrBytes + 0x10) * (f32)*(u8 *)((u8 *)cur + 0x1b9));
                    if (g > 0xff) g = 0xff;
                    *(u8 *)((u8 *)cur + 0x1b9) = (u8)g;
                    u32 b = (u32)(*(f32 *)(mgrBytes + 0x14) * (f32)*(u8 *)((u8 *)cur + 0x1b8));
                    if (b > 0xff) b = 0xff;
                    *(u8 *)((u8 *)cur + 0x1b8) = (u8)b;
                    u32 a = (u32)(*(f32 *)(mgrBytes + 0x18) * (f32)*(u8 *)((u8 *)cur + 0x1bb));
                    if (a > 0xff) a = 0xff;
                    *(u8 *)((u8 *)cur + 0x1bb) = (u8)a;
                }
                *(D3DXVECTOR3 *)((u8 *)cur + 0x1c8) = *(D3DXVECTOR3 *)((u8 *)cur + 0x24c);
                if (*(i8 *)((u8 *)cur + 0x2d0) == 1)
                {
                    g_AnmManager->Draw3NoOffset(&cur->vm);
                }
                else
                {
                    g_AnmManager->Draw3(&cur->vm);
                }
                if (*(i8 *)((u8 *)cur + 0x2cd) == 0x14)
                {
                    *(u8 *)((u8 *)cur + 0x1ba) = (u8)(i32)(*(f32 *)(mgrBytes + 0xc));
                    *(u8 *)((u8 *)cur + 0x1b9) = (u8)(i32)(*(f32 *)(mgrBytes + 0x10));
                    *(u8 *)((u8 *)cur + 0x1b8) = (u8)(i32)(*(f32 *)(mgrBytes + 0x14));
                    *(u8 *)((u8 *)cur + 0x1bb) = (u8)(i32)(*(f32 *)(mgrBytes + 0x18));
                }
            }
            cur = *(Effect **)((u8 *)cur + 0x2d4);
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult __fastcall EffectManager::AddedCallback(EffectManager *mgr)
{
    mgr->Reset();
    g_EffectAnmBaseIdx = 0;

    switch (g_GameManager_currentStage)
    {
    case 0:
    case 1:
        g_EffectAnmFileCount = 1;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff01.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 2:
        g_EffectAnmFileCount = 1;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff02.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 3:
        g_EffectAnmFileCount = 1;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff03.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 4:
        g_EffectAnmFileCount = 2;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff04.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (Anm_LoadAnm(ANM_FILE_EFFECTS + 1, "data/eff04b.anm", ANM_OFFSET_EFFECTS + 1) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 5:
        g_EffectAnmFileCount = 2;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff05.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 6:
        g_EffectAnmFileCount = 2;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff05.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (Anm_LoadAnm(ANM_FILE_EFFECTS + 2, "data/eff06.anm", ANM_OFFSET_EFFECTS + 2) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 7:
        g_EffectAnmFileCount = 1;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff02.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (Anm_LoadAnm(ANM_FILE_EFFECTS + 1, "data/eff07.anm", ANM_OFFSET_EFFECTS + 1) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 8:
        g_EffectAnmFileCount = 2;
        if (Anm_LoadAnm(ANM_FILE_EFFECTS, "data/eff07.anm", ANM_OFFSET_EFFECTS) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (Anm_LoadAnm(ANM_FILE_EFFECTS + 2, "data/eff08.anm", ANM_OFFSET_EFFECTS + 2) != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    }
    return ZUN_SUCCESS;
}

// DeletedCallback: release the eff0X.anm files loaded by AddedCallback.
// FUN_0041d050. Releases all four possible anm file slots (the binary
// unconditionally calls ReleaseAnm on ids 0x11..0x14).
ZunResult __fastcall EffectManager::DeletedCallback(EffectManager *mgr)
{
    g_AnmManager->ReleaseAnm(ANM_FILE_EFFECTS);
    Anm_ReleaseAnm(ANM_FILE_EFFECTS + 1);
    Anm_ReleaseAnm(ANM_FILE_EFFECTS + 2);
    Anm_ReleaseAnm(ANM_FILE_EFFECTS + 3);
    return ZUN_SUCCESS;
}

// RegisterChain: hook EffectManager into the calc + draw chains.
// FUN_0041d0a0. th07 uses priority 0xb (calc) and 0x9 (draw)  see
// ChainPriorities.hpp once that module lands.
ZunResult EffectManager::RegisterChain()
{
    EffectManager *mgr = &g_EffectManager;
    mgr->Reset();

    g_EffectManagerCalcChain.callback = (ChainCallback)mgr->OnUpdate;
    g_EffectManagerCalcChain.addedCallback = NULL;
    g_EffectManagerCalcChain.deletedCallback = NULL;
    g_EffectManagerCalcChain.addedCallback = (ChainAddedCallback)mgr->AddedCallback;
    g_EffectManagerCalcChain.deletedCallback = (ChainAddedCallback)mgr->DeletedCallback;
    g_EffectManagerCalcChain.arg = mgr;

    if (g_Chain.AddToCalcChain(&g_EffectManagerCalcChain, TH_CHAIN_PRIO_CALC_EFFECTMANAGER))
    {
        return ZUN_ERROR;
    }

    g_EffectManagerDrawChain.callback = (ChainCallback)mgr->OnDraw;
    g_EffectManagerDrawChain.addedCallback = NULL;
    g_EffectManagerDrawChain.deletedCallback = NULL;
    g_EffectManagerDrawChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_EffectManagerDrawChain, TH_CHAIN_PRIO_DRAW_EFFECTMANAGER);

    return ZUN_SUCCESS;
}

// CutChain: unhook both chains. FUN_0041d150.
void EffectManager::CutChain()
{
    g_Chain.Cut(&g_EffectManagerCalcChain);
    g_Chain.Cut(&g_EffectManagerDrawChain);
}
}; // namespace th07
