// Player module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary. The module is written in plain C++ so it
// ports cleanly to the SDL2 build; orig (DIFFBUILD) addresses are confined to
// #ifdef DIFFBUILD macros. Only the atan2 helper in AngleToPlayer uses a small
// guarded inline-asm block (FPU-stack argument passing has no C++ spelling).
//
// Cross-module call conventions (all verified from orig disassembly):
//   g_Chain methods   : __thiscall, ECX = &g_Chain @ 0x00626218
//   EffectManager     : __thiscall, ECX = 0x012fe250 (spawn singletons)
//   SoundPlayer       : __thiscall, ECX = 0x004ba0d8
//   AnmManager        : __thiscall, ECX = *0x004b9e44 (deref'd pointer)
//   GameManager method: __thiscall, ECX = 0x00626270 (g_GameManager object)
//   scoreSub fields   : reached via *(ScoreSub**)0x00626278 (= g_GameManager+0x8)
//
// To emit the exact orig call sequence (PUSH args; MOV ECX,this; CALL) for the
// not-yet-reversed singletons, they are declared as struct-method stubs and
// mapped to their orig addresses in config/mapping.csv.

#include "Player.hpp"

#include "AnmVm.hpp"
#include "Chain.hpp"
#include "ZunResult.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>
#include <math.h>
#include <string.h>

namespace th07
{
DIFFABLE_STATIC(Player, g_Player);

// CRT atan2 FPU-stack dispatcher (orig @ 0x0048bcaa; mapped in mapping.csv).
extern "C" void atan2_disp_0048bcaa();

// OnDrawLowPrio callee (kept as C++ extern; OnDrawLowPrio is plain C++).
extern "C" void __fastcall Player_DrawBulletExplosions(Player *p); // FUN_0043d790

// ---- thiscall callee stubs (full classes land when those modules reverse) ----
// Declared as struct methods so MSVC emits the exact orig call sequence.
struct EffectManagerSpawn
{
    void SpawnEffect(i32 kind, D3DXVECTOR3 *pos, i32 count, i32 b, u32 color);
    void SpawnParticles(i32 kind, D3DXVECTOR3 *pos, i32 count, u32 color);
};
struct SoundPlayerPlayback
{
    void PlaySoundByIdx(i32 idx, i32 mode);
};
struct AnmManagerFiles
{
    void ReleaseAnm(i32 fileIdx);
};
struct GameManagerScore
{
    void GuiClearFlags();
    void IncreaseSubrank(i32 amount);
    void AddScore(i32 amount);
    void AddGrazeScoreOnly(i32 amount);
};

// Singleton handles. DIFFBUILD (objdiff) uses the verified orig addresses so
// the generated MOV ECX,imm matches the binary; the SDL build uses real
// singletons (declared here, defined when those modules are reversed).
#ifdef DIFFBUILD
static EffectManagerSpawn *const g_EffectMgrSpawn = (EffectManagerSpawn *)0x12fe250;
static SoundPlayerPlayback *const g_SoundPlayer = (SoundPlayerPlayback *)0x4ba0d8;
static GameManagerScore *const g_GameManagerScore = (GameManagerScore *)0x626270;
// scoreSub reached via pointer at g_GameManager+0x8 (DAT_00626278).
#define SCORE_SUB (*reinterpret_cast<void **>(0x626278))
// AnmManager is a pointer global (DAT_004b9e44); deref each use.
#define ANM_MGR (*reinterpret_cast<AnmManagerFiles **>(0x4b9e44))
// Player's own chain callbacks (orig addresses; SDL build uses &Player::X).
#define PLAYER_ON_UPDATE_CB        ((ChainCallback)0x441fb0)
#define PLAYER_ON_DRAW_HIGH_CB     ((ChainCallback)0x4420b0)
#define PLAYER_ON_DRAW_LOW_CB      ((ChainCallback)0x442350)
#define PLAYER_ADDED_CB            ((ChainAddedCallback)0x4423e0)
#define PLAYER_DELETED_CB          ((ChainDeletedCallback)0x4428e0)
#else
extern EffectManagerSpawn g_EffectMgrSpawnObj;
extern SoundPlayerPlayback g_SoundPlayerObj;
extern GameManagerScore g_GameManagerScoreObj;
extern AnmManagerFiles *g_AnmManagerFilesObj;
extern void *g_ScoreSubObj;
static EffectManagerSpawn *const g_EffectMgrSpawn = &g_EffectMgrSpawnObj;
static SoundPlayerPlayback *const g_SoundPlayer = &g_SoundPlayerObj;
static GameManagerScore *const g_GameManagerScore = &g_GameManagerScoreObj;
#define SCORE_SUB (g_ScoreSubObj)
#define ANM_MGR (g_AnmManagerFilesObj)
#define PLAYER_ON_UPDATE_CB        ((ChainCallback)Player::OnUpdate)
#define PLAYER_ON_DRAW_HIGH_CB     ((ChainCallback)Player::OnDrawHighPrio)
#define PLAYER_ON_DRAW_LOW_CB      ((ChainCallback)Player::OnDrawLowPrio)
#define PLAYER_ADDED_CB            ((ChainAddedCallback)Player::AddedCallback)
#define PLAYER_DELETED_CB          ((ChainDeletedCallback)Player::DeletedCallback)
#endif

// scoreSub field access; each use re-reads the DAT_00626278 pointer (matching
// orig's repeated MOV reg,[0x626278]). Offsets per ScoreSub in GameManager.hpp.
#define SCORE_SUB_I32(off) (*reinterpret_cast<i32 *>(reinterpret_cast<u8 *>(SCORE_SUB) + (off)))

// =============================================================================
// OnDrawLowPrio  --  FUN_00442350  (__fastcall, Player* in ECX)
// =============================================================================
ChainCallbackResult __fastcall Player::OnDrawLowPrio(Player *p)
{
    Player_DrawBulletExplosions(p);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// CutChain  --  FUN_00442b10  (__cdecl)
// Detaches the three embedded chain nodes (stored at g_Player+0xb7e5c/60/64,
// which are the orig DAT_00575934/38/3c).
// =============================================================================
void Player::CutChain()
{
    g_Chain.Cut(*reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e5c]));
    *reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e5c]) = 0;
    g_Chain.Cut(*reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e60]));
    *reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e60]) = 0;
    g_Chain.Cut(*reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e64]));
    *reinterpret_cast<ChainElem **>(&g_Player.raw[0xb7e64]) = 0;
}

// =============================================================================
// StartFireBulletTimer  --  FUN_0043d990  (__fastcall, Player* in ECX)
// Resets fireBulletTimer (@+0x169f4) to {-999,0,0} if its current field is < 0.
// =============================================================================
void __fastcall Player::StartFireBulletTimer(Player *p)
{
    i32 cur = *reinterpret_cast<i32 *>(&p->raw[0x169fc]);
    if (cur < 0)
    {
        ZunTimer *t = reinterpret_cast<ZunTimer *>(&p->raw[0x169f4]);
        t->current = 0;
        t->subFrame = 0;
        t->previous = -999;
    }
}

// =============================================================================
// Die  --  FUN_0043edc0  (__fastcall, Player* in ECX)
// Clears Gui flags, spawns the death effects, sets state=DEAD, resets the
// invulnerability timer (@+0x16a00) and plays the death sound.
// =============================================================================
void __fastcall Player::Die(Player *p)
{
    g_GameManagerScore->GuiClearFlags();
    g_EffectMgrSpawn->SpawnEffect(0xc, p->PositionCenter(), 3, 1, 0xff4040ff);
    g_EffectMgrSpawn->SpawnParticles(6, p->PositionCenter(), 0x10, 0xffffffff);
    p->raw[0x2408] = PLAYER_STATE_DEAD;
    ZunTimer *t = reinterpret_cast<ZunTimer *>(&p->raw[0x16a00]);
    t->current = 0;
    t->subFrame = 0;
    t->previous = -999;
    g_SoundPlayer->PlaySoundByIdx(4, 0);
}

// =============================================================================
// RegisterChain  --  FUN_004429d0  (__fastcall, u8 unk in ECX)
// Zeroes g_Player (0xb7e78 bytes), seeds the invulnerability timer (@+0x16a00)
// to {-999,0,0}, stores `unk` at +0x2409, allocates three chain elements
// (callbacks OnUpdate/OnDrawHighPrio/OnDrawLowPrio), wires their arg to
// &g_Player and Added/Deleted onto the calc element, and registers them.
// =============================================================================
ZunResult __fastcall Player::RegisterChain(u8 unk)
{
    Player *p = &g_Player;
    memset(p, 0, 0xb7e78);

    ZunTimer *iv = reinterpret_cast<ZunTimer *>(&p->raw[0x16a00]);
    iv->current = 0;
    iv->subFrame = 0;
    iv->previous = -999;

    p->raw[0x2409] = unk;

    *reinterpret_cast<ChainElem **>(&p->raw[0xb7e5c]) = g_Chain.CreateElem(PLAYER_ON_UPDATE_CB);
    *reinterpret_cast<ChainElem **>(&p->raw[0xb7e60]) = g_Chain.CreateElem(PLAYER_ON_DRAW_HIGH_CB);
    *reinterpret_cast<ChainElem **>(&p->raw[0xb7e64]) = g_Chain.CreateElem(PLAYER_ON_DRAW_LOW_CB);

    (*reinterpret_cast<ChainElem **>(&p->raw[0xb7e5c]))->arg = p;
    (*reinterpret_cast<ChainElem **>(&p->raw[0xb7e60]))->arg = p;
    (*reinterpret_cast<ChainElem **>(&p->raw[0xb7e64]))->arg = p;
    (*reinterpret_cast<ChainElem **>(&p->raw[0xb7e5c]))->addedCallback = PLAYER_ADDED_CB;
    (*reinterpret_cast<ChainElem **>(&p->raw[0xb7e5c]))->deletedCallback = PLAYER_DELETED_CB;

    if (g_Chain.AddToCalcChain(*reinterpret_cast<ChainElem **>(&p->raw[0xb7e5c]), 8) != 0)
    {
        return ZUN_ERROR;
    }
    g_Chain.AddToDrawChain(*reinterpret_cast<ChainElem **>(&p->raw[0xb7e60]), 6);
    g_Chain.AddToDrawChain(*reinterpret_cast<ChainElem **>(&p->raw[0xb7e64]), 8);
    return ZUN_SUCCESS;
}

// =============================================================================
// DeletedCallback  --  FUN_004428e0  (__fastcall, Player* in ECX)
// Unless the supervisor is mid-state-transition (curState 3/0xb/0xc), releases
// the player ANM file (idx 10) and resets three Gui sprite counters; then frees
// the two option-table buffers stored at g_Player+0xb7e70/+0xb7e74 (orig
// DAT_00575948 / DAT_0057594c).
// =============================================================================
ZunResult __fastcall Player::DeletedCallback(Player *p)
{
    (void)p;
    i32 doRelease;
    if (*reinterpret_cast<i32 *>(0x575aa8) != 3 && *reinterpret_cast<i32 *>(0x575aa8) != 0xb &&
        *reinterpret_cast<i32 *>(0x575aa8) != 0xc)
    {
        doRelease = 1;
    }
    else
    {
        doRelease = 0;
    }
    if (doRelease != 0)
    {
        ANM_MGR->ReleaseAnm(10);
        *reinterpret_cast<u16 *>(0x134d476) = 0x63;
        *reinterpret_cast<u32 *>(0x13542ec) = 0x63;
#ifndef DIFFBUILD
        // orig indexes a stride-0x24c Gui counter array with literal 0/1/2 via
        // reg+IMUL (xor/mov reg,imm; imul reg,reg,0x24c; mov word,[base+reg]).
        __asm
        {
            xor eax, eax
            imul eax, eax, 0x24c
            mov word ptr [eax + 0x134db5a], 0x63
            mov ecx, 1
            imul ecx, ecx, 0x24c
            mov word ptr [ecx + 0x134db5a], 0x63
            mov edx, 2
            imul edx, edx, 0x24c
            mov word ptr [edx + 0x134db5a], 0x63
        }
#else
        *reinterpret_cast<u16 *>(0x134db5a + 0 * 0x24c) = 0x63;
        *reinterpret_cast<u16 *>(0x134db5a + 1 * 0x24c) = 0x63;
        *reinterpret_cast<u16 *>(0x134db5a + 2 * 0x24c) = 0x63;
#endif
    }
    if (*reinterpret_cast<void **>(&g_Player.raw[0xb7e70]) != 0)
    {
        void *optUnfocused = *reinterpret_cast<void **>(&g_Player.raw[0xb7e70]);
        free(optUnfocused);
        *reinterpret_cast<void **>(&g_Player.raw[0xb7e70]) = 0;
    }
    if (*reinterpret_cast<void **>(&g_Player.raw[0xb7e74]) != 0)
    {
        void *optFocused = *reinterpret_cast<void **>(&g_Player.raw[0xb7e74]);
        free(optFocused);
        *reinterpret_cast<void **>(&g_Player.raw[0xb7e74]) = 0;
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// AngleToPlayer  --  FUN_00442370  (__thiscall, this in ECX, pos at [ebp+8])
// Returns the angle from `pos` to the player center. PI/2 when the player is
// directly on top of `pos` (relX==relY==0). atan2 helper takes args on the FPU
// stack (st0=x, st1=y), so the call is guarded inline asm in DIFFBUILD.
// =============================================================================
f32 Player::AngleToPlayer(D3DXVECTOR3 *pos)
{
    f32 relX = PositionCenter()->x - pos->x;
    f32 relY = PositionCenter()->y - pos->y;
    f32 result;
    if (relY == 0.0f && relX == 0.0f)
    {
        result = *reinterpret_cast<f32 *>(0x498a9c); // PI/2
    }
    else
    {
#ifndef DIFFBUILD
        // orig passes args via the FPU stack to the CRT atan2 dispatcher at
        // 0x0048bcaa (mapped to atan2_disp_0048bcaa in config/mapping.csv).
        __asm
        {
            fld dword ptr [relY]
            fld dword ptr [relX]
            call atan2_disp_0048bcaa
            fst dword ptr [result]
        }
#else
        result = atan2f(relY, relX);
#endif
    }
    return result;
}

// =============================================================================
// CalcItemBoxCollision  --  FUN_0043e4e0
// (__thiscall, this in ECX, center at [ebp+8], size at [ebp+0xc])
// AABB test of `center`+/-`size`*scale against the player's graze box
// (@+0x978 topLeft, @+0x984 bottomRight). Returns 1 on overlap, else 0.
// Player must be ALIVE(0)/INVULNERABLE(3)/DYING(4); SPAWNING/DEAD suppress it.
// =============================================================================
i32 Player::CalcItemBoxCollision(D3DXVECTOR3 *center, D3DXVECTOR3 *size)
{
    u8 state = raw[0x2408];
    if (state != PLAYER_STATE_ALIVE && state != PLAYER_STATE_INVULNERABLE &&
        state != PLAYER_STATE_DYING)
    {
        return 0;
    }

    f32 scale = *reinterpret_cast<f32 *>(0x498a54) / *reinterpret_cast<f32 *>(0x498a70);
    D3DXVECTOR3 half;
    half.z = size->z * scale;
    half.y = size->y * scale;
    half.x = size->x * scale;

    D3DXVECTOR3 minCorner;
    minCorner.z = center->z - half.z;
    minCorner.y = center->y - half.y;
    minCorner.x = center->x - half.x;

    D3DXVECTOR3 half2;
    half2.z = size->z * scale;
    half2.y = size->y * scale;
    half2.x = size->x * scale;

    D3DXVECTOR3 maxCorner;
    maxCorner.z = center->z + half2.z;
    maxCorner.y = center->y + half2.y;
    maxCorner.x = center->x + half2.x;

    f32 *boxTL = reinterpret_cast<f32 *>(&raw[0x978]);
    f32 *boxBR = reinterpret_cast<f32 *>(&raw[0x984]);
    if (boxTL[0] < maxCorner.x)
    {
        if (boxBR[0] > minCorner.x)
        {
            if (boxTL[1] < minCorner.y)
            {
                if (boxBR[1] >= maxCorner.y)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// =============================================================================
// ScoreGraze  --  FUN_0043eb90  (__thiscall, this in ECX, center at [ebp+8])
// Increments graze counters (skipped while a bomb is active), spawns graze
// particles, bumps the Gui flag, plays the graze sound, awards cherry + score,
// and during a Supernatural Border awards extra subrank/score (focus-scaled).
// =============================================================================
void Player::ScoreGraze(D3DXVECTOR3 *center)
{
    i32 bombActive = *reinterpret_cast<i32 *>(0x4d44f8); // bombInfo.isInUse
    if (bombActive == 0)
    {
        if (SCORE_SUB_I32(0x14) < 9999)
        {
            SCORE_SUB_I32(0x14)++;
        }
        if (SCORE_SUB_I32(0x18) < 999999)
        {
            SCORE_SUB_I32(0x18)++;
        }
    }

    D3DXVECTOR3 *pc = PositionCenter();
    D3DXVECTOR3 sum;
    sum.z = pc->z + center->z;
    sum.y = pc->y + center->y;
    sum.x = pc->x + center->x;

    f32 scale = *reinterpret_cast<f32 *>(0x498a54) / *reinterpret_cast<f32 *>(0x498a70);
    D3DXVECTOR3 particlePos;
    particlePos.z = sum.z * scale;
    particlePos.y = sum.y * scale;
    particlePos.x = sum.x * scale;

    if (raw[0x240d] == 1) // in Supernatural Border
    {
        if (raw[0x240b] == 0) // unfocused
        {
            g_EffectMgrSpawn->SpawnParticles(8, &particlePos, 3, 0xffff8080);
        }
        else
        {
            g_EffectMgrSpawn->SpawnParticles(8, &particlePos, 1, 0xffffffff);
        }
    }
    else
    {
        g_EffectMgrSpawn->SpawnParticles(8, &particlePos, 1, 0xffffffff);
    }

    g_GameManagerScore->AddGrazeScoreOnly(6);
    u32 guiFlags = *reinterpret_cast<u32 *>(0x49fbf4);
    guiFlags = (guiFlags & 0xffffff3f) | 0x80;
    *reinterpret_cast<u32 *>(0x49fbf4) = guiFlags;
    g_SoundPlayer->PlaySoundByIdx(0x1e, 0);

    i32 cherry = *reinterpret_cast<i32 *>(0x12fe0d0);
    i32 scoreFrame = *reinterpret_cast<i32 *>(0x62f88c);
    cherry = cherry + 0x9c4 + (scoreFrame - SCORE_SUB_I32(0x88)) / 0x5dc * 0x14;
    *reinterpret_cast<i32 *>(0x12fe0d0) = cherry;

    SCORE_SUB_I32(0x4) = SCORE_SUB_I32(0x4) + 0x7d0 / 0xa;

    if (raw[0x240d] == 1) // in Supernatural Border
    {
        if (raw[0x240b] == 0) // unfocused
        {
            g_GameManagerScore->IncreaseSubrank(0x50);
            g_GameManagerScore->AddScore(0x50);
        }
        else
        {
            g_GameManagerScore->IncreaseSubrank(0x1e);
            g_GameManagerScore->AddScore(0x1e);
        }
    }
}
} // namespace th07
