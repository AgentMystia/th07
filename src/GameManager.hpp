#pragma once

// th07::GameManager  in-game state manager singleton @ 0x00626270.
// sizeof = 0x9700. The Item[1100] array is NOT here (it lives in ItemManager @
// 0x575c70); IsGameActive/CalculateChecksum are NOT GameManager methods (they
// belong to the Supervisor/StageGame singleton @ 0x49fbf0). GameManager owns
// only the 6 chain-lifecycle methods: RegisterChain/CutChain/OnUpdate/OnDraw/
// AddedCallback/DeletedCallback.
//
// Struct offsets verified against disassembly of AddedCallback(0x42e83e),
// OnUpdate(0x42d8d5), DeletedCallback(0x42f2e4), CutChain, RegisterChain.
// See workflows/gamemanager_final_struct.md for the full reconciliation.
//
// Key addresses:
//   g_GameManager             @ 0x00626270
//   g_Chain (Chain controller) @ 0x00626218
//   ItemManager singleton     @ 0x00575c70   (owns Item[1100])
//   active-game singleton     @ 0x0049fbf0   (IsGameActive/CalculateChecksum)

#include "Chain.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// Forward
struct GameManager;

// ScoreSub  score/stats/CRC sub-struct reached via GameManager+0x8 (DAT_00626278).
// Heap-allocated (0xC8 bytes) by AddedCallback. sizeof = 0xC8. Load-bearing
// offsets named (+0x0/+0x4/+0x34/+0x3c/+0x88/+0xac/+0xb4/+0xbc); the rest are
// descriptive labels for readability. +0x1fbac is NOT here (that is StageGame).
struct ScoreSub
{
    u32 guiScore;            // +0x00 displayed/committed score (CutChain syncs from +0x04)
    u32 score;               // +0x04 running score (CutChain clamps to 999999999)
    u32 scoreDelta;          // +0x08 per-frame smoothing delta (=(target-displayed)>>5, [1..0x8d55e])
    u32 highScore;           // +0x0c high score (init 100000; updated to guiScore on exceed)
    u8  highScoreStatusByte; // +0x10 copied from +0x20 on new high
    u8  pad11[3];            // +0x11
    i32 counter14;           // +0x14 cleared 0 at AddedCallback tail
    i32 counter18;           // +0x18 case-6/9 item-value scaling (=(this/0x28)*10+300)
    i32 counter1c;           // +0x1c case-7 graze-derived point value (=this*100+1000)
    u8  srcStatusByte20;     // +0x20 source status byte; gates extend-record update
    u8  pad21[3];            // +0x21
    i32 grazeCount;          // +0x24 graze counter (case-1 point item)
    i32 lifePieceProgress;   // +0x28 progress toward life-piece threshold
    i32 lifePieceTier;       // +0x2c life-piece tier index
    i32 lifePieceThreshold;  // +0x30 life-piece threshold target
    i32 seedIds7[7];         // +0x34 7 RNG ints [0x198f..0x1a02f]; checksum region 1 (0x78 bytes)
    i32 crcReseed38;         // +0x3c seed/base copied into +0xac each frame
    u8  pad40[0x50 - 0x40];  // +0x40
    i32 counter50;           // +0x50 cleared 0 at AddedCallback tail
    f32 randF3254[2];        // +0x54 2 random floats
    f32 playerCountF32;      // +0x5c =(f32)(byte)playerSub+0x1c (non-extra)
    f32 randF3260[2];        // +0x60 2 random floats
    i32 bombCount;           // +0x68 bomb count accumulator
    i32 counter6c;           // +0x6c cleared 0
    f32 randF3370[3];        // +0x70 3 random floats
    f32 pointItemAutoBonus;  // +0x7c point-item auto-collect bonus (128.0f on full-power)
    f32 randF3280[2];        // +0x80 2 random floats
    i32 mainSeedScoreBase;   // +0x88 = Rng%100000+0x198f; base for extend thresholds
    i32 seedIds8[8];         // +0x8c 8 RNG ints; range-checked by OnUpdate
    i32 stageTimeBase98;     // +0x98 stage-time base; added to CRC -> DAT_0062f884
    u8  pad9c[0xac - 0x9c];  // +0x9c
    i32 crcAcc;              // +0xac CRC/LCG accumulator; +=crcStepBc per byte (checksum loop)
    i32 checksumResultB0;    // +0xb0 stores CalculateChecksum return
    i32 seedIds5[5];         // +0xb4 5 RNG ints; checksum region 2 (0x14 bytes)
    i32 crcStepBc;           // +0xbc LCG increment added to +0xac per byte
    u8 padC0[0xC8 - 0xC0];   // +0xc0 trailing pad to operator_new(0xC8) allocation
};
ZUN_ASSERT_SIZE(ScoreSub, 0xC8)

// PlayerSub  player/character sub-struct reached via GameManager+0x4.
// Heap-allocated (0x38 bytes) by AddedCallback; first 0x38 bytes memcpy'd from
// g_PlayerInitTemplate @ 0x575a68.
struct PlayerSub
{
    u8 header[0x1c];         // +0x00 copied from g_PlayerInitTemplate
    u8 playerCountMode;      // +0x1c =2 if stage>3; =8 if extra-bit set
    u8 pad1d[0x25 - 0x1d];   // +0x1d
    u8 bonusExtendGate;      // +0x25 cleared when difficulty>=4 OR extra
    u8 pad26[0x38 - 0x26];   // +0x26
};
ZUN_ASSERT_SIZE(PlayerSub, 0x38)

#define GAME_MANAGER_SIZE 0x9700

DIFFABLE_EXTERN(GameManager, g_GameManager)

struct GameManager
{
    // --- header (+0x0..+0x14) ---
    void *scratchBuf;            // +0x0  malloc scratch (AddedCallback)
    PlayerSub *playerSub;        // +0x4  PlayerSub* (operator_new(0x38))
    ScoreSub *scoreSub;          // +0x8  ScoreSub* (operator_new(0xC8)); DAT_00626278
    i32 flag0c;                  // +0xc  generic flag (written 0 at AddedCallback tail)
    i32 difficulty;              // +0x10 difficulty/stage selector (0-5); DAT_00626280
    u32 difficultyMask;          // +0x14 = 1 << (difficulty & 0x1f)

    // +0xd (rankForceFlag / DAT_0062627d) lives inside the pad below; accessed
    // via raw offset in code (splitting flag0c to expose it would break the i32).
    u8 unk_18[0x93d8 - 0x18];     // +0x18 opaque (entity arrays, extend records @0x8454/0x845a)

    // --- control-word block (+0x93d8..+0x9644)  all load-bearing ---
    u32 statusBitfield;          // +0x93d8 (DAT_0062f648) bit0=extra,bit1=paused,bit2=chain-reg,bit3=practice,bit4=clr-on-success
    u8 unk_93dc;                 // +0x93dc pause-request flag (OnDraw: if !=0 -> =2)
    u8 unk_93dd;                 // +0x93dd pause-latch companion (cleared at AddedCallback success)
    u8 unk_93de[2];              // +0x93de (difficultySelector byte lives here as +0x93de)
    i32 pauseFrameCounter;       // +0x93e0 pause-watched-frame counter (thresholds 0x1fa4/0x1b6c/0x120c by difficultySelector)
    u8 unk_93e4[0x95e4 - 0x93e4]; // +0x93e4 opaque
    u16 randSeedWord;            // +0x95e4 written from DAT_0049fe20 (AddedCallback)
    u8 unk_95e6[2];              // +0x95e6
    i32 frameCounter;            // +0x95e8 (DAT_0062f858) zeroed by RegisterChain/DeletedCallback; ++per tick
    i32 playCount;               // +0x95ec ++1 at AddedCallback tail; picks ECL script
    u8 unk_95f0[0x95f4 - 0x95f0]; // +0x95f0
    i32 anmColorSetup[8];        // +0x95f4 Anm/sprite color setup (FUN_0042d657)
    f32 randSumFloat;            // +0x9614 from randSum+seed (FUN_004012b0)
    i32 extendThresholdA;        // +0x9618 primary extend threshold
    i32 extendThresholdB;        // +0x961c secondary extend threshold
    i32 initScoreMirror;         // +0x9620 = *(scoreSub+0x88)
    u8 unk_9624[4];              // +0x9624
    i32 counter9628;             // +0x9628 cleared 0 at AddedCallback init
    u32 rankCounter;             // +0x962c rank-up counter (mod 2/3/4/5/6 gating)
    i32 stageColorA;             // +0x9634 stage color dword 1
    i32 stageColorB;             // +0x9638 stage color dword 2
    i32 stageColorC;             // +0x963c stage color dword 3
    i32 counter9640;             // +0x9640 cleared 0 at AddedCallback tail

    // --- embedded chain nodes (+0x9644..+0x9684) ---
    ChainElem updateChainNode;   // +0x9644 calc-chain node (cb=OnUpdate, Added/Deleted set, arg=&this)
    ChainElem drawChainNode;     // +0x9664 draw-chain node (cb=OnDraw, Added/Deleted=NULL, arg=&this)

    // --- tail (+0x9684..+0x9700) ---
    u8 unk_9684[GAME_MANAGER_SIZE - 0x9684]; // +0x9684 chain-control blocks; highest xref +0x96e8

    // --- methods (chain lifecycle) ---
    static ZunResult RegisterChain();
    static void CutChain();
    static ChainCallbackResult __fastcall OnDraw(GameManager *gameManager);
    static ChainCallbackResult __fastcall OnUpdate(GameManager *gameManager);
    static ZunResult __fastcall AddedCallback(GameManager *gameManager);
    static ZunResult __fastcall DeletedCallback(GameManager *gameManager);
};
ZUN_ASSERT_SIZE(GameManager, GAME_MANAGER_SIZE)

// Static template @ 0x575a68  memcpy source for PlayerSub (AddedCallback) and
// the 4th checksum region in CalculateChecksum (Supervisor method).
DIFFABLE_EXTERN_ARRAY(u8, 0x38, g_PlayerInitTemplate)

// Opaque cross-module singletons reached by DeletedCallback (stub externs for
// codegen; real types land when those modules are reversed).
// g_Supervisor declared in Supervisor.hpp
extern u8   g_SupervisorState;   // @ 0x00575a87 (==2 -> MIDI teardown in DeletedCallback)
extern u32  g_SupervisorRedrawFlag; // @ 0x00575ae4
extern void *g_SupervisorSound;  // @ 0x004ba0d8 (sound-queue side; ECX for ProcessQueue)
extern void *g_MidiOutput;       // @ 0x00575acc (MidiOutput*)
extern void *g_COleDispParams;   // @ 0x01347938 (enemy/disp singleton)
// g_EffectManager is declared (typed) in EffectManager.hpp via DIFFABLE_EXTERN.

}; // namespace th07
