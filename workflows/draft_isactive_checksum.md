# GameManager::IsGameActive @ 0x42ad66 + CalculateChecksum @ 0x42d7be + helper FUN_0042d75a

> ⚠️ **[DRAFT — pre-2026-06-17-polish; SUPERSEDED guidance]**
>
> 本文件写于 objdiff 字节匹配优先的时代。文中 raw-address / DAT_ 模式
> 已被 2026-06-17 polish session 废弃。实现时请遵循 `AGENTS.md` §2
> "诚实重建（单一代码路径）"原则：typed C++ global + SYMBOL_MAP，
> 禁止 inline asm / naked / 函数级 `#ifdef DIFFBUILD` 分裂。
> 保留本文件仅供历史溯源与 ghidra 逆向数据参考。

Reverse-engineering + complete C++ draft for the two small anti-tamper / liveness
functions plus the byte-sum helper they share. Output is byte-accurate to the
Ghidra disassembly; the C++ below is written to reproduce MSVC 7.0 codegen under
the project's flag set.

---

## 0. CRITICAL FINDING — `this` is NOT g_GameManager (0x626270)

Every caller of both `IsGameActive` (0x42ad66) and `CalculateChecksum` (0x42d7be)
loads `ECX = 0x0049fbf0` before the CALL, **not** `0x00626270` (g_GameManager).
Verified call sites:

| Caller addr | Caller fn | MOV ECX,imm before CALL |
|---|---|---|
| 0x00420668 | FUN_00420620 | `MOV ECX,0x49fbf0` (0x00420663) |
| 0x00421e81 | FUN_00420620 | (0x00421e7c) |
| 0x0044197a | FUN_00441960 | (0x00441975) |
| 0x0042da72 | FUN_0042d8d5 (OnUpdate) | (0x0042da6d) |
| 0x0043c74c | FUN_0043c700 | (0x0043c747) |
| 0x0043c98c | FUN_0043c940 | (0x0043c987) |
| 0x0044073c | FUN_0043ee50 | (0x00440737) |
| 0x00440b48 | FUN_004409f0 | (0x00440b43) |
| 0x00442e72 | FUN_00442e50 | (0x00442e6d) |

`0x0049fbf0` is a **separate, larger game-state object** (the in-battle / active
game manager). FUN_0042d136 (called from AddedCallback) allocates a `0x20a30`-byte
sub-block and stores its pointer at `[0x49fbf0 + 0x8]`. That `0x20a30` allocation
is what `IsGameActive` reaches into at offset `+0x1fbac` — it comfortably exceeds
0xC8. The g_GameManager (@0x626270) `scoreSub` only allocates `0xC8`, so the
`+0x1fbac` read would be an out-of-bounds wild pointer on the menu GameManager.

**Implication for the draft:** the functions are most accurately typed as taking
a pointer to whichever struct sits at `0x49fbf0`. Because that struct is not yet
fully reversed (the th07 codebase consistently treats `0x49fbf0` and `0x626270`
as the same logical GameManager shape: `+0x4` playerSub, `+0x8` scoreSub/stateSub),
the draft reuses `GameManager*` as the parameter type with a `// ACTUAL RUNTIME
this = 0x49fbf0 (active game)` comment. The struct layout of GameManager already
declares `scoreSub` at `+0x8`, which is what the disassembly indexes. The
`+0x1fbac` field is declared on the sub-struct as a trailing opaque field.

**Note for the helper (FUN_0042d75a):** it unconditionally reads the GLOBAL
`DAT_00626278` (= `*(g_GameManager @ 0x626270 + 0x8)`, the g_GameManager's own
scoreSub pointer), NOT the `this->scoreSub` of whoever called CalculateChecksum.
This is why the helper's decompile "ignores" its arguments — the args (`ECX`=ptr,
`EDX`=len) ARE used (ECX is the byte-sum base, EDX is the length), but the CRC
accumulator it mutates lives in the g_GameManager scoreSub, hardwired via
`DAT_00626278`. Reproduce exactly.

---

## 1. IsGameActive — orig disassembly (0x42ad66 .. 0x42adaa, 0x44 bytes)

```asm
0042ad66: PUSH EBP
0042ad67: MOV EBP,ESP
0042ad69: PUSH ECX            ; reserve [EBP-4] for this
0042ad6a: PUSH ECX            ; reserve [EBP-8] for result
0042ad6b: MOV dword ptr [EBP + -0x4],ECX        ; [EBP-4] = this (GameManager*)
0042ad6e: MOV EAX,dword ptr [EBP + -0x4]
0042ad71: CMP dword ptr [EAX + 0x8],0x0         ; this->scoreSub == 0 ?
0042ad75: JNZ 0x0042ad7b
0042ad77: XOR EAX,EAX                          ; return 0
0042ad79: JMP 0x0042ada9
0042ad7b: MOV EAX,dword ptr [EBP + -0x4]
0042ad7e: MOV EAX,dword ptr [EAX + 0x8]         ; EAX = this->scoreSub
0042ad81: CMP dword ptr [EAX + 0x1fbac],0x0     ; stateSub->gameState >= 0 ?
0042ad88: JGE 0x0042ad9f                        ; yes -> result=1
0042ad8a: MOV EAX,dword ptr [EBP + -0x4]
0042ad8d: MOV EAX,dword ptr [EAX + 0x8]
0042ad90: CMP dword ptr [EAX + 0x1fbac],-0x2    ; stateSub->gameState == -2 ?
0042ad97: JZ 0x0042ad9f                         ; yes (the magic "still ok" sentinel) -> result=1
0042ad99: AND dword ptr [EBP + -0x8],0x0        ; result = 0
0042ad9d: JMP 0x0042ada6
0042ad9f: MOV dword ptr [EBP + -0x8],0x1        ; result = 1
0042ada6: MOV EAX,dword ptr [EBP + -0x8]
0042ada9: LEAVE
0042adaa: RET
```

Decompile (Ghidra, after fixing __fastcall ECX=this):
```c
int __fastcall GameManager::IsGameActive(GameManager *this)
{
    if (this->scoreSub == 0) return 0;
    if (this->scoreSub->gameState1fbac < 0 && this->scoreSub->gameState1fbac != -2)
        return 0;
    return 1;
}
```

### Field access table

| Effective addr | Field | Type | Semantics |
|---|---|---|---|
| `[this+0x8]` | GameManager.scoreSub | `ScoreSub*` | Pointer to score/state sub-struct (heap). NULL ⇒ not active. |
| `[scoreSub+0x1fbac]` | ScoreSub.gameState1fbac | `i32` | Liveness / state enum. `<0 && !=-2` ⇒ inactive. `-2` is the explicit "ok" sentinel. Only present on the 0x49fbf0 active-game sub-struct (0x20a30 alloc); does NOT exist on the menu g_GameManager's 0xC8 scoreSub. |

---

## 2. CalculateChecksum — orig disassembly (0x42d7be .. 0x42d839, 0x7c bytes)

```asm
0042d7be: PUSH EBP
0042d7bf: MOV EBP,ESP
0042d7c1: PUSH ECX            ; [EBP-4] = sum accumulator
0042d7c2: PUSH ECX            ; [EBP-8] = this
0042d7c3: MOV dword ptr [EBP + -0x8],ECX        ; [EBP-8] = this
0042d7c6: MOV EAX,dword ptr [EBP + -0x8]
0042d7c9: MOV EDX,dword ptr [EAX + 0x8]         ; EDX = this->scoreSub
0042d7cc: ADD EDX,0xac                          ; EDX = scoreSub+0xac
0042d7d2: MOV EAX,dword ptr [EBP + -0x8]
0042d7d5: MOV EAX,dword ptr [EAX + 0x8]         ; EAX = this->scoreSub
0042d7d8: ADD EAX,0x34                          ; EAX = scoreSub+0x34
0042d7db: SUB EDX,EAX                           ; EDX = 0xac-0x34 = 0x78  (length!)
0042d7dd: MOV ECX,dword ptr [0x00626278]        ; ECX = g_GameManager.scoreSub (GLOBAL)
0042d7e3: ADD ECX,0x34                          ; ECX = g_scoreSub+0x34    (ptr)
0042d7e6: CALL 0x0042d75a                       ; helper(ECX=ptr, EDX=0x78)
0042d7eb: MOV dword ptr [EBP + -0x4],EAX        ; sum = result
0042d7ee: MOV ECX,dword ptr [0x00626278]        ; ECX = g_GameManager.scoreSub
0042d7f4: ADD ECX,0xb4                          ; ECX = g_scoreSub+0xb4
0042d7fa: PUSH 0x14
0042d7fc: POP EDX                               ; EDX = 0x14              (length)
0042d7fd: CALL 0x0042d75a                       ; helper(ECX=ptr, EDX=0x14)
0042d802: MOV ECX,dword ptr [EBP + -0x4]
0042d805: ADD ECX,EAX
0042d807: MOV dword ptr [EBP + -0x4],ECX        ; sum += result
0042d80a: PUSH 0x38
0042d80c: POP EDX                               ; EDX = 0x38              (length)
0042d80d: MOV ECX,dword ptr [0x00626274]        ; ECX = g_GameManager.playerSub (GLOBAL @0x626274)
0042d813: CALL 0x0042d75a                       ; helper(ECX=playerSub, EDX=0x38)
0042d818: MOV ECX,dword ptr [EBP + -0x4]
0042d81b: ADD ECX,EAX
0042d81d: MOV dword ptr [EBP + -0x4],ECX        ; sum += result
0042d820: PUSH 0x38
0042d822: POP EDX                               ; EDX = 0x38              (length)
0042d823: MOV ECX,0x575a68                      ; ECX = &DAT_00575a68 (global, player-init table)
0042d828: CALL 0x0042d75a                       ; helper(ECX=0x575a68, EDX=0x38)
0042d82d: MOV ECX,dword ptr [EBP + -0x4]
0042d830: ADD ECX,EAX
0042d832: MOV dword ptr [EBP + -0x4],ECX        ; sum += result
0042d835: MOV EAX,dword ptr [EBP + -0x4]        ; return sum
0042d838: LEAVE
0042d839: RET
```

**Key facts:**
- The function reads `[this+0x8]` (this->scoreSub) twice ONLY to compute the
  first call's length `(scoreSub+0xac) - (scoreSub+0x34) = 0x78`. The pointer
  passed to the helper is the GLOBAL `DAT_00626278` (= g_GameManager.scoreSub),
  not `this->scoreSub`. This is a deliberate hardwiring — CalculateChecksum
  always CRCs the global g_GameManager's score state regardless of which
  GameManager variant invoked it.
- All four helper calls:
  1. `helper(g_scoreSub + 0x34, 0x78)` — covers scoreSub bytes `[0x34..0xac)` (the 7-ID array + 2 floats region per synthesis).
  2. `helper(g_scoreSub + 0xb4, 0x14)` — covers `[0xb4..0xc8)` (the 5-ID array region; 0xb4+0x14 = 0xc8 = full alloc size).
  3. `helper(g_playerSub + 0x00, 0x38)` — covers the full 0x38-byte PlayerSub.
  4. `helper(0x575a68,           0x38)` — covers a 0x38-byte global at `0x575a68` (the player-init template table that AddedCallback `memcpy`s into playerSub).
- `DAT_00626278` = `*(u32*)0x626278` = `g_GameManager.scoreSub` (g_GameManager @ 0x626270, +8).
- `DAT_00626274` = `*(u32*)0x626274` = `g_GameManager.playerSub` (g_GameManager @ 0x626270, +4).
- `0x575a68` is a static 0x38-byte player-init template (not a pointer; the raw address is passed).

### Field access table

| Effective addr | Field | Type | Semantics |
|---|---|---|---|
| `[this+0x8]` | GameManager.scoreSub | `ScoreSub*` | Read only to derive first-call length. |
| `[0x626278]` (g_GameManager+0x8) | g_GameManager.scoreSub | `ScoreSub*` | Hardwired source of all 4 helper CRC inputs. |
| `[0x626274]` (g_GameManager+0x4) | g_GameManager.playerSub | `PlayerSub*` | 3rd helper input (full 0x38-byte player sub). |
| `[g_scoreSub+0x34..0xac)` | ScoreSub ids/pos block | `u8[0x78]` | CRC input region 1. |
| `[g_scoreSub+0xb4..0xc8)` | ScoreSub ids5[5] block | `u8[0x14]` | CRC input region 2. |
| `[0x575a68..0x575aa0)` | global playerInitTemplate | `u8[0x38]` | CRC input region 4 (static data). |

---

## 3. Helper FUN_0042d75a — orig disassembly (0x42d75a .. 0x42d7bd, 0x64 bytes)

```asm
0042d75a: PUSH EBP
0042d75b: MOV EBP,ESP
0042d75d: SUB ESP,0x10
0042d760: MOV dword ptr [EBP + -0x10],EDX       ; [EBP-0x10] = len
0042d763: MOV dword ptr [EBP + -0xc],ECX        ; [EBP-0xc]  = ptr (byte cursor)
0042d766: AND dword ptr [EBP + -0x8],0x0        ; sum = 0
0042d76a: AND dword ptr [EBP + -0x4],0x0        ; i = 0
0042d76e: JMP 0x0042d77e                        ; -> cond check
0042d770: MOV EAX,dword ptr [EBP + -0x4]
0042d773: INC EAX
0042d774: MOV dword ptr [EBP + -0x4],EAX        ; i++
0042d777: MOV EAX,dword ptr [EBP + -0xc]
0042d77a: INC EAX
0042d77b: MOV dword ptr [EBP + -0xc],EAX        ; ptr++
0042d77e: MOV EAX,dword ptr [EBP + -0x4]
0042d781: CMP EAX,dword ptr [EBP + -0x10]       ; i < len ?
0042d784: JGE 0x0042d7b9                        ; done
0042d786: MOV EAX,dword ptr [EBP + -0xc]
0042d789: MOVZX EAX,byte ptr [EAX]              ; EAX = *ptr (zero-extended byte)
0042d78c: MOV ECX,dword ptr [EBP + -0x8]
0042d78f: ADD ECX,EAX
0042d791: MOV dword ptr [EBP + -0x8],ECX        ; sum += *ptr
0042d794: MOV EAX,[0x00626278]                  ; g_GameManager.scoreSub
0042d799: MOV EAX,dword ptr [EAX + 0xac]        ; crcAcc
0042d79f: MOV ECX,dword ptr [0x00626278]
0042d7a5: ADD EAX,dword ptr [ECX + 0xbc]        ; crcAcc += crcStepBc
0042d7ab: MOV ECX,dword ptr [0x00626278]
0042d7b1: MOV dword ptr [ECX + 0xac],EAX        ; store back crcAcc
0042d7b7: JMP 0x0042d770                        ; loop
0042d7b9: MOV EAX,dword ptr [EBP + -0x8]        ; return sum
0042d7bc: LEAVE
0042d7bd: RET
```

Decompile (Ghidra, fixed __fastcall):
```c
int __fastcall FUN_0042d75a(u8 *ptr, int len)
{
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += *ptr;
        ptr++;
        g_GameManager.scoreSub->crcAcc += g_GameManager.scoreSub->crcStepBc;
    }
    return sum;
}
```

**Notable:** the `crcAcc += crcStepBc` side-effect runs once per byte — i.e. the
CRC step is `len`-dependent. This is intentional anti-tamper: any byte-tamper
that also changes the region length drifts the accumulator.

### Field access table

| Effective addr | Field | Type | Semantics |
|---|---|---|---|
| `[0x626278]` | g_GameManager.scoreSub | `ScoreSub*` | Hardwired CRC-state source. |
| `[scoreSub+0xac]` | ScoreSub.crcAcc | `i32` | Rolling CRC accumulator (mutated each byte). |
| `[scoreSub+0xbc]` | ScoreSub.crcStepBc | `i32` | Per-byte CRC step added to crcAcc. |

---

## 4. Complete C++ draft

The C++ below assumes the project conventions already established by
`src/GameManager.cpp` (DIFFABLE_STATIC globals, `g_GameManager` /
`g_Chain` accessors, `__fastcall` callbacks, `u8/u32/i32` from `inttypes.hpp`).
Three new pieces are introduced:

1. A static helper `ChecksumRegion(u8*, int)` for `FUN_0042d75a`. Declared
   `static` so MSVC gives it internal linkage matching the orig (no PUBLIC
   symbol). It must NOT be inlined — keep it a free function so the 4 CALL
   relocations in CalculateChecksum reproduce.
2. Two `GameManager` static methods.
3. ScoreSub struct fields extended to cover `+0xac`, `+0xbc`, and a trailing
   `gameState1fbac` at `+0x1fbac` (only meaningful for the active-game sub-alloc).

### 4a. GameManager.hpp changes

```cpp
namespace th07
{

// ScoreSub — score + anti-tamper sub-struct reached via GameManager+0x8.
// Allocation size differs by owner:
//   * menu g_GameManager (@0x626270) sub-alloc: 0xC8 bytes
//   * active-game (@0x49fbf0) sub-alloc:        0x20a30 bytes (lives in [0x49fbf0+0x8])
// Only the header fields used by the small methods are typed; the body is opaque
// padding. The crcAcc/crcStepBc fields at +0xac/+0xbc are mutated by the
// CalculateChecksum helper. gameState1fbac at +0x1fbac is the IsGameActive
// liveness word (only valid on the active-game sub-alloc).
struct ScoreSub
{
    u32 guiScore;            // +0x00  displayed/committed score (CutChain)
    u32 score;               // +0x04  running score (CutChain clamps to 999999999)
    u8  pad08[0xac - 0x08];  // +0x08  opaque (delta@0x08, high@0xc, ids7[7]@0x34, ... )
    i32 crcAcc;              // +0xac  rolling CRC accumulator (helper mutates)
    u8  padb0[0xbc - 0xb0];  // +0xb0  opaque (randSum@0xb0, ids5[5]@0xb4)
    i32 crcStepBc;           // +0xbc  per-byte CRC step (helper adds to crcAcc)
    // The active-game sub-alloc extends to 0x20a30; only the liveness word is named.
    // Use a raw offset access for +0x1fbac so the menu-sized (0xC8) sub-struct does
    // not need to declare 0x1f000 bytes of padding.
};

// PlayerSub — reached via GameManager+0x4 (alloc 0x38). Body opaque for now.
struct PlayerSub
{
    u8 raw[0x38];
};

// Forward decl for the global player-init template at 0x575a68 (0x38 bytes).
// AddedCallback memcpy's this into playerSub. Declared as raw bytes.
DIFFABLE_EXTERN_ARRAY(u8, g_PlayerInitTemplate, 0x38)   // @ 0x575a68

struct GameManager
{
    // (existing header unchanged)
    void *scratchBuf;            // +0x0
    PlayerSub *playerSub;        // +0x4   (was `void *playerSub`)
    ScoreSub *scoreSub;          // +0x8
    // ... existing fields ...

    // --- methods ---
    static ZunResult RegisterChain();
    static void CutChain();
    static ChainCallbackResult __fastcall OnDraw(GameManager *gameManager);
    static ChainCallbackResult __fastcall OnUpdate(GameManager *gameManager);
    static ZunResult __fastcall AddedCallback(GameManager *gameManager);
    static ZunResult __fastcall DeletedCallback(GameManager *gameManager);

    // NEW: anti-tamper / liveness helpers
    static i32  __fastcall IsGameActive(GameManager *gameManager);
    static i32  __fastcall CalculateChecksum(GameManager *gameManager);
};

} // namespace th07
```

### 4b. GameManager.cpp additions

```cpp
// ---------------------------------------------------------------------------
// GameManager::ChecksumRegion (FUN_0042d75a) — byte-sum + CRC accumulator bump.
// Static (internal linkage) so the 4 CALL relocations in CalculateChecksum
// reproduce. Must NOT be force-inlined.
//
// Returns the sum of `len` bytes starting at `ptr`, and as a side effect bumps
// the GLOBAL g_GameManager.scoreSub->crcAcc by crcStepBc once per byte. Note
// the CRC state is always the g_GameManager (@0x626270) scoreSub, NOT the
// `this->scoreSub` of CalculateChecksum's caller — this hardwiring is in the
// orig (helper reads DAT_00626278 directly).
// ---------------------------------------------------------------------------
#pragma auto_inline(off)
static i32 __fastcall ChecksumRegion(u8 *ptr, i32 len)
{
    i32 sum;
    i32 i;

    sum = 0;
    // MSVC 7.0 /Od emits the classic for-loop: init, JMP to cond, body, i++.
    // Keep it as a plain for() — do not rewrite as while.
    for (i = 0; i < len; i++)
    {
        sum += (u32)*ptr;
        g_GameManager.scoreSub->crcAcc += g_GameManager.scoreSub->crcStepBc;
        ptr++;
    }
    return sum;
}
#pragma auto_inline(on)

// ---------------------------------------------------------------------------
// GameManager::IsGameActive (FUN_0042ad66) — 0x44 bytes.
// ACTUAL RUNTIME this = 0x0049fbf0 (active-game object), not g_GameManager.
// Reads this->scoreSub; if NULL -> 0. Else reads scoreSub->gameState1fbac; if
// (<0 && !=-2) -> 0, else -> 1. The -2 sentinel is an explicit "ok despite
// negative" state. The +0x1fbac offset exceeds the menu g_GameManager's 0xC8
// scoreSub alloc, so this function is only meaningful for the active-game
// sub-alloc (0x20a30 bytes) — never call it on the menu GameManager.
// ---------------------------------------------------------------------------
i32 __fastcall GameManager::IsGameActive(GameManager *gameManager)
{
    if (gameManager->scoreSub == 0)
    {
        return 0;
    }
    if ((*(i32 *)((u8 *)gameManager->scoreSub + 0x1fbac) < 0) &&
        (*(i32 *)((u8 *)gameManager->scoreSub + 0x1fbac) != -2))
    {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// GameManager::CalculateChecksum (FUN_0042d7be) — 0x7c bytes.
// ACTUAL RUNTIME this = 0x0049fbf0 (active-game object).
// Sums 4 regions via ChecksumRegion, always sourcing the CRC inputs from the
// GLOBAL g_GameManager.scoreSub / playerSub (not this->). The first call's
// length (0x78) is derived as (scoreSub+0xac) - (scoreSub+0x34) — reproduce
// that subtraction literally so MSVC emits the same SUB EAX,EDX sequence.
// ---------------------------------------------------------------------------
i32 __fastcall GameManager::CalculateChecksum(GameManager *gameManager)
{
    i32 sum;

    // First region: g_scoreSub[0x34..0xac), length = 0xac - 0x34.
    // The orig literally does: EDX = scoreSub+0xac; EAX = scoreSub+0x34; EDX -= EAX.
    // Express it the same way (pointer-difference) for codegen parity.
    sum = ChecksumRegion(
        (u8 *)g_GameManager.scoreSub + 0x34,
        (i32)((u8 *)g_GameManager.scoreSub + 0xac) - (i32)((u8 *)g_GameManager.scoreSub + 0x34));

    sum += ChecksumRegion((u8 *)g_GameManager.scoreSub + 0xb4, 0x14);

    sum += ChecksumRegion((u8 *)g_GameManager.playerSub, 0x38);

    sum += ChecksumRegion(g_PlayerInitTemplate, 0x38);

    return sum;
}
```

### 4c. Globals to declare (if not already)

```cpp
// In some appropriate header (e.g. GameManager.hpp or a generated globals header):
DIFFABLE_EXTERN(GameManager, g_GameManager)            // @ 0x626270  (existing)
DIFFABLE_EXTERN_ARRAY(u8, g_PlayerInitTemplate, 0x38)  // @ 0x575a68  (NEW)
```

`g_GameManager` already exists in the codebase; `g_PlayerInitTemplate` is new.
The `DIFFABLE_EXTERN_ARRAY` macro should mirror how the project declares other
fixed-size byte-array globals (follow the pattern used elsewhere — likely a
plain `extern u8 g_PlayerInitTemplate[0x38];` under the diffbuild guard).

---

## 5. Calling-convention & codegen notes

1. **__fastcall, ECX=this.** Both IsGameActive and CalculateChecksum store ECX
   into a stack local on entry (`MOV [EBP-x],ECX`). The helper FUN_0042d75a is
   also __fastcall (ECX=ptr, EDX=len) but is NOT a member function — its "this"
   slot is the byte pointer. Type it as a free `static` function.

2. **Stack frame layouts (must match MSVC's slot choice for objdiff):**
   - IsGameActive: `[EBP-4]` = this, `[EBP-8]` = result (the `return 0/1`
     value lives in a stack slot, not a register, because of the 3-way branch).
     C++ form: declare a local `i32 result;` and assign through it, OR rely on
     MSVC generating the slot itself from the control flow. The early `return 0`
     on the NULL path is `XOR EAX,EAX; JMP epilogue` — keep that path as an
     early return so MSVC emits the short-circuit.
   - CalculateChecksum: `[EBP-4]` = sum, `[EBP-8]` = this. MSVC will pick this
     ordering if `sum` is declared before the first helper call. Keep `i32 sum;`
     as the first local.
   - ChecksumRegion: `[EBP-0x10]` = len, `[EBP-0xc]` = ptr, `[EBP-8]` = sum,
     `[EBP-4]` = i. This is MSVC's standard layout for a 4-local function with
     a for-loop; declaring locals in order `sum, i` (with ptr/len as params)
     reproduces it. If objdiff complains, add `#pragma var_order(...)`.

3. **Do NOT inline ChecksumRegion.** The 4 CALL relocations in CalculateChecksum
   are load-bearing for objdiff symbol matching. `#pragma auto_inline(off)`
   around it guarantees MSVC 7.0 keeps it out-of-line. (Under `/Ob1` MSVC might
   otherwise inline a 4-local leaf.)

4. **The first-call length trick.** The orig computes `0x78` as
   `(scoreSub+0xac) - (scoreSub+0x34)` rather than loading the immediate `0x78`.
   This is almost certainly a leftover from when the bounds were variables. The
   draft reproduces the subtraction literally. If MSVC constant-folds it to
   `PUSH 0x78` under `/Od` (it should NOT under `/Od`, but `/Oi` might), and
   objdiff flags the mismatch, fall back to `ChecksumRegion(ptr, 0x78)` and add
   a comment. Under the project's `/Od` flag the SUB sequence should survive.

5. **objdiff global-naming limit.** As noted in the project conventions,
   `g_GameManager`-heavy functions cap around ~76% on objdiff because globals
   appear mangled in the reimpl vs `DAT_xxxxxxxx` in orig. CalculateChecksum
   has 4 global reads (2x `0x626278`, 1x `0x626274`, 1x `0x575a68`) plus 4
   CALL relocations to the helper — expect the match to plateau around 70-80%
   even with perfect logic. IsGameActive has zero global reads (all via `this`)
   and should hit ~95%+.

---

## 6. Dependencies on unreversed callees

- **None for execution.** Both functions and the helper are fully self-contained.
  The only external symbols are:
  - `g_GameManager` (already declared) — used by the helper for crcAcc/crcStepBc.
  - `g_PlayerInitTemplate` (@0x575a68, 0x38 bytes) — static data, just needs an
    `extern` declaration.
- **The `+0x1fbac` field** on the active-game scoreSub is part of the
  not-yet-reversed active-game object (@0x49fbf0). When that object is reversed,
  the `gameState1fbac` field should be promoted from raw-offset access to a
  named struct field. For now the raw `(i32*)((u8*)scoreSub + 0x1fbac)` cast is
  the safest expression.
- **CalculateChecksum's callers** are: `FUN_004012b0` (init re-seed),
  `FUN_00401390` (setter), `FUN_0042d8d5` (OnUpdate, integrity check),
  `FUN_0042e3da` (score randomizer). None of them need to be reversed for these
  two functions to compile; they just consume the return value as a seed.

---

## 7. Summary of struct changes needed

| Struct | Change | Reason |
|---|---|---|
| `ScoreSub` | Add `crcAcc @ +0xac` (i32), `crcStepBc @ +0xbc` (i32); keep body as `pad08[0xa4]` / `padb0[0xc]`. | Helper reads/writes these. Replace the current 2-field stub. |
| `ScoreSub` | Document `+0x1fbac` as raw-offset access (NOT a declared field — exceeds 0xC8 menu alloc). | IsGameActive reads it; only valid on active-game sub-alloc. |
| `PlayerSub` | New struct `{ u8 raw[0x38]; }`. | CalculateChecksum region 3 covers full 0x38 bytes; GameManager+0x4 type changes from `void*` to `PlayerSub*`. |
| `GameManager` | Change `+0x4` type `void *playerSub` -> `PlayerSub *playerSub`. | Enables `g_GameManager.playerSub` syntax in CalculateChecksum. |
| Globals | Add `g_PlayerInitTemplate[0x38]` @ `0x575a68`. | CalculateChecksum region 4. |
| `GameManager` | Declare `IsGameActive`, `CalculateChecksum` statics. | New methods. |
| `.cpp` | Add `static ChecksumRegion` (FUN_0042d75a). | Helper called 4x by CalculateChecksum. |

---

## 8. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| The first-call length subtraction constant-folds under `/Od` (it shouldn't, but `/Oi` is on). | Low | Fall back to literal `0x78`; document. |
| MSVC picks different stack slots for the 4 ChecksumRegion locals. | Medium | Add `#pragma var_order(...)` if objdiff flags it; the declared order `sum, i` with params `ptr, len` should match. |
| `gameState1fbac` raw-offset cast generates a different addressing mode than the orig's `MOV EAX,[EAX+0x1fbac]`. | Low | The double-load pattern (`MOV EAX,this; MOV EAX,[EAX+8]; CMP [EAX+0x1fbac],0`) reproduces from the C++ dereference chain; verify in objdiff. |
| objdiff plateau on CalculateChecksum due to 4 global reads. | Certain | Accept ~70-80%; do not chase 100% (per project convention). |
| The active-game object @0x49fbf0 turns out to need its own struct type distinct from GameManager. | Medium | When reversed, split the type; both functions' signatures change from `GameManager*` to the new type. Logic stays identical. |
