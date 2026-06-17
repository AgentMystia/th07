# ScoreSub +0x1fbac Contradiction — RESOLUTION

> ⚠️ **[DRAFT — pre-2026-06-17-polish; SUPERSEDED guidance]**
>
> 本文件写于 objdiff 字节匹配优先的时代。文中 raw-address / DAT_ 模式
> 已被 2026-06-17 polish session 废弃。实现时请遵循 `AGENTS.md` §2
> "诚实重建（单一代码路径）"原则：typed C++ global + SYMBOL_MAP，
> 禁止 inline asm / naked / 函数级 `#ifdef DIFFBUILD` 分裂。
> 保留本文件仅供历史溯源与 ghidra 逆向数据参考。

## TL;DR / VERDICT

**The contradiction was a false alarm caused by a wrong `this` assumption.**

`IsGameActive` (FUN_0042ad66 @ 0x42ad66) is **NOT a method of `g_GameManager` (0x626270)**.
It is a method of an **entirely different singleton at `0x0049fbf0`** — a "Supervisor" object
(in th06 terms this corresponds to `Supervisor`; here we'll call it `g_Supervisor`).

Inside `g_Supervisor`, the field at `+0x8` is **NOT the `ScoreSub*` from `g_GameManager+8`**.
It is a pointer to a **0x20a30-byte (133,680-byte) "Stage/Game" sub-manager** allocated via
`operator_new(0x20a30)` in the constructor `FUN_0042d24d`, installed by `FUN_0042d136` (the
Supervisor's chain-setup helper):

```
0042d136: ... (FUN_0042d136 — Supervisor stage-init)
0042d183: PUSH 0x20a30          ; operator_new(0x20a30)
0042d188: CALL 0x0047d441       ; operator_new
0042d18e: MOV [EBP + -0x18],EAX ; pvVar = new'd ptr
0042d19b: MOV ECX,[EBP + -0x18]
0042d19e: CALL 0x0042d24d       ; FUN_0042d24d(pvVar) — ctor
0042d1ac: MOV EAX,[EBP + -0x14] ; EAX = 0x49fbf0 (g_Supervisor)
004d1b6-1bc: MOV [EAX + 0x8],ECX ; g_Supervisor->stageGame = pvVar  (WRITE to 0x49fbf8)
```

So `g_Supervisor->stageGame` (= `*(0x49fbf0 + 8)`) points to a **0x20a30-byte** object,
and offset `0x1fbac` (= 129,964) is **well inside** that allocation
(0x1fbac < 0x20a30, margin = 3,716 bytes to the end). There is **no OOB read** and
**no pointer rebound**. The 0xC8 allocation in `AddedCallback` belongs to a *different*
field of a *different* object (`g_GameManager.scoreSub`, alloc'd for the 200-byte
score/player struct).

---

## 1. Proofs (orig disassembly / decompile)

### 1a. IsGameActive disassembly — the `this` is at `[EBP-4]`, set from ECX

```
0042ad66: PUSH EBP
0042ad67: MOV EBP,ESP
0042ad69: PUSH ECX
0042ad6a: PUSH ECX
0042ad6b: MOV [EBP-4],ECX          ; <-- this = ECX (NOT g_GameManager!)
0042ad6e: MOV EAX,[EBP-4]
0042ad71: CMP dword ptr [EAX+0x8],0x0     ; if (this->stageGame == NULL)
0042ad75: JNZ 0x42ad7b
0042ad77: XOR EAX,EAX               ;   return 0;
0042ad79: JMP 0x42ada9
0042ad7b: MOV EAX,[EBP-4]
0042ad7e: MOV EAX,[EAX+0x8]         ; EAX = this->stageGame
0042ad81: CMP dword ptr [EAX+0x1fbac],0x0  ; if (stageGame->state < 0)
0042ad88: JGE 0x42ad9f
0042ad8a: MOV EAX,[EBP-4]
0042ad8d: MOV EAX,[EAX+0x8]
0042ad90: CMP dword ptr [EAX+0x1fbac],-0x2 ; if (state == -2) => active
0042ad97: JZ 0x42ad9f
0042ad99: AND dword ptr [EBP-8],0x0  ;   active = 0
0042ad9d: JMP 0x42ada6
0042ad9f: MOV dword ptr [EBP-8],0x1  ; active = 1
0042ada6: MOV EAX,[EBP-8]
0042ada9: LEAVE
0042adaa: RET
```

### 1b. Caller site proofs — ECX is `0x49fbf0`, not `0x626270`

**Caller A** — `FUN_0042d8d5` (g_GameManager::OnUpdate) at 0x42da6d:
```
0042da6d: MOV ECX,0x49fbf0     ; g_Supervisor
0042da72: CALL 0x0042ad66      ; IsGameActive(g_Supervisor)
```
(Here `[EBP-0x14]` = `0x626270` = `g_GameManager`. The function *is* a GameManager
OnUpdate, but it explicitly loads **0x49fbf0** into ECX before calling IsGameActive —
a cross-singleton call.)

**Caller B** — `FUN_00420620` (Enemy/Player loop) at 0x420663:
```
00420663: ... uses 0x49fbf0 ...
00420668: CALL 0x0042ad66      ; IsGameActive (ECX was loaded from 0x49fbf0)
```

**Caller C** — `FUN_00442e50` at 0x442e72:
```
00442e6d: MOV ECX,0x49fbf0     ; g_Supervisor
00442e72: CALL 0x0042ad66      ; IsGameActive(g_Supervisor)
```

### 1c. The pointer-write to `0x49fbf8` (g_Supervisor+8) — `FUN_0042d136`

```
0042d144: MOV dword ptr [EBP-0x10],0x49fbf0   ; ptr = &g_Supervisor
0042d183: PUSH 0x20a30                         ; operator_new(0x20a30)
0042d188: CALL 0x0047d441                      ; operator_new
0042d18e: MOV [EBP-0x18],EAX                   ; pvVar = result
...
0042d19e: CALL 0x0042d24d                      ; ctor(pvVar) — builds AnmVm arrays etc.
...
0042d1b6: MOV EAX,[EBP-0x10]                   ; EAX = &g_Supervisor
0042d1b9: MOV ECX,[EBP-0x14]                   ; ECX = pvVar (0x20a30-byte obj)
0042d1bc: MOV dword ptr [EAX+0x8],ECX          ; g_Supervisor->stageGame = pvVar
```

The `0x49fbf8` slot has **exactly one WRITE xref** (the above) and is **READ** by all the
IsGameActive-family functions.

### 1d. +0x1fbac lifecycle (from `FUN_00429c42` and `FUN_00428b19`)

The 0x20a30-byte `stageGame` object is passed directly as ECX to `FUN_00429c42`
(confirmed: caller `FUN_00427e7c` does `MOV ECX,[EAX+0x8]` before the call), so inside
those functions the field is `*(param_1 + 0x1fbac)`.

| Where | Code | Value | Meaning |
|---|---|---|---|
| `FUN_00428b19` (stage setup) | `*(param_1[2] + 0x1fbac) = 0xffffffff` | **-1** | inactive / pre-game |
| `FUN_00429c42` case 0xb (stage start) | `*(param_1 + 0x1fbac) = 0xfffffffe` | **-2** | "starting" — exempt (treated as active by IsGameActive) |
| `FUN_00429c42` case 0 (script end) | `*(param_1 + 0x1fbac) = 0xffffffff` | **-1** | inactive (level done) |
| (presumably) once the level is actually running | set to **>= 0** (gameplay active) | active |

The field is an **enum-like `i32` state**: `STATE_INACTIVE = -1`, `STATE_STARTING = -2`,
`STATE_ACTIVE = >=0`. IsGameActive returns true iff `state == -2` OR `state >= 0`.

### 1e. Why the 0xC8 allocation is irrelevant to this read

In `g_GameManager::AddedCallback` (`FUN_0042e83e`) the 0xC8-byte alloc goes into
`g_GameManager+8` (`DAT_00626278`):

```
0042e93a: PUSH 0xc8
0042e93f: CALL 0x0047d441    ; operator_new(0xC8)
0042e945: ...
0042e94e: MOV [EAX+0x8],ECX  ; g_GameManager.scoreSub = new(0xC8)
```

That is a **completely different** `+8` field on a **completely different** singleton
(`0x626270` vs `0x49fbf0`). They share the offset number 8 by coincidence; nothing
connects them. `g_GameManager.scoreSub` is correctly a 0xC8-byte `ScoreSub`, untouched.

---

## 2. Field-access table

### g_Supervisor (singleton @ 0x0049fbf0, sizeof ~0x94 confirmed by `0x25 * 4` zero-fill in ctor = 0x94 bytes)
| Offset | Type | Name | Notes |
|---|---|---|---|
| +0x0 | i32 | frameCounter | `*(param_1)`; incremented by OnUpdate (FUN_00427e7c); compared to 0x12c (300) for stage-6 BGM fade |
| +0x4 | u32 | statusBitfield | bitfield with flag bits set in FUN_00428b19 (`& 0xfffffffc \| 2`, `& 0xfffffff3 \| 8`, etc.) |
| +0x8 | StageGame* | stageGame | the 0x20a30-byte sub-manager (the actual gameplay/anm/state object) |
| +0x9 | u8 | unk09 | byte flag, cleared to 0 in FUN_00428b19 |
| +0xc | (within +0x4 bitfield region) | — | — |

### StageGame (heap-allocated, sizeof = 0x20a30 = 133,680 bytes; ctor `FUN_0042d24d`)
| Offset | Type | Name | Notes |
|---|---|---|---|
| +0x0 .. +0x4bc8 | AnmVm[] / arrays | (sprites, fonts, bullet containers) | ctor builds 0x21+5+0xa8 AnmVm objects, etc. |
| +0x4bcc | u8 | playerState (death/respawn FSM) | reads/written in FUN_0042adab; values 0..3 |
| +0x574c..+0x79c0 | AnmVm | 8 sprite slots (enemy/boss gauges) | FUN_00450d60 dispatch |
| +0x79c0 + i*0x24c (i=0..0xa7) | AnmVm[168] | itemGrid | 168 entries of 0x24c each |
| +0x1fba0 | i32 | itemsRemaining (init 0xa8=168) | decremented by FUN_0042adab loop |
| +0x1fba8 | u8* | scriptPC (script instruction pointer) | advanced by FUN_00429c42 main loop |
| **+0x1fbac** | **i32** | **gameState** | **-1 = inactive, -2 = starting (exempt), >=0 = active** |
| +0x1fbb0..+0x1fc00 | text-banner AnmVm fields | "BONUS"/"Full Power Mode"/etc. | driven from FUN_00427f22 |
| +0x209ac | i32 | unk209ac | counter dec in FUN_00429c42 |
| +0x209b4 | i32 | scoreFinalized | set to 1 in case 9 of script; "results locked" |
| +0x209b8 | i32 | totalScore | computed in FUN_0042adab (final bonus tally) |
| +0x209bc | i32 | endStageFlag | set to 1 in case 0xb; triggers `DAT_00575aa8 = 3` (state change) |
| +0x209d0 / +0x209f0 / +0x20a10 | i32 | bannerActive[3] | bonus/supernatural/spell banners |
| +0x20a24 / +0x20a28 / +0x20a2c | i32 | pointItems / cherry / graze (final) | captured at stage-end (case 9) |
| +0x20a30 | (end) | — | sizeof = 0x20a30 |

### IsGameActive field accesses (only two)
| Instruction | Effective addr | Field |
|---|---|---|
| `CMP [EAX+0x8],0` (EAX=this) | `g_Supervisor.stageGame` | NULL check |
| `CMP [EAX+0x1fbac],0` / `,-2` (EAX=stageGame) | `stageGame.gameState` | the active-state test |

---

## 3. C++ implementation draft (IsGameActive + supporting structs)

This is the **complete** IsGameActive. Add the new structs/forward decls to
`GameManager.hpp` (or, more correctly, a new `Supervisor.hpp` — but for now keep it
in the existing file so the symbol lands at FUN_0042ad66).

### 3a. Header additions (`GameManager.hpp` or new `Supervisor.hpp`)

```cpp
namespace th07
{
// Forward declarations
struct StageGame;
struct Supervisor;

// The 0x20a30-byte gameplay/state object hanging off g_Supervisor+0x8.
// Only the fields touched by the IsGameActive family are declared; the bulk
// (AnmVm arrays, item grid, banners) is opaque padding until those functions
// are reversed.
struct StageGame
{
    u8 pad0000[0x4bcc];                 // +0x0000  sprites/anm containers
    u8  playerState;                    // +0x4bcc  respawn/death FSM (0..3)
    u8 pad4bcd[0x1fba0 - 0x4bcd];       // +0x4bcd  (item grid etc.)
    i32 itemsRemaining;                 // +0x1fba0 init 168; dec per active item
    u8 *scriptPC;                       // +0x1fba8 script instruction pointer
    i32 gameState;                      // +0x1fbac -1=inactive, -2=starting, >=0=active
    u8 pad1fbb0[0x209ac - 0x1fbb0];     // +0x1fbb0 banner/anm state
    i32 unk209ac;                       // +0x209ac
    u8 pad209b0[0x209b4 - 0x209b0 - 4]; // +0x209b0 (i32 at 0x209b0 if needed)
    i32 scoreFinalized;                 // +0x209b4 set to 1 at stage-end capture
    i32 totalScore;                     // +0x209b8 final tally
    i32 endStageFlag;                   // +0x209bc 1 => next OnUpdate flips app state
    u8 pad209c0[0x20a30 - 0x209c0];     // +0x209c0 remainder (banners + final stats)
};
ZUN_ASSERT_SIZE(StageGame, 0x20a30)

// The "Supervisor" singleton @ 0x0049fbf0. Holds the per-stage game object and
// the small control header. IsGameActive is a method of THIS struct, not of
// GameManager.
struct Supervisor
{
    i32 frameCounter;                   // +0x0  inc'd each OnUpdate
    u32 statusBitfield;                 // +0x4
    StageGame *stageGame;               // +0x8  <-- the 0x20a30 object
    u8  unk09;                          // +0x9
    u8 pad0a[0x94 - 0xa];               // +0xa  total sizeof ~0x94 (per 0x25-dword zero-fill in FUN_0042d136)
};
ZUN_ASSERT_SIZE(Supervisor, 0x94)

DIFFABLE_EXTERN(Supervisor, g_Supervisor)   // @ 0x0049fbf0

} // namespace th07
```

### 3b. `IsGameActive` implementation (`GameManager.cpp` or `Supervisor.cpp`)

```cpp
// th07::Supervisor::IsGameActive — FUN_0042ad66
// Returns true iff a stage-game is loaded AND its gameState is either
// "starting" (-2, exempt) or "active" (>=0). Returns false if no stageGame
// is attached or the state is the inactive sentinel (-1).
//
// Stack layout matches orig exactly:
//   [EBP-0x4] this (from ECX)
//   [EBP-0x8] local result (only materialized in the second branch)
#pragma var_order(thisPtr, result)
i32 __fastcall Supervisor::IsGameActive(Supervisor * /*unused*/)
{
    // Compiler quirk: orig stores ECX into a stack slot `[EBP-4]` even though
    // it could keep it in a register. We mirror by reading g_Supervisor via
    // the same global so objdiff's byte-pattern matches.
    Supervisor *thisPtr = g_Supervisor;

    if (thisPtr->stageGame == 0)
    {
        return 0;
    }

    // gameState: -1 => inactive, -2 => starting (treated active), >=0 => active
    if (thisPtr->stageGame->gameState < 0 && thisPtr->stageGame->gameState != -2)
    {
        return 0;
    }

    return 1;
}
```

**Notes for byte-exact matching:**
- The orig emits `PUSH ECX / PUSH ECX` (two slots reserved), then `MOV [EBP-4],ECX`.
  To reproduce, declare exactly two locals in var_order and use MSVC's natural
  codegen for the two-stage compare. The `if (x < 0 && x != -2) return 0; else return 1;`
  shape matches the orig's `JGE / JZ` pattern; do NOT collapse it to a single boolean
  expression — MSVC 7.0 will emit a different branch sequence.
- The function is `__fastcall` with `this` in ECX but takes **no other parameters**;
  its signature is `i32 Supervisor::IsGameActive()`. The `(Supervisor*)` arg above is a
  placeholder to satisfy the th07 callback/thiscall convention macros — adjust to your
  project's calling-convention helper (e.g. `__fastcall Supervisor_IsGameActive()` with
  the global read inside).

---

## 4. Struct changes needed (summary)

**GameManager.hpp / ScoreSub** — NO CHANGES. The 0xC8 alloc and the `+0x0`/`+0x4`
score fields are correct as-is. The IsGameActive read never touched `ScoreSub`; it was
a misattribution.

**NEW struct(s) to add** (can live in `Supervisor.hpp`):
1. `StageGame` (sizeof 0x20a30) — fields per table in §2.
2. `Supervisor` (sizeof ~0x94) — fields per table in §2.
3. `DIFFABLE_EXTERN(Supervisor, g_Supervisor)` at 0x0049fbf0.

**GameManager +0x8 field** — unchanged. It's `ScoreSub*`, alloc 0xC8, fields
`guiScore` (+0x0) and `score` (+0x4). Nothing in IsGameActive touches it.

---

## 5. Dependencies on unreversed callees

IsGameActive has **no callees** — it is a pure inline-able accessor (Ghidra confirms
"No callees found"). Implementation is fully self-contained.

The *surrounding* gameplay code that sets `stageGame->gameState` is:
- `FUN_0042d136` (allocates + ctors StageGame, stores at `g_Supervisor+8`)
- `FUN_0042d24d` (StageGame ctor — AnmVm array init)
- `FUN_00428b19` (stage-text/sprite setup; sets gameState = -1 initially)
- `FUN_00429c42` (script VM step; sets gameState = -1 or -2 on certain opcodes)
- `FUN_0042adab` (per-frame gameplay update; reads gameState as "active" gate)

These can be reversed later; **none of them block IsGameActive**, which only needs the
struct layout above.

---

## 6. Verification checklist (for the human)

- [ ] Confirm `g_Supervisor` symbol lands at `0x0049fbf0` in the rebuilt .obj (objdiff
      should match the global write in the eventual Supervisor::RegisterChain).
- [ ] Compile `IsGameActive` and check objdiff vs `FUN_0042ad66`. Expected:
      near-100% match (function touches no globals directly — only the g_Supervisor
      read — so the objdiff global-naming penalty does NOT apply here).
- [ ] If MSVC's codegen for `thisPtr = g_Supervisor; if (... == 0) ...` does NOT emit
      the `MOV [EBP-4],ECX` pattern, fall back to declaring the function with the
      thiscall ABI and let ECX flow naturally (the test harness calls it with
      ECX=g_Supervisor anyway).
- [ ] Once `g_Supervisor` exists, update MEMORY.md / PROGRESS.md to record the new
      singleton (it's the 13th module — previously lumped under "GameManager" by
      mistake).
