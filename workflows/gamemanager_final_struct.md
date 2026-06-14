# GameManager FINAL Struct + ScoreSub Reconciliation

Architect merge of 3 reverse-agent drafts (`draft_scoresub_resolution.md`,
`draft_deletedcallback.md`, `draft_isactive_checksum.md`) plus the existing
synthesis JSON (38-field map) and verified Ghidra disassembly.

Two draft files (`draft_addedcallback.md`, `draft_onupdate.md`) were NOT
written by their agents — their content is reconstructed here from the
synthesis JSON + direct decompilation of FUN_0042e83e (AddedCallback) and
FUN_0042d8d5 (OnUpdate).

---

## 0. RESOLVED CONTRADICTIONS

### A. `+0x1fbac` vs ScoreSub 0xC8 alloc — CLOSED

**VERDICT (confirmed against disasm @0x42ad66 + all 9 caller sites):**
`IsGameActive` (FUN_0042ad66) is a method of a **DIFFERENT singleton** at
`0x0049fbf0` (the "active-game" / `Supervisor.stageGame` owner), NOT of
`g_GameManager` (0x626270). Every caller loads `MOV ECX,0x49fbf0` before the
CALL. Inside, `this->stageGame` (= `*(0x49fbf0+0x8)`) is a 0x20a30-byte
sub-object; offset 0x1fbac lives well inside that allocation.

**ScoreSub is unaffected.** Its 0xC8 allocation and field layout stand as-is.
The `+0x1fbac` field belongs to `StageGame` (see §3), reached via a different
singleton's `+0x8`.

### B. Item[1100] location — CLOSED

Per established fact: `Item[1100]` (1100 × 0x288 = 0xbb280) lives in the
**ItemManager** singleton @ `0x575c70`, NOT in GameManager. The synthesis
JSON's "base 0xae2f0 inside GameManager" entry is a Ghidra constant-fold
artifact from OnItemUpdate receiving `&g_ItemManager` (not `&g_GameManager`)
as its `this`. GameManager sizeof stays **0x9700**.

### C. `statusBitfield` double-duty — INTENTIONAL

`GameManager+0x93d8` = absolute `0x62f648` = the global options/status
bitfield. It IS a field of GameManager (same bytes, one word, many uses):
bit1=paused, bit2=chain-registered / not-pausing, bit3=practice,
bit4=cleared-on-success, bit0=extra. GameManager legitimately owns the
options word. Confirmed by OnUpdate/AddedCallback/DeletedCallback all doing
RMW on `[0x62f648]`.

---

## 1. FINAL ScoreSub (sizeof = **0xC8 = 200**)

Allocation: `operator_new(0xC8)` in AddedCallback @0x42e93f, stored into
`g_GameManager.scoreSub` (+0x8 / DAT_00626278). **NOT** the 0x20a30 active-game
sub-object — that is `StageGame`, see §3.

```cpp
// th07::ScoreSub — score/stats/CRC sub-struct reached via GameManager+0x8.
// Heap-allocated (0xC8 bytes) by AddedCallback. sizeof = 0xC8.
// Field offsets verified from AddedCallback writes, CutChain/OnUpdate reads,
// and the CalculateChecksum helper (FUN_0042d75a) which mutates +0xac/+0xbc.
struct ScoreSub
{
    u32 guiScore;            // +0x00  displayed/committed score (CutChain syncs from +0x04)
    u32 score;               // +0x04  running score (CutChain clamps to 999999999)
    u32 scoreDelta;          // +0x08  per-frame smoothing delta (=(target-displayed)>>5, [1..0x8d55e])
    u32 highScore;           // +0x0c  high score (init 100000; updated to guiScore on exceed)
    u8  highScoreStatusByte; // +0x10  status byte copied from +0x20 on new high
    u8  pad11[3];            // +0x11
    i32 counter14;           // +0x14  cleared to 0 at AddedCallback tail
    i32 counter18;           // +0x18  case-6/9 item-value scaling counter (=(this/0x28)*10+300)
    i32 counter1c;           // +0x1c  case-7 graze-derived point value (=this*100+1000)
    u8  srcStatusByte20;     // +0x20  source status byte (copied into +0x10; gates extend-record)
    u8  pad21[3];            // +0x21
    i32 grazeCount;          // +0x24  graze counter (incremented in case-1)
    i32 lifePieceProgress;   // +0x28  progress toward life-piece threshold
    i32 lifePieceTier;       // +0x2c  life-piece tier index
    i32 lifePieceThreshold;  // +0x30  life-piece threshold target (0x32/200/0xc8/300...)
    i32 seedIds7[7];         // +0x34  7 RNG-seeded ints [0x198f..0x1a02f]; checksum region 1
    i32 crcReseed38;         // +0x3c  seed/base copied into +0xac each frame
    u8  pad40[0x50 - 0x40];  // +0x40
    i32 counter50;           // +0x50  cleared to 0 at tail
    f32 randF3254[2];        // +0x54  2 random floats (~6500..67640); likely positions
    f32 playerCountF32;      // +0x5c  = (float)(byte)playerSub+0x1c (non-extra)
    f32 randF3260[2];        // +0x60  2 random floats
    u8  pad68[0x68 - 0x68];  // (empty — 0x68 directly follows)
    i32 bombCount;           // +0x68  bomb count accumulator (FUN_0042d612)
    i32 counter6c;           // +0x6c  cleared to 0
    f32 randF3370[3];        // +0x70  3 random floats (RGB/pos)
    f32 pointItemAutoBonus;  // +0x7c  point-item auto-collect bonus (128.0f on full-power)
    f32 randF3280[2];        // +0x80  2 random floats
    i32 mainSeedScoreBase;   // +0x88  = Rng%100000+0x198f; base for extend thresholds
    i32 seedIds8[8];         // +0x8c  8 RNG-seeded ints; range-checked by OnUpdate
    i32 stageTimeBase98;     // +0x98  stage-time base; added to CRC result -> DAT_0062f884
    u8  pad9c[0xac - 0x9c];  // +0x9c
    i32 crcAcc;              // +0xac  CRC/LCG accumulator; +=crcStepBc once per byte (checksum loop)
    i32 checksumResultB0;    // +0xb0  stores CalculateChecksum return (FUN_0042e3da)
    i32 seedIds5[5];         // +0xb4  5 RNG-seeded ints; checksum region 2 (0x14 bytes)
    i32 crcStepBc;           // +0xbc  LCG increment added to +0xac per byte
    // sizeof = 0xC8 (0xbc + 4)
};
ZUN_ASSERT_SIZE(ScoreSub, 0xC8)
```

**Notes for implementers:**
- The field NAMES above (beyond guiScore/score/crcAcc/crcStepBc which are
  byte-load-bearing) are descriptive labels for readability. When only a
  handful of fields are touched by an implemented function, the rest may
  remain as typed padding to reduce churn — but the LOAD-BEARING offsets
  (`+0x00`, `+0x04`, `+0x34`, `+0x3c`, `+0x88`, `+0xac`, `+0xb4`, `+0xbc`)
  MUST be named structs in the final header because CalculateChecksum,
  OnUpdate, and AddedCallback index them directly.
- `+0x1fbac` is **NOT** a ScoreSub field. It is `StageGame.gameState` (§3).
- All accesses are direct (no double-deref); the synthesis JSON's note about
  Ghidra showing `**(int**)` is an over-deref artifact.

---

## 2. FINAL GameManager (sizeof = **0x9700**)

```cpp
// th07::GameManager — in-game state manager singleton @ 0x00626270.
// sizeof = 0x9700 (Item[1100] is in ItemManager @ 0x575c70, NOT here).
#define GAME_MANAGER_SIZE 0x9700

DIFFABLE_EXTERN(GameManager, g_GameManager)

struct GameManager
{
    // --- header (+0x0..+0x14) ---
    void *scratchBuf;            // +0x0   malloc scratch (AddedCallback: alloc rand-sized, copy 0x38, free)
    PlayerSub *playerSub;        // +0x4   PlayerSub* (operator_new(0x38)); see §4
    ScoreSub *scoreSub;          // +0x8   ScoreSub* (operator_new(0xC8)); see §1. DAT_00626278.
    i32 flag0c;                  // +0xc   generic flag (written 0 at AddedCallback success-tail)
    i32 difficulty;              // +0x10  difficulty/stage selector index (0-5). DAT_00626280.
    u32 difficultyMask;          // +0x14  = 1 << (difficulty & 0x1f)

    // --- mid-struct control bytes (inside the big opaque region) ---
    // NOTE: offsets +0xd, +0x29, +0x24c.., +0x8454, +0x845a live inside the
    // opaque pad below; only the bytes ACTUALLY touched by implemented
    // functions are split out (statusBitfield@0x93d8, unk_93dc@0x93dc, etc.).
    u8  rankForceFlag;           // +0xd   (DAT_0062627d) rank force-trigger; cleared by DeletedCallback
                                 //        LIVES INSIDE pad0c — see note below on layout.
    // To keep the C++ legal and offsets exact, we use a single big pad from
    // +0x14+4=0x18 down to +0x93d8 and access +0xd / +0x29 via raw offset.
    // (Declared-fields-within-pad is not legal C++; use raw casts in code.)

    u8 pad18[0x93d8 - 0x18];     // +0x18..+0x93d7  opaque (entity arrays, extend records @0x8454/0x845a)

    // --- control-word block (+0x93d8..+0x9644) — all load-bearing ---
    u32 statusBitfield;          // +0x93d8  (DAT_0062f648) bit1=paused, bit2=chain-reg,
                                 //          bit3=practice, bit4=clr-on-success, bit0=extra
    u8  unk_93dc;                // +0x93dc  pause-request flag (OnDraw: if !=0 -> =2)
    u8  unk_93dd;                // +0x93dd  pause-latch companion (cleared at AddedCallback success)
    u8  pad93de[2];              // +0x93de
    u8  difficultySelector93de;  // +0x93de  stage/diff selector (0/1/2) for pause thresholds
                                 //          (alias of pad93de[0]; kept as documented comment)
    i32 pauseFrameCounter;       // +0x93e0  pause-watched-frame counter (thresholds 0x1fa4/0x1b6c/0x120c)
    u8 pad93e4[0x95e4 - 0x93e4]; // +0x93e4..+0x95e3  opaque
    u16 randSeedWord;            // +0x95e4  written from DAT_0049fe20 (AddedCallback)
    u8 pad95e6[2];               // +0x95e6
    i32 frameCounter;            // +0x95e8  per-frame counter; zeroed by RegisterChain/DeletedCallback
                                 //          (DAT_0062f858 — chainActive alias in early drafts)
    i32 playCount;               // +0x95ec  stage play counter (incremented at AddedCallback tail)
    u8 pad95f0[0x95f4 - 0x95f0]; // +0x95f0
    i32 anmColorSetup[8];        // +0x95f4  8 dwords (Anm/sprite color setup, FUN_0042d657)
    f32 randSumFloat;            // +0x9614  float from randSum+seed (FUN_004012b0)
    i32 extendThresholdA;        // +0x9618  primary extend threshold (base+0x88 + per-difficulty)
    i32 extendThresholdB;        // +0x961c  secondary extend threshold
    i32 initScoreMirror;         // +0x9620  mirror of scoreSub+0x88 (initial score)
    u8 pad9624[4];               // +0x9624
    i32 counter9628;             // +0x9628  cleared to 0 at AddedCallback init
    u32 rankCounter;             // +0x962c  rank-up counter (modulo 2/3/4/5/6 for skip gating)
    i32 stageColorA;             // +0x9634  stage color dword 1 (FUN_0042e38c)
    i32 stageColorB;             // +0x9638  stage color dword 2
    i32 stageColorC;             // +0x963c  stage color dword 3
    i32 counter9640;             // +0x9640  cleared to 0 at AddedCallback tail; life-decrement base

    // --- embedded chain nodes (+0x9644..+0x9684) ---
    ChainElem updateChainNode;   // +0x9644  calc-chain node (cb=OnUpdate, Added/Deleted set, arg=&this)
    ChainElem drawChainNode;     // +0x9664  draw-chain node (cb=OnDraw, Added/Deleted=NULL, arg=&this)

    // --- tail (+0x9684..+0x9700) ---
    u8 tail[GAME_MANAGER_SIZE - 0x9684]; // +0x9684  chain-control blocks; highest xref +0x96e8

    // --- methods ---
    static ZunResult RegisterChain();
    static void CutChain();
    static ChainCallbackResult __fastcall OnDraw(GameManager *gameManager);
    static ChainCallbackResult __fastcall OnUpdate(GameManager *gameManager);
    static ZunResult __fastcall AddedCallback(GameManager *gameManager);
    static ZunResult __fastcall DeletedCallback(GameManager *gameManager);
    // IsGameActive / CalculateChecksum belong to Supervisor (§3), NOT GameManager.
};
ZUN_ASSERT_SIZE(GameManager, GAME_MANAGER_SIZE)
```

### Layout caveat: `+0xd` (rankForceFlag / DAT_0062627d)

`+0xd` falls INSIDE the `pad0c`/`pad18` region of the C++ layout above
(between `+0xc` and `+0x10`). In the implemented functions it is accessed as
an absolute global `DAT_0062627d`. **Recommendation: keep it as a raw-offset
global access in C++ (`*(u8*)((u8*)&g_GameManager + 0xd) = 0;`) rather than
a named struct field**, because splitting `flag0c`(+0xc, i32) to expose a
byte at +0xd would force `flag0c` to become a struct of bytes and break the
clean `i32` access. The synthesis JSON confirms OnUpdate reads/writes
`+0xd` via `DAT_0062627d` absolute addressing anyway.

The `statusBitfield@0x93d8` split IS clean because `pad18` ends exactly at
`0x93d8` and everything below it is opaque.

---

## 3. NEW: Supervisor + StageGame (where IsGameActive actually lives)

`IsGameActive` (FUN_0042ad66) and `CalculateChecksum` (FUN_0042d7be) are
methods of the **Supervisor** singleton @ `0x0049fbf0`, reached by all
callers via `MOV ECX,0x49fbf0`. NOT GameManager. These need their own module
(later). Minimal structs to type-check the IsGameActive / CalculateChecksum
drafts:

```cpp
// th07::StageGame — the 0x20a30-byte active-gameplay sub-object.
// Heap-allocated (operator_new(0x20a30)) by FUN_0042d136, stored at
// g_Supervisor.stageGame (+0x8). Only the liveness word is load-bearing
// for IsGameActive; the rest (AnmVm arrays, item grid, banners) is opaque
// until the Supervisor module is reversed.
struct StageGame
{
    u8  pad0000[0x1fba0];         // +0x0000  sprites/anm/item grid (~129,696 bytes)
    i32 itemsRemaining;           // +0x1fba0 init 168; decremented per active item
    u8 *scriptPC;                 // +0x1fba8 ECL/script VM instruction pointer
    i32 gameState;                // +0x1fbac -1=inactive, -2=starting(exempt), >=0=active
    u8 pad1fbb0[0x20a30 - 0x1fbb0]; // +0x1fbb0 remainder (banners, final stats)
};
ZUN_ASSERT_SIZE(StageGame, 0x20a30)

// th07::Supervisor — singleton @ 0x0049fbf0. Owns the active StageGame.
// IsGameActive is a method of THIS struct. sizeof ~0x94 (provisional).
struct Supervisor
{
    i32 frameCounter;             // +0x0  inc'd each OnUpdate; compared to 300 for stage-6 BGM
    u32 statusBitfield;           // +0x4
    StageGame *stageGame;         // +0x8  <-- the 0x20a30 object
    u8  unk09;                    // +0x9
    u8 pad0a[0x94 - 0xa];         // +0xa  total sizeof ~0x94 (per 0x25-dword zero-fill in ctor)
};
ZUN_ASSERT_SIZE(Supervisor, 0x94)

DIFFABLE_EXTERN(Supervisor, g_Supervisor)   // @ 0x0049fbf0
```

**IMPORTANT for the IsGameActive / CalculateChecksum draft:** both functions
read `[this+0x8]` as the sub-struct pointer. For Supervisor this is
`StageGame*`; for GameManager it is `ScoreSub*`. The two structs share the
`+0x8` offset coincidentally. The drafts that typed the param as
`GameManager*` were forced to use raw-offset casts for `+0x1fbac` — that is
acceptable as an interim, but the cleaner fix is to declare these two
functions as `Supervisor` methods once `Supervisor.hpp` exists. The C++ logic
is identical either way.

Note: the deleted-callback draft references a *different* `g_Supervisor` @
`0x575950` (the Supervisor that owns the chain / midi / accumulators). That
is a SEPARATE singleton from `0x49fbf0`. The naming collides — when the
Supervisor module is reversed, pick distinct names (e.g. `g_Supervisor` for
0x575950 and `g_ActiveGame` or `g_GameController` for 0x49fbf0). For now the
GameManager drafts do not depend on resolving this naming.

---

## 4. FINAL PlayerSub (sizeof = **0x38**)

```cpp
// th07::PlayerSub — player/character sub-struct reached via GameManager+0x4.
// Heap-allocated (0x38 bytes) by AddedCallback. First 0x38 bytes memcpy'd
// from the global template table @ 0x575a68 (g_PlayerInitTemplate).
struct PlayerSub
{
    u8 header[0x1c];          // +0x00  first 0x1c bytes copied from g_PlayerInitTemplate
    u8 playerCountMode;       // +0x1c  =2 if stage>3; =8 if extra-bit set
    u8 pad1d[0x25 - 0x1d];    // +0x1d
    u8 bonusExtendGate;       // +0x25  cleared when difficulty>=4 OR extra; gates 4-8x extend grants
    u8 pad26[0x38 - 0x26];    // +0x26
};
ZUN_ASSERT_SIZE(PlayerSub, 0x38)

// The 0x38-byte static template @ 0x575a68 (memcpy source for PlayerSub,
// and the 4th checksum region in CalculateChecksum).
DIFFABLE_EXTERN_ARRAY(u8, g_PlayerInitTemplate, 0x38)   // @ 0x575a68
```

GameManager+0x4 type changes from `void *` to `PlayerSub *playerSub;`.

---

## 5. Per-function implementation readiness

| Function | Addr | Size | Ready? | Key insight / blocker |
|---|---|---|---|---|
| `RegisterChain` | 0x42f3c5 | 0x97 | **DONE** (in GameManager.cpp) | Already implemented; uses updateChainNode/drawChainNode @ +0x9644/+0x9664. |
| `CutChain` | 0x42f45d | 0x4d | **DONE** | Touches only ScoreSub +0x0/+0x4; clamps to 999999999. |
| `OnDraw` | 0x42e1d4 | 0x23 | **DONE** | Trivial: if `unk_93dc != 0` -> `= 2`. |
| `AddedCallback` | 0x42e83e | ~0x4a0 | **READY** (draft from decompile) | Allocates playerSub(0x38)+scoreSub(0xC8); memcpy's g_PlayerInitTemplate; sets extend thresholds per difficulty; cascade of init calls. Needs: PlayerSub, ScoreSub, g_PlayerInitTemplate, statusBitfield@0x93d8, extendThresholdA/B@0x9618/0x961c, playCount@0x95ec. All confirmed in this struct. The draft file was never written but the synthesis JSON field map + the decompile above fully specify it. |
| `DeletedCallback` | 0x42f2e4 | 0xe1 | **READY** (draft_deletedcallback.md) | Pure cascade of singleton release calls + RMW on statusBitfield(& ~4) + zero `+0xd` and `+0x95e8`. GameManager* used ONLY for the +0x93d8 bit clear. Needs extern decls for g_Supervisor(0x575950)/g_MidiOutput/g_EffectManager etc. as opaque singletons — call signatures suffice for codegen. |
| `OnUpdate` | 0x42d8d5 | 0x4fe | **READY** (decompile above) | Pause-guard + pause-frame accounting + CRC re-seed + score-smoothing + rank-tier blocks. Reads/writes statusBitfield, unk_93dc/93dd, pauseFrameCounter@0x93e0, difficultySelector@0x93de, frameCounter@0x95e8, rankCounter@0x962c, scoreSub fields +0x0/+0x4/+0x8/+0xc/+0x10/+0x20/+0x34/+0x54/+0x60/+0x70/+0x80/+0x8c/+0xac/+0xb4. All offsets confirmed. The 6398-byte claim is WRONG (synthesis contradiction #6) — actual is 0x4fe=1278 bytes. |
| `IsGameActive` | 0x42ad66 | 0x44 | **READY** (draft_isactive_checksum.md) | Belongs to Supervisor(0x49fbf0), not GameManager. Reads `this->stageGame`(=+0x8) NULL-check, then `stageGame->gameState`(=+0x1fbac) compare. Self-contained, no callees. ~95% objdiff ceiling (no globals). |
| `CalculateChecksum` | 0x42d7be | 0x7c | **READY** (draft_isactive_checksum.md) | Supervisor method. 4 calls to ChecksumRegion helper (FUN_0042d75a): regions = scoreSub+0x34(len 0x78), scoreSub+0xb4(len 0x14), playerSub(len 0x38), g_PlayerInitTemplate(len 0x38). All inputs from GLOBAL g_GameManager (@0x626270), not `this`. First-call length computed as `(scoreSub+0xac)-(scoreSub+0x34)` — reproduce literally. ~70-80% objdiff ceiling (4 global reads). |
| `ChecksumRegion` (helper) | 0x42d75a | 0x64 | **READY** | `static` internal-linkage. Byte-sum loop; each iteration bumps `g_GameManager.scoreSub->crcAcc += crcStepBc`. Must NOT be inlined (4 CALL relocations). |

### Globals to declare (cross-module, for codegen)

```cpp
// These live in OTHER modules and get proper types when those are reversed.
// For now, opaque externs drive correct codegen (MOV ECX,imm32; CALL).
namespace th07 {
extern Supervisor  g_Supervisor;              // @ 0x00575950  (chain/midi/accumulator owner)
extern u8          g_Supervisor_state;        // @ 0x00575a87  (== g_Supervisor.state alias)
extern u32         g_Supervisor_redrawFlag;   // @ 0x00575ae4
extern void       *g_SupervisorSound;         // @ 0x004ba0d8  (sound-queue side; ECX for ProcessQueue)
extern void       *g_MidiOutput;              // @ 0x00575acc  (MidiOutput*)
extern void       *g_COleDispParams;          // @ 0x01347938  (enemy/disp singleton)
extern void       *g_EffectManager;           // @ 0x0134ce18  (EffectManager; Reset target)
} // namespace th07
```

(Use `void *` placeholders; replace with real types as each module lands.)

---

## 6. Summary of struct changes vs current GameManager.hpp

| Change | Reason |
|---|---|
| `+0x4` type: `void*` → `PlayerSub*` | Enables `g_GameManager.playerSub` syntax; CalculateChecksum + AddedCallback index it. |
| Add `PlayerSub` struct (0x38) with named `playerCountMode@0x1c`, `bonusExtendGate@0x25` | AddedCallback writes these. |
| Expand `ScoreSub` from 2 fields → full 0xC8 layout (load-bearing: +0x0/+0x4/+0x34/+0x3c/+0x88/+0xac/+0xb4/+0xbc named) | CutChain/OnUpdate/CalculateChecksum/AddedCallback all index these. |
| Split `pad0c[0x93dc-0x0c]` → `pad18[0x93d8-0x18]` + named `statusBitfield@0x93d8` + `unk_93dc@0x93dc` + `unk_93dd@0x93dd` + `difficultySelector@0x93de` + `pauseFrameCounter@0x93e0` | OnUpdate/AddedCallback/DeletedCallback/OnDraw all RMW these. |
| Add named fields `randSeedWord@0x95e4`, `frameCounter@0x95e8`(was chainActive), `playCount@0x95ec`, `anmColorSetup[8]@0x95f4`, `randSumFloat@0x9614`, `extendThresholdA/B@0x9618/0x961c`, `initScoreMirror@0x9620`, `counter9628@0x9628`, `rankCounter@0x962c`, `stageColorA/B/C@0x9634/8/c`, `counter9640@0x9640` | AddedCallback/OnUpdate write these directly. |
| Add `StageGame` (0x20a30) + `Supervisor` (0x94) forward structs + `g_Supervisor` @ 0x49fbf0 | IsGameActive/CalculateChecksum are Supervisor methods, not GameManager. |
| Add `g_PlayerInitTemplate[0x38]` @ 0x575a68 | AddedCallback memcpy source + CalculateChecksum region 4. |
| `chainActive@0x95e8` renamed `frameCounter` | Synthesis confirms it's a per-frame counter zeroed by RegisterChain/DeletedCallback, incremented in OnUpdate. `chainActive` was a misnomer. |
| `IsGameActive`/`CalculateChecksum` NOT added as GameManager methods | They belong to Supervisor (0x49fbf0). Implement them in a future Supervisor.cpp. |

---

## 7. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Two singletons both called `g_Supervisor` (0x575950 chain-owner vs 0x49fbf0 active-game) | Naming | Rename on Supervisor module reversal (e.g. `g_Supervisor` / `g_GameController`). GameManager drafts unaffected. |
| OnUpdate `+0x93de` is read as a byte but pad93de declares it inside a 2-byte pad | Low | The byte IS accessible via raw offset; if OnUpdate implemented, split pad to expose `u8 difficultySelector93de` explicitly. |
| AddedCallback draft was never written by its agent — reconstructed from synthesis JSON + decompile | Medium | Cross-check every field write against the 0x42e83e decompile above before committing the .cpp. The synthesis field map matches the decompile exactly. |
| objdiff plateau on global-heavy functions (OnUpdate/AddedCallback/CalculateChecksum) | Certain (~70-80%) | Per project convention: accept the plateau, get logic + non-global instructions byte-exact. Don't chase 100%. |
| `difficultyMask@0x14` declared but `flag0c@0xc` is i32 while `rankForceFlag@0xd` lives inside it | Layout | `+0xd` accessed as absolute global `DAT_0062627d` in code (raw cast), not as a struct field. Keep `flag0c` as i32. |
