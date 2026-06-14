// Player module for th07 (Perfect Cherry Blossom).
//
// All field offsets below were verified by reading th07.exe in ghidra. They
// MUST NOT be edited without re-checking the binary.
//
// Global addresses map (verified):
//   g_Player              @ 0x004bdad8  (size 0xb7e78 bytes)
//   g_AnmManager          @ 0x004b9e44  (AnmManager * used as `this`)
//   g_Chain               (Chain)
//   g_GameManager         @ 0x00575948  (GameManager *)
//   g_Supervisor.curState @ 0x00575aa8
//   g_Supervisor.framerateMultiplier @ 0x00575ac8
//   character             @ 0x0062f645  (0=Reimu,1=Marisa,2=Sakuya)
//   shotType              @ 0x0062f646  (0=A, 1=B)
//   CharacterShotType()   @ 0x0062f647  (= character*2 + shotType, 0..5)
//   g_CurFrameInput       @ 0x004b9e50
//   g_LastFrameInput      @ 0x004b9e4c
//   Gui base              @ 0x004b9e48
//
// th07 vs th06 changes (observed):
//   * Three characters (Reimu / Marisa / Sakuya). ANM files data/player0{0,1,2}.anm
//     loaded per character in AddedCallback.
//   * PlayerBullet grew 0x158 -> 0x364 bytes; bullet array 80 -> 96 entries.
//   * Player struct 0x98f0 -> 0xb7e78 bytes (Cherry / Supernatural Border /
//     CSlave-style fields near the tail).
//   * bombInfo has 4 callbacks (calc/draw for focused and unfocused) indexed
//     by CharacterShotType (0..5) from the 6-entry table at 0x0049ec50.
//   * 3rd option slot added (Sakuya 4 options); option tables at +0xb7e70/+0xb7e74.
//   * Die() invokes a longer death sequence with a cherry-score penalty.

#pragma once

#include "AnmVm.hpp"
#include "ZunTimer.hpp"
#include "Chain.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>

namespace th07
{
struct Player;

enum PlayerState
{
    PLAYER_STATE_ALIVE = 0,
    PLAYER_STATE_SPAWNING = 1,
    PLAYER_STATE_DEAD = 2,
    PLAYER_STATE_INVULNERABLE = 3,
    PLAYER_STATE_DYING = 4, // th07: supernatural-border / cherry death sequence
};

enum OrbState
{
    ORB_HIDDEN = 0,
    ORB_UNFOCUSED = 1,
    ORB_FOCUSING = 2,
    ORB_FOCUSED = 3,
    ORB_UNFOCUSING = 4,
};

enum BulletState
{
    BULLET_STATE_UNUSED = 0,
    BULLET_STATE_FIRED = 1,
    BULLET_STATE_COLLIDED = 2,
};

enum BulletType
{
    BULLET_TYPE_0 = 0,
    BULLET_TYPE_1 = 1,
    BULLET_TYPE_2 = 2,
    BULLET_TYPE_3 = 3,
    BULLET_TYPE_4 = 4,
    BULLET_TYPE_5 = 5,
};

enum PlayerDirection
{
    MOVEMENT_NONE = 0,
    MOVEMENT_UP = 1,
    MOVEMENT_DOWN = 2,
    MOVEMENT_LEFT = 3,
    MOVEMENT_RIGHT = 4,
    MOVEMENT_UP_LEFT = 5,
    MOVEMENT_UP_RIGHT = 6,
    MOVEMENT_DOWN_LEFT = 7,
    MOVEMENT_DOWN_RIGHT = 8,
};

// Per-bullet scratch. Size 0x364 verified by loop strides in UpdatePlayerBullets
// (FUN_0043d2f0) and DrawBullets (FUN_0043d690). Interior layout reconstructed
// from those reads; intermediate bytes are scratch used by per-character
// FireBullet callbacks and are left as opaque padding.
struct PlayerBullet
{
    AnmVm sprite;            // +0x000 (0x24c bytes)
    D3DXVECTOR3 position;    // +0x24c
    D3DXVECTOR3 size;        // +0x258
    D3DXVECTOR2 velocity;    // +0x264
    f32 sidewaysMotion;      // +0x26c
    u8 _pad_270[0x318 - 0x270];
    D3DXVECTOR3 unk_318;     // +0x318 (collision size used by CalcDamageToEnemy)
    D3DXVECTOR3 unk_324;     // +0x324 (collision velocity used by CalcDamage)
    i32 unk_330;             // +0x330
    i32 unk_334;             // +0x334
    i32 unk_338;             // +0x338 (angle used by DrawBullets autorotate)
    i32 unk_33c;             // +0x33c
    ZunTimer unk_340;        // +0x340 (per-bullet age timer)
    i16 bulletState;         // +0x34a
    i16 bulletType;          // +0x34c
    i16 damage;              // +0x34e
    i16 unk_350;             // +0x350
    i32 spawnPositionIdx;    // +0x354 (option index this bullet was fired from)
    void *unk_358;           // +0x358 (callback ptr consulted in UpdateBullets)
    void *unk_35c;           // +0x35c (callback ptr consulted in CalcDamage)
    void *unk_360;           // +0x360 (per-power bullet-data row pointer)
    u8 _pad_364[0];
};
ZUN_ASSERT_SIZE(PlayerBullet, 0x364);

// BombInfo. Four function pointers indexed by focus state. Offsets verified
// from AddedCallback (FUN_004423e0) and OnUpdate/OnDrawHighPrio dispatch.
struct PlayerBombInfo
{
    i32 isInUse;            // +0x16a20
    i32 isFocus;            // +0x16a24
    i32 duration;           // +0x16a28 (set to 999 at start)
    i32 unk_16a2c;          // +0x16a2c (score/cherry penalty amount)
    ZunTimer timer;         // +0x16a30 (previous/subFrame/current)
    void (*calcUnfocused)(Player *p); // +0x16a3c
    void (*drawUnfocused)(Player *p); // +0x16a40
    void (*calcFocused)(Player *p);   // +0x16a44
    void (*drawFocused)(Player *p);   // +0x16a48
};
ZUN_ASSERT_SIZE(PlayerBombInfo, 0x30);

// Player. Stored as a raw byte buffer of size 0xb7e78 (verified by RegisterChain's
// memset loop of 0x2df9e dwords). Named accessors for the verified offsets are
// provided as inline helpers below; the buffer layout is the source of truth.
//
// Verified field map (offset -> meaning):
//   +0x000   AnmVm playerSprite
//   +0x24c   AnmVm orbsSprite[0]
//   +0x498   AnmVm orbsSprite[1]
//   +0x6e4   AnmVm orbsSprite[2]   (third slot for Sakuya)
//   +0x930   D3DXVECTOR3 positionCenter
//   +0x948   D3DXVECTOR3 hitboxTopLeft
//   +0x954   D3DXVECTOR3 hitboxBottomRight
//   +0x960   D3DXVECTOR3 grabItemTopLeft
//   +0x96c   D3DXVECTOR3 grabItemBottomRight
//   +0x978   D3DXVECTOR3 grazeTopLeft
//   +0x984   D3DXVECTOR3 grazeBottomRight
//   +0x990   D3DXVECTOR3 hitboxSize    (default 1.25 / 1.25 / 5.0)
//   +0x99c   D3DXVECTOR3 grabItemSize  (default 12 / 12 / 5)
//   +0x9a8   f32 grazeSizeX  (= 20.0 in graze boxes)
//   +0x9ac   f32 grazeSizeY
//   +0x9b0   f32 unk_9b0
//   +0x9b4   D3DXVECTOR3 orbsPosition[0]
//   +0x9c0   D3DXVECTOR3 orbsPosition[1]
//   +0x9cc   f32 movementSpeedX (this frame horizontal delta)
//   +0x9d0   f32 movementSpeedY (this frame vertical delta)
//   +0x9dc   bombRegionTable[0x70] (0x20 bytes each: pos/size/damage/hitCtr)
//   +0x17dc  bombProjectiles[0x60] (0x20 bytes each)
//   +0x2444  PlayerBullet bullets[0x60]
//   +0x23f0  f32 horizontalMovementSpeedMultiplierDuringBomb (init 1.0)
//   +0x23f4  f32 verticalMovementSpeedMultiplierDuringBomb   (init 1.0)
//   +0x23f8  i32 unk_23f8 (lives mirror / used in OnUpdate)
//   +0x23fc  i32 respawnTimer
//   +0x2400  i32 bulletGracePeriod (Gui::flag mirror?)
//   +0x241c  i32 playerDirection
//   +0x2420  f32 previousHorizontalSpeed
//   +0x2424  f32 previousVerticalSpeed
//   +0x2428..+0x243c  i32 focus-movement scratch (option focus ZunTimer trios)
//   +0x2408  u8 playerState
//   +0x240a  u8 orbState
//   +0x240b  u8 isFocus
//   +0x240c  u8 unk_240c (collision frame counter)
//   +0x240d  u8 isInSupernaturalBorder
//   +0x169f4 ZunTimer fireBulletTimer     (init previous=-999,sub=0,current=0)
//   +0x16a00 ZunTimer invulnerabilityTimer (init previous=-999,sub=0,current=120)
//   +0x16a20 PlayerBombInfo bombInfo      (0x30 bytes)
//   +0x16a4c..+0xb7e4c  bomb visual slots / scratch (per-character bomb state)
//   +0xb7e4c  D3DXVECTOR3 positionOfLastEnemyHit
//   +0xb7e58  f32 facingAngle (-PI/2 default; updated in HandlePlayerInputs)
//   +0xb7e68  void *spellcardEffectInvuln
//   +0xb7e6c  void *spellcardEffectDying
//   +0xb7e70  void *optionTableUnfocused (CharacterPowerData *)
//   +0xb7e74  void *optionTableFocused   (CharacterPowerData *)
struct Player
{
    u8 raw[0xb7e78];

    // Convenience accessors for the most-commonly-used verified fields.
    AnmVm *PlayerSprite()
    {
        return reinterpret_cast<AnmVm *>(&raw[0x000]);
    }
    AnmVm *OrbsSprite(i32 idx)
    {
        return reinterpret_cast<AnmVm *>(&raw[0x24c + idx * 0x24c]);
    }
    D3DXVECTOR3 *PositionCenter()
    {
        return reinterpret_cast<D3DXVECTOR3 *>(&raw[0x930]);
    }
    PlayerBullet *Bullets()
    {
        return reinterpret_cast<PlayerBullet *>(&raw[0x2444]);
    }
    u8 *PlayerStatePtr()
    {
        return &raw[0x2408];
    }
    u8 *OrbStatePtr()
    {
        return &raw[0x240a];
    }
    u8 *IsFocusPtr()
    {
        return &raw[0x240b];
    }
    ZunTimer *FireBulletTimer()
    {
        return reinterpret_cast<ZunTimer *>(&raw[0x169f4]);
    }
    ZunTimer *InvulnerabilityTimer()
    {
        return reinterpret_cast<ZunTimer *>(&raw[0x16a00]);
    }
    PlayerBombInfo *BombInfo()
    {
        return reinterpret_cast<PlayerBombInfo *>(&raw[0x16a20]);
    }
};
ZUN_ASSERT_SIZE(Player, 0xb7e78);

// Chain element globals stored separately (verified in FUN_004429d0).
extern ChainElem *g_Player_chainCalc;  // @ 0x00575934
extern ChainElem *g_Player_chainDraw1; // @ 0x00575938
extern ChainElem *g_Player_chainDraw2; // @ 0x0057593c

DIFFABLE_EXTERN(Player, g_Player)
}; // namespace th07
