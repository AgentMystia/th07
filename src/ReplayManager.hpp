// ReplayManager module for th07.
//
// Reverse engineered from th07.exe via Ghidra. Every field offset, immediate
// constant, and chain priority in this header is read directly from the
// disassembly -- do not "tidy up" without re-checking the binary.
//
// The th07 ReplayManager diverges substantially from the th06 reference:
//   * The on-disk header grew from sizeof(ReplayData) (th06) to 0x54 bytes.
//   * Stage data is split into two parallel per-stage arrays (offsets 0x1c and
//     0x38 inside ReplayData, each 7 pointers) -- one for the player input
//     stream, one for a side input stream that stores things like CKey-style
//     extra state.
//   * Pbg4 is gone; the compressed payload is produced by a LZSS encoder
//     (Compression::Compress / FUN_0045ead0) living in a separate module.
//   * date is written with _strftime("%m/%d") rather than _strdate.
//   * "exesumcheck" is a score.dat concept in th07 and does NOT appear in the
//     replay path -- there is no replay-file checksum over the executable.
//
// All comments are in English because MSVC 7.0 does not interpret UTF-8 source
// files and the diffbuild must be byte-clean.

#pragma once

#include "Chain.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "ZunResult.hpp"

namespace th07
{
// Maximum number of stages that a replay can hold. Read from the
// ARRAY_SIZE_SIGNED loops that count 0..7 across replayData->stageReplayData[]
// (e.g. 00443ee1, 00443f75).
#define REPLAY_STAGE_COUNT 7

// On-disk replay header size. Read from the SaveReplay immediate at 00443eb3
// (`mov [ebp+...], 0x54`) and the WriteFile at 00443bf (`push 0x54`).
#define REPLAY_HEADER_SIZE 0x54

// Checksum seed and obfuscation constants. Read from SaveReplay at 0044421f
// (`mov dword [ebp+...], 0x3f000318`) and the obf step `+7` at 00444320.
#define REPLAY_CHECKSUM_SEED 0x3f000318
#define REPLAY_OBFUSCATE_STEP 7

// Number of bytes covered by the header checksum (starting at the `key` byte)
// and the header obfuscation pass (starting at `rngValue3`). Read from the
// loop bounds 0x47 (0044424a) and 0x44 (004442f9) in SaveReplay.
#define REPLAY_CHECKSUM_HEADER_BYTES 0x47
#define REPLAY_OBFUSCATE_HEADER_BYTES 0x44

// Per-stage input buffer capacity, read from the `_malloc(0x70800)` immediate
// in AddedCallback (00443241).
#define REPLAY_STAGE_INPUT_BYTES 0x70800

// Top-level scratch buffer used to assemble the serialized replay before
// compression. Read from the `_malloc(0x100000)` in SaveReplay (00443e4f).
#define REPLAY_SCRATCH_BUFFER_BYTES 0x100000

// Magic written into the first four bytes of the header. Read from
// DAT_00496aa8 ("T7RP") -- verified by read_memory at 0x496aa8.
#define REPLAY_MAGIC (*(i32 *)"T7RP")

// Version field stored at header offset 0x04 (low 16 bits) and 0x06 (high 16
// bits). Read from AddedCallback at 004430f5 (`mov word [eax+4], 0x1100`) and
// 0044310a (`mov word [eax+0x6a], 0x100`).
#define REPLAY_VERSION_LO 0x1100
#define REPLAY_VERSION_HI 0x0100

// Chain priorities. Read from RegisterChain (FUN_00443aa0) immediates:
//   calc, !isDemo      -> 0x10  (00443b9a)
//   calc, isDemo high  -> 0x05  (00443c6a)
//   calc, isDemo low   -> 0x11  (00443cb3)
//   draw, both         -> 0x0e  (00443ce5)
#define TH_CHAIN_PRIO_CALC_REPLAYMANAGER 0x10
#define TH_CHAIN_PRIO_CALC_REPLAYMANAGER_DEMO_HIGH 0x05
#define TH_CHAIN_PRIO_CALC_REPLAYMANAGER_DEMO_LOW 0x11
#define TH_CHAIN_PRIO_DRAW_REPLAYMANAGER 0x0e

// Single frame of recorded player input. Layout matches the field writes in
// OnUpdate (00442d39..00442d44): a 16-bit input mask followed by a 16-bit
// stage-relative frame counter. 4 bytes per entry.
struct ReplayDataInput
{
    u16 inputKey;
    u16 frameNum;
};
ZUN_ASSERT_SIZE(ReplayDataInput, 0x4);

// Header written at the start of every stage input block. Field offsets are
// read directly from AddedCallback (FUN_00443040) and AddedCallbackDemo
// (FUN_00443550): score at +0x00, GameManager hook pointers at +0x08/+0x0c/
// +0x10, lives/power/rank at +0x14..+0x20, then the input stream starts at
// +0x2c (see 0044320d `mov [param_1+0x21], iVar4+0x2c`).
struct StageReplayData
{
    i32 score;                  // +0x00, written at 00443eb1
    i32 unk04;                  // +0x04
    i32 unk08;                  // +0x08 -- GameManager skyAdjust delta
    i32 unk0c;                  // +0x0c -- GameManager skyAdjust delta
    i32 unk10;                  // +0x10 -- GameManager skyAdjust delta
    i32 unk14;                  // +0x14 -- power
    i32 unk18;                  // +0x18
    i32 unk1c;                  // +0x1c
    i32 unk20;                  // +0x20 -- difficulty/flags
    u8 unk24;                   // +0x24
    u8 unk25;                   // +0x25
    u8 unk26;                   // +0x26
    u8 unk27;                   // +0x27
    ReplayDataInput replayInputs; // +0x2c.. (variable-length stream follows)
};
// Stage header before the input stream. The full stage block is allocated as
// REPLAY_STAGE_INPUT_BYTES; only the leading 0x2c bytes are stage metadata.
#define STAGE_REPLAY_DATA_HEADER_SIZE 0x2c

// On-disk replay file header (REPLAY_HEADER_SIZE = 0x54 bytes). Field offsets
// cross-verified against SaveReplay (FUN_00443da0), ReadReplayFile
// (FUN_004433b0) and AddedCallbackDemo (FUN_00443550).
struct ReplayData
{
    i32 magic;                  // +0x00, "T7RP"
    u16 version;                // +0x04, REPLAY_VERSION_LO
    u16 difficulty;             // +0x06
    u8 key;                     // +0x0d, obfuscation seed
    u32 checksum;               // +0x08, REPLAY_CHECKSUM_SEED + sum
    i32 unk10;                  // +0x10, payload size written by ReadReplayFile
    u8 rngValue3;               // +0x11, start of obfuscation range
    u8 rngValue2;               // +0x12
    u8 rngValue1;               // +0x13
    u8 shottypeChara;           // +0x14, chara*2 + shotType (added at runtime)
    u8 unk15[0x41];             // +0x15..0x55
    StageReplayData *stageReplayData[REPLAY_STAGE_COUNT];   // +0x1c (ghidra field name; offsets +0x1c..+0x38)
    StageReplayData *stageReplayData2[REPLAY_STAGE_COUNT];  // +0x38..+0x54
    u8 name[8];                 // displayed in menu, written via strcpy
    u8 date[8];                 // "%m/%d" via _strftime
    f32 slowdownRate;           // (1 - slowdown) * 100
    f32 slowdownRate2;          // +1.12
    f32 slowdownRate3;          // +2.34
    f32 score;                  // guiScore at save time
};
// The actual on-disk struct is exactly REPLAY_HEADER_SIZE bytes. The C++ view
// above documents named fields; the assert is on the serialized size only.
ZUN_ASSERT_SIZE(ReplayData, REPLAY_HEADER_SIZE);

// Runtime replay manager state. Allocated as operator_new(0xd8) in
// RegisterChain (00443acb). Every field offset below is cross-checked against
// the disassembly -- do not reorder.
struct ReplayManager
{
    // -- Lifecycle callbacks (Chain targets). --
    static ZunResult RegisterChain(i32 isDemo, char *replayFile);
    static ChainCallbackResult OnUpdate(ReplayManager *mgr);
    static ChainCallbackResult OnUpdateDemoHighPrio(ReplayManager *mgr);
    static ChainCallbackResult OnUpdateDemoLowPrio(ReplayManager *mgr);
    static ChainCallbackResult OnDraw(ReplayManager *mgr);
    static ZunResult AddedCallback(ReplayManager *mgr);
    static ZunResult AddedCallbackDemo(ReplayManager *mgr);
    static ZunResult DeletedCallback(ReplayManager *mgr);

    // -- Save / stop helpers. --
    static void StopRecording();
    static void SaveReplay(char *replayPath, char *replayName);
    static void RewriteReplay(char *replayPath);

    // -- Validate + decode an in-memory replay blob. --
    // (In th07 this logic is inlined into ReadReplayFile / FUN_004433b0 rather
    // than living in a standalone ValidateReplayData; kept here for parity with
    // the th06 reference and to document the on-disk format.)
    static ZunResult ValidateReplayData(ReplayData *data, i32 fileSize);

    ReplayManager()
    {
    }

    i32 IsDemo()
    {
        return this->isDemo;
    }

    // +0x00 -- frame counter, incremented each OnUpdate tick.
    i32 frameId;
    // +0x04 -- decoded replay header (heap-allocated, freed in DeletedCallback).
    ReplayData *replayData;
    // +0x08..+0x24 -- per-stage byte sizes for the primary input stream.
    // Filled in AddedCallbackDemo (00443645..) so SaveReplay can skip empties.
    i32 stageSizes1[REPLAY_STAGE_COUNT];
    // +0x24..+0x40 -- per-stage byte sizes for the secondary input stream.
    i32 stageSizes2[REPLAY_STAGE_COUNT];
    // +0x40 -- scratch pointer freed in DeletedCallback (00443a30).
    void *unk40;
    // +0x44 -- 0 for normal recording/playback, non-zero for demo mode.
    i32 isDemo;
    // +0x48 -- path of the demo file to load (AddedCallbackDemo only).
    char *replayFile;
    // +0x4c..+0x80 -- reserved; touched by adjacent chain math.
    u8 unk4c[0x34];
    // +0x80 -- combo of the per-stage last-frame markers and stage selection
    // scratch used by OnUpdate (see 00442d0f..00442d44). Named for the closest
    // th06 concept.
    i32 unk80;
    // +0x84 -- current write cursor into the primary input stream.
    // Stored as byte* but kept as i32 to match the ghidra signature exactly.
    i32 replayInputsCursor;
    // +0x88..+0xa4 -- end-of-stage markers into the primary input stream
    // (one per stage). Set by OnUpdate/StopRecording; consumed by SaveReplay.
    u8 *replayInputStageBookmarks1[REPLAY_STAGE_COUNT];
    // +0xa4 -- current write cursor into the secondary input stream.
    i32 replayInputs2Cursor;
    // +0xa8..+0xc4 -- end-of-stage markers into the secondary input stream.
    u8 *replayInputStageBookmarks2[REPLAY_STAGE_COUNT];
    // +0xc4 -- calc-chain element (OnUpdate or OnUpdateDemoHighPrio).
    ChainElem *calcChain;
    // +0xc8 -- draw-chain element (OnDraw).
    ChainElem *drawChain;
    // +0xcc -- secondary calc-chain element (OnUpdateDemoLowPrio); NULL when
    // isDemo is false.
    ChainElem *calcChainDemoLowPrio;
    // +0xd0 -- reserved; NULL in both branches of RegisterChain.
    ChainElem *unkD0;
};
ZUN_ASSERT_SIZE(ReplayManager, 0xd8);
}; // namespace th07
