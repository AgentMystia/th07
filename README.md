# 東方妖々夢　～ Perfect Cherry Blossom

This project aims to perfectly reconstruct the source code of [Touhou Youyoumu ~ Perfect Cherry Blossom v1.00b](https://en.touhouwiki.net/wiki/Perfect_Cherry_Blossom) by Team Shanghai Alice.

The ultimate acceptance criterion is that the compiled product's **game behavior matches the original `th07.exe`**. [`objdiff`](https://github.com/encounter/objdiff) is our verification tool: it measures how faithfully our reconstruction matches the original at the byte level (match%).

## Progress

Two complementary metrics track how close we are to a complete, behavior-identical reconstruction.

### Overall completion: 228 / 1721 functions under objdiff verification

Of the original exe's **1721 functions**, **228** are currently implemented enough to be objdiff-verified (the rest are either stubbed or not yet reversed). Among those 228:

```
Functions under objdiff verification        228 / 1721  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  13.2 %
```

### Match quality across tracked modules (mean 75.81%)

Of the 23 modules currently tracked, here is the match% tier breakdown. A module is "done" at ≥90%.

```
Modules at >=90% match (core done)          10  ██████████████████████████████████████████  43.5 %
Modules at 80–90% match (near done)          4  ███████████████████                        17.4 %
Modules at 50–80% match (in progress)        4  ███████████████████                        17.4 %
Modules at <50% match (early / blocked)      5  ██████████████████████                     21.7 %
```

### Match quality across the 228 tracked functions

```
Functions in modules >=90%                  80  ████████████████████████████████████        35.1 %
Functions in modules 80–90%                 45  ████████████████████                        19.7 %
Functions in modules 50–80%                 49  █████████████████████                       21.5 %
Functions in modules <50%                   54  ███████████████████████                     23.7 %
```

> The "tracked functions" denominator (228) is much smaller than the target (1721) because many modules are not yet reversed at all. As more modules land, both the completion bar and the match-quality bars grow together. The detailed per-module breakdown lives in [`PROGRESS.md`](PROGRESS.md).

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

This will automatically generate a ninja build script `build.ninja`, and run ninja on it.

## Contributing

### Reimplementation

The easiest way to work on the reimplementation is through the use of [`objdiff`](https://github.com/encounter/objdiff).

1. Follow the instructions above to set up the devenv.
1. Copy the original `東方妖々夢.exe` to `th07/th07.exe`.
1. Run `python3 scripts/export_ghidra_objs.py --import-csv` to extract object files from the original exe for objdiff comparison.
1. Run objdiff and open the th07 project (`objdiff.json`).

#### Choosing a function to decompile

Look at the `config/stubbed.csv` files for functions that still need to be implemented. Use Ghidra to reverse-engineer the original binary, then implement the function in C++ in the appropriate `src/*.cpp` file.

#### Build flags

- **Normal build** (`python3 scripts/build.py`): Produces the final `th07e.exe`.
- **Objdiff build** (`python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj`): Produces individual `.obj` files for objdiff comparison.
- **Diffbuild** (`python3 scripts/build.py --build-type=diffbuild`): Produces a build with `DIFFBUILD` defined, using extern globals instead of definitions. *(Legacy; the objdiff build with `SYMBOL_MAP` is the preferred verification path.)*

### Honesty rules (single code path)

This project follows a strict "honest reconstruction" standard — objdiff verifies exactly the code that runs. See [`AGENTS.md`](AGENTS.md) §2 for the full rules. In short:

- ✅ One C++ code path for both objdiff and normal builds
- ✅ `DIFFABLE_*` macros for global-variable definitions only (inherited from th06)
- ✅ `#pragma var_order`, raw-offset field access, intrinsics, de-cache
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
