# GameManager::DeletedCallback @ 0x42f2e4 — Reversal + C++ Draft

> ⚠️ **[DRAFT — pre-2026-06-17-polish; SUPERSEDED guidance]**
>
> 本文件写于 objdiff 字节匹配优先的时代。文中 raw-address / DAT_ 模式
> 已被 2026-06-17 polish session 废弃。实现时请遵循 `AGENTS.md` §2
> "诚实重建（单一代码路径）"原则：typed C++ global + SYMBOL_MAP，
> 禁止 inline asm / naked / 函数级 `#ifdef DIFFBUILD` 分裂。
> 保留本文件仅供历史溯源与 ghidra 逆向数据参考。

Address span: `0x42f2e4 .. 0x42f3c4` (inclusive) = **0xe1 / 225 bytes**, 41 instructions.
Return type: `ZunResult` (always returns `0` = `ZUN_SUCCESS`). Single return path.
Calling convention: **`__fastcall`**, ECX = `GameManager*` (= `&g_GameManager` @ 0x626270).

This is the GameManager chain-node **OnDelete** callback (stored in
`updateChainNode.deletedCallback` at +0x9644+0xc by RegisterChain). The Chain
`Cut()` implementation invokes it after unlinking the update node. It performs
cross-subsystem teardown: shuts down MIDI (conditional on Supervisor state),
drains the streaming-sound command queue, releases every subsystem singleton,
updates the two wall-clock play-time accumulators, clears the chain-registered
bit, big-zeroes an Effect singleton, and clears two GameManager liveness fields.

---

## 1. Original decompile (Ghidra, misleading — see note)

```c
undefined4 __fastcall FUN_0042f2e4(int param_1)
{
  int iVar1;

  FUN_0043a05f(param_1, param_1);                       // misleading arg passing
  if ((DAT_00575a87 == '\x02') && (DAT_00575acc != 0)) {
    FUN_00436b30(param_1, DAT_00575acc);
    FUN_00436790(0x1e);
    FUN_00436ad0();
  }
  do {
    iVar1 = FUN_0044c9c0();
  } while (iVar1 != 0);
  FUN_004075d0();
  FUN_00427760();
  FUN_00442b10();
  FUN_00423050();
  COleDispParams::~COleDispParams((COleDispParams *)&DAT_01347938);
  FUN_0041d150();
  FUN_0042d53d();
  FUN_00443d30();
  if ((DAT_0062f648 >> 3 & 1) == 0) {
    FUN_0043a3f4();
  }
  _DAT_00575ae4 = 0;
  FUN_0043a27f();
  *(uint *)(param_1 + 0x93d8) = *(uint *)(param_1 + 0x93d8) & 0xfffffffb;
  FUN_00401a00();
  DAT_0062627d = 0;
  DAT_0062f858 = 0;
  return 0;
}
```

**Why the decompile is misleading:** Ghidra believes `param_1` is forwarded into
the callee ECX because `FUN_0043a05f` / `FUN_00436b30` were auto-typed with
`param_1`-shaped args. The **disassembly** proves otherwise (see §2). After
saving the incoming GameManager* to a stack slot `[EBP-8]`, every subsequent CALL
is preceded by `MOV ECX, <hardcoded singleton>` or takes no `this` at all. The
GameManager* is **only** re-read once, to perform the `+0x93d8 &= ~4` RMW.

---

## 2. Disassembly (authoritative) with annotation

```
0042f2e4  PUSH EBP
0042f2e5  MOV EBP,ESP
0042f2e7  PUSH ECX                  ; reserve [EBP-4]  (midi dev cache)
0042f2e8  PUSH ECX                  ; reserve [EBP-8]  (saved GameManager*)
0042f2e9  MOV [EBP-8],ECX           ; stash GameManager*
0042f2ec  MOV ECX,0x575950          ; g_Supervisor
0042f2f1  CALL 0x0043a05f           ; Supervisor::OnDelete()  [__thiscall]

        ; --- MIDI teardown, conditional ---
0042f2f6  MOVZX EAX,byte [0x575a87] ; Supervisor.state
0042f2fd  CMP EAX,0x2
0042f300  JNZ 0x0042f32d            ; skip if state != 2
0042f302  CMP dword [0x575acc],0x0  ; g_MidiOutput ptr
0042f309  JZ  0x0042f32d            ; skip if no midi device
0042f30b  MOV EAX,[0x575acc]
0042f310  MOV [EBP-4],EAX           ; cache midi dev ptr in local
0042f313  MOV ECX,[EBP-4]
0042f316  CALL 0x00436b30           ; MidiOutput::CloseAll()  [__thiscall]
0042f31b  PUSH 0x1e                 ; arg: device index 30
0042f31d  MOV ECX,[EBP-4]
0042f320  CALL 0x00436790           ; MidiOutput::Open(30)    [__thiscall, RET 4]
0042f325  MOV ECX,[EBP-4]
0042f328  CALL 0x00436ad0           ; MidiOutput::Play()      [__thiscall]

        ; --- drain streaming-sound command queue ---
0042f32d  MOV ECX,0x4ba0d8          ; g_Supervisor (sound side)
0042f332  CALL 0x0044c9c0           ; StreamingSound::ProcessQueue()  [__thiscall]
0042f337  TEST EAX,EAX
0042f339  JZ  0x0042f33d
0042f33b  JMP 0x0042f32d            ; do { ... } while (ret != 0)

        ; --- release subsystem singletons (all __fastcall or __cdecl, no this) ---
0042f33d  CALL 0x004075d0           ; release g_ItemManager-ish trio @ 0x134cdd4/0x1347ae0/0x134cdb4
0042f342  CALL 0x00427760           ; Effect/Item teardown @ 0x9a9abc, 0x62f934; zero Item-array region
0042f347  CALL 0x00442b10           ; AnmManager teardown @ 0x575934/575938/57593c
0042f34c  CALL 0x00423050           ; release @ 0x12fe210/0x9a9adc/0x12fe230
0042f351  MOV ECX,0x1347938         ; g_COleDispParams / enemy singleton
0042f356  CALL 0x0040e4f0           ; ~dtor (frees internal buf)
0042f35b  CALL 0x0041d150           ; release @ 0x13478f8/0x1347918
0042f360  CALL 0x0042d53d           ; release chain-control blocks @ 0x62f914/0x62f8f4
0042f365  CALL 0x00443d30           ; BGM/fade bookkeeping bump @ 0x4b9e48

        ; --- optional play-time accumulator B ---
0042f36a  MOV EAX,[0x62f648]        ; options bitfield (g_Supervisor.opts)
0042f36f  SHR EAX,0x3
0042f372  AND EAX,0x1
0042f375  TEST EAX,EAX
0042f377  JNZ 0x0042f383            ; skip if bit3 (practice mode) SET
0042f379  MOV ECX,0x575950
0042f37e  CALL 0x0043a3f4           ; Supervisor::UpdateTimeAccumulatorB()

        ; --- null redraw flag, primary time accumulator ---
0042f383  AND dword [0x575ae4],0x0  ; g_Supervisor->redrawFlag = 0
0042f38a  MOV ECX,0x575950
0042f38f  CALL 0x0043a27f           ; Supervisor::UpdateTimeAccumulatorA()

        ; --- clear GameManager chain-registered bit (bit2 of +0x93d8) ---
0042f394  MOV EAX,[EBP-8]
0042f397  MOV EAX,[EAX+0x93d8]
0042f39d  AND EAX,0xfffffffb        ; &= ~4
0042f3a0  MOV ECX,[EBP-8]
0042f3a3  MOV [ECX+0x93d8],EAX

        ; --- big-zero Effect singleton ---
0042f3a9  MOV ECX,0x134ce18         ; g_EffectManager (AnmVm-ish)
0042f3ae  CALL 0x00401a00           ; EffectManager::Reset()

        ; --- clear GameManager liveness markers ---
0042f3b3  AND byte [0x62627d],0x0   ; GameManager+0xd = 0  (frame-skip/rank gate)
0042f3ba  AND dword [0x62f858],0x0  ; GameManager+0x95e8 = 0 (chainActive / frameCounter)

0042f3c1  XOR EAX,EAX              ; return 0
0042f3c3  LEAVE
0042f3c4  RET
```

---

## 3. Field-access table (offsets touched by this function)

### GameManager fields (this = `&g_GameManager` @ 0x626270)

| Offset | Addr (abs) | Type | Semantics | Access |
|--------|-----------|------|-----------|--------|
| `+0xd`   | 0x62627d | `u8`  | frame-skip / "isInitialized" gate. Also written by OnUpdate & AddedCallback. | write (=0, via `AND byte,0`) |
| `+0x93d8`| 0x62f648-bitfield twin (NOTE: see text) | `u32` | statusBitfield. bit2 (=0x4) is the "chain registered" latch set by AddedCallback (`|= 4`). DeletedCallback clears it (`&= ~4`). | read-modify-write |
| `+0x95e8`| 0x62f858 | `i32` | `chainActive` / state counter. Reset to 0. Also zeroed by RegisterChain. | write (=0, via `AND dword,0`) |

**Subtle point about `+0x93d8` vs `DAT_0062f648`:**
The disassembly at `0x42f36a` reads `DAT_0062f648` (the *options* bitfield global
that lives at absolute 0x62f648, i.e. **outside** GameManager — GameManager ends at
0x626270 + 0x9700 = 0x62f970, so 0x62f648 is GameManager + 0x93d8). Wait —
`0x626270 + 0x93d8 = 0x62f648`. **So 0x62f648 IS GameManager+0x93d8.** Good: the
single options bitfield is the `+0x93d8` field of GameManager itself. Same field
is read for the bit3 (practice) test AND written for the bit2 (chain-registered)
clear. The synthesis JSON's "DAT_0062f648 = options bitfield" and the GameManager
struct's "+0x93d8 = statusBitfield" are the **same field**. The HPP already
reflects this correctly (`statusBitfield @ +0x93d8`).

### Globals (singletons NOT inside GameManager)

| Address | Symbol | Role |
|---------|--------|------|
| 0x575950 | `g_Supervisor` | `this` for OnDelete / UpdateTimeAccumulator{A,B} |
| 0x575a87 | `g_Supervisor->state` (u8) | compared == 2 for MIDI teardown |
| 0x575acc | `g_MidiOutput` (ptr) | cached into `[EBP-4]`, `this` for the 3 midi calls |
| 0x575ae4 | `g_Supervisor->redrawFlag` (u32) | nulled via `AND dword,0` |
| 0x4ba0d8 | `g_Supervisor` (sound side) | `this` for `ProcessQueue` |
| 0x1347938 | `g_COleDispParams` (enemy mgr) | `this` for dtor |
| 0x134ce18 | `g_EffectManager` | `this` for `Reset()` |
| 0x62f648 | `g_GameManager.statusBitfield` (= +0x93d8) | read for bit3 test |

### Callees

| Addr | Signature | Calling conv | Purpose |
|------|-----------|-------------|---------|
| `0x0043a05f` | `Supervisor::OnDelete()` | `__thiscall` ECX=0x575950 | If state==2 & midi open: MidiClose. If state==1: SoundQue fade. |
| `0x00436b30` | `MidiOutput::CloseAll()` | `__thiscall` ECX=dev | Loop 0x20 device slots calling slot-close, then teardown. |
| `0x00436790` | `MidiOutput::Open(u32 idx)` | `__thiscall` ECX=dev, arg PUSHED, **RET 4** | Open midi device index (here 0x1e=30). |
| `0x00436ad0` | `MidiOutput::Play()` | `__thiscall` ECX=dev | Issue midi play command. |
| `0x0044c9c0` | `StreamingSound::ProcessQueue()` | `__thiscall` ECX=0x4ba0d8 | Returns nonzero while pending cmds exist; drain loop. |
| `0x004075d0` | (release trio) | `__fastcall`/`__cdecl` (no setup) | Release globals @ 0x134cdd4/0x1347ae0/0x134cdb4. |
| `0x00427760` | (effect/item teardown) | `__fastcall`/`__cdecl` | Release 0x9a9abc/0x62f934; zero Item-array region. |
| `0x00442b10` | (AnmManager teardown) | `__fastcall`/`__cdecl` | Release DAT_00575934/8/c; null them. |
| `0x00423050` | (release trio) | `__fastcall`/`__cdecl` | Release 0x12fe210/0x9a9adc/0x12fe230. |
| `0x0040e4f0` | `~COleDispParams()` | `__thiscall` ECX=0x1347938 | Free internal buf, null. |
| `0x0041d150` | (release pair) | `__fastcall`/`__cdecl` | Release 0x13478f8/0x1347918. |
| `0x0042d53d` | (chain control release) | `__fastcall`/`__cdecl` | Release 0x62f914/0x62f8f4. |
| `0x00443d30` | (bgm bookkeeping) | `__fastcall`/`__cdecl` | Bump write cursor, write 0 marker, index slot table. |
| `0x0043a3f4` | `Supervisor::UpdateTimeAccumulatorB()` | `__thiscall` ECX=0x575950 | Play-time accumulator B (gated). |
| `0x0043a27f` | `Supervisor::UpdateTimeAccumulatorA()` | `__thiscall` ECX=0x575950 | Play-time accumulator A (always). |
| `0x00401a00` | `EffectManager::Reset()` | `__fastcall` ECX=0x134ce18 | Big-zero struct in 6 chunks + AnmVm init. |

---

## 4. C++ draft (drop-in for GameManager.cpp)

Notes on conventions used (matching the existing `GameManager.cpp` style):
- `g_Supervisor` etc. are written as free globals, because that is exactly what
  the disassembly does (`MOV ECX, 0x575950` then `CALL`). Do **not** rewrite
  them as `this->supervisor.X` — there is no `this` indirection at runtime.
- The midi `Open(0x1e)` call is the only `__thiscall`-with-stack-arg (callee
  does `RET 4`); all others are pure `__thiscall` (ECX only) or `__cdecl`.
- The `AND [mem],0` form for `redrawFlag`, `+0xd`, `+0x95e8` zero-stores is what
  MSVC 7.0 emits for `= 0` on already-addressed globals/fields. Write them as
  plain `= 0` assignments; the compiler will reproduce the AND form.
- The `+0x93d8 &= ~4` MUST stay a read-modify-write (the orig loads, ANDs
  immediate, stores back) — never collapse to a blind store.
- `#pragma auto_inline(off)` / `(on)` bracket the function to keep MSVC 7.0 from
  inlining helpers; the orig is a standalone 225-byte function.

```cpp
// extern globals — declare once at file scope (or in a shared header).
// Addresses taken from disasm; types inferred from callees.
namespace th07 {

extern "C"
{
    // Singletons referenced by absolute address in DeletedCallback.
    // These are not part of GameManager; they live in other modules.
    // The exact C++ class types are TBD during those modules' reversal —
    // for now they are opaque, and the calls go through reinterpret'd
    // function pointers so that the generated `MOV ECX,imm32; CALL` matches.
}

} // namespace th07

// ---------------------------------------------------------------------------
// GameManager::DeletedCallback  FUN_0042f2e4  (0xe1 bytes)
//
// Chain "OnDelete" callback for the update node (registered in RegisterChain
// at updateChainNode.deletedCallback = (ChainDeletedCallback)DeletedCallback).
// Invoked by Chain::Cut() after unlinking the node. Performs cross-subsystem
// teardown: MIDI shutdown (if Supervisor state == 2 and a midi device is
// open), streaming-sound queue drain, release of every subsystem singleton,
// play-time accumulator update, clear of the chain-registered bit, Effect
// singleton reset, and clear of two GameManager liveness fields.
//
// Body is a flat sequence of singleton calls; GameManager* is only used for
// the +0x93d8 bit clear at the tail.
#pragma auto_inline(off)
ZunResult __fastcall GameManager::DeletedCallback(GameManager *gameManager)
{
    // 1. Supervisor::OnDelete() — if state==2 & midi open, closes midi;
    //    if state==1, issues a SoundQue fade.
    g_Supervisor.OnDelete();

    // 2. Conditional MIDI teardown: state == 2 AND midi device open.
    //    The orig caches the midi ptr into a stack slot and reuses it for
    //    all three calls. Match by reading the global once into a local.
    if (g_Supervisor.state == 2 && g_MidiOutput != 0)
    {
        MidiOutput *midi = g_MidiOutput;
        midi->CloseAll();
        midi->Open(0x1e);          // device index 30 (PUSH 0x1e; callee RET 4)
        midi->Play();
    }

    // 3. Drain the streaming-sound command queue (busy-wait to completion).
    do
    {
        // ProcessQueue returns nonzero while pending commands remain.
    } while (g_Supervisor.ProcessSoundQueue() != 0);

    // 4. Release subsystem singletons. Each is a free/static helper that
    //    takes a hardcoded singleton pointer; the orig emits bare CALLs.
    ReleaseItemSingletons();        // 0x004075d0
    EffectAndItemTeardown();        // 0x00427760
    AnmManagerTeardown();           // 0x00442b10
    ReleaseEnemySingletons();       // 0x00423050
    g_COleDispParams.~COleDispParams(); // 0x0040e4f0 (ECX=0x1347938)
    ReleaseBossSingletons();        // 0x0041d150
    ReleaseChainControlBlocks();    // 0x0042d53d
    BgmFadeBookkeeping();           // 0x00443d30

    // 5. Optional play-time accumulator B (only if practice-mode bit3 CLEAR).
    if ((g_GameManager.statusBitfield >> 3 & 1) == 0)
    {
        g_Supervisor.UpdateTimeAccumulatorB();
    }

    // 6. Null the Supervisor redraw flag, then always run accumulator A.
    g_Supervisor.redrawFlag = 0;
    g_Supervisor.UpdateTimeAccumulatorA();

    // 7. Clear the GameManager chain-registered bit (bit2 of +0x93d8).
    //    Read-modify-write; the orig loads, ANDs 0xfffffffb, stores.
    g_GameManager.statusBitfield &= 0xfffffffbu;

    // 8. Big-zero the EffectManager singleton.
    g_EffectManager.Reset();

    // 9. Clear two GameManager liveness markers. Orig emits these as
    //    AND-with-zero stores (byte then dword).
    *(u8 *)&g_GameManager + 0xd   = 0;   // DAT_0062627d = 0
    g_GameManager.chainActive     = 0;   // DAT_0062f858 = 0  (+0x95e8)

    return ZUN_SUCCESS;
}
#pragma auto_inline(on)
```

### Equivalent form using raw addresses (highest objdiff fidelity)

If the C++ class wrappers above introduce naming/layout risk, the byte-exact
form is to call through `extern` function pointers / `extern` globals at the
fixed addresses. The compiler will then emit the identical `MOV ECX,imm32; CALL`
sequences:

```cpp
// Highest-fidelity form. Each call reproduces the exact orig instruction pair.
namespace th07 {

// Opaque singleton types — only the address matters for codegen.
extern Supervisor   g_Supervisor;            // 0x00575950
extern MidiOutput  *g_MidiOutput;            // 0x00575acc
extern u8           g_Supervisor_state;      // 0x00575a87  (alias of g_Supervisor.state)
extern u32          g_Supervisor_redrawFlag; // 0x00575ae4
extern Supervisor   g_SupervisorSound;       // 0x004ba0d8  (sound-side this)
extern DispParams   g_COleDispParams;        // 0x01347938
extern EffectMgr    g_EffectManager;         // 0x0134ce18

// Subsystem release helpers (free functions, __cdecl).
extern void ReleaseItemSingletons_4075d0();
extern void EffectAndItemTeardown_427760();
extern void AnmManagerTeardown_442b10();
extern void ReleaseEnemySingletons_423050();
extern void ReleaseBossSingletons_41d150();
extern void ReleaseChainControlBlocks_42d53d();
extern void BgmFadeBookkeeping_443d30();

} // namespace th07

#pragma auto_inline(off)
ZunResult __fastcall GameManager::DeletedCallback(GameManager *gameManager)
{
    // 1. Supervisor::OnDelete
    g_Supervisor.OnDelete_43a05f();

    // 2. MIDI teardown (state==2 && midi!=0)
    if (g_Supervisor_state == 2 && g_MidiOutput != 0)
    {
        MidiOutput *midi = g_MidiOutput;
        midi->CloseAll_436b30();
        midi->Open_436790(0x1e);
        midi->Play_436ad0();
    }

    // 3. Drain streaming-sound queue
    while (g_SupervisorSound.ProcessSoundQueue_44c9c0() != 0) { }

    // 4. Subsystem teardown cascade (all __cdecl free fns)
    ReleaseItemSingletons_4075d0();
    EffectAndItemTeardown_427760();
    AnmManagerTeardown_442b10();
    ReleaseEnemySingletons_423050();
    g_COleDispParams.~DispParams_40e4f0();
    ReleaseBossSingletons_41d150();
    ReleaseChainControlBlocks_42d53d();
    BgmFadeBookkeeping_443d30();

    // 5. Optional accumulator B (gated by bit3 of GameManager.statusBitfield)
    if ((g_GameManager.statusBitfield >> 3 & 1) == 0)
    {
        g_Supervisor.UpdateTimeAccumulatorB_43a3f4();
    }

    // 6. Null redraw + primary accumulator A
    g_Supervisor_redrawFlag = 0;
    g_Supervisor.UpdateTimeAccumulatorA_43a27f();

    // 7. Clear chain-registered bit (bit2 of +0x93d8)
    g_GameManager.statusBitfield &= 0xfffffffbu;

    // 8. Reset EffectManager
    g_EffectManager.Reset_401a00();

    // 9. Clear GameManager liveness markers
    *(u8 *)((u8 *)&g_GameManager + 0xd) = 0;   // AND byte [0x62627d],0
    g_GameManager.chainActive = 0;             // AND dword [0x62f858],0

    return ZUN_SUCCESS;
}
#pragma auto_inline(on)
```

---

## 5. Required struct / header changes

### GameManager.hpp — `statusBitfield` already declared at +0x93d8

The current HPP has:
```cpp
u8 pad93dd[0x95e8 - 0x93dd]; // +0x93dd opaque control fields (statusBitfield@0x93d8, etc.)
```
This treats `+0x93d8` as opaque padding. For DeletedCallback (and OnUpdate, and
AddedCallback) we need a **named `u32` field at exactly +0x93d8**. Adjust the
padding split:

```cpp
u8 unk_93dc;                 // +0x93dc pause-request flag (OnDraw: if !=0 set to 2)
u8 pad93dd[0x93d8 - 0x93dd]; // +0x93dd..0x93d7 opaque (4 bytes; isUsed by AddedCallback)
u32 statusBitfield;          // +0x93d8 bit2=chain-registered, bit3=practice, bit1=paused
u8 pad93dc_after[0x95e8 - 0x93dc]; // +0x93dc..0x95e7 opaque control fields
```

Wait — `unk_93dc` is currently at +0x93dc, which is **after** +0x93d8. The
current ordering is wrong: `pad0c[0x93dc-0x0c]` ends at 0x93dc, then `unk_93dc`
at 0x93dc, then `pad93dd` fills 0x93dd..0x95e8. But `statusBitfield` must be at
**0x93d8**, i.e. 4 bytes *before* `unk_93dc`. The existing layout accidentally
works only because everything between 0x0c and 0x93dc is one big opaque pad. To
expose `statusBitfield` cleanly without moving `unk_93dc`:

```cpp
u8 pad0c[0x93d8 - 0x0c];     // +0x0c..0x93d7 opaque
u32 statusBitfield;          // +0x93d8 bit1=paused, bit2=chain-registered, bit3=practice
u8 unk_93dc;                 // +0x93dc pause-request flag (OnDraw)
u8 pad93dd[0x95e8 - 0x93dd]; // +0x93dd..0x95e7 opaque
i32 chainActive;             // +0x95e8 (already declared)
```

This is a **non-breaking** reshape (same bytes, same offsets, just splits the
big pad to name the u32 at +0x93d8). All other functions that read/write
+0x93d8 (OnUpdate, AddedCallback) get the same field name for free.

### No new ScoreSub / PlayerSub fields needed

DeletedCallback does **not** touch ScoreSub or PlayerSub. It does not free them
either — the synthesis JSON's "possibly frees scoreSub/playerSub" hint turned
out to be wrong; the disassembly confirms no `operator delete` / `free` of
`+0x4` or `+0x8`. Those sub-structs outlive DeletedCallback (they are reused
across stage transitions; only AddedCallback allocates them, and a separate
final-destructor frees them).

### Globals to declare (in a Supervisor.hpp / Sound.hpp / EffectManager.hpp)

These live in other modules and will get proper types when those modules are
reversed. For now, opaque extern declarations suffice to drive codegen:

```cpp
// Supervisor.hpp (sketch)
extern Supervisor g_Supervisor;          // 0x00575950
extern u8         g_Supervisor_state;    // 0x00575a87  (== g_Supervisor.state)
extern u32        g_Supervisor_redrawFlag; // 0x00575ae4
extern Supervisor g_SupervisorSound;     // 0x004ba0d8  (sound queue side)

// Midi.hpp (sketch)
extern MidiOutput *g_MidiOutput;         // 0x00575acc

// EffectManager.hpp (sketch)
extern EffectMgr  g_EffectManager;       // 0x0134ce18

// EnemyManager.hpp (sketch) — the COleDispParams dtor target
extern DispParams g_COleDispParams;      // 0x01347938
```

---

## 6. Dependencies on unreversed callees

This function compiles cleanly once the callees are *declared* (even as stubs).
Full byte-exact objdiff requires the callees to exist as **separate non-inlined
functions** at the right addresses:

| Callee | Status | Notes for objdiff |
|--------|--------|-------------------|
| `0x0043a05f` Supervisor::OnDelete | unreversed | small leaf, will reverse with Supervisor module |
| `0x00436b30` MidiOutput::CloseAll | unreversed | loops 0x20 slots — reverse with Midi module |
| `0x00436790` MidiOutput::Open | unreversed | `__thiscall` + stack arg, `RET 4` |
| `0x00436ad0` MidiOutput::Play | unreversed | leaf |
| `0x0044c9c0` ProcessSoundQueue | unreversed | big switch (already disassembled above) |
| `0x004075d0`/`0x00427760`/`0x00442b10`/`0x00423050`/`0x0041d150`/`0x0042d53d`/`0x00443d30` | unreversed | release helpers; reverse with respective subsystem modules |
| `0x0040e4f0` ~DispParams | unreversed | dtor; reverse with EnemyManager module |
| `0x0043a3f4`/`0x0043a27f` UpdateTimeAccumulator{B,A} | unreversed | Supervisor time math |
| `0x00401a00` EffectManager::Reset | unreversed | big-zero; reverse with EffectManager module |

**Key reimpl guarantee:** None of these callees should be inlined into
DeletedCallback. The orig emits each as a distinct `CALL`. Wrap the function in
`#pragma auto_inline(off)` / `(on)` and avoid `__forceinline` on any of the
helpers. MSVC 7.0 at `/Ob1` will not inline functions not marked inline, so
plain `extern` declarations are sufficient.

---

## 7. objdiff expectations

- **Global-heavy tail** (`AND byte [0x62627d],0`, `AND dword [0x62f858],0`,
  the `+0x93d8` RMW) will show as named-global vs `DAT_xxxxxxxx` mismatches in
  objdiff — same address, different symbol. This is the known objdiff
  limitation; don't chase 100% on those.
- The **MIDI conditional** (`MOVZX byte; CMP 2; JNZ; CMP dword 0; JZ`) and the
  **do/while drain** (`MOV ECX; CALL; TEST; JZ; JMP`) must match byte-exact and
  will — they're pure logic with no global name involvement.
- The **seven singleton CALLs in a row** will match if each callee address is
  correct and ECX setup is reproduced (`MOV ECX,imm32` before each
  `__thiscall`).
- Expected realistic ceiling: **~90-95%** on this function (high because most
  instructions are logic / fixed-address ECX loads, and only the 3 trailing
  GameManager-field stores hit the global-naming penalty).
