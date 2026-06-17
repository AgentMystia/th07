# 東方妖々夢　～ Perfect Cherry Blossom

This project aims to perfectly reconstruct the source code of [Touhou Youyoumu ~ Perfect Cherry Blossom v1.00b](https://en.touhouwiki.net/wiki/Perfect_Cherry_Blossom) by Team Shanghai Alice.

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
- **Diffbuild** (`python3 scripts/build.py --build-type=diffbuild`): Produces a build with `DIFFBUILD` defined, using extern globals instead of definitions.

### Compiler

This project uses MSVC 7.0 (Visual Studio .NET 2002) with the following flags:

```
/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy
```

Note: Unlike th06, th07 does NOT use `/G5`, `/Op`, or `/GS`.

## Credits

- The [th06 decompilation project](https://github.com/happyhavoc/th06) for providing the foundation, tooling, and reference implementation.
- @EstexNT for the [`var_order` pragma](scripts/pragma_var_order.cpp) ported to MSVC7.
