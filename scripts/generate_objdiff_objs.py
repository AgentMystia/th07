import coff
import csv
from pathlib import Path
import struct
import sys

SCRIPTS_DIR = Path(__file__).parent


def demangle_msvc(sym):
    if len(sym) == 0:
        return sym

    if sym[0:1] == b"_":
        # Handle stdcall symbols first, those are simple. First remove the leading
        # underscore, then split on the last @ and remove everything that comes afterwards
        end_of_sym = sym.rfind(b"@")
        if end_of_sym == -1:
            # Not an stdcall. Check if it's a _DAT_ or _FUN_ symbol (cdecl data/func ref).
            # Strip the leading underscore to match orig delinked obj naming.
            if sym[1:5] in (b"DAT_", b"FUN_", b"PTR_"):
                return sym[1:]
            # Otherwise, don't demangle.
            return sym
        else:
            return sym[1:end_of_sym]

    if sym[0:1] != b"?":
        # Unmangled symbol?
        return sym

    # Handle CPP mangling.
    offset = 1

    # Read name. Start with special symbols
    special = None
    name = None
    if sym[offset : offset + 1] == b"?" and sym[offset + 1 : offset + 2].isdigit():
        # Special symbol.
        special = sym[offset + 1] - ord("0")
        offset += 2
    else:
        # Read a normal name.
        start_of_name = offset
        end_of_name = sym.find(b"@", offset)
        if end_of_name == -1:
            end_of_name = len(sym)
            offset = len(sym)
        else:
            offset = end_of_name + 1
        name = sym[start_of_name:end_of_name]

    # Read scope
    scope = []
    while True:
        start_of_scope = offset
        end_of_scope = sym.find(b"@", offset)
        if end_of_scope == -1:
            end_of_scope = len(sym)
            offset = len(sym)
        else:
            offset = end_of_scope + 1
        cur_scope = sym[start_of_scope:end_of_scope]
        if len(cur_scope) == 0:
            break
        scope.append(cur_scope)

    if name is not None:
        return b"::".join(scope[::-1]) + b"::" + name
    elif special == 0:
        return b"::".join(scope[::-1]) + b"::" + scope[0]
    elif special == 1:
        return b"::".join(scope[::-1]) + b"::~" + scope[0]
    else:
        return sym




# Map known typed global names to their DAT_ address equivalents.
# This makes objdiff compare them correctly against orig delinked symbols.
SYMBOL_MAP = {
    b"th07::g_Supervisor": b"DAT_00575950",
    b"th07::g_AnmManager": b"DAT_004b9e44",
    b"th07::g_Chain": b"DAT_00626218",
    b"th07::g_CurFrameInput": b"DAT_004b9e4c",
    b"th07::g_LastFrameInput": b"DAT_004b9e54",
    b"th07::g_IsEigthFrameOfHeldInput": b"DAT_004b9e5c",
    b"th07::g_NumOfFramesInputsWereHeld": b"DAT_004b9e60",
    b"th07::g_ControllerMapping": b"DAT_00575a68",  # Actually starts at a different addr
    b"th07::g_Pbg4Archive": b"DAT_00575c1c",
    b"th07::g_Pbg4ArchiveName": b"DAT_00575c14",
    b"th07::g_GameErrorContext": b"DAT_00624210",
    b"th07::g_GameManager": b"DAT_00626278",
    # EffectManager rdata float constants (defined in EffectManager.cpp as
    # extern "C" const f32). Their COFF symbols are "_g_EffectConstNNN" (the
    # leading underscore is kept by demangle_msvc since the suffix isn't DAT_/
    # FUN_/PTR_), so the SYMBOL_MAP keys use that exact form.
    b"_g_EffectConst256": b"DAT_00498a98",  # 256.0f
    b"_g_EffectConst60": b"DAT_00498a48",   # 60.0f
    b"_g_EffectConst240": b"DAT_00498b50",  # 240.0f
    # Player.cpp game-state globals + rdata float constants.
    b"_g_BombIsActive": b"DAT_004d44f8",
    b"_g_GuiSpriteFlags": b"DAT_0049fbf4",
    b"_g_Cherry": b"DAT_012fe0d0",
    b"_g_ScoreFrame": b"DAT_0062f88c",
    b"_g_GuiCounter1": b"DAT_0134d476",
    b"_g_GuiCounter2": b"DAT_013542ec",
    b"_g_GuiCounterSlots": b"DAT_0134db5a",
    b"_g_PlayerConst1p0": b"DAT_00498a54",
    b"_g_PlayerConst0p99": b"DAT_00498a70",
    b"_g_PlayerConstHalfPi": b"DAT_00498a9c",
    # Supervisor.cpp rdata float constants + game-state globals (Part C.1).
    b"_g_SupervisorC0x498a48": b"DAT_00498A48",
    b"_g_SupervisorC0x498a4c": b"DAT_00498A4C",
    b"_g_SupervisorC0x498a50": b"DAT_00498A50",
    b"_g_SupervisorC0x498a54": b"DAT_00498A54",
    b"_g_SupervisorC0x498a58": b"DAT_00498A58",
    b"_g_SupervisorC0x498a68": b"DAT_00498A68",
    b"_g_SupervisorC0x498a6c": b"DAT_00498A6C",
    b"_g_SupervisorC0x498aa0": b"DAT_00498AA0",
    b"_g_SupervisorC0x498ab8": b"DAT_00498AB8",
    b"_g_SupervisorC0x498b10": b"DAT_00498B10",
    b"_g_SupervisorC0x498b14": b"DAT_00498B14",
    b"_g_SupervisorC0x498b18": b"DAT_00498B18",
    b"_g_SupervisorC0x498b1c": b"DAT_00498B1C",
    b"_g_SupervisorC0x498b20": b"DAT_00498B20",
    b"_g_SupervisorG0x49fe20": b"DAT_0049FE20",
    b"_g_SupervisorG0x4b9e64": b"DAT_004B9E64",
    b"_g_SupervisorG0x4bdaa0": b"DAT_004BDAA0",
    b"_g_SupervisorG0x575a64": b"DAT_00575A64",
    b"_g_SupervisorG0x575bbc": b"DAT_00575BBC",
    b"_g_SupervisorG0x575bf8": b"DAT_00575BF8",
    b"_g_SupervisorG0x575c0c": b"DAT_00575C0C",
    b"_g_SupervisorG0x575c10": b"DAT_00575C10",
    b"_g_SupervisorG0x575c14": b"DAT_00575C14",
    b"_g_SupervisorG0x62f4e0": b"DAT_0062F4E0",
    b"_g_SupervisorG0x62f4e4": b"DAT_0062F4E4",
    b"_g_SupervisorG0x62f4e6": b"DAT_0062F4E6",
    b"_g_SupervisorG0x62f4e8": b"DAT_0062F4E8",
    b"_g_SupervisorG0x62f85c": b"DAT_0062F85C",
    b"_g_SupervisorG0x13542d8": b"DAT_013542D8",
    b"_g_SupervisorG0x135dfec": b"DAT_0135DFEC",
    b"_g_SupervisorG0x135e1f0": b"DAT_0135E1F0",
    b"_g_SupervisorG0x135e298": b"DAT_0135E298",
    b"_g_SupervisorG0x135e29c": b"DAT_0135E29C",
    b"_g_SupervisorG0x135e2a0": b"DAT_0135E2A0",
    b"_g_SupervisorG0x135e2a4": b"DAT_0135E2A4",
    b"_g_SupervisorG0x135dff0": b"DAT_0135DFF0",
    b"_g_SupervisorG0x135e0f0": b"DAT_0135E0F0",
    b"_g_SupervisorG0x496c0c": b"DAT_00496C0C",
    b"_g_SupervisorG0x4980c4": b"DAT_004980C4",
    b"_g_SupervisorG0x4bd994": b"DAT_004BD994",
    b"_g_GameManagerRankForceFlag": b"DAT_0062627D",
    # SoundPlayer / AsciiManager / GameManager singletons (cross-module refs).
    b"_g_SoundPlayer": b"DAT_004BA0D8",
    b"_g_AsciiManager": b"DAT_0134CE18",
    # GameManager.cpp rdata floats + game-state globals (Part C.2).
    b"_g_GameMgrG0x1347f9c": b"DAT_01347F9C",
    b"_g_GameMgrG0x1347fe4": b"DAT_01347FE4",
    b"_g_GameMgrC0x498a68": b"DAT_00498A68",
    b"_g_GameMgrC0x498a6c": b"DAT_00498A6C",
    b"_g_GameMgrC0x498a80": b"DAT_00498A80",
    b"_g_GameMgrC0x498a84": b"DAT_00498A84",
    b"_g_GameMgrC0x498a8c": b"DAT_00498A8C",
    b"_g_GameMgrC0x498b24": b"DAT_00498B24",
    b"_g_GameMgrC0x498bac": b"DAT_00498BAC",
    b"_g_GameMgrC0x498c7c": b"DAT_00498C7C",
    b"_g_GameMgrC0x498c80": b"DAT_00498C80",
    b"_g_GameMgrG0x49fe20": b"DAT_0049FE20",
    b"_g_GameMgrG0x49fe24": b"DAT_0049FE24",
    b"_g_SupervisorG0x575a18": b"DAT_00575A18",
    b"_g_SupervisorG0x575a1c": b"DAT_00575A1C",
    b"_g_SupervisorG0x575a20": b"DAT_00575A20",
    b"_g_SupervisorG0x575a24": b"DAT_00575A24",
    b"_g_SupervisorG0x575a28": b"DAT_00575A28",
    b"_g_SupervisorG0x575a2c": b"DAT_00575A2C",
    b"_g_SupervisorG0x575c08": b"DAT_00575C08",
    b"_g_GameMgrG0x62f647": b"DAT_0062F647",
    b"_g_GameMgrG0x62f64f": b"DAT_0062F64F",
    b"_g_GameMgrG0x62f884": b"DAT_0062F884",
    b"_g_GameMgrG0x9a9a80": b"DAT_009A9A80",
    b"_g_SupervisorG0x575a68": b"DAT_00575A68",
    b"_g_GameMgrG0x1347938": b"DAT_01347938",
    b"_g_GameMgrG0x497e1c": b"DAT_00497E1C",
    b"_g_GameMgrG0x497e30": b"DAT_00497E30",
    b"_g_GameMgrG0x497e58": b"DAT_00497E58",
    b"_g_GameMgrG0x497e84": b"DAT_00497E84",
    b"_g_GameMgrG0x497f4c": b"DAT_00497F4C",
    b"_g_GameMgrG0x498010": b"DAT_00498010",
    b"_g_GameMgrG0x498038": b"DAT_00498038",
    b"_g_GameMgrG0x498064": b"DAT_00498064",
    b"_g_GameMgrG0x498524": b"DAT_00498524",
    b"_g_GameMgrG0x4986b4": b"DAT_004986B4",
    b"_g_GameMgrG0x49f560": b"DAT_0049F560",
    b"_g_GameMgrG0x49f588": b"DAT_0049F588",
    b"_g_GameMgrG0x49f58c": b"DAT_0049F58C",
    b"_g_GameMgrG0x49fbf0": b"DAT_0049FBF0",
    b"_g_GameMgrG0x62f50c": b"DAT_0062F50C",
    b"_g_GameMgrG0x62f510": b"DAT_0062F510",
    b"_g_GameMgrG0x62f528": b"DAT_0062F528",
    b"_g_GameMgrG0x62f534": b"DAT_0062F534",
    b"_g_GameMgrG0x62f614": b"DAT_0062F614",
    b"_g_GameMgrG0x62f618": b"DAT_0062F618",
    b"_g_GameMgrG0x62f630": b"DAT_0062F630",
    b"_g_GameMgrG0x62f63c": b"DAT_0062F63C",
    b"_g_GameMgrG0x62f654": b"DAT_0062F654",
    # BombData.cpp rdata floats + game-state globals (Part C.3).
    b"_g_BombC0x498a64": b"DAT_00498A64",
    b"_g_BombC0x498a70": b"DAT_00498A70",
    b"_g_BombC0x498a8c": b"DAT_00498A8C",
    b"_g_BombC0x498b08": b"DAT_00498B08",
    b"_g_BombC0x498b2c": b"DAT_00498B2C",
    b"_g_BombC0x498b54": b"DAT_00498B54",
    b"_g_BombC0x498c68": b"DAT_00498C68",
    b"_g_BombC0x498c74": b"DAT_00498C74",
    b"_g_BombC0x498c7c": b"DAT_00498C7C",
    b"_g_BombC0x498c80": b"DAT_00498C80",
    b"_g_BombC0x498ce4": b"DAT_00498CE4",
    b"_g_BombC0x498ce8": b"DAT_00498CE8",
    b"_g_BombC0x498768": b"DAT_00498768",
    b"_g_BombG0x62f864": b"DAT_0062F864",
    b"_g_BombG0x62f868": b"DAT_0062F868",
    b"_g_BombDataAnmMgr": b"DAT_004B9E44",
    # ScreenEffect/AsciiManager/EffectManager typed globals (Parts C.4-C.6).
    b"_g_ScreenEffectRng": b"DAT_0049FE20",
    b"_g_ScreenEffectG0x62627c": b"DAT_0062627C",
    b"_g_AsciiIsInGameMenu": b"DAT_0062F64C",
    b"_g_AsciiIsInRetryMenu": b"DAT_0062F64D",
    b"_g_GameManagerStatusBitfield": b"DAT_0062F648",
    b"_g_AsciiPopupOffsetX": b"DAT_0062F864",
    b"_g_AsciiPopupOffsetY": b"DAT_0062F868",
    b"_g_AsciiAnmMgr": b"DAT_004B9E44",
    # AsciiManager/EffectManager final rdata + state globals (Part C.7).
    b"_g_AsciiC0x498a50": b"DAT_00498A50",
    b"_g_AsciiC0x498a80": b"DAT_00498A80",
    b"_g_EffectMgrPlayModeA": b"DAT_00575A8C",
}

def map_symbol(sym):
    """Map known typed global names to DAT_ address equivalents."""
    return SYMBOL_MAP.get(sym, sym)

def sym_prefix(full_sym, prefix):
    return full_sym == prefix or full_sym.startswith(prefix + b"::")


def rename_symbols(filename):
    reimpl_folder = SCRIPTS_DIR.parent / "build" / "objdiff" / "reimpl"
    orig_folder = SCRIPTS_DIR.parent / "build" / "objdiff" / "orig"
    config_folder = SCRIPTS_DIR.parent / "config"

    ns_to_obj = {}

    with open(str(config_folder / "ghidra_ns_to_obj.csv")) as f:
        ghidra_ns_to_obj = csv.reader(f)
        for vals in ghidra_ns_to_obj:
            ns_to_obj[vals[0]] = vals[1:]

    obj = coff.ObjectModule()
    with open(str(filename), "rb") as f:
        obj.unpack(f.read(), 0)

    # Demangle ALL symbols: function definitions (filtered by namespace) AND
    # cross-module CALL references (demangled to match orig delinked obj names).
    seen = {}
    for sym_obj in obj.symbols:
        sym = sym_obj.get_name(obj.string_table)
        if seen.get(sym, False):
            continue
        seen[sym] = True

        demangled_sym = demangle_msvc(sym)
        
        # Check if this symbol belongs to this module's namespace
        in_namespace = any(
            sym_prefix(demangled_sym, val.encode("utf8"))
            for val in ns_to_obj[filename.stem]
        )
        
        if in_namespace:
            # This is a function definition in our module -- rename it
            offset = obj.string_table.append(demangled_sym)
            sym_obj.name = b"\0\0\0\0" + struct.pack("I", offset)
        elif sym != demangled_sym or demangled_sym in SYMBOL_MAP:
            # This is a mangled/mapped cross-module reference -- demangle+map it
            mapped = map_symbol(demangled_sym)
            offset = obj.string_table.append(mapped)
            sym_obj.name = b"\0\0\0\0" + struct.pack("I", offset)

    if not reimpl_folder.exists():
        reimpl_folder.mkdir(parents=True, exist_ok=True)
        orig_folder.mkdir(parents=True, exist_ok=True)

    with open(str(reimpl_folder / filename.name), "wb") as f:
        f.write(obj.get_buffer())


if __name__ == "__main__":
    rename_symbols(Path(sys.argv[1]))
