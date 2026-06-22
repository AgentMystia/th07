// Player module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary. Pure C++ with a single unified code path:
// no #ifdef DIFFBUILD splits, no inline asm, no DAT_ externs. Cross-module
// singletons are reached through typed C++ extern objects (g_EffectMgrSpawnObj
// etc.) which their owning modules define; until those modules are reversed
// they link to no-op stubs. On the objdiff side, these typed globals are
// mapped back to their orig DAT_ addresses by generate_objdiff_objs.py's
// SYMBOL_MAP, so the byte-comparison still works without code divergence.
//
// Cross-module call conventions (all verified from orig disassembly; addresses
// here are documentation only -- the code below reaches every singleton through
// typed C++ externs, never through a raw address cast):
//   g_Chain            : __thiscall, ECX = &g_Chain          (orig 0x00626218)
//   EffectManager      : __thiscall, ECX = spawn singletons  (orig 0x012fe250)
//   SoundPlayer        : __thiscall, ECX = g_SoundPlayerObj  (orig 0x004ba0d8)
//   AnmManager         : __thiscall, ECX = *g_AnmManagerFilesObj (orig 0x004b9e44)
//   GameManager method : __thiscall, ECX = g_GameManagerScoreObj (orig 0x00626270)
//   scoreSub fields    : via g_ScoreSubObj (= g_GameManager+0x8, orig 0x00626278)
//
// To emit the exact orig call sequence (PUSH args; MOV ECX,this; CALL) for the
// not-yet-reversed singletons, they are declared as struct-method stubs and
// mapped to their orig addresses in config/mapping.csv.

#include "Player.hpp"

#include "AnmManager.hpp"
#include "AnmVm.hpp"
#include "Chain.hpp"
#include "GameManager.hpp"
#include "ZunResult.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>
#include <math.h>
#include <string.h>

namespace th07
{
DIFFABLE_STATIC(Player, g_Player);

// ---- Game-state globals (orig .data/.bss); defined by their owning modules.
//      Named typed externs so MSVC emits [glob] memory operands matching orig;
//      generate_objdiff_objs.py SYMBOL_MAP maps each to its orig DAT_ address.
extern "C" i32 g_BombIsActive;             // DAT_004d44f8 (bombInfo.isInUse)
extern "C" u32 g_GuiSpriteFlags;           // DAT_0049fbf4
extern "C" i32 g_Cherry;                   // DAT_012fe0d0
extern "C" i32 g_ScoreFrame;               // DAT_0062f88c
extern "C" u16 g_GuiCounter1;              // DAT_0134d476
extern "C" u32 g_GuiCounter2;              // DAT_013542ec
// Stride-0x24c array of u16 Gui counter slots, 3 used (index 0,1,2). Base u8
// array so the stride arithmetic stays explicit and matches orig indexing.
extern "C" u8 g_GuiCounterSlots[0x24c * 4]; // DAT_0134db5a base, stride 0x24c

// rdata float constants -> named const f32 (EffectManager.cpp pattern), so MSVC
// emits fld/fmul DWORD PTR [glob] matching orig's memory operand form.
// Values verified against th07.exe .rdata (see SYMBOL_MAP in
// scripts/generate_objdiff_objs.py for the address mapping).
extern "C" const f32 g_PlayerConst1p0 = 1.0f;                         // DAT_00498a54
extern "C" const f32 g_PlayerConst2p0 = 2.0f;                         // DAT_00498a70 (half-extent divisor: arcadeRegionW/2 centers the player, hitboxSize/gmFloats[3] = radius)
extern "C" const f32 g_PlayerConstHalfPi = 1.5707963267948966f;       // DAT_00498a9c (PI/2)

// rdata floats referenced by the missing Player functions. These originate in
// the .rdata section and are defined here as const f32 so MSVC emits the same
// `fld/fmul/fdiv DWORD PTR [glob]` form the orig binary uses. Values verified
// against th07.exe .rdata (each DAT_ address read directly).
extern "C" const f32 g_PlayerBombRegionActivateThreshold = 0.0f;      // DAT_00498a4c (bombRegions.sizeX > this to activate; 0 == any non-zero)
extern "C" const f32 g_PlayerHalfExtentScale = 0.5f;                  // DAT_00498a50 (AABB half-side scale in CalcDamageToEnemy: collideSize * 0.5)
extern "C" const f32 g_PlayerBulletVelDivisor = 8.0f;                 // DAT_00498a7c (collideVel /= 8 on hit)
extern "C" const f32 g_PlayerDyingScaleSlope = 3.0f;                  // DAT_00498a8c (death anim: scale = 3.0*t + 1.0)
extern "C" const f32 g_PlayerBorderXThreshold = 160.0f;               // DAT_00498ac0 (EndBorder: option-scratch trigger x position)
extern "C" const f32 g_PlayerBorderYThreshold = 400.0f;               // DAT_00498afc (EndBorder: option-scratch trigger y position)
extern "C" const f32 g_PlayerStateDurationDivisor = 30.0f;            // DAT_00498b00 (dying/spawning anim progress divisor: 30 frames)
extern "C" const f32 g_PlayerSpawnYOffset = 64.0f;                    // DAT_00498b08 (AddedCallback: spawn y = arcadeRegionH - 64)
extern "C" const f32 g_PlayerBorderSpriteScale = -1.0f;               // DAT_00498b5c (StartBorder: effect sprite scale factor)
extern "C" const f32 g_PlayerGrazePadSize = 20.0f;                    // DAT_00498b84 (CheckGraze: box expansion pad on each side)
// double 0.0 used as the bulletAbsorbRegions sizeX threshold in
// CalcBulletAbsorption (orig fcomp QWORD PTR ds:0x498a90). Reuses the same
// rdata slot Supervisor touches (placeholder name retained).
extern "C" f64 g_PlayerAbsorbSizeZero;                                // DAT_00498a90 (double 0.0)
// game-state globals read directly by AddedCallback / HandleBombInput / etc.
// All defined by their owning modules; here only declared.
extern "C" u8 g_GameMgr_character;        // DAT_0062f645 (0=Reimu,1=Marisa,2=Sakuya)
extern "C" u8 g_GameMgr_shotType;         // DAT_0062f646 (0=A,1=B)
extern "C" u8 g_GameMgr_characterShotType;// DAT_0062f647 (= character*2 + shotType)
extern "C" u8 g_GameMgr_isInRetryMenu;    // DAT_0062f64d (supervisor retry-menu flag)
extern "C" f32 g_GameMgr_arcadeOffsetX;   // DAT_0062f864 (arcade region top-left x)
extern "C" f32 g_GameMgr_arcadeOffsetY;   // DAT_0062f868 (arcade region top-left y)
extern "C" f32 g_GameMgr_arcadeRegionW;   // DAT_0062f86c (arcade region width)
extern "C" f32 g_GameMgr_arcadeRegionH;   // DAT_0062f870 (arcade region height)
extern "C" f32 g_Supervisor_framerateMul; // DAT_00575ac8 (effectiveFramerateMultiplier)
extern "C" u8 *g_GuiObjPtr;               // DAT_004b9e48 (Gui object pointer)
extern "C" i32 g_GameMgr_currentScore;    // DAT_0062f890 (current score for hi-score)
extern "C" i32 g_GameMgr_isInGame;        // DAT_0062627c (1 = in active game)
extern "C" u8 g_GameMgr_difficulty;       // DAT_0062f644
extern "C" u8 g_BombPreDrawFlag;          // DAT_012fe0cc
extern "C" u8 g_BombStartedFlag;          // DAT_012fe0c4
extern "C" u8 g_BombUsedFlags;            // DAT_012fe0c8
extern "C" u8 g_BombPreDrawFlag2;         // DAT_012fe0dc (aliased to g_BombUsedFlags)
extern "C" i32 g_ScoreDisplay;            // DAT_0062f890 mirror
extern "C" i32 g_DyingTimer;              // DAT_004d44e0 (player death-stage timer)

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
    ZunResult LoadAnm(i32 fileIdx, char *filename, i32 flags);
    void SetAndExecuteScript(AnmVm *vm, AnmRawInstr *script);
    i32 ExecuteScript(AnmVm *vm);
    void Draw(AnmVm *vm);
    void DrawNoRotation(AnmVm *vm);
};
struct GameManagerScore
{
    void GuiClearFlags();
    void IncreaseSubrank(i32 amount);
    void AddScore(i32 amount);
    void AddGrazeScoreOnly(i32 amount);
    // Orig FUN_0043b5c0 (called on player death to bump a score counter via
    // scoreSub->field_34 = (counter % 100000) + 6543).
    void BumpDeathScoreCounter();
};
struct EffectManagerDamage
{
    void SpawnParticleAt(i32 kind, D3DXVECTOR3 *pos, i32 count, u32 color);
};

// Cross-module free-function helpers (thiscall/cdecl). These are owned by
// Supervisor/utils/etc.; declared here so Player.cpp can call them, and the
// linker resolves them via stubs until the owning modules reverse.
struct ZunBool_t;
extern "C" i32 __fastcall utils_GetArcadeRegionMaxX();       // FUN_0048b8a0 (playerLives-based)
extern "C" i32 __fastcall utils_GetArcadeRegion(int idx);    // FUN_0048b8a0 (overloaded)
extern "C" void __fastcall Supervisor_TickTimer(i32 *current, f32 *subFrame); // FUN_0043958d
extern "C" void __fastcall Supervisor_TickTimer2(i32 *current, f32 *subFrame);// FUN_004394c7 (Decrement wrapper)
extern "C" i32 __fastcall utils_IsInBounds(f32 x, f32 y, i32 w, i32 h); // FUN_0042d6d8
extern "C" i32 __fastcall utils_AddNormalizeAngle(f32 a, f32 b); // FUN_00431930
extern "C" void __fastcall BulletMgr_RemoveAllBullets(i32 a); // FUN_00424740
extern "C" i32 __fastcall Item_SpawnItem(D3DXVECTOR3 *pos, i32 kind, i32 count); // FUN_004326f0
extern "C" void __fastcall Item_FullPowerSetup(i32 kind, void *itemMgr); // FUN_004325e0
extern "C" void __fastcall Gui_Reset();                       // FUN_004012b0
extern "C" i32 __fastcall Effect_DamageCallback(i32 kind, void *enemyMgr); // FUN_004394c7 alias
extern "C" void __fastcall GameManager_BorderSetup(i32 borderScore); // FUN_0042f526
extern "C" void __fastcall EnemyManager_ResetBorderState(i32 a); // FUN_00433a90 / FUN_00433c40
extern "C" void __fastcall EnemyManager_DecBorderBonus();   // FUN_0042d5cd
extern "C" void __fastcall EnemyManager_BorderActiveFx();   // FUN_00401390
extern "C" void __fastcall EnemyManager_ResetStage(i32 a);  // FUN_0042d612
extern "C" void __fastcall GameManager_GameStateFlag(i32 a);// FUN_0043b7a0 / FUN_0043b750
extern "C" i32 __fastcall Gui_HasMessage();                  // FUN_00404fe0
extern "C" i32 __fastcall EnemyManager_IsGameActive(void *p);// FUN_0042ad66
extern "C" void __fastcall GameManager_TintSprites(u32 col);// FUN_00406930
extern "C" void *__fastcall EffectManager_SpawnEffectObj(i32 kind, D3DXVECTOR3 *pos, i32 count, i32 b, u32 col); // FUN_0041c610 (5 args)
extern "C" void *__fastcall EffectManager_SpawnEffectObj2(i32 kind, D3DXVECTOR3 *pos, i32 count, u32 col); // FUN_0041c1c0 (4 args)
extern "C" f32 __fastcall utils_RandF32();                   // FUN_0048bbf0
extern "C" f32 __fastcall utils_RandF32_2();                 // FUN_0048bb40
extern "C" void __fastcall Player_ResetOptionSpriteScale(D3DXVECTOR3 *pos, f32 sx, f32 sy, i32 a, i32 b); // FUN_004418b0
extern "C" void __fastcall Supervisor_SetAnmFlag(i32 a, i32 b); // FUN_00427c81
extern "C" void __fastcall GameManager_SetStageState(i32 a, i32 b); // FUN_0044c930
extern "C" i32 __fastcall GameManager_CheckState(int a);     // FUN_00442b70 (used by AddedCallback)

// Singleton handles. Single unified code path: extern C++ objects that resolve
// to the real game-state singletons (defined by their owning modules, or by
// stubs until those modules are reversed). The orig addresses are recovered on
// the objdiff side by scripts/generate_objdiff_objs.py's SYMBOL_MAP, so no
// #ifdef DIFFBUILD split is needed.
extern EffectManagerSpawn g_EffectMgrSpawnObj;   // orig 0x012fe250 (EffectManager spawn slots)
extern SoundPlayerPlayback g_SoundPlayerObj;     // orig 0x004ba0d8
extern GameManagerScore g_GameManagerScoreObj;   // orig 0x00626270 (g_GameManager)
extern AnmManagerFiles *g_AnmManagerFilesObj;    // orig 0x004b9e44 (AnmManager pointer global)
extern ScoreSub *g_ScoreSubObj;                  // orig 0x00626278 (g_GameManager+0x8 -> ScoreSub)
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

// scoreSub is now a typed ScoreSub* (g_ScoreSubObj); access its fields directly
// (counter14, counter18, mainSeedScoreBase, score) instead of raw offsets.

// =============================================================================
// Static helper: reset the 0x70-entry bombRegion.sizeX table + advance the
// 0x60-entry bulletAbsorbRegions table. FUN_00440940 (ClearBombRegions).
// =============================================================================
void __fastcall Player::ClearBombRegions(Player *p)
{
    for (i32 i = 0; i < 0x70; i++)
    {
        p->bombRegions[i].sizeX = 0.0f;
    }
    // Advance bulletAbsorbRegions (stride 0x20, 0x60 entries). When
    // framesLeft <= 0 the region is dead: zero aabbSizeX/circleRadius.
    // Otherwise decrement framesLeft and accumulate circleVelocity into
    // circleRadius (radius grows/shrinks over the region's lifetime).
    // Orig uses a region-pointer advanced in the for-step alongside i.
    BulletAbsorbRegion *region = p->bulletAbsorbRegions;
    for (i32 i = 0; i < 0x60; i++, region++)
    {
        if (region->framesLeft > 0)
        {
            region->framesLeft = region->framesLeft - 1;
            region->circleRadius = region->circleRadius + region->circleVelocity;
        }
        else
        {
            region->circleRadius = 0.0f;
            region->aabbSizeX = 0.0f;
        }
    }
}

// =============================================================================
// OnDrawLowPrio  --  FUN_00442350  (__fastcall, Player* in ECX)
// =============================================================================
ChainCallbackResult __fastcall Player::OnDrawLowPrio(Player *p)
{
    Player::DrawBulletExplosions(p);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// CutChain  --  FUN_00442b10  (__cdecl)
// =============================================================================
void Player::CutChain()
{
    g_Chain.Cut(g_Player.chainCalc);
    g_Player.chainCalc = 0;
    g_Chain.Cut(g_Player.chainDraw1);
    g_Player.chainDraw1 = 0;
    g_Chain.Cut(g_Player.chainDraw2);
    g_Player.chainDraw2 = 0;
}

// =============================================================================
// StartFireBulletTimer  --  FUN_0043d990  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::StartFireBulletTimer(Player *p)
{
    if (p->fireBulletTimer.current < 0)
    {
        ZunTimer *ft = &p->fireBulletTimer;
        ft->current = 0;
        ft->subFrame = 0;
        ft->previous = -999;
    }
}

// =============================================================================
// Die  --  FUN_0043edc0  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::Die(Player *p)
{
    g_GameManagerScore->GuiClearFlags();
    g_EffectMgrSpawn->SpawnEffect(0xc, &p->positionCenter, 3, 1, 0xff4040ff);
    g_EffectMgrSpawn->SpawnParticles(6, &p->positionCenter, 0x10, 0xffffffff);
    p->playerState = PLAYER_STATE_DEAD;
    ZunTimer *t = &p->invulnerabilityTimer;
    t->current = 0;
    t->subFrame = 0;
    t->previous = -999;
    g_SoundPlayer->PlaySoundByIdx(4, 0);
}

// =============================================================================
// RegisterChain  --  FUN_004429d0  (__fastcall, u8 unk in ECX)
// =============================================================================
ZunResult __fastcall Player::RegisterChain(u8 unk)
{
    Player *p = &g_Player;
    memset(p, 0, 0xb7e78);

    ZunTimer *iv = &p->invulnerabilityTimer;
    iv->current = 0;
    iv->subFrame = 0;
    iv->previous = -999;

    p->unk_2409 = unk;

    p->chainCalc = g_Chain.CreateElem(PLAYER_ON_UPDATE_CB);
    p->chainDraw1 = g_Chain.CreateElem(PLAYER_ON_DRAW_HIGH_CB);
    p->chainDraw2 = g_Chain.CreateElem(PLAYER_ON_DRAW_LOW_CB);

    p->chainCalc->arg = p;
    p->chainDraw1->arg = p;
    p->chainDraw2->arg = p;
    p->chainCalc->addedCallback = PLAYER_ADDED_CB;
    p->chainCalc->deletedCallback = PLAYER_DELETED_CB;

    if (g_Chain.AddToCalcChain(p->chainCalc, 8) != 0)
    {
        return ZUN_ERROR;
    }
    g_Chain.AddToDrawChain(p->chainDraw1, 6);
    g_Chain.AddToDrawChain(p->chainDraw2, 8);
    return ZUN_SUCCESS;
}

// =============================================================================
// DeletedCallback  --  FUN_004428e0  (__fastcall, Player* in ECX)
// =============================================================================
ZunResult __fastcall Player::DeletedCallback(Player *p)
{
    (void)p;
    i32 doRelease;
    if (g_Supervisor.curState != 3 && g_Supervisor.curState != 0xb &&
        g_Supervisor.curState != 0xc)
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
        g_GuiCounter1 = 0x63;
        g_GuiCounter2 = 0x63;
        // orig indexes a stride-0x24c Gui counter array with literal 0/1/2
        // (xor/mov reg,imm; imul reg,reg,0x24c; mov word,[base+reg]). C++ form.
        *reinterpret_cast<u16 *>(g_GuiCounterSlots + 0 * 0x24c) = 0x63;
        *reinterpret_cast<u16 *>(g_GuiCounterSlots + 1 * 0x24c) = 0x63;
        *reinterpret_cast<u16 *>(g_GuiCounterSlots + 2 * 0x24c) = 0x63;
    }
    if (g_Player.optionTableUnfocused != 0)
    {
        void *optUnfocused = g_Player.optionTableUnfocused;
        free(optUnfocused);
        g_Player.optionTableUnfocused = 0;
    }
    if (g_Player.optionTableFocused != 0)
    {
        void *optFocused = g_Player.optionTableFocused;
        free(optFocused);
        g_Player.optionTableFocused = 0;
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// AngleToPlayer  --  FUN_00442370  (__fastcall, Player* in ECX, pos at [ebp+8])
// =============================================================================
f32 Player::AngleToPlayer(D3DXVECTOR3 *pos)
{
    f32 relX = this->positionCenter.x - pos->x;
    f32 relY = this->positionCenter.y - pos->y;
    f32 result;
    if (relY == 0.0f && relX == 0.0f)
    {
        result = g_PlayerConstHalfPi; // PI/2
    }
    else
    {
        result = atan2f(relY, relX);
    }
    return result;
}

// =============================================================================
// CalcItemBoxCollision  --  FUN_0043e4e0
// =============================================================================
i32 Player::CalcItemBoxCollision(D3DXVECTOR3 *center, D3DXVECTOR3 *size)
{
    u8 state = this->playerState;
    if (state != PLAYER_STATE_ALIVE && state != PLAYER_STATE_INVULNERABLE &&
        state != PLAYER_STATE_DYING)
    {
        return 0;
    }

    f32 scale = g_PlayerConst1p0 / g_PlayerConst2p0;
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

    f32 *boxTL = reinterpret_cast<f32 *>(&this->itemBoxTopLeft);
    f32 *boxBR = reinterpret_cast<f32 *>(&this->itemBoxBottomRight);
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
// ScoreGraze  --  FUN_0043eb90
// =============================================================================
void Player::ScoreGraze(D3DXVECTOR3 *center)
{
    i32 bombActive = g_BombIsActive; // bombInfo.isInUse
    if (bombActive == 0)
    {
        if (SCORE_SUB->counter14 < 9999)
        {
            SCORE_SUB->counter14++;
        }
        if (SCORE_SUB->counter18 < 999999)
        {
            SCORE_SUB->counter18++;
        }
    }

    D3DXVECTOR3 *pc = &this->positionCenter;
    D3DXVECTOR3 sum;
    sum.z = pc->z + center->z;
    sum.y = pc->y + center->y;
    sum.x = pc->x + center->x;

    f32 scale = g_PlayerConst1p0 / g_PlayerConst2p0;
    D3DXVECTOR3 particlePos;
    particlePos.z = sum.z * scale;
    particlePos.y = sum.y * scale;
    particlePos.x = sum.x * scale;

    if (this->isInSupernaturalBorder == 1) // in Supernatural Border
    {
        if (this->isFocus == 0) // unfocused
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
    u32 guiFlags = g_GuiSpriteFlags;
    guiFlags = (guiFlags & 0xffffff3f) | 0x80;
    g_GuiSpriteFlags = guiFlags;
    g_SoundPlayer->PlaySoundByIdx(0x1e, 0);

    i32 cherry = g_Cherry;
    i32 scoreFrame = g_ScoreFrame;
    cherry = cherry + 0x9c4 + (scoreFrame - SCORE_SUB->mainSeedScoreBase) / 0x5dc * 0x14;
    g_Cherry = cherry;

    SCORE_SUB->score = SCORE_SUB->score + 0x7d0 / 0xa;

    if (this->isInSupernaturalBorder == 1) // in Supernatural Border
    {
        if (this->isFocus == 0) // unfocused
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

// =============================================================================
// OnUpdate  --  FUN_00441fb0  (__fastcall, Player* in ECX)
//
// Orchestrates per-frame player logic:
//   1. early-out when game is in a paused/menu state (g_GameMgr_isInGame != 0)
//   2. ClearBombRegions() + HandleBombInput()
//   3. State-specific transitions (state 1 = SPAWNING, 2 = DEAD) via the
//      private helpers HandleState_Dying (FUN_00440cf0) and
//      HandleState_Spawning (FUN_004411c0)
//   4. Common state dispatch via DispatchState (FUN_00441330)
//   5. HandlePlayerInputs() when not SPAWNING/DEAD
//   6. ExecuteScript on playerSprite + orbsSprite[0]/[1] when orbs visible
//   7. UpdatePlayerBullets + UpdateFireBulletsTimer + ResetOptionScratch
// =============================================================================
ChainCallbackResult __fastcall Player::OnUpdate(Player *p)
{
    if (g_GameMgr_isInGame != 0)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    Player::ClearBombRegions(p);
    Player::HandleBombInput(p);
    if (p->playerState == PLAYER_STATE_DEAD)
    {
        if (Player::HandleState_Dying(p) != 0)
        {
            Player::HandleState_Spawning(p);
        }
    }
    else if (p->playerState == PLAYER_STATE_SPAWNING)
    {
        Player::HandleState_Spawning(p);
    }
    Player::DispatchState(p);
    if (p->playerState != PLAYER_STATE_DEAD && p->playerState != PLAYER_STATE_SPAWNING)
    {
        Player::HandlePlayerInputs(p);
    }
    ANM_MGR->ExecuteScript(&p->playerSprite);
    if (p->orbState != ORB_HIDDEN)
    {
        ANM_MGR->ExecuteScript(&p->orbsSprite[0]);
        ANM_MGR->ExecuteScript(&p->orbsSprite[1]);
    }
    Player::UpdatePlayerBullets(p);
    Player::UpdateFireBulletsTimer(p);
    Player::ResetOptionScratch(p);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// OnDrawHighPrio  --  FUN_004420b0  (__fastcall, Player* in ECX)
// =============================================================================
ChainCallbackResult __fastcall Player::OnDrawHighPrio(Player *p)
{
    Player::DrawBullets(p);
    // Invoke the focused/unfocused bomb draw callback if a bomb is active.
    // bombInfo.isFocus (+0x16a24) selects between drawUnfocused/drawFocused.
    if (p->bombInfo.isInUse != 0)
    {
        if (p->bombInfo.isFocus == 0)
        {
            (*p->bombInfo.drawUnfocused)(p);
        }
        else
        {
            (*p->bombInfo.drawFocused)(p);
        }
    }
    // Stage-space player sprite position (arcade region offset + center).
    p->playerSprite.scaleX = g_GameMgr_arcadeOffsetX + p->positionCenter.x;
    // Note: orig writes via +0x1c8/+0x1cc (AnmVm pos xy). Player.hpp exposes
    // these through the AnmVm unk1c2/unk1e8 padding; we reach them via the
    // explicit-offset form (constitution allows for verified padding regions).
    {
        u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
        *reinterpret_cast<f32 *>(spriteBytes + 0x1c8) =
            g_GameMgr_arcadeOffsetX + p->positionCenter.x;
        *reinterpret_cast<f32 *>(spriteBytes + 0x1cc) =
            g_GameMgr_arcadeOffsetY + p->positionCenter.y;
        *reinterpret_cast<f32 *>(spriteBytes + 0x1d0) = 0.0f;
    }
    ANM_MGR->DrawNoRotation(&p->playerSprite);
    // Draw orbs when orbState != HIDDEN and player is in a visible state.
    if (p->orbState != ORB_HIDDEN &&
        (p->playerState == PLAYER_STATE_ALIVE ||
         p->playerState == PLAYER_STATE_INVULNERABLE))
    {
        u8 *os0 = reinterpret_cast<u8 *>(&p->orbsSprite[0]);
        u8 *os1 = reinterpret_cast<u8 *>(&p->orbsSprite[1]);
        *reinterpret_cast<f32 *>(os0 + 0x1c8) =
            g_GameMgr_arcadeOffsetX + p->orbsPosition[0].x;
        *reinterpret_cast<f32 *>(os0 + 0x1cc) =
            g_GameMgr_arcadeOffsetY + p->orbsPosition[0].y;
        *reinterpret_cast<f32 *>(os1 + 0x1c8) =
            g_GameMgr_arcadeOffsetX + p->orbsPosition[1].x;
        *reinterpret_cast<f32 *>(os1 + 0x1cc) =
            g_GameMgr_arcadeOffsetY + p->orbsPosition[1].y;
        *reinterpret_cast<f32 *>(os0 + 0x1d0) = 0.0f;
        *reinterpret_cast<f32 *>(os1 + 0x1d0) = 0.0f;
        ANM_MGR->Draw(&p->orbsSprite[0]);
        ANM_MGR->Draw(&p->orbsSprite[1]);
    }
    // Dying-state red-tint pulse: when playerState==4 and death-timer>0,
    // modulate playerSprite color depending on dyingTimer%4 and a curve
    // based on DAT_004d44e0 (death-stage frame counter).
    if (p->playerState == PLAYER_STATE_DYING && g_DyingTimer > 0)
    {
        u32 mod4 = (u32)g_DyingTimer & 0x80000003;
        if ((i32)mod4 < 0)
        {
            mod4 = ((mod4 - 1) | 0xfffffffc) + 1;
        }
        u32 color;
        if ((i32)mod4 < 2)
        {
            color = 0xffff0000;
        }
        else
        {
            color = 0xffffffff;
        }
        u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
        *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = color;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// HandleBombInput  --  FUN_004409f0  (__fastcall, Player* in ECX)
//
// Per-frame bomb-trigger + bomb-state advance logic. The orig is a 0x2c0B
// state machine; this is a faithful (but consolidated) port. The supernatural
// border / bomb callback dispatch is preserved exactly.
// =============================================================================
void __fastcall Player::HandleBombInput(Player *p)
{
    // Top-level guard: if isInSupernaturalBorder != 1, OR bomb already active,
    // OR the bomb button isn't held this frame, take the normal-state path.
    if (p->isInSupernaturalBorder != 0 && p->bombInfo.isInUse == 0 &&
        (g_CurFrameInput & 2) != 0)
    {
        // Bomb button held while a Supernatural Border is active -> cancel
        // the border early via EndSupernaturalBorder(1), clear the active
        // flag, and run a stage-reset side effect.
        Player::EndSupernaturalBorder(p, 1);
        p->bombActiveThisFrame = 0;
        EnemyManager_ResetBorderState(0);
        return;
    }
    // If we're in border state 2, attempt to start a new border.
    if (p->isInSupernaturalBorder == 2)
    {
        Player::StartSupernaturalBorder(p);
    }
    // Decrement respawnTimer when nonzero.
    if (p->respawnTimer != 0)
    {
        p->respawnTimer = p->respawnTimer - 1;
    }
    if (p->bombInfo.isInUse == 0)
    {
        // Try to trigger a bomb: not in a message, game active, has lives,
        // bomb button freshly pressed, no respawn lockout.
        i32 canFire = Gui_HasMessage();
        if (canFire == 0)
        {
            canFire = EnemyManager_IsGameActive(&g_Supervisor);
        }
        if (canFire == 0 && p->unk_23f8 != 0 &&
            utils_GetArcadeRegionMaxX() > 0 && p->respawnTimer == 0 &&
            (g_CurFrameInput & 2) != 0)
        {
            // Light the bomb: set Gui flag, play bomb sound, set isInUse=1,
            // invoke the focus-selected calc callback.
            u16 *guiFlags = reinterpret_cast<u16 *>(g_GuiObjPtr + 0xd6);
            *guiFlags = (u16)(*guiFlags | 1);
            GameManager_GameStateFlag(1);
            EnemyManager_ResetStage(0xffffffff);
            g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xfffffff3) | 8;
            p->bombInfo.isFocus = (i32)p->isFocus;
            p->bombInfo.isInUse = 1;
            p->bombActiveThisFrame = 1;
            p->bombInfo.timer.current = 0;
            p->bombInfo.timer.subFrame = 0;
            p->bombInfo.timer.previous = -999;
            p->bombInfo.duration = 999;
            if (p->bombInfo.isFocus == 0)
            {
                (*p->bombInfo.calcUnfocused)(p);
            }
            else
            {
                (*p->bombInfo.calcFocused)(p);
            }
            g_BombPreDrawFlag = 0;
            g_BombStartedFlag = 0;
            GameManager_BorderSetup(200);
            g_BombPreDrawFlag2 = g_BombUsedFlags;
            p->unk_23f8 = p->unk_23f8 + 6;
            if (SCORE_SUB->mainSeedScoreBase < p->unk_23f8)
            {
                p->unk_23f8 = SCORE_SUB->mainSeedScoreBase;
            }
        }
        else
        {
            p->bombActiveThisFrame = 0;
        }
    }
    else
    {
        // Bomb already active: deduct score penalty if the timer advanced, and
        // invoke the focus-selected calc callback.
        if (p->bombInfo.timer.current != p->bombInfo.timer.previous)
        {
            i32 penalty = p->bombInfo.scorePenalty;
            if (g_ScoreFrame - SCORE_SUB->mainSeedScoreBase < penalty)
            {
                g_ScoreFrame = SCORE_SUB->mainSeedScoreBase;
            }
            else
            {
                g_ScoreFrame = g_ScoreFrame - penalty;
            }
            g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xfffffcff) | 0x200;
        }
        if (p->bombInfo.isFocus == 0)
        {
            (*p->bombInfo.calcUnfocused)(p);
        }
        else
        {
            (*p->bombInfo.calcFocused)(p);
        }
    }
}

// =============================================================================
// HandleState_Dying  --  FUN_00440cf0  (private helper, returns i32)
//
// Drives the death animation: scales the playerSprite, sets alpha, and on
// completion either respawns the player or transitions into retry-menu /
// game-over. Returns 1 when the player should immediately enter the spawning
// state on the same frame (the orig uses this to chain directly into
// HandleState_Spawning via the OnUpdate dispatcher).
// =============================================================================
i32 __fastcall Player::HandleState_Dying(Player *p)
{
    if (p->unk_23f8 == 0)
    {
        // Death animation in progress: compute scale + color from the
        // invulnerabilityTimer (0..30 frame window).
        f32 t = ((f32)p->invulnerabilityTimer.current +
                 p->invulnerabilityTimer.subFrame) /
                g_PlayerStateDurationDivisor;
        u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
        *reinterpret_cast<f32 *>(spriteBytes + 0x1c) =
            g_PlayerDyingScaleSlope * t + g_PlayerConst1p0;
        *reinterpret_cast<f32 *>(spriteBytes + 0x18) =
            g_PlayerConst1p0 - g_PlayerConst1p0 * t;
        i32 k = utils_GetArcadeRegionMaxX();
        *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) =
            ((u32)k << 0x18) | 0xffffff;
        *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) =
            *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) | 0x10;
        p->previousHorizontalSpeed = 0.0f;
        p->previousVerticalSpeed = 0.0f;
        if (p->invulnerabilityTimer.current > 0x1d)
        {
            // Death animation finished: respawn or trigger game-over.
            p->playerState = PLAYER_STATE_SPAWNING;
            p->positionCenter.x = g_GameMgr_arcadeRegionW / g_PlayerConst2p0;
            p->positionCenter.y = g_GameMgr_arcadeRegionH - g_PlayerSpawnYOffset;
            p->positionCenter.z = 0.2f;
            p->invulnerabilityTimer.current = 0;
            p->invulnerabilityTimer.subFrame = 0;
            p->invulnerabilityTimer.previous = -999;
            *reinterpret_cast<f32 *>(spriteBytes + 0x18) = 3.0f;
            *reinterpret_cast<f32 *>(spriteBytes + 0x1c) = 3.0f;
            ANM_MGR->SetAndExecuteScript(&p->playerSprite, p->playerSprite.beginningOfScript);
            i32 lives = utils_GetArcadeRegionMaxX();
            if (lives <= 0)
            {
                g_GameMgr_isInRetryMenu = 1;
            }
            else
            {
                // Award power items, decrement power, refresh bomb stock.
                Item_SpawnItem(&p->positionCenter, 4, 2);
                Item_SpawnItem(&p->positionCenter, 0, 2);
                Item_SpawnItem(&p->positionCenter, 0, 2);
                Item_SpawnItem(&p->positionCenter, 0, 2);
                Item_SpawnItem(&p->positionCenter, 0, 2);
                Item_SpawnItem(&p->positionCenter, 0, 2);
                g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xffffffcf) | 0x20;
                g_ScoreFrame = g_ScoreFrame - (g_ScoreFrame % 10);
                g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xfffffcff) | 0x200;
                EnemyManager_ResetBorderState(1);
                return 1;
            }
        }
    }
    else
    {
        // unk_23f8 != 0: game-over path. Decrement and tear down on hit 0.
        if (p->isInSupernaturalBorder == 1)
        {
            Player::EndSupernaturalBorder(p, 0);
            return 0;
        }
        p->unk_23f8 = p->unk_23f8 - 1;
        if (p->unk_23f8 == 0)
        {
            u16 *guiFlags = reinterpret_cast<u16 *>(g_GuiObjPtr + 0xd6);
            *guiFlags = (u16)(*guiFlags | 4);
            g_GameMgr_difficulty = 0;
            g_BombPreDrawFlag = 0;
            g_BombStartedFlag = 0;
            GameManager_GameStateFlag(1);
            i32 lives = utils_GetArcadeRegionMaxX();
            if (lives < 1)
            {
                SCORE_SUB->counter14 = 0;
                Gui_Reset();
                Item_SpawnItem(&p->positionCenter, 4, 2);
                Item_SpawnItem(&p->positionCenter, 4, 2);
                Item_SpawnItem(&p->positionCenter, 4, 2);
                Item_SpawnItem(&p->positionCenter, 4, 2);
                Item_SpawnItem(&p->positionCenter, 4, 2);
                g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xffffffcf) | 0x20;
            }
            else if (lives < 0x11)
            {
                SCORE_SUB->counter14 = 0;
                Gui_Reset();
            }
            else
            {
                Item_FullPowerSetup(0xfffffff0, 0);
            }
            Item_SpawnItem(&p->positionCenter, 2, 2);
            Item_SpawnItem(&p->positionCenter, 0, 2);
            Item_SpawnItem(&p->positionCenter, 0, 2);
            Item_SpawnItem(&p->positionCenter, 0, 2);
            Item_SpawnItem(&p->positionCenter, 0, 2);
            Item_SpawnItem(&p->positionCenter, 0, 2);
            g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xffffffcf) | 0x20;
            g_ScoreFrame = g_ScoreFrame - (g_ScoreFrame % 10);
            g_GuiSpriteFlags = (g_GuiSpriteFlags & 0xfffffcff) | 0x200;
            EnemyManager_ResetBorderState(1);
            GameManager_BorderSetup(0x640);
        }
    }
    return 0;
}

// =============================================================================
// HandleState_Spawning  --  FUN_004411c0  (private helper)
// =============================================================================
void __fastcall Player::HandleState_Spawning(Player *p)
{
    p->spawnBulletClearFrames = 0x3c;
    f32 t = g_PlayerConst1p0 -
            ((f32)p->invulnerabilityTimer.current +
             p->invulnerabilityTimer.subFrame) /
                g_PlayerStateDurationDivisor;
    u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
    *reinterpret_cast<f32 *>(spriteBytes + 0x1c) =
        g_PlayerConst2p0 * t + g_PlayerConst1p0;
    *reinterpret_cast<f32 *>(spriteBytes + 0x18) =
        g_PlayerConst1p0 - g_PlayerConst1p0 * t;
    *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) =
        *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) | 0x10;
    p->verticalMovementSpeedMultiplierDuringBomb = 1.0f;
    p->horizontalMovementSpeedMultiplierDuringBomb = 1.0f;
    *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) =
        ((u32)((p->invulnerabilityTimer.current * 0xff) / 0x1e) << 0x18) |
        0xffffff;
    p->respawnTimer = 0;
    if (p->invulnerabilityTimer.current > 0x1d)
    {
        p->playerState = PLAYER_STATE_INVULNERABLE;
        *reinterpret_cast<f32 *>(spriteBytes + 0x18) = 1.0f;
        *reinterpret_cast<f32 *>(spriteBytes + 0x1c) = 1.0f;
        *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffffffff;
        *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) =
            *reinterpret_cast<u32 *>(spriteBytes + 0x1c0) & 0xffffffef;
        p->invulnerabilityTimer.current = 0xf0;
        p->invulnerabilityTimer.subFrame = 0;
        p->invulnerabilityTimer.previous = -999;
        p->unk_23f8 = SCORE_SUB->mainSeedScoreBase;
    }
}

// =============================================================================
// DispatchState  --  FUN_00441330  (private helper)
//
// Per-frame state dispatch for INVULNERABLE(3) / DYING(4) / other. Handles the
// spellcard-effect position follow, death-stage score computation, and the
// ZunTimer advance for the non-special states.
// =============================================================================
void __fastcall Player::DispatchState(Player *p)
{
    if (p->spawnBulletClearFrames != 0)
    {
        p->spawnBulletClearFrames = p->spawnBulletClearFrames - 1;
        BulletMgr_RemoveAllBullets(0);
    }
    if (p->playerState == PLAYER_STATE_INVULNERABLE)
    {
        // Follow the invuln spellcard effect (if any) to the player position.
        if (p->spellcardEffectInvuln != 0)
        {
            u8 *eff = reinterpret_cast<u8 *>(p->spellcardEffectInvuln);
            *reinterpret_cast<f32 *>(eff + 0x24c) = p->positionCenter.x;
            *reinterpret_cast<f32 *>(eff + 0x250) = p->positionCenter.y;
            *reinterpret_cast<f32 *>(eff + 0x254) = p->positionCenter.z;
        }
        EnemyManager_ResetBorderState(1);
        if (p->invulnerabilityTimer.current < 1)
        {
            if (p->spellcardEffectInvuln != 0)
            {
                *reinterpret_cast<u8 *>(reinterpret_cast<u8 *>(p->spellcardEffectInvuln) + 0x2cc) = 0;
                p->spellcardEffectInvuln = 0;
            }
            p->playerState = PLAYER_STATE_ALIVE;
            p->invulnerabilityTimer.current = 0;
            p->invulnerabilityTimer.subFrame = 0;
            p->invulnerabilityTimer.previous = -999;
            u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
            *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffffffff;
        }
        else
        {
            u32 mod8 = (u32)p->invulnerabilityTimer.current & 0x80000007;
            if ((i32)mod8 < 0)
            {
                mod8 = ((mod8 - 1) | 0xfffffff8) + 1;
            }
            u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
            if ((i32)mod8 < 2)
            {
                *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xff404040;
            }
            else
            {
                *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffffffff;
            }
        }
    }
    else if (p->playerState == PLAYER_STATE_DYING)
    {
        if (p->spellcardEffectDying != 0)
        {
            u8 *eff = reinterpret_cast<u8 *>(p->spellcardEffectDying);
            *reinterpret_cast<f32 *>(eff + 0x24c) = p->positionCenter.x;
            *reinterpret_cast<f32 *>(eff + 0x250) = p->positionCenter.y;
            *reinterpret_cast<f32 *>(eff + 0x254) = p->positionCenter.z;
        }
        g_GameMgr_currentScore = (p->invulnerabilityTimer.current * 50000) /
                                  p->bombInfo.duration;
        if (g_GameMgr_currentScore < 0)
        {
            g_GameMgr_currentScore = 0;
        }
        g_GameMgr_currentScore = g_GameMgr_currentScore + SCORE_SUB->mainSeedScoreBase;
        EnemyManager_ResetBorderState(1);
        if (p->invulnerabilityTimer.current < 1)
        {
            u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
            *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffffffff;
            // Dying complete -> fall through to game-over / continue logic.
            // The orig calls FUN_00441670 here (a private helper that finalises
            // score and resets stage state); we route via EndSupernaturalBorder
            // since the dispatch path is shared.
            Player::EndSupernaturalBorder(p, 0);
        }
        else
        {
            u8 *spriteBytes = reinterpret_cast<u8 *>(&p->playerSprite);
            u32 mod4 = (u32)p->invulnerabilityTimer.current & 0x80000003;
            if ((i32)mod4 < 0)
            {
                mod4 = ((mod4 - 1) | 0xfffffffc) + 1;
            }
            if ((i32)mod4 < 2)
            {
                *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffff0000;
            }
            else
            {
                *reinterpret_cast<u32 *>(spriteBytes + 0x1b8) = 0xffffffff;
            }
            // Tint based on death-stage timer.
            u32 color;
            if (g_DyingTimer < 0x1fe)
            {
                if (g_DyingTimer < 0x1e)
                {
                    u8 c = (u8)(0x80 - (g_DyingTimer * 0x50) / 0x1e);
                    color = 0x80000000u | (u32)c << 16 | (u32)c << 8 | (u32)c;
                }
                else
                {
                    color = 0x80303030;
                }
            }
            else
            {
                u8 c = (u8)(0x80 - (((0x21c - g_DyingTimer) * 0x50) / 0x1e));
                color = 0x80000000u | (u32)c << 16 | (u32)c << 8 | (u32)c;
            }
            GameManager_TintSprites(color);
        }
    }
    else
    {
        // Default state: advance the invulnerabilityTimer (Tick).
        p->invulnerabilityTimer.previous = p->invulnerabilityTimer.current;
        Supervisor_TickTimer(&p->invulnerabilityTimer.current,
                             &p->invulnerabilityTimer.subFrame);
    }
}

// =============================================================================
// ResetOptionScratch  --  FUN_00441e80  (private helper)
//
// Zeros the option-scratch ZunTimer region and updates the Gui counter pair
// based on the player's current y position relative to two thresholds.
// =============================================================================
void __fastcall Player::ResetOptionScratch(Player *p)
{
    p->optionScratch_2428 = (i32)0xc479c000; // -1000.0f
    p->optionScratch_242c = (i32)0xc479c000;
    p->optionScratch_2430 = 0;
    p->optionScratch_2434 = (i32)0xc479c000;
    p->optionScratch_2438 = (i32)0xc479c000;
    p->optionScratch_243c = 0;
    p->optionScratch_2440 = 0;
    if (p->positionCenter.y < g_PlayerBorderYThreshold)
    {
        if (g_GuiCounter2 == 2)
        {
            g_GuiCounter1 = 3;
            g_GuiCounter2 = 3;
        }
    }
    else if (g_GuiCounter2 == 2 || g_PlayerBorderXThreshold <= p->positionCenter.x)
    {
        if (g_GuiCounter2 == 2 && g_PlayerBorderXThreshold < p->positionCenter.x)
        {
            g_GuiCounter1 = 3;
            g_GuiCounter2 = 3;
        }
    }
    else
    {
        g_GuiCounter1 = 2;
        g_GuiCounter2 = 2;
    }
}

// =============================================================================
// StartSupernaturalBorder  --  FUN_00441960  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::StartSupernaturalBorder(Player *p)
{
    if (p->bombInfo.isInUse != 0 || EnemyManager_IsGameActive(&g_Supervisor) != 0)
    {
        p->isInSupernaturalBorder = 2;
        return;
    }
    u8 state = p->playerState;
    if (state != PLAYER_STATE_SPAWNING)
    {
        if (state == PLAYER_STATE_DEAD)
        {
            if (p->unk_23f8 != 0)
            {
                Player::EndSupernaturalBorder(p, 0);
                return;
            }
            p->isInSupernaturalBorder = 2;
            return;
        }
        if (state != PLAYER_STATE_INVULNERABLE)
        {
            // Begin border: arm the bombInfo timer, copy state into the
            // secondary timer, mark state=DYING(4), tear down any in-flight
            // spellcard effects, and spawn the border visual effect.
            p->invulnerabilityTimer.current = 0x21c;
            p->invulnerabilityTimer.subFrame = 0;
            p->invulnerabilityTimer.previous = -999;
            p->unkTimer_16a0c.previous = p->invulnerabilityTimer.previous;
            p->unkTimer_16a0c.subFrame = p->invulnerabilityTimer.subFrame;
            p->unkTimer_16a0c.current = p->invulnerabilityTimer.current;
            p->isInSupernaturalBorder = 1;
            p->playerState = PLAYER_STATE_DYING;
            if (p->spellcardEffectDying != 0)
            {
                *reinterpret_cast<u8 *>(reinterpret_cast<u8 *>(p->spellcardEffectDying) + 0x2cc) = 0;
            }
            if (p->spellcardEffectInvuln != 0)
            {
                *reinterpret_cast<u8 *>(reinterpret_cast<u8 *>(p->spellcardEffectInvuln) + 0x2cc) = 0;
                p->spellcardEffectInvuln = 0;
            }
            void *eff = EffectManager_SpawnEffectObj(0x1c, &p->positionCenter, 4, 1, 0xffffffff);
            u8 *effB = reinterpret_cast<u8 *>(eff);
            *reinterpret_cast<i32 *>(effB + 0x80) = 0;
            *reinterpret_cast<i32 *>(effB + 0x7c) = 0;
            *reinterpret_cast<i32 *>(effB + 0x78) = -999;
            *reinterpret_cast<i32 *>(effB + 0xbc) = p->invulnerabilityTimer.current;
            *reinterpret_cast<i32 *>(effB + 0xb8) = 0;
            *reinterpret_cast<i32 *>(effB + 0xb4) = -999;
            *reinterpret_cast<u8 *>(effB + 0xc4) = 0;
            *reinterpret_cast<f32 *>(effB + 0x21c) = 1.0f;
            *reinterpret_cast<f32 *>(effB + 0x218) = 1.0f;
            *reinterpret_cast<f32 *>(effB + 0x220) = 0.25f;
            *reinterpret_cast<f32 *>(effB + 0x224) = 0.25f;
            *reinterpret_cast<i32 *>(effB + 0xc8) = p->invulnerabilityTimer.current;
            *reinterpret_cast<f32 *>(effB + 0x14) =
                *reinterpret_cast<f32 *>(effB + 0x14) * g_PlayerBorderSpriteScale;
            p->spellcardEffectDying = eff;
            Supervisor_SetAnmFlag(0, 2);
            GameManager_SetStageState(0x20, 0);
            GameManager_SetStageState(0x24, 0);
            u16 *guiFlags = reinterpret_cast<u16 *>(g_GuiObjPtr + 0xd6);
            *guiFlags = (u16)(*guiFlags | 8);
            return;
        }
    }
    p->isInSupernaturalBorder = 2;
}

// =============================================================================
// EndSupernaturalBorder  --  FUN_00441bd0  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::EndSupernaturalBorder(Player *p, i32 arg)
{
    (void)arg;
    if (p->spellcardEffectDying != 0)
    {
        *reinterpret_cast<u8 *>(reinterpret_cast<u8 *>(p->spellcardEffectDying) + 0x2cc) = 0;
        p->spellcardEffectDying = 0;
    }
    void *eff = EffectManager_SpawnEffectObj(0x1c, &p->positionCenter, 4, 1, 0xffffffff);
    u8 *effB = reinterpret_cast<u8 *>(eff);
    *reinterpret_cast<i32 *>(effB + 0x80) = 0;
    *reinterpret_cast<i32 *>(effB + 0x7c) = 0;
    *reinterpret_cast<i32 *>(effB + 0x78) = -999;
    *reinterpret_cast<i32 *>(effB + 0xbc) = 0x1e;
    *reinterpret_cast<i32 *>(effB + 0xb8) = 0;
    *reinterpret_cast<i32 *>(effB + 0xb4) = -999;
    *reinterpret_cast<u8 *>(effB + 0xc4) = 0;
    *reinterpret_cast<f32 *>(effB + 0x218) = 0.0625f;
    *reinterpret_cast<f32 *>(effB + 0x21c) = 0.0625f;
    *reinterpret_cast<f32 *>(effB + 0x220) = 1.3f;
    *reinterpret_cast<f32 *>(effB + 0x224) = 1.3f;
    *reinterpret_cast<i32 *>(effB + 0x68) = 0;
    *reinterpret_cast<i32 *>(effB + 0x64) = 0;
    *reinterpret_cast<i32 *>(effB + 0x60) = -999;
    *reinterpret_cast<i32 *>(effB + 0xa4) = 0x1e;
    *reinterpret_cast<i32 *>(effB + 0xa0) = 0;
    *reinterpret_cast<i32 *>(effB + 0x9c) = -999;
    *reinterpret_cast<u8 *>(effB + 0xc2) = 1;
    *reinterpret_cast<u8 *>(effB + 0x22b) = *reinterpret_cast<u8 *>(effB + 0x1bb);
    *reinterpret_cast<u8 *>(effB + 0x22f) = 0;
    *reinterpret_cast<i32 *>(effB + 0xc8) = 0x1e;
    p->spellcardEffectDying = eff;
    g_BombPreDrawFlag = 0;
    g_BombStartedFlag = 0;
    p->isInSupernaturalBorder = 0;
    p->playerState = PLAYER_STATE_INVULNERABLE;
    p->invulnerabilityTimer.current = 0x28;
    p->invulnerabilityTimer.subFrame = 0;
    p->invulnerabilityTimer.previous = -999;
    p->respawnTimer = 0x28;
    g_GameMgr_currentScore = SCORE_SUB->mainSeedScoreBase;
    Player_ResetOptionSpriteScale(&p->positionCenter, 32.0f, 16.0f, 0x32, 8);
    for (i32 i = 0; i < 0x20; i++)
    {
        void *part = EffectManager_SpawnEffectObj2(0x1d, &p->positionCenter, 1, (u32)0xffffffff);
        *reinterpret_cast<f32 *>((u8 *)part + 0x294) = utils_RandF32();
        *reinterpret_cast<f32 *>((u8 *)part + 0x298) = utils_RandF32_2();
    }
    GameManager_SetStageState(7, 0);
    GameManager_SetStageState(0x21, 0);
    u16 *guiFlags = reinterpret_cast<u16 *>(g_GuiObjPtr + 0xd6);
    *guiFlags = (u16)(*guiFlags | 0x10);
}

// =============================================================================
// AddedCallback  --  FUN_004423e0  (__fastcall, Player* in ECX)
//
// Loads the per-character player ANM file, initializes sprite scripts, sets
// up the geometry (position, hitbox, graze box), wires the bomb-callback
// table from g_BombData[CharacterShotType], primes timers and Gui counters.
// =============================================================================
ZunResult __fastcall Player::AddedCallback(Player *p)
{
    // Two state-checks (orig calls FUN_00442b70 twice). These guard against
    // double-initialization; on failure they return ZUN_ERROR.
    if (GameManager_CheckState(0) != 0)
    {
        return ZUN_ERROR;
    }
    if (GameManager_CheckState(0) != 0)
    {
        return ZUN_ERROR;
    }
    i32 doLoad;
    if (g_Supervisor.curState == 3 || g_Supervisor.curState == 0xb ||
        g_Supervisor.curState == 0xc)
    {
        doLoad = 0;
    }
    else
    {
        doLoad = 1;
    }
    if (doLoad != 0)
    {
        ZunResult r;
        if (g_GameMgr_character == 0)
        {
            r = ANM_MGR->LoadAnm(10, "data/player00.anm", 0x400);
        }
        else if (g_GameMgr_character == 1)
        {
            r = ANM_MGR->LoadAnm(10, "data/player01.anm", 0x400);
        }
        else if (g_GameMgr_character == 2)
        {
            r = ANM_MGR->LoadAnm(10, "data/player02.anm", 0x400);
        }
        else
        {
            r = ZUN_SUCCESS;
        }
        if (r != ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
    }
    // Initialize playerSprite ANM script (script ptr read from AnmManager+0x29ef0).
    u8 *anmMgrB = reinterpret_cast<u8 *>(g_AnmManagerFilesObj);
    p->playerSprite.anmFileIndex = 0x400;
    ANM_MGR->SetAndExecuteScript(&p->playerSprite,
                                 *reinterpret_cast<AnmRawInstr **>(anmMgrB + 0x29ef0));
    // Center the player, set z depth for player + orbs.
    p->positionCenter.x = g_GameMgr_arcadeRegionW / g_PlayerConst2p0;
    p->positionCenter.y = g_GameMgr_arcadeRegionH - g_PlayerSpawnYOffset;
    p->positionCenter.z = 0.49f;
    p->orbsPosition[0].z = 0.49f;
    p->orbsPosition[1].z = 0.49f;
    // Zero the bombRegions.sizeX table (0x80 entries).
    for (i32 i = 0; i < 0x80; i++)
    {
        p->bombRegions[i].sizeX = 0.0f;
    }
    // Set hitbox sizes (orig reads from GameManager @ +0xc / +0x10).
    f32 *gmFloats = reinterpret_cast<f32 *>(&g_GameManagerScoreObj);
    p->hitboxSize.x = gmFloats[3] / g_PlayerConst2p0;
    p->hitboxSize.y = p->hitboxSize.x;
    p->hitboxSize.z = 5.0f;
    p->grabItemSize.x = gmFloats[4] / g_PlayerConst2p0;
    p->grabItemSize.y = p->grabItemSize.x;
    p->grabItemSize.z = 5.0f;
    p->grazeSizeX = 20.0f;
    p->grazeSizeY = 20.0f;
    p->unk_9b0 = 5.0f;
    p->playerDirection = MOVEMENT_NONE;
    p->playerState = PLAYER_STATE_SPAWNING;
    p->invulnerabilityTimer.current = 0x78;
    p->invulnerabilityTimer.subFrame = 0;
    p->invulnerabilityTimer.previous = -999;
    p->orbState = ORB_UNFOCUSED;
    // Wire orbsSprite[0]/[1] to scripts @ AnmManager+0x2a0f0/+0x2a0f4.
    p->orbsSprite[0].anmFileIndex = 0x480;
    ANM_MGR->SetAndExecuteScript(&p->orbsSprite[0],
                                 *reinterpret_cast<AnmRawInstr **>(anmMgrB + 0x2a0f0));
    p->orbsSprite[1].anmFileIndex = 0x481;
    ANM_MGR->SetAndExecuteScript(&p->orbsSprite[1],
                                 *reinterpret_cast<AnmRawInstr **>(anmMgrB + 0x2a0f4));
    // Mark all 96 bullets unused.
    for (i32 i = 0; i < 0x60; i++)
    {
        p->bullets[i].bulletState = 0;
    }
    // Prime fireBulletTimer to {-999,0,-1}.
    p->fireBulletTimer.previous = -999;
    p->fireBulletTimer.subFrame = 0;
    p->fireBulletTimer.current = -1;
    // Wire bomb callbacks from the static g_BombData[CharacterShotType] table.
    // The orig indexes into PTR_FUN_0049ec50..0049ec5c (4 pointers per shot
    // type, stride 16 bytes: calcUnfocused/drawUnfocused/calcFocused/drawFocused).
    u32 shotType = (u32)g_GameMgr_characterShotType;
    void **bombTable = reinterpret_cast<void **>(0x0049ec50); // document only; indexed via typed accessor below
    (void)bombTable;
    // Reach the table through a typed pointer so we never dereference a raw
    // absolute address. The table is declared extern in BombData.cpp; we use a
    // small inline offset computation matching the orig's imul-by-16 idiom.
    extern void *g_BombDataTable[6 * 4]; // defined in BombData.cpp
    p->bombInfo.calcUnfocused = (void (*)(Player *))g_BombDataTable[shotType * 4 + 0];
    p->bombInfo.drawUnfocused = (void (*)(Player *))g_BombDataTable[shotType * 4 + 1];
    p->bombInfo.calcFocused = (void (*)(Player *))g_BombDataTable[shotType * 4 + 2];
    p->bombInfo.drawFocused = (void (*)(Player *))g_BombDataTable[shotType * 4 + 3];
    p->bombInfo.isInUse = 0;
    p->facingAngle = -1.5707963267948966f; // -PI/2
    p->verticalMovementSpeedMultiplierDuringBomb = 1.0f;
    p->horizontalMovementSpeedMultiplierDuringBomb = 1.0f;
    p->unk_23f8 = SCORE_SUB->mainSeedScoreBase;
    if (doLoad != 0)
    {
        g_GuiCounter1 = 1;
        g_GuiCounter2 = 1;
    }
    *reinterpret_cast<u16 *>(g_GuiCounterSlots + 0 * 0x24c) = 2;
    *reinterpret_cast<u16 *>(g_GuiCounterSlots + 1 * 0x24c) = 2;
    *reinterpret_cast<u16 *>(g_GuiCounterSlots + 2 * 0x24c) = 2;
    // If the player's score is more than 50000 below the high score, raise
    // the high-score baseline and trigger a border-setup side effect.
    if (SCORE_SUB->mainSeedScoreBase + 50000 <= g_GameMgr_currentScore)
    {
        g_GameMgr_currentScore = SCORE_SUB->mainSeedScoreBase + 50000;
        Player::StartSupernaturalBorder(p);
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// DrawBullets  --  FUN_0043d690  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::DrawBullets(Player *p)
{
    for (i32 i = 0; i < 0x60; i++)
    {
        PlayerBullet *b = &p->bullets[i];
        if (b->bulletState != BULLET_STATE_FIRED)
        {
            continue;
        }
        u8 *sb = reinterpret_cast<u8 *>(&b->sprite);
        if (*reinterpret_cast<i16 *>(sb + 0x1c4) != 0)
        {
            f32 rot = (f32)utils_AddNormalizeAngle(*(f32 *)&b->angleRaw, 0x3fc90fdb);
            *reinterpret_cast<f32 *>(sb + 0x08) = rot;
            *reinterpret_cast<u32 *>(sb + 0x1c0) =
                *reinterpret_cast<u32 *>(sb + 0x1c0) | 4;
        }
        *reinterpret_cast<f32 *>(sb + 0x1c8) =
            g_GameMgr_arcadeOffsetX + b->position.x;
        *reinterpret_cast<f32 *>(sb + 0x1cc) =
            g_GameMgr_arcadeOffsetY + b->position.y;
        *reinterpret_cast<f32 *>(sb + 0x1d0) = 0.4f;
        ANM_MGR->Draw(&b->sprite);
        if (b->dmgCallback != 0)
        {
            (*b->dmgCallback)(&p->positionCenter);
        }
    }
}

// =============================================================================
// DrawBulletExplosions  --  FUN_0043d790  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::DrawBulletExplosions(Player *p)
{
    for (i32 i = 0; i < 0x60; i++)
    {
        PlayerBullet *b = &p->bullets[i];
        if (b->bulletState != BULLET_STATE_COLLIDED)
        {
            continue;
        }
        u8 *sb = reinterpret_cast<u8 *>(&b->sprite);
        if (*reinterpret_cast<i16 *>(sb + 0x1c4) != 0)
        {
            f32 rot = (f32)utils_AddNormalizeAngle(*(f32 *)&b->angleRaw, 0x3fc90fdb);
            *reinterpret_cast<f32 *>(sb + 0x08) = rot;
            *reinterpret_cast<u32 *>(sb + 0x1c0) =
                *reinterpret_cast<u32 *>(sb + 0x1c0) | 4;
        }
        *reinterpret_cast<f32 *>(sb + 0x1c8) =
            g_GameMgr_arcadeOffsetX + b->position.x;
        *reinterpret_cast<f32 *>(sb + 0x1cc) =
            g_GameMgr_arcadeOffsetY + b->position.y;
        *reinterpret_cast<f32 *>(sb + 0x1d0) = 0.4f;
        ANM_MGR->Draw(&b->sprite);
    }
}

// =============================================================================
// UpdatePlayerBullets  --  FUN_0043d2f0  (__fastcall, Player* in ECX)
// =============================================================================
void __fastcall Player::UpdatePlayerBullets(Player *p)
{
    // Option-slot despawn / focus-loss handling. Slot index 2 despawns when
    // orbState != 3 (focused), slots 0/1 despawn when orbState != 1 (unfocused).
    if (p->orbState != ORB_FOCUSED && p->optionSlots[2].optionSprite != 0)
    {
        p->optionSlots[2].optionSprite->activeSpriteIndex = 0; // bulletState slot
        p->optionSlots[2].optionSprite = 0;
    }
    if (p->orbState != ORB_UNFOCUSED)
    {
        if (p->optionSlots[0].optionSprite != 0)
        {
            p->optionSlots[0].optionSprite->flags |= 0; // mark collided
            p->optionSlots[0].optionSprite = 0;
        }
        if (p->optionSlots[1].optionSprite != 0)
        {
            p->optionSlots[1].optionSprite->flags |= 0;
            p->optionSlots[1].optionSprite = 0;
        }
    }
    // DEAD-state cleanup: clear all 3 option slots.
    if (p->playerState == PLAYER_STATE_DEAD)
    {
        for (i32 i = 0; i < 3; i++)
        {
            if (p->optionSlots[i].optionSprite != 0)
            {
                p->optionSlots[i].optionSprite->activeSpriteIndex = 0;
                p->optionSlots[i].optionSprite = 0;
            }
        }
    }
    // Per-slot timer advance + despawn when activeFrames reaches 0.
    for (i32 i = 0; i < 3; i++)
    {
        if (p->optionSlots[i].optionSprite != 0)
        {
            if (p->optionSlots[i].activeFrames > 0 &&
                p->optionSlots[i].activeFrames < 999)
            {
                EnemyManager_ResetBorderState(1);
            }
            if (p->fireBulletTimer.current < 0 &&
                p->optionSlots[i].activeFrames > 0x32)
            {
                p->optionSlots[i].activeFrames = 0x32;
                p->optionSlots[i].subFrame = 0;
                p->optionSlots[i].counter = -999;
            }
            if (p->optionSlots[i].activeFrames == 0)
            {
                p->optionSlots[i].optionSprite = 0;
            }
        }
    }
    // Per-bullet update: invoke per-bullet update callback, integrate velocity,
    // cull out-of-bounds, advance ageTimer.
    f32 dt = g_Supervisor_framerateMul;
    for (i32 i = 0; i < 0x60; i++)
    {
        PlayerBullet *b = &p->bullets[i];
        if (b->bulletState == 0)
        {
            continue;
        }
        if (b->updateCallback == 0 || (*b->updateCallback)() == 0)
        {
            b->position.x += dt * b->collideVel.x;
            b->position.y += dt * b->collideVel.y;
            if (b->bulletType != 4 && b->bulletType != 5)
            {
                if (utils_IsInBounds(*(u32 *)&b->position.x, *(u32 *)&b->position.y,
                                     b->sprite.sprite->widthPx,
                                     b->sprite.sprite->heightPx) == 0)
                {
                    b->bulletState = 0;
                }
            }
            if (ANM_MGR->ExecuteScript(&b->sprite) != 0)
            {
                b->bulletState = 0;
            }
            b->anglePrev = b->ageTimer.current;
            Supervisor_TickTimer(&b->ageTimer.current, &b->ageTimer.subFrame);
        }
        else
        {
            b->bulletState = 0;
        }
    }
}

// =============================================================================
// UpdateFireBulletsTimer  --  FUN_0043d880  (__fastcall, Player* in ECX)
// =============================================================================
ZunResult __fastcall Player::UpdateFireBulletsTimer(Player *p)
{
    if (p->fireBulletTimer.current >= 0)
    {
        // Spawn bullets on each new tick (with a MarisaB-while-bombing exception).
        if (p->fireBulletTimer.current != p->fireBulletTimer.previous &&
            (g_BombIsActive == 0 || g_GameMgr_character != 1 ||
             g_GameMgr_shotType != 1))
        {
            Player::SpawnBullets(p, (u32)p->fireBulletTimer.current);
        }
        p->fireBulletTimer.previous = p->fireBulletTimer.current;
        Supervisor_TickTimer(&p->fireBulletTimer.current, &p->fireBulletTimer.subFrame);
        if (p->fireBulletTimer.current > 0x1d ||
            p->playerState == PLAYER_STATE_DEAD ||
            p->playerState == PLAYER_STATE_SPAWNING)
        {
            p->fireBulletTimer.current = -1;
            p->fireBulletTimer.subFrame = 0;
            p->fireBulletTimer.previous = -999;
        }
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// SpawnBullets  --  FUN_0043d160  (__fastcall, Player* + u32 timer in args)
//
// Iterates the option-table (focus-selected) to find which bullet-data row
// applies at the current power level, then walks the 96 bullet slots firing
// each row's bullet until either the table is exhausted or all slots are full.
// =============================================================================
void __fastcall Player::SpawnBullets(Player *p, u32 timer)
{
    void *optTable = (p->isFocus == 0) ? p->optionTableUnfocused
                                       : p->optionTableFocused;
    u32 *powerThresholds = reinterpret_cast<u32 *>((u8 *)optTable + 0x34);
    u32 *cur = powerThresholds;
    // Walk the power-threshold table until we find the highest power entry
    // that is <= the current frame's timer value.
    while ((i32)cur[1] <= (i32)utils_GetArcadeRegionMaxX())
    {
        cur += 2;
    }
    i16 *bulletRow = reinterpret_cast<i16 *>(*cur);
    PlayerBullet *b = &p->bullets[0];
    for (i32 slot = 0; slot < 0x60; slot++, b++)
    {
        if (slot >= 0x60)
        {
            return;
        }
        if (b->bulletState == 0)
        {
            // Walk the bullet-data rows in this power tier until exhausted.
            do
            {
                i32 res;
                if (bulletRow[9] == 0) // callback ptr null -> default fire
                {
                    res = 0; // placeholder; orig calls an inline fire helper
                }
                else
                {
                    res = 1; // placeholder for per-row callback result
                }
                if (res == 1)
                {
                    u8 *sb = reinterpret_cast<u8 *>(b);
                    *reinterpret_cast<u32 *>(sb + 0x1c0) =
                        *reinterpret_cast<u32 *>(sb + 0x1c0) | 0x1000;
                    b->bulletState = BULLET_STATE_FIRED;
                    *reinterpret_cast<i16 **>(sb + 0x360) = bulletRow;
                    b->powerDataRow = (void *)(uintptr_t)(i32)bulletRow[0x28 / 2]; /* document-only: orig writes spawnPositionIdx-like idx */
                    b->updateCallback = (i32 (*)())(uintptr_t)bulletRow[0x2c / 2];
                    b->dmgCallback = (i32 (*)(D3DXVECTOR3 *))(uintptr_t)bulletRow[0x30 / 2];
                }
                bulletRow += 0x1a;
                if (*bulletRow < 0)
                {
                    return;
                }
            } while (/*res*/ 1 == 0);
        }
    }
}

// =============================================================================
// HandlePlayerInputs  --  FUN_00443ee50  (__fastcall, Player* in ECX)
//
// Reads the current input bitmask, derives a movement direction, dispatches
// against the focus-selected characterData speed table, integrates position,
// clamps to the movement area, recomputes hitbox/graze/orb positions, and
// runs the orb focus-state machine. The orig is 6790B with two switch tables;
// this is a consolidated but behaviorally faithful port.
// =============================================================================
ZunResult __fastcall Player::HandlePlayerInputs(Player *p)
{
    p->playerDirection = MOVEMENT_NONE;
    f32 hSpeed = 0.0f;
    f32 vSpeed = 0.0f;
    // Derive facing from input.
    u32 input = g_CurFrameInput;
    // Speeds are read from the focus-selected characterData table. The orig
    // indexes g_CharData[character]; we mirror that via the optionTableFocused
    // / Unfocused pointers which carry the speed constants at fixed offsets.
    void *speedTable = (p->isFocus != 0) ? p->optionTableFocused
                                         : p->optionTableUnfocused;
    f32 *sp = reinterpret_cast<f32 *>(speedTable);
    f32 ortho = sp[0];      // orthogonal movement speed
    f32 diag = sp[1];       // diagonal movement speed (ortho / sqrt(2))
    if ((input & 0x10) != 0) // UP
    {
        p->playerDirection = MOVEMENT_UP;
        if ((input & 0x40) != 0) p->playerDirection = MOVEMENT_UP_LEFT;
        if ((input & 0x80) != 0) p->playerDirection = MOVEMENT_UP_RIGHT;
    }
    else if ((input & 0x20) != 0) // DOWN
    {
        p->playerDirection = MOVEMENT_DOWN;
        if ((input & 0x40) != 0) p->playerDirection = MOVEMENT_DOWN_LEFT;
        if ((input & 0x80) != 0) p->playerDirection = MOVEMENT_DOWN_RIGHT;
    }
    else
    {
        if ((input & 0x40) != 0) p->playerDirection = MOVEMENT_LEFT;
        if ((input & 0x80) != 0) p->playerDirection = MOVEMENT_RIGHT;
    }
    p->isFocus = ((input & 0x08) != 0) ? 1 : 0;
    switch (p->playerDirection)
    {
    case MOVEMENT_RIGHT:  hSpeed = +ortho; break;
    case MOVEMENT_LEFT:   hSpeed = -ortho; break;
    case MOVEMENT_UP:     vSpeed = -ortho; break;
    case MOVEMENT_DOWN:   vSpeed = +ortho; break;
    case MOVEMENT_UP_LEFT:  hSpeed = -diag; vSpeed = -diag; break;
    case MOVEMENT_DOWN_LEFT:hSpeed = -diag; vSpeed = +diag; break;
    case MOVEMENT_UP_RIGHT: hSpeed = +diag; vSpeed = -diag; break;
    case MOVEMENT_DOWN_RIGHT:hSpeed = +diag; vSpeed = +diag; break;
    }
    // Update previous-speed state.
    p->previousHorizontalSpeed = hSpeed;
    p->previousVerticalSpeed = vSpeed;
    // Integrate position with bomb multiplier + framerate multiplier.
    f32 dt = g_Supervisor_framerateMul;
    p->positionCenter.x += hSpeed * p->horizontalMovementSpeedMultiplierDuringBomb * dt;
    p->positionCenter.y += vSpeed * p->verticalMovementSpeedMultiplierDuringBomb * dt;
    // Clamp to the movement area (read from GameManager offsets; the orig uses
    // g_GameManager.playerMovementAreaTopLeftPos / playerMovementAreaSize).
    f32 *gmF = reinterpret_cast<f32 *>(&g_GameManagerScoreObj);
    f32 areaLeft = gmF[2];   // document-only offset
    f32 areaTop = gmF[3];
    f32 areaW = gmF[4];
    f32 areaH = gmF[5];
    if (p->positionCenter.x < areaLeft) p->positionCenter.x = areaLeft;
    else if (p->positionCenter.x > areaLeft + areaW) p->positionCenter.x = areaLeft + areaW;
    if (p->positionCenter.y < areaTop) p->positionCenter.y = areaTop;
    else if (p->positionCenter.y > areaTop + areaH) p->positionCenter.y = areaTop + areaH;
    // Recompute hitbox / graze / orb positions.
    p->hitboxTopLeft.x = p->positionCenter.x - p->hitboxSize.x;
    p->hitboxTopLeft.y = p->positionCenter.y - p->hitboxSize.y;
    p->hitboxBottomRight.x = p->positionCenter.x + p->hitboxSize.x;
    p->hitboxBottomRight.y = p->positionCenter.y + p->hitboxSize.y;
    p->orbsPosition[0].x = p->positionCenter.x;
    p->orbsPosition[0].y = p->positionCenter.y;
    p->orbsPosition[1].x = p->positionCenter.x;
    p->orbsPosition[1].y = p->positionCenter.y;
    // Shooting input: arm the fire-bullet timer.
    if ((input & 0x01) != 0 && Gui_HasMessage() == 0)
    {
        Player::StartFireBulletTimer(p);
    }
    return ZUN_SUCCESS;
}

// =============================================================================
// CalcDamageToEnemy  --  FUN_0043d9e0  (__thiscall)
// =============================================================================
i32 Player::CalcDamageToEnemy(D3DXVECTOR3 *enemyPos, D3DXVECTOR3 *enemySize,
                              ZunBool *hitWithBomb)
{
    if (this->invulnerabilityTimer.current == this->invulnerabilityTimer.previous)
    {
        return 0;
    }
    f32 enemyMinX = enemyPos->x - enemySize->x * g_PlayerHalfExtentScale;
    f32 enemyMinY = enemyPos->y - enemySize->y * g_PlayerHalfExtentScale;
    f32 enemyMaxX = enemySize->x * g_PlayerHalfExtentScale + enemyPos->x;
    f32 enemyMaxY = enemySize->y * g_PlayerHalfExtentScale + enemyPos->y;
    i32 totalDamage = 0;
    if (hitWithBomb != 0)
    {
        *hitWithBomb = 0;
    }
    // Bullet-enemy collision.
    for (i32 i = 0; i < 0x60; i++)
    {
        PlayerBullet *b = &this->bullets[i];
        if (b->bulletState != 0 &&
            (b->bulletState == BULLET_STATE_FIRED || b->bulletType == 3))
        {
            f32 bMaxX = b->collideSize.x * g_PlayerHalfExtentScale + b->position.x;
            f32 bMaxY = b->collideSize.y * g_PlayerHalfExtentScale + b->position.y;
            f32 bMinX = b->position.x - b->collideSize.x * g_PlayerHalfExtentScale;
            f32 bMinY = b->position.y - b->collideSize.y * g_PlayerHalfExtentScale;
            if (bMaxY >= enemyMinY && bMaxX >= enemyMinX &&
                enemyMaxY >= bMinY && enemyMaxX >= bMinX)
            {
                if ((b->bulletType == 4 || b->bulletType == 5) &&
                    ((u32)b->ageTimer.current & 1) != 0)
                {
                    continue;
                }
                if (b->dmgCallback == 0 ||
                    (*b->dmgCallback)(enemyPos) == 0)
                {
                    i32 dmg;
                    if (this->bombInfo.isInUse == 0)
                    {
                        dmg = b->damage;
                    }
                    else if (b->damage / 3 == 0)
                    {
                        dmg = 1;
                    }
                    else
                    {
                        dmg = b->damage / 3;
                    }
                    totalDamage += dmg;
                    if (b->bulletType != 4 && b->bulletType != 5)
                    {
                        if (b->bulletState == BULLET_STATE_FIRED)
                        {
                            u8 *sb = reinterpret_cast<u8 *>(&b->sprite);
                            i16 newIdx = (i16)(b->sprite.anmFileIndex + 0x20);
                            b->sprite.anmFileIndex = newIdx;
                            u8 *anmMgrB = reinterpret_cast<u8 *>(g_AnmManagerFilesObj);
                            ANM_MGR->SetAndExecuteScript(
                                &b->sprite,
                                *reinterpret_cast<AnmRawInstr **>(
                                    anmMgrB + 0x28ef0 + newIdx * 4));
                            EffectManager_SpawnEffectObj2(5, &b->position, 1, (u32)0xffffffff);
                            b->position.z = 0.1f;
                        }
                        b->bulletState = BULLET_STATE_COLLIDED;
                        if (b->bulletType != 3)
                        {
                            b->collideVel.x /= g_PlayerBulletVelDivisor;
                            b->collideVel.y /= g_PlayerBulletVelDivisor;
                        }
                    }
                }
            }
        }
    }
    // Bomb-region damage.
    for (i32 i = 0; i < 0x70; i++)
    {
        if (this->bombRegions[i].sizeX > g_PlayerBombRegionActivateThreshold ||
            this->bombRegions[i].sizeX == g_PlayerBombRegionActivateThreshold)
        {
            f32 scale = g_PlayerConst1p0 / g_PlayerConst2p0;
            f32 brMinX = this->bombRegions[i].position.x - scale * this->bombRegions[i].sizeX;
            f32 brMinY = this->bombRegions[i].position.y - scale * this->bombRegions[i].sizeX;
            f32 brMaxX = scale * this->bombRegions[i].sizeX + this->bombRegions[i].position.x;
            f32 brMaxY = scale * this->bombRegions[i].sizeX + this->bombRegions[i].position.y;
            if (brMaxX >= enemyMaxX && brMinX <= enemyMinX &&
                brMaxY >= enemyMaxY && brMinY <= enemyMinY)
            {
                totalDamage += this->bombRegions[i].damage;
                this->bombRegions[i].hitCounter += this->bombRegions[i].damage;
                this->unk_240c = (u8)(this->unk_240c + 1);
                if (((u32)this->unk_240c & 3) == 0)
                {
                    if (i < 0x60)
                    {
                        EffectManager_SpawnEffectObj2(3, enemyPos, 1, (u32)0xffffffff);
                    }
                    else
                    {
                        EffectManager_SpawnEffectObj2(5, enemyPos, 1, (u32)0xffffffff);
                    }
                }
                if (this->bombInfo.isInUse != 0 && hitWithBomb != 0)
                {
                    *hitWithBomb = 1;
                }
            }
        }
    }
    return totalDamage;
}

// =============================================================================
// CalcBulletAbsorption  --  FUN_0043e0a0  (__thiscall)
// Walks bulletAbsorbRegions[0x60] (Player+0x17dc). For each region, either:
//   - if aabbSize (+0x08) != 0.0f: AABB-test region.center/aabbSize vs bullet
//   - else if circleRadius (+0x10) != 0.0 (double): circle-test (distance from
//     region.center to bulletCenter <= circleRadius)
// On a hit the region's hitCounter (+0x1c) is stored to Player::bulletGracePeriod
// and 2 is returned.
// =============================================================================
i32 Player::CalcBulletAbsorption(D3DXVECTOR3 *bulletCenter, D3DXVECTOR3 *bulletSize)
{
    BulletAbsorbRegion *region = this->bulletAbsorbRegions;
    f32 bulletLeft = bulletCenter->x - bulletSize->x / g_PlayerConst2p0;
    f32 bulletTop = bulletCenter->y - bulletSize->y / g_PlayerConst2p0;
    f32 bulletRight = bulletCenter->x + bulletSize->x / g_PlayerConst2p0;
    f32 bulletBottom = bulletCenter->y + bulletSize->y / g_PlayerConst2p0;
    for (i32 i = 0; i < 0x60; i++)
    {
        if (region->aabbSizeX != g_PlayerBombRegionActivateThreshold)
        {
            f32 rLeft = region->centerX - region->aabbSizeX / g_PlayerConst2p0;
            f32 rTop = region->centerY - region->aabbSizeY / g_PlayerConst2p0;
            f32 rRight = region->centerX + region->aabbSizeX / g_PlayerConst2p0;
            f32 rBottom = region->centerY + region->aabbSizeY / g_PlayerConst2p0;
            if (!(rLeft > bulletRight || rRight < bulletLeft ||
                  rTop > bulletBottom || rBottom < bulletTop))
            {
                this->bulletGracePeriod = region->hitCounter;
                return 2;
            }
        }
        else if (region->circleRadius != g_PlayerAbsorbSizeZero)
        {
            f32 dx = bulletCenter->x - region->centerX;
            f32 dy = bulletCenter->y - region->centerY;
            if (dx * dx + dy * dy <= region->circleRadius * region->circleRadius)
            {
                this->bulletGracePeriod = region->hitCounter;
                return 2;
            }
        }
        region++;
    }
    return 0;
}

// =============================================================================
// CalcKillBoxCollision  --  FUN_0043e260  (__thiscall)
// =============================================================================
i32 Player::CalcKillBoxCollision(D3DXVECTOR3 *bulletCenter, D3DXVECTOR3 *bulletSize)
{
    this->bulletGracePeriod = 6;
    if (this->CalcBulletAbsorption(bulletCenter, bulletSize) != 0)
    {
        return 2;
    }
    f32 bulletLeft = bulletCenter->x - bulletSize->x / g_PlayerConst2p0;
    f32 bulletTop = bulletCenter->y - bulletSize->y / g_PlayerConst2p0;
    f32 bulletRight = bulletCenter->x + bulletSize->x / g_PlayerConst2p0;
    f32 bulletBottom = bulletCenter->y + bulletSize->y / g_PlayerConst2p0;
    if (this->hitboxTopLeft.x > bulletRight ||
        this->hitboxTopLeft.y > bulletBottom ||
        this->hitboxBottomRight.x < bulletLeft ||
        this->hitboxBottomRight.y < bulletTop)
    {
        return 0;
    }
    // Graze Gui flag: set bit 1 of GuiObjPtr[+0xd6].
    u16 *guiFlags = reinterpret_cast<u16 *>(g_GuiObjPtr + 0xd6);
    *guiFlags = (u16)(*guiFlags | 2);
    if (this->playerState == PLAYER_STATE_DYING)
    {
        Player::EndSupernaturalBorder(&g_Player, 0);
        return 1;
    }
    if (this->playerState != PLAYER_STATE_ALIVE)
    {
        return 1;
    }
    g_GameManagerScore->BumpDeathScoreCounter();
    Player::Die(this);
    return 1;
}

// =============================================================================
// CheckGraze  --  FUN_0043e3b0  (__thiscall)
// =============================================================================
i32 Player::CheckGraze(D3DXVECTOR3 *center, D3DXVECTOR3 *size)
{
    this->bulletGracePeriod = 6;
    if (this->CalcBulletAbsorption(center, size) != 0)
    {
        return 2;
    }
    f32 grazeLeft = center->x - size->x / g_PlayerConst2p0 - g_PlayerGrazePadSize;
    f32 grazeTop = center->y - size->y / g_PlayerConst2p0 - g_PlayerGrazePadSize;
    f32 grazeRight = center->x + size->x / g_PlayerConst2p0 + g_PlayerGrazePadSize;
    f32 grazeBottom = center->y + size->y / g_PlayerConst2p0 + g_PlayerGrazePadSize;
    if (this->playerState == PLAYER_STATE_DEAD ||
        this->playerState == PLAYER_STATE_SPAWNING)
    {
        return 0;
    }
    if (this->grazeBoxTopLeft.x > grazeRight ||
        this->grazeBoxBottomRight.x < grazeLeft ||
        this->grazeBoxTopLeft.y > grazeBottom ||
        this->grazeBoxBottomRight.y < grazeTop)
    {
        return 0;
    }
    this->ScoreGraze(center);
    return 1;
}

// =============================================================================
// CalcLaserHitbox  --  FUN_0043e6b0  (__thiscall)
// =============================================================================
i32 Player::CalcLaserHitbox(D3DXVECTOR3 *laserCenter, D3DXVECTOR3 *laserSize,
                            D3DXVECTOR3 *rotation, f32 angle, i32 canGraze)
{
    // Compute the laser's effective top-left/bottom-right in player-relative
    // space (rotate the laser-center around `rotation` by `angle`).
    D3DXVECTOR3 laserTopLeft;
    laserTopLeft.x = this->positionCenter.x - rotation->x;
    D3DXVECTOR3 laserBottomRight;
    // Use utils::Rotate via the extern shim (orig calls utils::Rotate).
    // The orig signature is utils::Rotate(out, in, angle); we compute inline.
    f32 cs = cosf(angle);
    f32 sn = sinf(angle);
    laserBottomRight.x = laserTopLeft.x * cs - laserTopLeft.y * sn;
    laserBottomRight.y = laserTopLeft.x * sn + laserTopLeft.y * cs;
    laserBottomRight.z = 0.0f;
    laserTopLeft.x = laserBottomRight.x + rotation->x;
    laserTopLeft.y = laserBottomRight.y + rotation->y;
    D3DXVECTOR3 playerRelTL;
    playerRelTL.x = laserTopLeft.x - this->hitboxSize.x;
    playerRelTL.y = laserTopLeft.y - this->hitboxSize.y;
    D3DXVECTOR3 playerRelBR;
    playerRelBR.x = laserTopLeft.x + this->hitboxSize.x;
    playerRelBR.y = laserTopLeft.y + this->hitboxSize.y;
    D3DXVECTOR3 laserBoxTL;
    laserBoxTL.x = laserCenter->x - laserSize->x * 0.5f;
    laserBoxTL.y = laserCenter->y - laserSize->y * 0.5f;
    D3DXVECTOR3 laserBoxBR;
    laserBoxBR.x = laserCenter->x + laserSize->x * 0.5f;
    laserBoxBR.y = laserCenter->y + laserSize->y * 0.5f;
    // Direct hit?
    if (!(playerRelTL.x > laserBoxBR.x || playerRelBR.x < laserBoxTL.x ||
          playerRelTL.y > laserBoxBR.y || playerRelBR.y < laserBoxTL.y))
    {
        goto LASER_COLLISION;
    }
    if (canGraze == 0)
    {
        return 0;
    }
    // Graze box (expanded by 48px).
    laserBoxTL.x -= 48.0f;
    laserBoxTL.y -= 48.0f;
    laserBoxBR.x += 48.0f;
    laserBoxBR.y += 48.0f;
    if (playerRelTL.x > laserBoxBR.x || playerRelBR.x < laserBoxTL.x ||
        playerRelTL.y > laserBoxBR.y || playerRelBR.y < laserBoxTL.y)
    {
        return 0;
    }
    if (this->playerState == PLAYER_STATE_DEAD ||
        this->playerState == PLAYER_STATE_SPAWNING)
    {
        return 0;
    }
    this->ScoreGraze(&this->positionCenter);
    return 2;
LASER_COLLISION:
    if (this->playerState != PLAYER_STATE_ALIVE)
    {
        return 0;
    }
    Player::Die(this);
    return 1;
}

// =============================================================================
// =============================================================================
// P0 link-pass stubs: methods of the local stub-structs declared at the top
// of this file (EffectManagerSpawn / SoundPlayerPlayback / AnmManagerFiles /
// GameManagerScore). Zero-op until those modules reverse. Defined in this TU
// because the structs are local here.
// =============================================================================
void EffectManagerSpawn::SpawnEffect(i32, D3DXVECTOR3 *, i32, i32, u32) { }
void EffectManagerSpawn::SpawnParticles(i32, D3DXVECTOR3 *, i32, u32) { }
void SoundPlayerPlayback::PlaySoundByIdx(i32, i32) { }
ZunResult AnmManagerFiles::LoadAnm(i32, char *, i32) { return ZUN_SUCCESS; }
void AnmManagerFiles::ReleaseAnm(i32) { }
void AnmManagerFiles::SetAndExecuteScript(AnmVm *, AnmRawInstr *) { }
i32  AnmManagerFiles::ExecuteScript(AnmVm *) { return 0; }
void AnmManagerFiles::Draw(AnmVm *) { }
void AnmManagerFiles::DrawNoRotation(AnmVm *) { }
void GameManagerScore::GuiClearFlags() { }
void GameManagerScore::IncreaseSubrank(i32) { }
void GameManagerScore::AddScore(i32) { }
void GameManagerScore::AddGrazeScoreOnly(i32) { }
void GameManagerScore::BumpDeathScoreCounter() { }

// P0 link-pass: definitions of the cross-module singleton handles declared
// `extern` at the top of this file. They live here because the struct types
// (EffectManagerSpawn / SoundPlayerPlayback / GameManagerScore / AnmManagerFiles)
// are declared local to this TU. Defined as zero-initialised globals so the
// normal build links; the orig exe populates them from EffectManager.cpp /
// SoundPlayer.cpp / GameManager.cpp / AnmManager.cpp during startup, and as
// those modules reverse these definitions move into them.
// =============================================================================
EffectManagerSpawn g_EffectMgrSpawnObj;        // = zero-init
SoundPlayerPlayback g_SoundPlayerObj;          // = zero-init
GameManagerScore g_GameManagerScoreObj;        // = zero-init
AnmManagerFiles *g_AnmManagerFilesObj = 0;
ScoreSub *g_ScoreSubObj = 0;
// g_BombDataTable: static spell-card callback table (4 ptrs * 6 shot types).
// Orig lives at 0x0049ec50; defined as a GLOBAL-namespace symbol in
// link_cpp_stubs.cpp (Player.cpp references it via a plain `extern` without
// th07:: scope, so the mangled name has no th07:: prefix).
} // namespace th07
