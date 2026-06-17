# 東方妖々夢　～ Perfect Cherry Blossom

This project aims to perfectly reconstruct the source code of [Touhou Youyoumu ~ Perfect Cherry Blossom v1.00b](https://en.touhouwiki.net/wiki/Perfect_Cherry_Blossom) by Team Shanghai Alice.

The ultimate acceptance criterion is that the compiled product's **game behavior matches the original `th07.exe`**. [`objdiff`](https://github.com/encounter/objdiff) is our verification tool: it measures how faithfully our reconstruction matches the original at the byte level (match%).

## Progress

> Last updated: 2026-06-18. Detailed per-module breakdown lives in [`PROGRESS.md`](PROGRESS.md).

### 🎉 Milestone: P0 reached — `th07e.exe` builds and links

The normal build now produces a runnable `build/th07e.exe` (PE32 i386 GUI). All **40 source modules compile** and the linker resolves **0 unresolved externals** (down from 311). The exe loads under wine without crashing. Player movement, sprites, etc. are not yet visible because many cross-module singletons are still zero-init stubs — replacing those stubs with real implementations is the next phase (P1).

### Match quality across tracked modules

Of the original exe's **1721 functions** (1562 non-thunk functions mapped in `config/mapping.csv`), **248** are currently objdiff-verified across **25 tracked modules**. Here is the match% tier breakdown. A module is "done" at ≥90%.

```
Modules at >=90% match (core done)          11  ██████████████████████████████████████████  44.0 %
Modules at 80–90% match (near done)          2  ████████                                   8.0 %
Modules at 50–80% match (in progress)        5  ████████████████████                       20.0 %
Modules at <50% match (early / blocked)      7  ███████████████████████████                28.0 %
```

### Match quality across the 248 tracked functions

```
Functions in modules >=90%                 111  ████████████████████████████████           44.8 %
Functions in modules 80–90%                 39  ████████                                   15.7 %
Functions in modules 50–80%                 49  ██████████████                             19.8 %
Functions in modules <50%                   49  ██████████████                             19.8 %
```

Overall weighted match across the 248 tracked functions: **44.82%** (arithmetic per-module mean 69.60%).

> The "tracked functions" denominator (248) is much smaller than the target (1721) because many modules are not yet reversed at all — they exist only as zero-init stubs that satisfy the linker. As more modules land, both the completion bar and the match-quality bars grow together.

### Implementation status by module

**Core complete (≥90%, 11 modules):** AnmVm, GameErrorContext, utils, Controller, Chain, ZunTimer, zwave, FileSystem, Rng, MidiOutput, ScreenEffect

**Near done (80–90%, 2 modules):** GameManager (89.11%), EffectManager (84.18%)

**In progress (50–80%, 5 modules):** Player (62.24% — 17 missing functions just landed this session), SoundPlayer (74.34%), CMyFont (70.75%), AsciiManager (75.04%), Supervisor (67.37%)

**Early / blocked (<50%, 7 modules):** AnmManager (41.07% — `ExecuteScript`/`DrawInner` big fns), BombData (40.87% — 11 calc fns), ReplayManager (34.70%), Pbg4Parser (19.72%)

**Not yet reversed (15 modules tracked, but 0 objdiff'd fns):** BulletData, BulletManager, EclManager, EnemyEclInstr, EnemyManager, FileAbstraction, GameWindow, Gui, IPbg4Parser, ItemManager, main, Pbg4Archive, ResultScreen, Stage, TextHelper

## Remaining work

**Total target:** 1721 functions in `th07.exe` (1562 mapped non-thunk + 159 thunks/glue).

| Bucket | Count | Status |
|---|---|---|
| Functions objdiff-verified (implemented) | **248** | Tracked & measuring match% |
| Functions compiled but stubbed (no-op) | ~260 | Linker resolves them; behavior wrong |
| Functions not yet referenced at all | ~1000 | In modules not yet started (Stage, Gui, EnemyManager, BulletManager, ItemManager, EclManager, ...) |

**Concrete next steps (P1, in priority order):**

1. **AnmManager** (41%): `ExecuteScript` (13178B ANM interpreter), `DrawInner`, `LoadAnmEntry`, `SetRenderStateForVm`, `LoadTextureAlphaChannel`, `LoadTextureFromMemory`. Without these nothing renders on screen.
2. **BombData** (41%): 11 of 12 per-character spell-card calc functions still stubbed (each 800–2400B of Player state-machine). `MarisaABombCalc2` (42.75%) is the verified template.
3. **ReplayManager** (35%): `SaveReplay`, `RewriteReplay`, `AddedCallback`.
4. **Pbg4Parser** (20%): LZSS decoder (`Reset`/`AdvanceNode`/`SetIndex`) — pure-algorithm module.
5. **ItemManager** (0%): `OnUpdate` is a single 4297B switch — needs per-case reverse.
6. **Replace link-pass stubs with real init**: as the modules above land, move singleton definitions out of `link_globals.cpp`/`link_stubs.cpp`/`link_cpp_stubs.cpp` into their owning `.cpp` files so the game actually boots into a playable state.

## Installation

### Executable

This project requires the original `東方妖々夢.exe` version 1.00b.

Copy `東方妖々夢.exe` to `th07/th07.exe`.

### Dependencies

The build system has the following package requirements:

- `python3` >= 3.4
- `wine` (on linux/macos only)
- `msiextract` (on linux/macos only)

The rest of the build system is constructed out of Visual Studio 2002 and DirectX 8.0 from the Web Archive.

#### Configure devenv

This will download and install compiler, libraries, and other tools.

```
python3 scripts/create_devenv.py scripts/dls scripts/prefix
```

On linux and mac, run:
```bash
./scripts/create_th06_prefix
```

#### Building

Run the following script:

```
python3 ./scripts/build.py
```

This will automatically generate a ninja build script `build.ninja`, and run ninja on it. The result is `build/th07e.exe`.

## Contributing

### Reimplementation

The easiest way to work on the reimplementation is through the use of [`objdiff`](https://github.com/encounter/objdiff).

1. Follow the instructions above to set up the devenv.
1. Copy the original `東方妖々夢.exe` to `th07/th07.exe`.
1. Run `python3 scripts/export_ghidra_objs.py --import-csv` to extract object files from the original exe for objdiff comparison.
1. Run objdiff and open the th07 project (`objdiff.json`).

#### Choosing a function to decompile

Look at [`PROGRESS.md`](PROGRESS.md) §"剩余工作" for the current priority list. Use Ghidra to reverse-engineer the original binary, then implement the function in C++ in the appropriate `src/*.cpp` file.

#### Build flags

- **Normal build** (`python3 scripts/build.py`): Produces the final `build/th07e.exe`. ✅ Links cleanly as of 2026-06-18.
- **Objdiff build** (`python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj`): Produces individual `.obj` files for objdiff comparison.
- **Diffbuild** (`python3 scripts/build.py --build-type=diffbuild`): Produces a build with `DIFFBUILD` defined, using extern globals instead of definitions. *(Legacy; the objdiff build with `SYMBOL_MAP` is the preferred verification path.)*

#### Link-pass stubs (P0 artefact)

To make the normal build link before every owning module is reversed, three files contain zero-init / no-op definitions for cross-module symbols:

- `src/link_globals.cpp`: primitive-typed `extern "C"` globals
- `src/link_stubs.cpp`: `@Name@N` `__fastcall` and `_Name` cdecl no-op stubs
- `src/link_cpp_stubs.cpp`: th07::-namespace and global-namespace free-function/data stubs

These are registered in `scripts/configure.py` as `cxx_sources` and participate in the normal build only — they are **excluded from objdiff** (not in `objdiff.json` / `config/ghidra_ns_to_obj.csv`). As owning modules reverse, definitions move out of these files into the proper `.cpp`.

### Honesty rules (pure typed C++, th06-aligned)

This project follows a strict "honest reconstruction" standard — objdiff verifies exactly the code that runs, and that code is pure typed C++ (no address hacking). The th06 reference reaches ~97% match this way, proving raw addresses are both unnecessary and harmful (they prevent the code from ever running correctly). See [`AGENTS.md`](AGENTS.md) §2 for the full rules. In short:

- ✅ Typed C++ member/global access: `g_Supervisor.curState`, `g_GameManager.arcadeRegionSize.x`
- ✅ String literals (`"th07.cfg"`), float literals (`256.0f`, `ZUN_PI`)
- ✅ One C++ code path for both objdiff and normal builds
- ✅ `DIFFABLE_*` macros for global-variable definitions only (inherited from th06)
- ✅ `#pragma var_order`, raw-offset field access, intrinsics, de-cache
- ❌ Raw absolute addresses (`(*(T*)0xADDR)`, `(char*)0x496fe0`, `(void*)0x4ba0d8`) — zero exceptions, D3D device included
- ❌ Raw-offset buffer indexing (`raw[0x978]`, `SCORE_SUB_I32(off)` macros) — use typed accessors or named struct members
- ❌ Function-level `#ifdef DIFFBUILD` splits, inline asm, `__declspec(naked)`, DAT_ const-slot externs, `nullptr`

### Compiler

This project uses MSVC 7.0 (Visual Studio .NET 2002) with the following flags:

```
/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy
```

Note: Unlike th06, th07 does NOT use `/G5`, `/Op`, or `/GS`.

## Credits

- The [th06 decompilation project](https://github.com/happyhavoc/th06) for providing the foundation, tooling, and reference implementation.
- @EstexNT for the [`var_order` pragma](scripts/pragma_var_order.cpp) ported to MSVC7.
