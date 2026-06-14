// BulletData.cpp
//
// Static player-fire bullet data tables for th07.
//
// LOW-CONFIDENCE MODULE — DO NOT TRUST VALUES.
//
// Verified facts (from Ghidra reading th07.exe):
//   * config/globals.csv addresses for g_CharacterPowerBulletData* are PORTED
//     VERBATIM FROM th06 (e.g. 0x004767a0 = th06 ReimuA Rank1 entry, confirmed
//     by reading the same VA in the th06 binary). They land in th07's .text
//     segment and are NOT the th07 data addresses.
//   * th07's per-rank bullet record stride is 0x54 bytes (see FUN_00476785:
//     `piVar9 = piVar9 + 0x15;` with int* piVar9). th06's CharacterPowerBulletData
//     is 0x24 bytes, so th07's layout is different.
//   * A whole-file scan for th06-style 0x24-byte entries with non-zero size.x,
//     size.y, velocity returns ZERO matches. The th07 bullet data therefore does
//     not share th06's field layout.
//
// Consequence: the actual th07 tables and their field layout cannot be produced
// without a dedicated reverse of the th07 Player module (structure recovery +
// table-address discovery). This file is a COMPILATION-ONLY SKELETON so that the
// `th07::BulletData` namespace exists for the objdiff/ExportDelinker pipeline.
// All values below are PLACEHOLDER ZEROES and will not match the original binary.

#include "BulletData.hpp"

namespace th07
{
namespace BulletData
{
// PLACEHOLDER. The th07 layout (0x54-byte records, unknown field set) and the real
// table addresses are unknown as of this pass. These zero-initialised arrays keep
// the symbols resolvable; they MUST be regenerated from the binary before this
// module can pass objdiff.

DIFFABLE_STATIC_ARRAY_ASSIGN(CharacterPowerBulletData, 1, g_CharacterPowerBulletDataReimuARank1) = {
    {0, 0, {0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f, 0.0f, 0, 0, 0, 0, 0},
};

DIFFABLE_STATIC_ARRAY_ASSIGN(CharacterPowerData, 9, g_CharacterPowerDataReimuA) = {
    {0, 0, g_CharacterPowerBulletDataReimuARank1},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
    {0, 0, nullptr},
};

// ReimuB / MarisaA / MarisaB placeholder tables omitted for brevity; add them
// once the real th07 layout is recovered.
DIFFABLE_STATIC_ARRAY_ASSIGN(CharacterPowerData, 9, g_CharacterPowerDataReimuB) = {
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
};

DIFFABLE_STATIC_ARRAY_ASSIGN(CharacterPowerData, 9, g_CharacterPowerDataMarisaA) = {
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
};

DIFFABLE_STATIC_ARRAY_ASSIGN(CharacterPowerData, 9, g_CharacterPowerDataMarisaB) = {
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
    {0, 0, nullptr}, {0, 0, nullptr}, {0, 0, nullptr},
};
} // namespace BulletData
} // namespace th07
