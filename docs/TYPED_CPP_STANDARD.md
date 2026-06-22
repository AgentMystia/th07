# TH07-RE typed-C++ Standard (对齐 th06 严格标准)

This document is the **authoritative style guide** for all `src/` code in
TH07-RE. It is enforced by `AGENTS.md` §2 and verified by the project-wide
audit greps at the bottom. Every contributor (human or agent) MUST follow it.

## 1. The one-sentence rule

**Write standard, portable, typed C++. No raw addresses, no raw byte buffers,
no accessor methods returning `&raw[OFF]`. Every byte of every struct is a
named member.** The compiler and objdiff together verify correctness; we do
not "cheat" with address arithmetic.

## 2. Struct definitions

### Required

- Every `struct` declares **all** of its bytes as named members.
- Known-semantic offsets get business names (`positionCenter`, `playerState`).
- Unexplored regions get `unk_XXXX[N]` padding where `XXXX` is the starting
  hex offset (e.g. `u8 unk_3ef50[0xb7e4c - 0x3ef50];`). This makes layout
  auditable and the running offset total exact.
- Sub-structures are composed as named members or arrays of named sub-structs
  (`PlayerBullet bullets[96];`, `BombProjectileSlot bombProjectileSlots[32];`).
- Each struct ends with `ZUN_ASSERT_SIZE(Name, 0xNNNN);` so the layout is
  locked at compile time.
- Every field carries an inline `// +0xNNN` offset comment.

### Forbidden

| Form | Why | Use instead |
|---|---|---|
| `u8 raw[0xNNNN];` byte buffer | Bypasses named fields; unverifiable | Named members + `unk_XXXX[N]` padding |
| `T *Accessor() { return (T *)&raw[OFF]; }` | Hides layout behind a call | Named member directly |
| `u8 tail[N];` / `u8 pad[N];` (no offset in name) | Loses audit trail | `u8 unk_XXXX[N];` |
| `u8 _pad_364[0];` zero-length | Makes the struct non-arrayable | Omit the trailing pad |

### The single allowed exception

A single trailing `scratchRegion[N]` may appear at the end of a struct
**only** when the region is (a) runtime-allocated pooled buffers, (b) not
zero-initialised by any ctor, and (c) not indexed by any documented method
with a fixed stride. See `AnmManager.hpp` and `SoundPlayer.hpp` for the
canonical pattern. As soon as a specific offset inside the scratch region is
anchored, it must be promoted to a named member.

## 3. Function bodies

### Required

- Field access is by name: `p->positionCenter`, `g_Supervisor.curState`,
  `g_SoundPlayer.soundBuffers[idx]`.
- Cross-module singletons are reached through typed `extern` globals
  (`g_Supervisor`, `g_SoundPlayer`, `g_AnmManager`, `g_AsciiManager`, etc.),
  declared in the owning module's header via `DIFFABLE_EXTERN`.
- rdata float constants become `extern "C" const f32 g_<Module>C0xNNNNNNN;`
  and are mapped back to their orig `DAT_` address in
  `scripts/generate_objdiff_objs.py`'s `SYMBOL_MAP`.
- rdata string constants become string literals (`"bgm/thbgm.fmt"`).
- Shift-JIS strings in source are ASCII-only octal escapes
  (`"\202\314..."`); MSVC 7.0 does not parse UTF-8 in any form.

### Forbidden in function bodies

| Form | Why | Use instead |
|---|---|---|
| `(*(T *)0xADDR)` absolute-address cast | Non-portable, unverifiable | typed global `g_X` or member |
| `*reinterpret_cast<T *>(0xADDR)` | same | same |
| `(char *)0xADDR` | same | string literal |
| `((void(*)())0xADDR)()` code-address call | same | typed `extern` declaration |
| `*(T *)((u8 *)this + OFF)` on `this` | Bypasses named struct | `this->member` |
| `*(T *)(g_Bytes + idx*stride + off)` on a typed byte-array global | Bypasses named fields; magic-number offsets | element struct + `g_Array[idx].field` (see §3.1) |
| `nullptr` | MSVC 7.0 doesn't support C++11 | `0` |

### 3.1 Fixed-stride byte arrays → element struct (verified zero-cost)

When a typed global is declared as a flat byte array (`extern "C" u8 g_X[N];`)
but every access follows the pattern `*(T*)(g_X + idx*stride + fieldOff)`,
the array is conceptually an array of fixed-size records. **Promote it to an
element struct** — this is both more readable and provably free at the
codegen level.

**Pattern**:

```cpp
// BEFORE (forbidden): magic strides, opaque offsets, unreadable
extern "C" u8 g_Pbg4Nodes[0x2001 * 0xc];
// ...
*(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x0) = 0x2000;
*(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x8) = 0;
Pbg4_NodeShrink(idx, *(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x4));

// AFTER (preferred): named element struct, self-documenting
struct Pbg4Node { i32 parent; i32 leftChild; i32 rightChild; };  // 0xc bytes
extern "C" Pbg4Node g_Pbg4Nodes[0x2001];
// ...
g_Pbg4Nodes[idx].parent     = 0x2000;
g_Pbg4Nodes[idx].rightChild = 0;
Pbg4_NodeShrink(idx, g_Pbg4Nodes[idx].leftChild);
```

**Why this is safe — verified equivalence (Pbg4Parser, 2026-06-22)**:

MSVC 7.0 at `/Od` emits **byte-identical** code for both forms. Each
`g_Nodes[idx].field` lowers to `IMUL reg, idx, sizeof(Node)` +
`mov [reg + DAT_addr + fieldOff]`, exactly matching the hand-written
byte-pointer arithmetic. objdiff match% was unchanged across all three
Pbg4Parser functions after the refactor:

| Function | Before (byte casts) | After (element struct) |
|---|---|---|
| SetIndex | 99.44% | 99.44% |
| Reset | 94.85% | 94.85% |
| AdvanceNode | 91.90% | 91.90% |
| **module avg** | **95.40%** | **95.40%** |

This is the same principle the rest of the standard relies on: **standard
typed C++ is a zero-cost abstraction under MSVC 7.0 /Od**, so we never need
to sacrifice readability for codegen. The rule of thumb — if you find
yourself writing `idx*stride + fieldOff` more than once on the same array,
stop and define the element struct.

### Allowed (with justification)

- `(T *)((u8 *)typed_ptr + OFF)` for **struct-internal** offsets that fall
  inside a documented `unk_XXXX[]` pad. This is honest: the base pointer is
  typed, and the offset access is explicit. Preferred form is to promote the
  offset to a named member once verified. Example: `*(f32 *)((u8 *)anm + 0x1c8)`
  reading an unverified AnmVm padding byte.
- `#pragma var_order(...)` to control MSVC `/Od` local stack layout.
- `memset`/`memcpy` intrinsics (generate `rep stosd`/`rep movsd`).
- Early-return control flow; `(u16)param` casts (generate `movzx`).

## 4. Cross-module globals workflow

When a function needs to read a game-state global at orig address `0xADDR`:

1. **If the address is a field of a known typed struct** (`g_Supervisor`,
   `g_GameManager`, etc.): access it as `g_Supervisor.member` (no new
   declaration needed; the struct header already exposes it).
2. **If the address is a standalone global**: add a typed `extern "C"`
   declaration at the top of the `.cpp` file:
   ```cpp
   extern "C" const f32 g_ModuleC0x498a54;   // rdata float
   extern "C" i32 g_ModuleG0x62f884;         // .data global
   ```
   Use `const` for rdata (read-only section) addresses.
3. **Add a SYMBOL_MAP entry** in `scripts/generate_objdiff_objs.py`:
   ```python
   b"_g_ModuleC0x498a54": b"DAT_00498A54",
   ```
   This maps the typed-global COFF symbol back to the orig delinked `DAT_`
   name so objdiff compares them correctly.
4. **Naming convention**: `g_<Module>C0x<addr>` for rdata constants,
   `g_<Module>G0x<addr>` for `.data`/`.bss` globals. The `0x<addr>` suffix
   keeps the orig address visible in the symbol name for traceability.

## 5. hpp file skeleton (template)

See `src/AnmManager.hpp` for the canonical example. Every hpp follows:

```cpp
// <Module> module for th07.
//
// All field offsets below were verified by reading th07.exe in ghidra. They
// MUST NOT be edited without re-checking the binary.
//
// LAYOUT NOTE (th06 standard): every byte is a named member. Known-semantic
// offsets use business names; unexplored regions use `unk_XXXX[N]` padding.

#pragma once

#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
// ... other includes

namespace th07
{
// enums, forward decls, sub-structs with ZUN_ASSERT_SIZE...

struct ModuleName
{
    // methods...

    // ===== +0x000 .. +0xNNN : <region> =====
    NamedType fieldName;                    // +0x000
    NamedType anotherField;                 // +0x004
    u8 unk_008[0x10 - 0x08];                // +0x008 opaque pad

    // ===== +0xNNN .. end : <region> =====
    // ...
    u8 scratchRegion[TOTAL - LAST_NAMED];   // only if runtime-allocated tail
};
ZUN_ASSERT_SIZE(ModuleName, 0xNNNN);

DIFFABLE_EXTERN(ModuleName, g_ModuleName)
}; // namespace th07
```

## 6. Per-file migration checklist

When migrating a `.cpp` from raw-address to typed C++:

1. **Inventory**: `grep -nE '\*\([^)]+\*?\)\s*0x|\*reinterpret_cast<[^>]+>\(0x'`
   to list all sites.
2. **Classify each address**:
   - g_Supervisor/g_GameManager member → struct field access
   - rdata float (0x498xxx) → `extern "C" const f32`
   - rdata string (0x496xxx/0x497xxx) → string literal (decode from binary)
   - other global → `extern "C" T` + SYMBOL_MAP
3. **Run the migration** (see `scripts/migrate_supervisor_cpp.py` and
   `scripts/migrate_gamemanager_cpp.py` for reusable templates).
4. **Add extern declarations** at the top of the `.cpp` after includes.
5. **Extend SYMBOL_MAP** in `scripts/generate_objdiff_objs.py`.
6. **Compile + objdiff verify**:
   ```bash
   python3 scripts/build.py --build-type=objdiffbuild --object-name <M>.obj
   objdiff-cli diff -1 build/objdiff/orig/<M>.obj -2 build/objdiff/reimpl/<M>.obj \
     -o /tmp/<m>.json --format json-pretty
   ```
   Accept if match% holds within ±2pp.
7. **Commit** with a message listing the pattern counts and match% delta.

## 7. Project-wide audit (run after every migration)

```bash
# Must print ZERO non-comment matches:
grep -rE '\*\([^)]+\*?\)\s*0x[0-9a-fA-F]{5,}|reinterpret_cast<[^>]+>\(0x[0-9a-fA-F]{5,}' src/

# Must print ZERO non-comment matches:
grep -rnE 'u8 raw\[|&raw\[0x' src/

# Fixed-stride byte-array casts (see S3.1). Each hit is a candidate for
# promotion to an element struct. Must print ZERO non-comment matches:
grep -rnE '\*\([^)]+\*\)\s*\(\s*g_[A-Za-z0-9_]+\s*\+\s*[A-Za-z0-9_]+\s*\*\s*0x[0-9a-fA-F]+' src/

# Must print ZERO non-comment matches:
grep -rn 'nullptr' src/

# Source files must contain no non-ASCII bytes (MSVC 7.0 can't parse UTF-8):
python3 -c "import pathlib; [print(p) for p in pathlib.Path('src').rglob('*.[hc]pp') if any(b>0x7f for b in p.read_bytes())]"
```

If any of these return hits, the migration is incomplete; address them before
committing.
