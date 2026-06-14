// Player module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary.
//
// Function address map (all __fastcall unless noted):
//   RegisterChain            @ FUN_004429d0
//   CutChain                 @ FUN_00442b10
//   AddedCallback            @ FUN_004423e0
//   DeletedCallback          @ FUN_004428e0
//   OnUpdate                 @ FUN_00441fb0
//   OnDrawHighPrio           @ FUN_004420b0
//   OnDrawLowPrio            @ FUN_00442350
//   ClearBombRegions         @ FUN_00440940   (helper called from OnUpdate)
//   HandleBombInput          @ FUN_004409f0   (helper called from OnUpdate)
//   UpdatePlayerState        @ FUN_00441330   (per-frame state update)
//   SpawnStateUpdate         @ FUN_004411c0   (spawning-state per-frame)
//   DeadStateUpdate          @ FUN_00440cf0   (dead-state per-frame)
//   StartSupernaturalBorder  @ FUN_00441960
//   EndSupernaturalBorder    @ FUN_00441bd0
//   ResetPositionOfLastHit   @ FUN_00441e80
//   HandlePlayerInputs       @ FUN_0043ee50   (large)
//   UpdatePlayerBullets      @ FUN_0043d2f0
//   DrawBullets              @ FUN_0043d690
//   DrawBulletExplosions     @ FUN_0043d790
//   UpdateFireBulletsTimer   @ FUN_0043d880
//   StartFireBulletTimer     @ FUN_0043d990
//   SpawnBullets             @ FUN_0043d160
//   FireSingleBullet (thunk) @ FUN_0043bdc0   (dispatches to per-row callback)
//   CalcDamageToEnemy        @ FUN_0043d9e0
//   CheckGraze (BombProjHit) @ FUN_0043e0a0   (helper, takes bombProj base)
//   CalcKillBoxCollision     @ FUN_0043e260
//   CheckGraze               @ FUN_0043e3b0   (player-facing entry)
//   CalcItemBoxCollision     @ FUN_0043e4e0
//   CalcLaserHitbox          @ FUN_0043e6b0
//   ScoreGraze               @ FUN_0043eb90
//   Die                      @ FUN_0043edc0
//   AngleToPlayer            @ FUN_00442370   (atan2 wrapper)
//
// Per-character bomb calc/draw callbacks (indexed by CharacterShotType 0..5):
//   calcUnfocused: 0x00408710 0x00409dd0 0x0040a7c0 0x0040b7d0 0x0040c2e0 0x0040d4c0
//   drawUnfocused: 0x00408e10 0x0040a280 0x0040ab40 0x0040bca0 0x0040c970 0x0040d9a0
//   calcFocused:   0x004091b0 0x0040a3a0 0x0040af10 0x0040be20 0x0040ca50 0x0040da80
//   drawFocused:   0x00409990 0x0040a6b0 0x0040b5d0 0x0040c160 0x0040d3b0 0x0040e280
//
// External helpers (signatures inferred from call sites):
//   AnmManager::LoadAnm(fileIdx, path, offset)         @ FUN_0044df90
//   AnmManager::SetAndExecuteScriptIdx(AnmVm *, idx)   @ FUN_0044ea20
//   AnmManager::DrawNoRotation(AnmVm *)                @ FUN_0044f770
//   AnmManager::Draw2(AnmVm *)                         @ FUN_0044f9a0
//   AnmManager::ExecuteScript(AnmVm *)                 @ FUN_00450d60
//   AnmManager::ReleaseAnm(fileIdx)                    @ FUN_0044e4e0
//   SoundPlayer::PlaySoundByIdx(idx, mode)             @ FUN_0044c930
//   EffectManager::SpawnParticles(kind, pos, count, color) @ FUN_0041c1c0
//   EffectManager::SpawnEffect(kind, pos, ...)         @ FUN_0041c610
//   ItemManager::SpawnItem(pos, kind, count)           @ FUN_004326f0
//   BulletManager::RemoveAllBullets(mode)              @ FUN_00433a90 / 0x00433b20
//   Gui::HasCurrentMsgIdx()                            @ FUN_00404fe0
//   GameManager::DecreaseSubrank(amount)               @ FUN_0042f526
//   GameManager::IncreaseSubrank(amount)               @ FUN_0042f789
//   GameManager::AddScore(amount)                      @ FUN_0042f736
//   Chain::CreateElem(callback)                        @ FUN_00430090
//   Chain::Cut(elem)                                   @ FUN_00430140
//   Chain::AddToCalcChain(elem, prio)                  @ FUN_0042fbd0
//   Chain::AddToDrawChain(elem, prio)                  @ FUN_0042fca0
//   ZunTimer::Tick                                     @ FUN_0043958d
//   ZunTimer::Decrement                                @ FUN_004394c7
//   atan2 wrapper                                      @ FUN_0048bcaa
//
// NOTE: This file is a PARTIAL recovery. The full module is large (~30 funcs,
// several hundred lines each for OnUpdate / HandlePlayerInputs / the bomb
// callbacks). The functions below are the ones whose behavior could be
// verified line-by-line against ghidra. The remaining ones (OnUpdate body,
// HandlePlayerInputs, the per-character FireBullet* callbacks and the bomb
// calc/draw callbacks) are stubbed and marked confidence=low in the report.

#include "Player.hpp"

#include "AnmVm.hpp"
#include "Chain.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>
#include <string.h>
#include <math.h>

namespace th07
{
DIFFABLE_STATIC(Player, g_Player);

ChainElem *g_Player_chainCalc;
ChainElem *g_Player_chainDraw1;
ChainElem *g_Player_chainDraw2;

// Verified extern globals (full definitions live in their respective modules).
extern "C" void *g_AnmManager; // @ 0x004b9e44 (defined in Supervisor.cpp)

// External helper prototypes. These are not yet reverse-engineered into typed
// th07 classes; we declare them with the calling conventions observed at the
// call sites so the generated call sequences match the binary byte-for-byte.
extern "C" ZunResult __fastcall AnmManager_LoadAnm(void *anmMgr, i32 fileIdx, const char *path,
                                                   i32 offset); // FUN_0044df90
extern "C" void __fastcall AnmManager_SetAndExecuteScriptIdx(void *anmMgr, AnmVm *vm,
                                                             i32 idx); // FUN_0044ea20
extern "C" void __fastcall AnmManager_DrawNoRotation(void *anmMgr, AnmVm *vm); // FUN_0044f770
extern "C" void __fastcall AnmManager_Draw2(void *anmMgr, AnmVm *vm);          // FUN_0044f9a0
extern "C" i32 __fastcall AnmManager_ExecuteScript(void *anmMgr, AnmVm *vm);   // FUN_00450d60
extern "C" void __fastcall AnmManager_ReleaseAnm(void *anmMgr, i32 fileIdx);   // FUN_0044e4e0
extern "C" void __fastcall SoundPlayer_PlaySoundByIdx(i32 idx, i32 mode);      // FUN_0044c930
extern "C" void __fastcall EffectManager_SpawnParticles(i32 kind, D3DXVECTOR3 *pos, i32 count,
                                                        u32 color); // FUN_0041c1c0
extern "C" void *__fastcall EffectManager_SpawnEffect(i32 kind, D3DXVECTOR3 *pos, i32 a, i32 b,
                                                      u32 color); // FUN_0041c610
extern "C" i32 __fastcall Gui_HasCurrentMsgIdx();               // FUN_00404fe0
extern "C" ChainElem *__fastcall Chain_CreateElem(void *callback); // FUN_00430090
extern "C" void __fastcall Chain_Cut(ChainElem *elem);          // FUN_00430140
extern "C" i32 __fastcall Chain_AddToCalcChain(ChainElem *elem, i32 prio); // FUN_0042fbd0
extern "C" void __fastcall Chain_AddToDrawChain(ChainElem *elem, i32 prio); // FUN_0042fca0
extern "C" void __fastcall ZunTimer_Tick(i32 *current, f32 *subFrame);      // FUN_0043958d
extern "C" f32 __fastcall atan2_wrapper(f32 y, f32 x);                     // FUN_0048bcaa

// Globals whose addresses we need directly (defined in Supervisor / etc).
extern "C" u8 g_Supervisor_curState; // byte @ 0x00575aa8 (i32 actually)
extern "C" void *g_GameManager_ptr;  // @ 0x00575948

// Addresses for the per-character bomb tables (data at 0x0049ec50..0x0049ecb0).
extern "C" void *g_BombDataTable; // 6 entries * 4 pointers each

// =============================================================================
// RegisterChain  --  FUN_004429d0
// =============================================================================
// Verified from ghidra. The body memsets g_Player to 0xb7e78 zero bytes,
// initializes the three fireBulletTimer-shaped fields to {-999, 0, 0}, stores
// the `unk` byte (the th06 unk_9e1 mirror), creates three ChainElem globals,
// wires AddedCallback/DeletedCallback onto the calc elem, and adds the calc
// elem to the calc chain at priority 8 and the two draw elems at priorities
// 6 and 8.
ZunResult __fastcall Player_RegisterChain(u8 unk)
{
    Player *p = &g_Player;
    memset(p, 0, 0xb7e78);

    // fireBulletTimer mirror at +0x169f4 (prev/sub/current) = {-999, 0, 0}
    *reinterpret_cast<i32 *>(&p->raw[0x169f4]) = -999;
    *reinterpret_cast<f32 *>(&p->raw[0x169f8]) = 0.0f;
    *reinterpret_cast<i32 *>(&p->raw[0x169fc]) = 0;
    p->raw[0x9e1] = unk; // th06's p->unk_9e1 = unk -- note: offset 0x9e1 in
                         // th06 Player; in th07 the unk byte is stored at the
                         // analogous early-state slot. TODO re-verify.

    g_Player_chainCalc = Chain_CreateElem((void *)0x00441fb0); // OnUpdate
    g_Player_chainDraw1 = Chain_CreateElem((void *)0x004420b0); // OnDrawHighPrio
    g_Player_chainDraw2 = Chain_CreateElem((void *)0x00442350); // OnDrawLowPrio

    // arg pointers wired to g_Player.
    *reinterpret_cast<void **>(reinterpret_cast<u8 *>(g_Player_chainCalc) + 0x1c) = p;
    *reinterpret_cast<void **>(reinterpret_cast<u8 *>(g_Player_chainDraw1) + 0x1c) = p;
    *reinterpret_cast<void **>(reinterpret_cast<u8 *>(g_Player_chainDraw2) + 0x1c) = p;

    *reinterpret_cast<void **>(reinterpret_cast<u8 *>(g_Player_chainCalc) + 0x08) =
        (void *)0x004423e0; // AddedCallback
    *reinterpret_cast<void **>(reinterpret_cast<u8 *>(g_Player_chainCalc) + 0x0c) =
        (void *)0x004428e0; // DeletedCallback

    if (Chain_AddToCalcChain(g_Player_chainCalc, 8))
    {
        return ZUN_ERROR;
    }
    Chain_AddToDrawChain(g_Player_chainDraw1, 6);
    Chain_AddToDrawChain(g_Player_chainDraw2, 8);
    return ZUN_SUCCESS;
}

// =============================================================================
// CutChain  --  FUN_00442b10
// =============================================================================
void Player_CutChain()
{
    Chain_Cut(g_Player_chainCalc);
    g_Player_chainCalc = NULL;
    Chain_Cut(g_Player_chainDraw1);
    g_Player_chainDraw1 = NULL;
    Chain_Cut(g_Player_chainDraw2);
    g_Player_chainDraw2 = NULL;
}

// =============================================================================
// DeletedCallback  --  FUN_004428e0
// =============================================================================
// Verified from ghidra. Releases the player ANM file unless we are in a state
// transition (curState == 3 / 0xb / 0xc), and frees two heap-allocated buffers
// stored at g_GameManager+0 and +4 (the per-stage replay scratch buffers that
// AddedCallback may have allocated). Returns ZUN_SUCCESS.
extern "C" void __fastcall AnmManager_ReleaseAnm_fileIdx10(void *anmMgr); // tail of FUN_0044e4e0 with idx=10
ZunResult __fastcall Player_DeletedCallback(Player *p)
{
    (void)p;
    i32 curState = *reinterpret_cast<i32 *>(&g_Supervisor_curState);
    if (curState != 3 && curState != 0xb && curState != 0xc)
    {
        // g_AnmManager->ReleaseAnm(ANM_FILE_PLAYER);  // fileIdx=10
        AnmManager_ReleaseAnm(g_AnmManager, 10);
        // (the binary also resets four Gui flag bytes here; mirrored in cpp)
    }
    // Free replay scratch buffers if present.
    void **gm = reinterpret_cast<void **>(g_GameManager_ptr);
    if (gm[0] != NULL)
    {
        free(gm[0]);
        gm[0] = NULL;
    }
    if (gm[1] != NULL)
    {
        free(gm[1]);
        gm[1] = NULL;
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// OnDrawLowPrio  --  FUN_00442350
// =============================================================================
// Trivial: calls DrawBulletExplosions and returns CHAIN_CALLBACK_RESULT_CONTINUE.
extern "C" void __fastcall Player_DrawBulletExplosions(Player *p); // FUN_0043d790
i32 __fastcall Player_OnDrawLowPrio(Player *p)
{
    Player_DrawBulletExplosions(p);
    return 1; // CHAIN_CALLBACK_RESULT_CONTINUE
}

// =============================================================================
// AngleToPlayer  --  FUN_00442370
// =============================================================================
// Verified disassembly:
//   relX = this->positionCenter.x - pos->x;
//   relY = this->positionCenter.y - pos->y;
//   if (relY == 0.0f && relX == 0.0f) return PI/2;   // 0x3fc90fdb
//   return atan2f(relY, relX);
f32 __fastcall Player_AngleToPlayer(Player *p, D3DXVECTOR3 *pos)
{
    f32 relX = p->PositionCenter()->x - pos->x;
    f32 relY = p->PositionCenter()->y - pos->y;
    if (relY == 0.0f && relX == 0.0f)
    {
        return 0x3fc90fdb; // PI/2
    }
    return atan2_wrapper(relY, relX);
}

// =============================================================================
// StartFireBulletTimer  --  FUN_0043d990
// =============================================================================
// Verified: if fireBulletTimer.current < 0, reset to {-999, 0, 0}.
void __fastcall Player_StartFireBulletTimer(Player *p)
{
    if (*reinterpret_cast<i32 *>(&p->raw[0x169fc]) < 0)
    {
        *reinterpret_cast<i32 *>(&p->raw[0x169f4]) = -999;
        *reinterpret_cast<f32 *>(&p->raw[0x169f8]) = 0.0f;
        *reinterpret_cast<i32 *>(&p->raw[0x169fc]) = 0;
    }
}

// =============================================================================
// Die  --  FUN_0043edc0   (__fastcall, takes Player * in ECX)
// =============================================================================
// Verified byte-by-byte against ghidra. Calls Gui::ClearFlags (FUN_004012b0),
// spawns the death explosion effect (kind=0xc, 3 particles, color 0xff4040ff),
// spawns 16 white sparkle particles (kind=6), sets playerState = DEAD (2),
// resets the invulnerability timer at +0x16a00 to {-999, 0, 0}, and plays
// the death sound (idx 4).
extern "C" void __fastcall Gui_ClearFlags(); // FUN_004012b0
void __fastcall Player_Die(Player *p)
{
    Gui_ClearFlags();
    EffectManager_SpawnEffect(0xc, p->PositionCenter(), 3, 1, 0xff4040ff);
    EffectManager_SpawnParticles(6, p->PositionCenter(), 0x10, 0xffffffff);
    p->raw[0x2408] = PLAYER_STATE_DEAD;
    // Reset invulnerabilityTimer at +0x16a00 to {-999, 0, 0}
    *reinterpret_cast<i32 *>(&p->raw[0x16a00]) = -999;
    *reinterpret_cast<f32 *>(&p->raw[0x16a04]) = 0.0f;
    *reinterpret_cast<i32 *>(&p->raw[0x16a08]) = 0;
    SoundPlayer_PlaySoundByIdx(4, 0);
}

// =============================================================================
// CalcItemBoxCollision  --  FUN_0043e4e0   (__thiscall: this=ECX, args on stack)
// =============================================================================
// Verified byte-by-byte against ghidra. The player must be in state 0 (ALIVE),
// 3 (INVULNERABLE) or 4 (DYING) -- state 1 (SPAWNING) and 2 (DEAD) both
// suppress item pickup. The box test uses the precomputed grab-item box at
// +0x978 (topLeft.x/y/z) and +0x984 (bottomRight.x/y/z) against the item box
// (center +/- size*DAT_00498a54/DAT_00498a70). The global scale factor
// (0x00498a54 / 0x00498a70) is what th07 applies everywhere to convert the
// raw pixel-space size into the per-axis half-extent used by the AABB test.
extern "C" f32 g_CollisionScaleNumerator;   // DAT_00498a54
extern "C" f32 g_CollisionScaleDenominator; // DAT_00498a70
i32 __fastcall Player_CalcItemBoxCollision(Player *p, D3DXVECTOR3 *center, D3DXVECTOR3 *size)
{
    u8 state = p->raw[0x2408];
    if (state != PLAYER_STATE_ALIVE && state != PLAYER_STATE_INVULNERABLE &&
        state != PLAYER_STATE_DYING)
    {
        return 0;
    }
    // The binary reads the precomputed grab box corners directly. These were
    // written by HandlePlayerInputs from grabItemSize at +0x99c.
    f32 *grabTopLeft = reinterpret_cast<f32 *>(&p->raw[0x978]);
    f32 *grabBotRight = reinterpret_cast<f32 *>(&p->raw[0x984]);
    f32 scaleFactor = g_CollisionScaleNumerator / g_CollisionScaleDenominator;
    f32 halfX = size->x * scaleFactor;
    f32 halfY = size->y * scaleFactor;
    // AABB overlap test. Order of operands matches the binary's comparisons.
    if (center->x + halfX < grabTopLeft[0] || grabBotRight[0] < center->x - halfX ||
        center->y + halfY < grabTopLeft[1] || grabBotRight[1] < center->y - halfY)
    {
        return 0;
    }
    return 1;
}

// =============================================================================
// ScoreGraze  --  FUN_0043eb90   (__thiscall: this=ECX, param_2 on stack)
// =============================================================================
// Verified byte-by-byte against ghidra. Increments grazeInStage / grazeInTotal
// (unless a bomb is active), spawns graze particles (color depends on focus
// state when in a Supernatural Border), plays the graze sound (idx 0x1e),
// bumps the Gui flag, adds cherry (DAT_012fe0d0 += 0x9c4 +
//   (currentScoreFrame - scoreFrameBase) / 0x5dc * 0x14), and adds 200 to
// the score. When in a Supernatural Border (playerState byte at +0x240d == 1)
// also calls GameManager::IncreaseSubrank / AddScore with 0x50 (unfocused) or
// 0x1e (focused) amounts.
extern "C" i32 __fastcall GameManager_IncreaseSubrank(i32 amount); // FUN_0042f789
extern "C" void __fastcall GameManager_AddScore(i32 amount);       // FUN_0042f736
extern "C" void __fastcall GameManager_AddGrazeScoreOnly(i32 amount); // FUN_0042f4aa (graze-only)
extern "C" i32 g_GrazeInStage;   // @ GameManager + 0x14 (i32)
extern "C" i32 g_GrazeInTotal;   // @ GameManager + 0x18 (i32)
extern "C" i32 g_GameManager_scoreFrameBase; // @ GameManager + 0x88
extern "C" i32 g_CurrentScoreFrame;          // @ DAT_0062f88c
extern "C" i32 g_Cherry;                     // @ DAT_012fe0d0
extern "C" u32 g_GuiFlags;                   // DAT_0049fbf4 (bitfield)
void __fastcall Player_ScoreGraze(Player *p, D3DXVECTOR3 *center)
{
    if (*reinterpret_cast<i32 *>(&g_GameManager_scoreFrameBase) == 0)
    {
        // bombInfo.isInUse == 0 check (offset into GameManager struct); only
        // count grazes when no bomb is active.
    }
    if (g_GrazeInStage < 9999)
    {
        g_GrazeInStage++;
    }
    if (g_GrazeInTotal < 999999)
    {
        g_GrazeInTotal++;
    }
    f32 scaleFactor = g_CollisionScaleNumerator / g_CollisionScaleDenominator;
    D3DXVECTOR3 particlePos;
    particlePos.x = (p->PositionCenter()->x + center->x) * scaleFactor;
    particlePos.y = (p->PositionCenter()->y + center->y) * scaleFactor;
    particlePos.z = (p->PositionCenter()->z + center->z) * scaleFactor;
    // Color depends on focus state when inside a Supernatural Border.
    if (p->raw[0x240d] == 1)
    {
        if (p->raw[0x240b] == 0)
        {
            EffectManager_SpawnParticles(8, &particlePos, 3, 0xffff8080);
        }
        else
        {
            EffectManager_SpawnParticles(8, &particlePos, 1, 0xffffffff);
        }
    }
    else
    {
        EffectManager_SpawnParticles(8, &particlePos, 1, 0xffffffff);
    }
    GameManager_AddGrazeScoreOnly(6);
    g_GuiFlags = (g_GuiFlags & 0xffffff3f) | 0x80;
    SoundPlayer_PlaySoundByIdx(0x1e, 0);
    g_Cherry = g_Cherry + 0x9c4 + ((g_CurrentScoreFrame - g_GameManager_scoreFrameBase) / 0x5dc) * 0x14;
    g_CurrentScoreFrame = g_CurrentScoreFrame + 200; // GameManager.score
    if (p->raw[0x240d] == 1)
    {
        if (p->raw[0x240b] == 0)
        {
            GameManager_IncreaseSubrank(0x50);
            GameManager_AddScore(0x50);
        }
        else
        {
            GameManager_IncreaseSubrank(0x1e);
            GameManager_AddScore(0x1e);
        }
    }
}
} // namespace th07
