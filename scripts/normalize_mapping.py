#!/usr/bin/env python3
"""Build the canonical th07 config/mapping.csv from two sources:

1. The existing hand-curated config/mapping.csv (~273 rows with full calling-
   convention / return-type / parameter typing for the game code).
2. A raw ghidra export (/tmp/th07_all_functions_raw.csv by default; ~1561 rows
   covering every function in th07.exe with name/address/size and best-effort
   typing for the CRT/D3DX library functions, but `unknown` / `u8` for game
   code).

Merge rules:
- Existing rows WIN: their cc/return/params are the curated truth. We only
  re-format them (strip param names, normalize the address, collapse col4
  qualifiers, ensure this-pointer is listed).
- New addresses (in raw export but not in existing) are appended using the
  raw export's data as-is (after format normalization).

Format normalization (the four th06 conventions, applied to every row):

  1. Address: lowercase, no leading-zero padding.  `0x004363e0` -> `0x4363e0`.
  2. col4 is a single calling-convention token. Compound qualifiers like
     `__fastcall static` or `__thiscall virtual` get the qualifier moved into
     col5 (the modifier column); col5 already holding `varargs` is preserved by
     joining with a space.
  3. this-pointer rule: for `__thiscall` and `__fastcall`, ensure the first
     parameter is the receiver pointer. If Ghidra omitted it, synthesise a
     `void*` so the row is informative (mirrors th06's always-list-this rule).
  4. Parameter columns are type-only: any embedded parameter names (tokens
     after the type within a single column, e.g. `u16 val`, `MidiOutput *this`,
     `u32 idx;char *path`) are stripped to just the type.

The result is written to the --output path. With --in-place, it overwrites
config/mapping.csv directly. Always prints a summary.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_EXISTING = REPO / "config" / "mapping.csv"
DEFAULT_RAW = Path("/tmp/th07_all_functions_raw.csv")
DEFAULT_OUT = REPO / "config" / "mapping.csv"


# ----------------------------------------------------------------------------
# Format normalizers
# ----------------------------------------------------------------------------

ADDR_RE = re.compile(r"^0x0*([0-9a-fA-F]+)$")


def norm_addr(raw: str) -> str:
    """`0x004363e0` / `0x431870` -> `0x431870` (lowercase, no padding)."""
    s = raw.strip()
    m = ADDR_RE.match(s)
    if not m:
        # Already malformed; leave alone but lowercase.
        return s.lower()
    return "0x" + m.group(1).lower()


# Calling-convention tokens we recognise as the sole content of col4.
PLAIN_CC = {
    "__thiscall",
    "__stdcall",
    "__fastcall",
    "__cdecl",
    "default",
    "unknown",
}


def split_cc_modifier(col4: str, col5: str) -> tuple[str, str]:
    """Return (cc, modifier) with any stray qualifier moved out of col4.

    Ghidra occasionally emits `__fastcall static` or `__thiscall virtual` in
    the calling-convention column. th06 keeps col4 a single token; the
    qualifier goes into the modifier column.
    """
    cc = col4.strip()
    modifier = col5.strip()
    if cc not in PLAIN_CC:
        parts = cc.split()
        if parts and parts[0] in PLAIN_CC:
            extra = " ".join(parts[1:]).strip()
            cc = parts[0]
            if extra:
                modifier = (modifier + " " + extra).strip() if modifier else extra
        # else: leave cc as-is (defensive; should not happen for th07)
    return cc, modifier


# Token that looks like a known type (used to decide whether the leading word
# in a parameter cell is a type or an embedded name).
TYPE_TOKEN_RE = re.compile(
    r"^(u8|u16|u32|i8|i16|i32|f32|f64|void|char|short|int|long|bool|"
    r"unsigned|ZunResult|ZunBool|ZunColor|HRESULT|DWORD|HANDLE|HWND|HINSTANCE|"
    r"HMODULE|LPVOID|LPCSTR|LPSTR|BYTE|ULONG|BOOL|f32|LPMIDIHDR|WAVEFORMATEX|"
    r"GUID|i16|u32)\b"
)


# Identifier character class for parameter-name detection.
_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Leading type-qualifier words that need the next token to form the base type.
_QUALIFIERS = {"unsigned", "const", "static", "volatile", "long", "short",
               "signed", "struct", "class", "enum"}


def strip_param_name(cell: str) -> str:
    """Reduce a parameter cell to its type only.

    Handles all four forms found in th07's mapping data:
      `u16 val`              -> `u16`           (space-separated name)
      `MidiOutput *this`     -> `MidiOutput*`   (pointer + name)
      `MidiTrack*track`      -> `MidiTrack*`    (no space, name glued to *)
      `u8 **curTrackDataCursor` -> `u8**`       (double pointer + name)
      `LPMIDIHDR pmh`        -> `LPMIDIHDR`     (typedef + name)
      `D3DXVECTOR3*`         -> `D3DXVECTOR3*`  (unchanged, no name)
      `u32 idx;char *path`   -> `u32`           (glitch multi-param cell)
      `i32`                  -> `i32`           (unchanged)
    """
    s = cell.strip()
    if not s:
        return s
    # Drop anything after a ';' (multi-param glitch cell) - keep the type of
    # the first. Such rows are rare and were already reviewed in mapping.csv.
    if ";" in s:
        s = s.split(";", 1)[0].strip()

    # Step 1: separate the leading type group from a trailing identifier that
    # is glued directly to '*' with no space. e.g. `MidiTrack*track`,
    # `u8**curTrackDataCursor`. We split on '*' and inspect the last non-empty
    # piece: if it's a bare identifier, it's the parameter name.
    #
    # Walk left-to-right accumulating type tokens, but split glued `*name`
    # tails.
    #
    # Implementation: tokenise on whitespace first, then for each token strip
    # a trailing identifier if the token contains '*'.
    tokens = s.split()
    if not tokens:
        return s

    type_pieces: list[str] = []
    for tok in tokens:
        # If the token is a bare identifier AND we already have a type, it's a
        # parameter name (e.g. `u16 val`).
        if _IDENT_RE.match(tok) and type_pieces:
            # Could be either a name OR a continuation of a multi-word type
            # (e.g. `unsigned int`). Multi-word types start with a qualifier.
            if type_pieces[-1] in _QUALIFIERS:
                type_pieces.append(tok)
                continue
            # Otherwise this is the parameter name - stop here.
            break
        # Token contains '*' - it's a pointer type, possibly with a glued name
        # on the right (e.g. `MidiTrack*track`, `u8**`, `char*path`).
        if "*" in tok:
            # Split off any trailing identifier after the last '*'.
            star_idx = tok.rfind("*")
            head = tok[: star_idx + 1]      # includes the '*'
            tail = tok[star_idx + 1:]
            if tail and _IDENT_RE.match(tail):
                # Trailing identifier is the parameter name; drop it.
                tok = head
            type_pieces.append(tok)
            continue
        # Plain token - it's part of the type (a primitive or typedef name).
        type_pieces.append(tok)

    return "".join(type_pieces)


def normalize_row(row: list[str]) -> list[str]:
    """Apply the four th06 format rules to one CSV row.

    The row schema is:
      [name, address, size, calling_conv, modifier, return_type, param*]
    """
    if not row:
        return row
    # Pad to at least 6 columns so indexing is safe.
    while len(row) < 6:
        row.append("")

    name = row[0].strip()
    address = norm_addr(row[1])
    size = row[2].strip()
    cc, modifier = split_cc_modifier(row[3], row[4])
    ret = strip_param_name(row[5])
    params = [strip_param_name(c) for c in row[6:]]

    # Rule 3: ensure __thiscall / __fastcall carry a receiver pointer.
    # If the row has no parameters at all, synthesise `void*`. (Ghidra often
    # omits the implicit this; th06 always lists it.)
    if cc in ("__thiscall", "__fastcall") and not any(p.strip() for p in params):
        # Derive a receiver type from the symbol when possible: for
        # `th07::Class::Method` the receiver is `Class*`.
        recv = "void*"
        m = re.match(r"^th07::([A-Za-z_][A-Za-z0-9_]*)::", name)
        if m:
            recv = m.group(1) + "*"
        params = [recv]

    out = [name, address, size, cc, modifier, ret]
    out.extend(p for p in params)
    return out


# ----------------------------------------------------------------------------
# Merge
# ----------------------------------------------------------------------------

def load_existing(path: Path) -> dict[str, list[str]]:
    """Load existing mapping, keyed by normalized address. First row per
    address wins (existing file is hand-curated, no duplicates expected)."""
    by_addr: dict[str, list[str]] = {}
    with path.open(newline="", encoding="utf-8") as f:
        for raw_row in csv.reader(f):
            if not raw_row or all(not c.strip() for c in raw_row):
                continue
            addr = norm_addr(raw_row[1]) if len(raw_row) > 1 else ""
            if not addr:
                continue
            if addr not in by_addr:
                by_addr[addr] = normalize_row(list(raw_row))
    return by_addr


def load_raw(path: Path) -> list[list[str]]:
    rows: list[list[str]] = []
    with path.open(newline="", encoding="utf-8") as f:
        for raw_row in csv.reader(f):
            if not raw_row or all(not c.strip() for c in raw_row):
                continue
            rows.append(normalize_row(list(raw_row)))
    return rows


def merge(existing: dict[str, list[str]], raw_rows: list[list[str]]) -> list[list[str]]:
    """Merge: existing rows win; new addresses from raw are appended.

    Output order: by ascending address.
    """
    merged: dict[str, list[str]] = dict(existing)  # existing wins by construction

    for row in raw_rows:
        if len(row) < 2:
            continue
        addr = row[1]
        if addr in merged:
            # Existing curated row already covers this address. Don't clobber.
            continue
        merged[addr] = row

    # Sort by address numerically for a stable, reviewable file.
    def addr_key(row: list[str]) -> int:
        try:
            return int(row[1], 16)
        except ValueError:
            return 0

    return sorted(merged.values(), key=addr_key)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--existing", type=Path, default=DEFAULT_EXISTING,
                   help=f"Curated mapping (default: {DEFAULT_EXISTING})")
    p.add_argument("--raw", type=Path, default=DEFAULT_RAW,
                   help=f"Raw ghidra export (default: {DEFAULT_RAW})")
    p.add_argument("--output", type=Path, default=DEFAULT_OUT,
                   help=f"Output path (default: {DEFAULT_OUT})")
    p.add_argument("--in-place", action="store_true",
                   help="Alias for writing to the existing path.")
    args = p.parse_args()

    if not args.existing.exists():
        print(f"ERROR: existing mapping not found: {args.existing}", file=sys.stderr)
        return 1
    if not args.raw.exists():
        print(f"ERROR: raw export not found: {args.raw}", file=sys.stderr)
        print("  Run scripts/ghidra/ExportAllFunctions.java via analyzeHeadless "
              "first to produce it.", file=sys.stderr)
        return 1

    existing = load_existing(args.existing)
    raw_rows = load_raw(args.raw)
    merged_rows = merge(existing, raw_rows)

    out_path = args.existing if args.in_place else args.output
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        for row in merged_rows:
            w.writerow(row)

    # Summary
    raw_addrs = {r[1] for r in raw_rows if len(r) >= 2}
    new_addrs = raw_addrs - set(existing.keys())
    print(f"existing curated rows : {len(existing)}")
    print(f"raw export rows       : {len(raw_rows)}")
    print(f"new addresses added   : {len(new_addrs)}")
    print(f"total merged rows     : {len(merged_rows)}")
    print(f"written to            : {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
