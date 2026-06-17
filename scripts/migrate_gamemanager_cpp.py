#!/usr/bin/env python3
"""One-shot migration of src/GameManager.cpp from raw-address access to typed C++.

Same pattern as migrate_supervisor_cpp.py. Replaces every *(T*)0xADDR form
with its typed C++ equivalent per AGENTS.md §2.
"""
from __future__ import annotations
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src" / "GameManager.cpp"

# Address -> (replacement, kind, mangled_or_None)
ADDR_MAP = {
    # ---- rdata float constants (0x00498xxx) ----
    '0x498a68': ('g_GameMgrC0x498a68', 'extern', '_g_GameMgrC0x498a68'),
    '0x498a6c': ('g_GameMgrC0x498a6c', 'extern', '_g_GameMgrC0x498a6c'),
    '0x498a80': ('g_GameMgrC0x498a80', 'extern', '_g_GameMgrC0x498a80'),
    '0x498a84': ('g_GameMgrC0x498a84', 'extern', '_g_GameMgrC0x498a84'),
    '0x498a8c': ('g_GameMgrC0x498a8c', 'extern', '_g_GameMgrC0x498a8c'),
    '0x498b24': ('g_GameMgrC0x498b24', 'extern', '_g_GameMgrC0x498b24'),
    '0x498bac': ('g_GameMgrC0x498bac', 'extern', '_g_GameMgrC0x498bac'),
    '0x498c7c': ('g_GameMgrC0x498c7c', 'extern', '_g_GameMgrC0x498c7c'),
    '0x498c80': ('g_GameMgrC0x498c80', 'extern', '_g_GameMgrC0x498c80'),
    # ---- input state ----
    '0x4b9e4c': ('g_LastFrameInput', 'extern', None),  # already typed (GameManager.hpp)
    '0x4b9e54': ('g_CurFrameInput', 'extern', None),
    '0x4b9e44': ('g_AnmManager', 'extern', None),  # AnmManager* global (now typed via AnmManager.hpp)
    # ---- rng state @ 0x49fe20 ----
    '0x49fe20': ('g_GameMgrG0x49fe20', 'extern', '_g_GameMgrG0x49fe20'),
    '0x49fe24': ('g_GameMgrG0x49fe24', 'extern', '_g_GameMgrG0x49fe24'),
    # ---- g_Supervisor (0x00575xxx) ----
    '0x575948': ('g_GameManager', 'extern', None),  # GameManager* @ 0x575948 (yes, this addr holds GameManager*)
    '0x575950': ('g_Supervisor', 'sup_member', None),
    '0x575958': ('g_Supervisor.d3dDevice', 'sup_member', None),
    '0x575a18': ('g_SupervisorG0x575a18', 'extern', '_g_SupervisorG0x575a18'),
    '0x575a1c': ('g_SupervisorG0x575a1c', 'extern', '_g_SupervisorG0x575a1c'),
    '0x575a20': ('g_SupervisorG0x575a20', 'extern', '_g_SupervisorG0x575a20'),
    '0x575a24': ('g_SupervisorG0x575a24', 'extern', '_g_SupervisorG0x575a24'),
    '0x575a28': ('g_SupervisorG0x575a28', 'extern', '_g_SupervisorG0x575a28'),
    '0x575a2c': ('g_SupervisorG0x575a2c', 'extern', '_g_SupervisorG0x575a2c'),
    '0x575a87': ('g_Supervisor.cfg.musicMode', 'sup_cfg', None),
    '0x575aa8': ('g_Supervisor.curState', 'sup_member', None),
    '0x575ac8': ('g_Supervisor.framerateMultiplier', 'sup_member', None),
    '0x575acc': ('g_Supervisor.midiOutput', 'sup_member', None),
    '0x575ad0': ('g_Supervisor.d3dDeviceCaps180', 'sup_member', None),
    '0x575ad4': ('g_Supervisor.d3dDeviceCaps184', 'sup_member', None),
    '0x575ae4': ('g_Supervisor.lastFrameTime', 'sup_member', None),
    '0x575c08': ('g_SupervisorG0x575c08', 'extern', '_g_SupervisorG0x575c08'),
    # ---- g_GameManager (0x00626xxx) ----
    '0x62627d': ('g_GameManagerRankForceFlag', 'extern', '_g_GameManagerRankForceFlag'),
    '0x626270': ('g_GameManager', 'gamemgr', None),
    '0x626280': ('g_GameManager.difficulty', 'gamemgr', None),
    # ---- other game-state globals ----
    '0x62f647': ('g_GameMgrG0x62f647', 'extern', '_g_GameMgrG0x62f647'),  # CharacterShotType
    '0x62f648': ('g_GameManager.statusBitfield', 'gamemgr', None),
    '0x62f64f': ('g_GameMgrG0x62f64f', 'extern', '_g_GameMgrG0x62f64f'),
    '0x62f858': ('g_GameManager.frameCounter', 'gamemgr', None),
    '0x62f884': ('g_GameMgrG0x62f884', 'extern', '_g_GameMgrG0x62f884'),  # checksum accumulator
    '0x9a9a80': ('g_GameMgrG0x9a9a80', 'extern', '_g_GameMgrG0x9a9a80'),  # difficulty threshold
    # ---- effect/score state ----
    '0x1347f9c': ('g_GameMgrG0x1347f9c', 'extern', '_g_GameMgrG0x1347f9c'),
    '0x1347fe4': ('g_GameMgrG0x1347fe4', 'extern', '_g_GameMgrG0x1347fe4'),
}

# GameManager this-offset -> member name (from GameManager.hpp)
# gameManager is GameManager*, offset is from base.
GM_THIS_OFFSETS = {
    0x0c: 'flag0c',
    0x0d: 'flag0c',  # +0xd is inside flag0c i32; alias
    0x10: 'difficulty',
    0x14: 'difficultyMask',
    0x8454: 'unk_18',  # extend records inside the opaque pad
    0x845a: 'unk_18',
    0x93d4: 'unk_93dc',  # the byte region +0x93dc (named u8 unk_93dc/93dd/93de[])
    0x93d5: 'unk_93dc',
    0x93d6: 'unk_93dc',
    0x93d7: 'unk_93dc',
    0x93dc: 'unk_93dc',
    0x93dd: 'unk_93dd',
    0x93de: 'unk_93de',  # 2-byte array
}

CAST_TYPES = r'(?:i8|i16|i32|u8|u16|u32|f32|void|char|short|long|unsigned|' \
             r'IDirect3D\w+|D3D\w+|SoundPlayer|MidiOutput|Supervisor|HRESULT|DWORD|HANDLE|HWND|' \
             r'LPCSTR|LPSTR|LPVOID|BYTE|BOOL|D3DDeviceStub|ChainElem|GameManager|ScoreSub)'


def infer_type(cast_match: str) -> str:
    s = cast_match
    if 'f32' in s or 'float' in s: return 'f32'
    if 'i32' in s or 'int ' in s or 'int)' in s: return 'i32'
    if 'u32' in s or 'uint' in s or 'DWORD' in s: return 'u32'
    if 'u16' in s or 'ushort' in s: return 'u16'
    if 'i16' in s or 'short' in s: return 'i16'
    if 'u8' in s or 'byte' in s or 'BYTE' in s: return 'u8'
    if 'i8' in s: return 'i8'
    if 'void **' in s or 'void*' in s or 'void *' in s: return 'void*'
    if 'IDirect3D' in s or 'D3D' in s or 'D3DDeviceStub' in s: return 'void*'
    if 'SoundPlayer' in s or 'MidiOutput' in s or 'Supervisor' in s: return 'void*'
    if 'GameManager' in s or 'ScoreSub' in s or 'ChainElem' in s: return 'void*'
    if 'char' in s: return 'char*'
    return 'i32'


def norm_addr(a: str) -> str:
    a = a.lower()
    if a.startswith('0x0x'): a = '0x' + a[4:]
    return a


def main():
    src = SRC.read_text()
    new_externs = {}
    unmapped = set()

    pat_deref = re.compile(
        r'(\*\s*\()\s*(' + CAST_TYPES + r'\s*\*+\s*)\)\s*'
        r'(0x0x[0-9a-fA-F]{6,8}|0x[0-9a-fA-F]{6,8})'
    )
    pat_cast = re.compile(
        r'(\(\s*)(' + CAST_TYPES + r'\s*\*+\s*)(\)\s*)'
        r'(0x0x[0-9a-fA-F]{6,8}|0x[0-9a-fA-F]{6,8})'
    )
    # (u8 *)ptr + 0xOFFSET
    pat_u8off = re.compile(
        r'\(\s*u8\s*\*\s*\)\s*(\w+)\s*\+\s*(0x[0-9a-fA-F]+)'
    )

    def replace_deref(m):
        cast_body = m.group(2)
        addr = norm_addr(m.group(3))
        if addr not in ADDR_MAP:
            unmapped.add(addr)
            return m.group(0)
        repl, kind, mangled = ADDR_MAP[addr]
        if kind == 'extern':
            ctype = infer_type(cast_body)
            if mangled and mangled not in new_externs:
                new_externs[mangled] = (addr, ctype)
            elif mangled is None and repl not in new_externs:
                # g_LastFrameInput / g_CurFrameInput / g_AnmManager etc. -- declared elsewhere
                pass
            return repl
        return repl

    def replace_cast(m):
        cast_body = m.group(2)
        addr = norm_addr(m.group(4))
        if addr not in ADDR_MAP:
            unmapped.add(addr)
            return m.group(0)
        repl, kind, mangled = ADDR_MAP[addr]
        if kind == 'extern':
            ctype = infer_type(cast_body)
            if mangled and mangled not in new_externs:
                new_externs[mangled] = (addr, ctype)
            return f'&{repl}'
        return f'({cast_body.strip()})&{repl}'

    def replace_u8off(m):
        ptr = m.group(1)
        off_str = m.group(2)
        off = int(off_str, 16)
        # Map gameManager this-offsets
        if ptr in ('gameManager', 'this', 'gm') and off in GM_THIS_OFFSETS:
            member = GM_THIS_OFFSETS[off]
            return f'(u8 *)&{ptr}->{member} /* +{off_str} */'
        unmapped.add(f'(u8*){ptr}+{off_str}')
        return m.group(0)

    src = pat_deref.sub(replace_deref, src)
    src = pat_cast.sub(replace_cast, src)
    src = pat_u8off.sub(replace_u8off, src)

    SRC.write_text(src)

    # Emit externs + SYMBOL_MAP
    out = Path('/tmp/gamemanager_new_globals.txt')
    with out.open('w') as f:
        f.write("// extern \"C\" declarations to add near top of GameManager.cpp\n")
        for mangled, (addr, ctype) in sorted(new_externs.items(), key=lambda kv: kv[1][0]):
            is_const = addr.startswith('0x498')
            cstr = 'const ' if is_const else ''
            f.write(f'extern "C" {cstr}{ctype} {mangled[1:]};  // {addr}\n')
        f.write("\n// SYMBOL_MAP entries for scripts/generate_objdiff_objs.py\n")
        for mangled, (addr, _) in sorted(new_externs.items(), key=lambda kv: kv[1][0]):
            dat = f'DAT_{addr[2:].upper().zfill(8)}'
            f.write(f'    b"{mangled}": b"{dat}",\n')

    print(f"Rewrote {SRC}")
    print(f"New externs: {len(new_externs)}")
    if unmapped:
        print(f"UNMAPPED ({len(unmapped)}):")
        for a in sorted(unmapped):
            print(f"  {a}")
    print(f"Externs+SYMBOL_MAP: {out}")


if __name__ == '__main__':
    main()
