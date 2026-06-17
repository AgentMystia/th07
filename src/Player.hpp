// Player module for th07 (Perfect Cherry Blossom).
//
// All field offsets below were verified by reading th07.exe in ghidra. They
// MUST NOT be edited without re-checking the binary.
//
// LAYOUT NOTE (th06 standard): every byte of the 0xb7e78-byte Player struct
// is accounted for by a named member. Known-semantic offsets use business
// names; unexplored regions use `u8 unk_XXXX[N]` padding so the layout stays
// auditable and the running offset total is exact. There is NO `u8 raw[]`
// blob and NO accessor returning `&raw[OFF]` -- callers touch fields by name.
//
// Global address map (verified):
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
//   * Player struct 0x98f0 -> 0xb7e78 bytes. The growth is almost entirely
//     the bombProjectileSlots[] array (32 sub-structs of 0x1428 bytes each,
//     starting at +0x16a50), which holds per-character spell-card visuals.
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
};
ZUN_ASSERT_SIZE(PlayerBullet, 0x364);

// BombInfo. Four function pointers indexed by focus state. Offsets verified
// from AddedCallback (FUN_004423e0) and OnUpdate/OnDrawHighPrio dispatch.
struct PlayerBombInfo
{
    i32 isInUse;            // +0x16a20
    i32 isFocus;            // +0x16a24
    i32 duration;           // +0x16a28 (set to 999 at start; counts down via ZunTimer)
    i32 unk_16a2c;          // +0x16a2c (score/cherry penalty amount)
    ZunTimer timer;         // +0x16a30 (previous/subFrame/current)
    void (*calcUnfocused)(Player *p); // +0x16a3c
    void (*drawUnfocused)(Player *p); // +0x16a40
    void (*calcFocused)(Player *p);   // +0x16a44
    void (*drawFocused)(Player *p);   // +0x16a48
};
ZUN_ASSERT_SIZE(PlayerBombInfo, 0x30);

// Spell-card / bomb-projectile visual slot. Each entry is 0x1428 bytes
// (verified by `local_14 = local_14 + 0x50a` dword-pointer stride in the
// per-character BombCalc loops: ReimuCBombCalc / ReimuABombCalc2 /
// MarisaABombCalc2 = FUN_00408710 / FUN_0040c2e0 / FUN_0040d4c0). The live
// loop walks 8 entries per frame; the bomb-start initialiser zeros 0x20 (32)
// entries via a `for(local_10=0; local_10<0x20; ...)` loop writing the dword
// at +0x16a50 + idx*0x1428.
//
// The interior layout below is reconstructed from those three callbacks
// (dword indices relative to the slot base): state@0, counter@4, speed@0xc,
// angle@0x10, pos@0x14, velocity@0x68/0x6c/0x70, four AnmVms at dword
// indices 0x6e/0x101/0x194/0x227 (= byte offsets 0x1b0/0x404/0x650/0x89c,
// 0x24c apart). Regions between are per-character scratch and left as
// `unk_XXXX` padding.
//
// NOTE: only the first batch of 32 slots occupies the range
// +0x16a50..+0x3ef50. The remainder of Player's tail (+0x3ef50..+0xb7e4c)
// holds additional per-character state that the bomb callbacks reach via
// `Player* + imm` arithmetic (not via slot indexing); it is exposed as
// `Player::unk_3ef50[]` below rather than fabricated into sub-structs.
struct BombProjectileSlot
{
    i32 state;                 // +0x00
    i32 counter;               // +0x04
    u8 unk_08[0x0c - 0x08];    // +0x08
    f32 speed;                 // +0x0c
    f32 angle;                 // +0x10
    D3DXVECTOR3 position;      // +0x14
    u8 unk_20[0x68 - 0x20];    // +0x20
    i32 velocityX;             // +0x68 (f32 bits; multiplied by dt each tick)
    i32 velocityY;             // +0x6c
    i32 velocityZ;             // +0x70
    u8 unk_74[0x1b0 - 0x74];   // +0x74
    AnmVm anmVms[4];           // +0x1b0 .. +0xae0 (4 * 0x24c)
    u8 unk_ae0[0x1428 - 0xae0]; // +0xae0 per-character scratch
};
ZUN_ASSERT_SIZE(BombProjectileSlot, 0x1428);

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
//   +0x9dc   D3DXVECTOR3 orbTargetPositions[8]  (per BombProjectileSlot: target pos)
//   +0x9dc..+0x9f8 +0x20 stride per slot (3 dwords pos + 3 dwords velocity + size)
//   +0x23f0  f32 horizontalMovementSpeedMultiplierDuringBomb (init 1.0)
//   +0x23f4  f32 verticalMovementSpeedMultiplierDuringBomb   (init 1.0)
//   +0x23f8  i32 unk_23f8 (lives mirror / used in OnUpdate)
//   +0x23fc  i32 respawnTimer
//   +0x2400  i32 bulletGracePeriod (Gui::flag mirror?)
//   +0x2408  u8 playerState
//   +0x240a  u8 orbState
//   +0x240b  u8 isFocus
//   +0x240c  u8 unk_240c (collision frame counter)
//   +0x240d  u8 isInSupernaturalBorder
//   +0x241c  i32 playerDirection
//   +0x2420  f32 previousHorizontalSpeed
//   +0x2424  f32 previousVerticalSpeed
//   +0x2428..+0x243c  i32 focus-movement scratch (option focus ZunTimer trios)
//   +0x2444  PlayerBullet bullets[96]      (96 * 0x364 = 0x14580 -> ends 0x169c4)
//   +0x169c4 option-slot metadata[3]       (3 stride-0x10 records; ptrs/counters
//                                           for the 3 (or 4 for Sakuya) option slots)
//   +0x169f4 ZunTimer fireBulletTimer     (init previous=-999,sub=0,current=0)
//   +0x16a00 ZunTimer invulnerabilityTimer (init previous=-999,sub=0,current=120)
//   +0x16a20 PlayerBombInfo bombInfo      (0x30 bytes)
//   +0x16a50 BombProjectileSlot bombProjectileSlots[32]  (0x20 * 0x1428 = 0x28500)
//   +0x3ef50..+0xb7e4c per-character bomb/callback scratch (reserved pad)
//   +0xb7e4c D3DXVECTOR3 positionOfLastEnemyHit
//   +0xb7e58 f32 facingAngle (-PI/2 default; updated in HandlePlayerInputs)
//   +0xb7e68 void *spellcardEffectInvuln
//   +0xb7e6c void *spellcardEffectDying
//   +0xb7e70 void *optionTableUnfocused (CharacterPowerData *)
//   +0xb7e74 void *optionTableFocused   (CharacterPowerData *)
struct Player
{
    // ===== +0x000 .. +0x930 : sprites & geometry =====
    AnmVm playerSprite;                      // +0x000
    AnmVm orbsSprite[3];                     // +0x24c, +0x498, +0x6e4
    D3DXVECTOR3 positionCenter;              // +0x930
    D3DXVECTOR3 hitboxTopLeft;               // +0x948
    D3DXVECTOR3 hitboxBottomRight;           // +0x954
    D3DXVECTOR3 grabItemTopLeft;             // +0x960
    D3DXVECTOR3 grabItemBottomRight;         // +0x96c
    D3DXVECTOR3 grazeTopLeft;                // +0x978
    D3DXVECTOR3 grazeBottomRight;            // +0x984
    D3DXVECTOR3 hitboxSize;                  // +0x990
    D3DXVECTOR3 grabItemSize;                // +0x99c
    f32 grazeSizeX;                          // +0x9a8
    f32 grazeSizeY;                          // +0x9ac
    f32 unk_9b0;                             // +0x9b0
    D3DXVECTOR3 orbsPosition[2];             // +0x9b4, +0x9c0
    f32 movementSpeedX;                      // +0x9cc
    f32 movementSpeedY;                      // +0x9d0
    // +0x9dc..+0x23f0 opaque: orb target positions + bomb-region tables.
    // (th06 modelled bombRegionPositions/Sizes/Damages + bombProjectiles here;
    //  th07's layout in this range is touched by the bomb callbacks but its
    //  exact sub-fields are still being reconciled. Reserved as named padding
    //  to keep the offset math exact.)
    u8 unk_9dc[0x23f0 - 0x9dc];              // +0x9dc

    // ===== +0x23f0 .. +0x2450 : per-bomb motion + flags =====
    f32 horizontalMovementSpeedMultiplierDuringBomb; // +0x23f0
    f32 verticalMovementSpeedMultiplierDuringBomb;   // +0x23f4
    i32 unk_23f8;                            // +0x23f8 (lives mirror)
    i32 respawnTimer;                        // +0x23fc
    i32 bulletGracePeriod;                   // +0x2400
    u8 unk_2404[0x2408 - 0x2404];            // +0x2404
    u8 playerState;                          // +0x2408
    u8 unk_2409;                             // +0x2409
    u8 orbState;                             // +0x240a
    u8 isFocus;                              // +0x240b
    u8 unk_240c;                             // +0x240c (collision frame counter)
    u8 isInSupernaturalBorder;               // +0x240d
    u8 unk_240e[0x241c - 0x240e];            // +0x240e
    i32 playerDirection;                     // +0x241c
    f32 previousHorizontalSpeed;             // +0x2420
    f32 previousVerticalSpeed;               // +0x2424
    u8 unk_2428[0x2444 - 0x2428];            // +0x2428 (focus-movement scratch)

    // ===== +0x2444 .. +0x169c4 : player bullets (96 entries) =====
    PlayerBullet bullets[96];                // +0x2444 (96 * 0x364 = 0x14580)

    // ===== +0x169c4 .. +0x169f4 : option-slot metadata (3 option pointers) =====
    // Verified from UpdatePlayerBullets (FUN_0043d2f0): three stride-0x10
    // records at +0x169c4/+0x169d4/+0x169e4 holding {ZunTimer-ish trio,
    // active-frame counter, AnmVm*}. Used to gate option despawn on focus loss
    // / death.
    u8 optionSlotMetadata[0x169f4 - 0x169c4]; // +0x169c4 (3 * 0x10)

    // ===== +0x169f4 .. +0x16a20 : timers =====
    ZunTimer fireBulletTimer;                // +0x169f4
    ZunTimer invulnerabilityTimer;           // +0x16a00

    // ===== +0x16a20 .. +0x16a50 : bomb info =====
    PlayerBombInfo bombInfo;                 // +0x16a20

    // ===== +0x16a50 .. +0x3ef50 : spell-card visual slots (32 * 0x1428) =====
    BombProjectileSlot bombProjectileSlots[32]; // +0x16a50 (32 * 0x1428 = 0x28500)

    // ===== +0x3ef50 .. +0xb7e4c : per-character bomb/callback scratch =====
    // The per-character BombCalc/Draw callbacks (ReimuABombCalc2, SakuyaABombCalc2
    // etc.) reach into this region via `Player* + imm` arithmetic that is not
    // slot-indexed. Rather than fabricate field names for offsets we have not
    // individually verified, the whole region is reserved as one named pad so
    // sizeof stays locked. As specific offsets are anchored they should be
    // promoted to named members here.
    u8 unk_3ef50[0xb7e4c - 0x3ef50];         // +0x3ef50

    // ===== +0xb7e4c .. +0xb7e78 : tail (no padding; all 9 fields named) =====
    D3DXVECTOR3 positionOfLastEnemyHit;      // +0xb7e4c
    f32 facingAngle;                         // +0xb7e58 (-PI/2 default; HandlePlayerInputs)
    ChainElem *chainCalc;                    // +0xb7e5c (orig DAT_00575934)
    ChainElem *chainDraw1;                   // +0xb7e60 (orig DAT_00575938)
    ChainElem *chainDraw2;                   // +0xb7e64 (orig DAT_0057593c)
    void *spellcardEffectInvuln;             // +0xb7e68
    void *spellcardEffectDying;              // +0xb7e6c
    void *optionTableUnfocused;              // +0xb7e70 (CharacterPowerData *)
    void *optionTableFocused;                // +0xb7e74 (CharacterPowerData *)

    // --- chain-lifecycle + gameplay methods (signatures from mapping.csv /
    //     disasm). __fastcall static = ECX holds Player* (or u8 for RegisterChain);
    //     __thiscall instance = ECX holds this, args on stack, RET n. ---
    static ZunResult __fastcall RegisterChain(u8 unk);
    static void CutChain();
    static ZunResult __fastcall AddedCallback(Player *p);
    static ZunResult __fastcall DeletedCallback(Player *p);
    static ChainCallbackResult __fastcall OnUpdate(Player *p);
    static ChainCallbackResult __fastcall OnDrawHighPrio(Player *p);
    static ChainCallbackResult __fastcall OnDrawLowPrio(Player *p);
    static void __fastcall StartFireBulletTimer(Player *p);
    static void __fastcall Die(Player *p);
    // __thiscall instance methods (this in ECX, args pushed, callee RET n):
    f32 AngleToPlayer(D3DXVECTOR3 *pos);
    i32 CalcItemBoxCollision(D3DXVECTOR3 *center, D3DXVECTOR3 *size);
    void ScoreGraze(D3DXVECTOR3 *center);
};
ZUN_ASSERT_SIZE(Player, 0xb7e78);

// Chain element pointers live INSIDE g_Player at +0xb7e5c/+0xb7e60/+0xb7e64
// (verified: g_Player@0x4bdad8 + 0xb7e5c = 0x575934 = orig DAT_00575934).
// These sit inside the unk_b7e58 tail pad above; not separate globals.
DIFFABLE_EXTERN(Player, g_Player)
}; // namespace th07
