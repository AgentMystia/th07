// th07 BombData header.
//
// IMPORTANT — this module is a STRUCTURAL PLACEHOLDER, not a 1:1 port of the
// th06 BombData. The reverse-engineering of the player-side bomb system in
// th07.exe revealed that the bomb dispatch table grew from th06's 4 entries
// (ReimuA/B, MarisaA/B) to 12 entries. The 12 entries most likely cover the
// 6 player shot-types of PCB (ReimuA/B, MarisaA/B, SakuyaA/B) plus a second
// variant per shot-type (e.g. a low/high power form). Without the full
// Player struct (Player.hpp is not yet ported to th07) and a per-entry
// semantic identification, the function bodies cannot be matched to canonical
// names with high confidence.
//
// What IS verified from th07.exe (ghidra real reads):
//   - g_BombData lives at 0x0049ec50 (NOT 0x00476708 as in config/globals.csv;
//     that address points into .text and is a stale PDB-derived label).
//   - The table has 12 entries, each 8 bytes = {calc: void(*)(Player*),
//     draw: void(*)(Player*)}. Total table size 0x60 bytes.
//   - All 12 calc callbacks share the prologue pattern
//       if (player->bombTimer >= player->bombDuration) {
//           EndPlayerSpellcard(); player->bombIsInUse = 0; return;
//       }
//     with EndPlayerSpellcard = FUN_00427b21 and the timer fields at
//     player+0x16a38 (current), player+0x16a28 (duration), player+0x16a30
//     (previous, for HasTicked()), player+0x16a20 (isInUse).
//   - DarkenViewport is NOT a standalone function in th07; its body is inlined
//     into the bomb dispatcher FUN_00406de0 (verified by the unique viewport
//     constants 32.0f/16.0f/416.0f/464.0f at 0x406e52-0x406e67). FUN_00406de0
//     is the registered bomb-draw chain callback (stored via
//     _DAT_0134cdb8 = FUN_00406de0 in FUN_004074c0 Player registration).
//
// Below we declare the entry layout and the 12 anonymous calc/draw slots so
// the symbol table is correct, but the C++ bodies are deliberately left as
// thin stubs that call into yet-unported helpers. objdiff matching of these
// bodies MUST wait for Player.hpp to be ported first.

#pragma once

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// Forward declaration — the real Player struct lives in Player.hpp (not yet
// ported to th07). All bomb callbacks take Player* via __fastcall (ECX).
struct Player;

// Bomb dispatch table entry. Layout matches the 8-byte rows in g_BombData at
// 0x0049ec50: first dword is the calc callback, second is the draw callback.
struct BombData
{
    void (*calc)(Player *player);
    void (*draw)(Player *player);
};
DIFFABLE_EXTERN_ARRAY(BombData, 12, g_BombData);
}; // namespace th07
