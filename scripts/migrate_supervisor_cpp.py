#!/usr/bin/env python3
"""One-shot migration of src/Supervisor.cpp from raw-address access to typed C++.

Replaces every `*(T*)0xADDR` / `(T*)0xADDR` / `(u8*)this + OFF` form with its
typed C++ equivalent per AGENTS.md §2:

  * g_Supervisor member offsets    -> g_Supervisor.<member> / g_Supervisor.cfg.<opt>
  * g_SoundPlayer instance/fields  -> g_SoundPlayer.<member>
  * g_GameManager fields           -> g_GameManager.<member>
  * rdata float constants          -> extern "C" const f32 g_SupervisorC<addr>
  * other game-state globals       -> extern "C" <T> g_SupervisorG<addr>

The extern "C" declarations and SYMBOL_MAP entries are emitted to a separate
file (/tmp/supervisor_new_globals.txt) for review before being merged into
Player.cpp's existing extern block + scripts/generate_objdiff_objs.py.
"""
from __future__ import annotations
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src" / "Supervisor.cpp"


# Address -> (replacement_expression, kind, mangled_global_name_or_None)
# kind in {'sup_member','sup_cfg','soundplayer','gamemgr','extern'}
# For 'extern' rows, mangled_global_name is the C symbol to declare+SYMBOL_MAP.
ADDR_MAP = {
    # ---- rdata float constants (0x00498xxx) -> extern const f32 ----
    '0x00498a48': ('g_SupervisorC0x498a48', 'extern', '_g_SupervisorC0x498a48'),
    '0x00498a4c': ('g_SupervisorC0x498a4c', 'extern', '_g_SupervisorC0x498a4c'),
    '0x00498a50': ('g_SupervisorC0x498a50', 'extern', '_g_SupervisorC0x498a50'),
    '0x00498a54': ('g_SupervisorC0x498a54', 'extern', '_g_SupervisorC0x498a54'),
    '0x00498a58': ('g_SupervisorC0x498a58', 'extern', '_g_SupervisorC0x498a58'),
    '0x00498a68': ('g_SupervisorC0x498a68', 'extern', '_g_SupervisorC0x498a68'),
    '0x00498a6c': ('g_SupervisorC0x498a6c', 'extern', '_g_SupervisorC0x498a6c'),
    '0x00498aa0': ('g_SupervisorC0x498aa0', 'extern', '_g_SupervisorC0x498aa0'),
    '0x00498ab8': ('g_SupervisorC0x498ab8', 'extern', '_g_SupervisorC0x498ab8'),
    '0x00498b10': ('g_SupervisorC0x498b10', 'extern', '_g_SupervisorC0x498b10'),
    '0x00498b14': ('g_SupervisorC0x498b14', 'extern', '_g_SupervisorC0x498b14'),
    '0x00498b18': ('g_SupervisorC0x498b18', 'extern', '_g_SupervisorC0x498b18'),
    '0x00498b1c': ('g_SupervisorC0x498b1c', 'extern', '_g_SupervisorC0x498b1c'),
    '0x00498b20': ('g_SupervisorC0x498b20', 'extern', '_g_SupervisorC0x498b20'),
    # ---- rdata string literals (0x00496c1e, 0x004980d0, 0x00497228) ----
    # These are NUL string constants; replace with string literals once content
    # is known. For now, route through extern char* with SYMBOL_MAP.
    '0x00496c1e': ('g_SupervisorStr0x496c1e', 'extern', '_g_SupervisorStr0x496c1e'),
    '0x004980d0': ('g_SupervisorStr0x4980d0', 'extern', '_g_SupervisorStr0x4980d0'),
    '0x00497228': ('g_SupervisorStr0x497228', 'extern', '_g_SupervisorStr0x497228'),
    # ---- input state globals ----
    '0x004b9e64': ('g_SupervisorG0x4b9e64', 'extern', '_g_SupervisorG0x4b9e64'),  # u32 input-crc?
    '0x0049fe20': ('g_SupervisorG0x49fe20', 'extern', '_g_SupervisorG0x49fe20'),  # u16 seedWord src
    # ---- g_SoundPlayer (0x004ba0d8) + its documented fields ----
    '0x004ba0d8': ('g_SoundPlayer', 'soundplayer', None),                # SoundPlayer instance
    '0x004bda94': ('g_SoundPlayer.backgroundMusic', 'soundplayer', None),  # CStreamingSound*
    '0x004bdaa0': ('g_SupervisorG0x4bdaa0', 'extern', '_g_SupervisorG0x4bdaa0'),  # i32 BGM state
    # ---- g_Supervisor members (0x00575xxx) ----
    '0x00575958': ('g_Supervisor.d3dDevice', 'sup_member', None),
    '0x00575a64': ('g_SupervisorG0x575a64', 'extern', '_g_SupervisorG0x575a64'),  # void* listNode sink (in presentParameters tail)
    '0x00575a8b': ('g_Supervisor.cfg.frameskipConfig', 'sup_cfg', None),
    '0x00575a9c': ('g_Supervisor.cfg.opts', 'sup_cfg', None),
    '0x00575aa8': ('g_Supervisor.curState', 'sup_member', None),
    '0x00575ab8': ('g_Supervisor.unkIsInEnding', 'sup_member', None),
    '0x00575abc': ('g_Supervisor.vsyncDisabled', 'sup_member', None),
    '0x00575ad0': ('g_Supervisor.unk1b4', 'sup_member', None),
    '0x00575ad4': ('g_Supervisor.unk1b8', 'sup_member', None),
    # Supervisor tail / d3dCaps region (not exposed as named members)
    '0x00575bbc': ('g_SupervisorG0x575bbc', 'extern', '_g_SupervisorG0x575bbc'),
    '0x00575bf8': ('g_SupervisorG0x575bf8', 'extern', '_g_SupervisorG0x575bf8'),
    '0x00575c0c': ('g_SupervisorG0x575c0c', 'extern', '_g_SupervisorG0x575c0c'),
    '0x00575c10': ('g_SupervisorG0x575c10', 'extern', '_g_SupervisorG0x575c10'),
    '0x00575c14': ('g_SupervisorG0x575c14', 'extern', '_g_SupervisorG0x575c14'),
    # ---- g_GameManager (0x00626270) fields ----
    '0x00626274': ('g_GameManager.playerSub', 'gamemgr', None),   # +0x4 PlayerSub*
    '0x00626278': ('g_GameManager.scoreSub', 'gamemgr', None),    # +0x8 ScoreSub*
    '0x00626280': ('g_GameManager.difficulty', 'gamemgr', None),  # +0x10 i32
    # ---- other game-state globals (0x0062fxxx, 0x0135xxxx) ----
    '0x0062f4e0': ('g_SupervisorG0x62f4e0', 'extern', '_g_SupervisorG0x62f4e0'),  # wav fmt table
    '0x0062f4e4': ('g_SupervisorG0x62f4e4', 'extern', '_g_SupervisorG0x62f4e4'),
    '0x0062f4e6': ('g_SupervisorG0x62f4e6', 'extern', '_g_SupervisorG0x62f4e6'),
    '0x0062f4e8': ('g_SupervisorG0x62f4e8', 'extern', '_g_SupervisorG0x62f4e8'),
    '0x0062f648': ('g_GameManager.statusBitfield', 'gamemgr', None),  # +0x93d8 GameManager.statusBitfield
    '0x0062f85c': ('g_SupervisorG0x62f85c', 'extern', '_g_SupervisorG0x62f85c'),
    '0x013542d8': ('g_SupervisorG0x13542d8', 'extern', '_g_SupervisorG0x13542d8'),
    '0x0135dfec': ('g_SupervisorG0x135dfec', 'extern', '_g_SupervisorG0x135dfec'),
    '0x0135e1f0': ('g_SupervisorG0x135e1f0', 'extern', '_g_SupervisorG0x135e1f0'),
    '0x0135e298': ('g_SupervisorG0x135e298', 'extern', '_g_SupervisorG0x135e298'),
    '0x0135e29c': ('g_SupervisorG0x135e29c', 'extern', '_g_SupervisorG0x135e29c'),
    '0x0135e2a0': ('g_SupervisorG0x135e2a0', 'extern', '_g_SupervisorG0x135e2a0'),
    '0x0135e2a4': ('g_SupervisorG0x135e2a4', 'extern', '_g_SupervisorG0x135e2a4'),
}

# Detect type by inspecting the cast in the source line.
def infer_type(cast_match: str) -> str:
    """Given the cast prefix like '*(i32 *)' or '(void **)', return C type."""
    s = cast_match
    if 'f32' in s or 'float' in s:
        return 'f32'
    if 'i32' in s or 'int ' in s or 'int)' in s:
        return 'i32'
    if 'u32' in s or 'uint' in s or 'DWORD' in s:
        return 'u32'
    if 'u16' in s or 'ushort' in s:
        return 'u16'
    if 'i16' in s or 'short' in s:
        return 'i16'
    if 'u8' in s or 'byte' in s or 'BYTE' in s:
        return 'u8'
    if 'i8' in s:
        return 'i8'
    if 'void **' in s or 'void*' in s or 'void *' in s:
        return 'void*'
    if 'IDirect3D' in s or 'D3D' in s:
        return 'void*'
    if 'SoundPlayer' in s or 'MidiOutput' in s or 'Supervisor' in s:
        return 'void*'
    if 'char' in s:
        return 'char*'
    return 'i32'  # default


def main():
    src = SRC.read_text()
    new_externs = {}  # mangled_name -> (addr, ctype)
    unmapped = set()

    # Pattern 1: *(T *)0xADDR  and  *(T *)0x0xADDR (the "0x0x" prefix glitch)
    # We match the cast type, the optional *, and the address.
    pat_deref = re.compile(
        r'(\*\s*\()\s*'
        r'((?:i8|i16|i32|u8|u16|u32|f32|void|char|short|long|unsigned|'
        r'IDirect3D\w+|D3D\w+|SoundPlayer|MidiOutput|Supervisor|HRESULT|DWORD|HANDLE|HWND|'
        r'LPCSTR|LPSTR|LPVOID|BYTE|BOOL)\s*\*+\s*)'
        r'\)\s*'
        r'(0x0x[0-9a-fA-F]{6,8}|0x[0-9a-fA-F]{6,8})'
    )
    pat_cast = re.compile(
        r'(\(\s*)'
        r'((?:i8|i16|i32|u8|u16|u32|f32|void|char|short|long|unsigned|'
        r'IDirect3D\w+|D3D\w+|SoundPlayer|MidiOutput|Supervisor|HRESULT|DWORD|HANDLE|HWND|'
        r'LPCSTR|LPSTR|LPVOID|BYTE|BOOL)\s*\*+\s*)'
        r'(\)\s*)'
        r'(0x0x[0-9a-fA-F]{6,8}|0x[0-9a-fA-F]{6,8})'
    )

    def norm_addr(a: str) -> str:
        a = a.lower()
        if a.startswith('0x0x'):
            a = '0x' + a[4:]
        return a

    def replace_deref(m):
        prefix_star_paren = m.group(1)   # "*("
        cast_body = m.group(2)           # "i32 *"
        close_paren = ')'
        addr = norm_addr(m.group(3))
        if addr not in ADDR_MAP:
            unmapped.add(addr)
            return m.group(0)
        repl, kind, mangled = ADDR_MAP[addr]
        if kind == 'extern':
            ctype = infer_type(cast_body)
            if mangled not in new_externs:
                new_externs[mangled] = (addr, ctype)
            return repl
        return repl

    def replace_cast(m):
        open_paren = m.group(1)
        cast_body = m.group(2)
        close_paren = m.group(3)
        addr = norm_addr(m.group(4))
        if addr not in ADDR_MAP:
            unmapped.add(addr)
            return m.group(0)
        repl, kind, mangled = ADDR_MAP[addr]
        if kind == 'extern':
            ctype = infer_type(cast_body)
            if mangled not in new_externs:
                new_externs[mangled] = (addr, ctype)
            return f'&{repl}'
        # For typed singletons/members, cast through the appropriate pointer
        # type so the expression stays well-formed when used as a pointer.
        return f'({cast_body.strip()})&{repl}'

    src = pat_deref.sub(replace_deref, src)
    src = pat_cast.sub(replace_cast, src)

    # Pattern 2: #define X ((T *)0xADDR)  -- macro-style defs at top of file
    pat_define = re.compile(
        r'(#define\s+(\w+)\s+\(\s*\(([^)]+)\s*\*\s*\)\s*)(0x[0-9a-fA-F]{6,8})\s*\)'
    )
    def replace_define(m):
        addr = norm_addr(m.group(4))
        if addr not in ADDR_MAP:
            unmapped.add(addr)
            return m.group(0)
        repl, kind, mangled = ADDR_MAP[addr]
        whole = m.group(0)
        name = m.group(2)
        cast_type = m.group(3).strip()
        if kind == 'extern':
            ctype = infer_type(cast_type + ' *')
            if mangled not in new_externs:
                new_externs[mangled] = (addr, ctype)
        # Rewrite the macro body to use the replacement.
        return f'#define {name} ({repl})'
    src = pat_define.sub(replace_define, src)

    SRC.write_text(src)

    # Emit the new extern declarations + SYMBOL_MAP entries
    out = Path('/tmp/supervisor_new_globals.txt')
    with out.open('w') as f:
        f.write("// === extern \"C\" declarations to add near the top of Supervisor.cpp ===\n")
        for mangled, (addr, ctype) in sorted(new_externs.items(), key=lambda kv: kv[1][0]):
            # const qualifier for rdata float constants (0x00498xxx) and string ptrs
            is_const = addr.startswith('0x00498') or addr.startswith('0x00496') or addr.startswith('0x00497')
            cstr = 'const ' if is_const else ''
            f.write(f'extern "C" {cstr}{ctype} {mangled[1:]};  // {addr}\n')
        f.write("\n// === SYMBOL_MAP entries to add to scripts/generate_objdiff_objs.py ===\n")
        for mangled, (addr, _) in sorted(new_externs.items(), key=lambda kv: kv[1][0]):
            dat = f'DAT_{addr[2:].upper().zfill(8)}'
            f.write(f'    b"{mangled}": b"{dat}",\n')

    print(f"Rewrote {SRC}")
    print(f"New extern globals: {len(new_externs)}")
    if unmapped:
        print(f"UNMAPPED addresses ({len(unmapped)}):")
        for a in sorted(unmapped):
            print(f"  {a}")
    print(f"Extern declarations + SYMBOL_MAP written to {out}")


if __name__ == '__main__':
    main()
