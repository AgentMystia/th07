// BulletData.hpp
//
// Static player-fire bullet data tables for th07.
//
// IMPORTANT (low-confidence module):
//   * config/globals.csv lists addresses for g_CharacterPowerBulletDataReimuARank1
//     etc. that were PORTED VERBATIM FROM th06 (verified: 0x004767a0 resolves to the
//     th06 ReimuA Rank1 entry, not to any th07 data). These addresses land inside
//     th07's .text segment and must NOT be trusted for th07.
//   * Ghidra confirms th07's per-rank bullet record stride is 0x54 bytes (see
//     FUN_00476785 body, `piVar9 = piVar9 + 0x15`), NOT th06's 0x24. This means the
//     th07 CharacterPowerBulletData layout is different from th06's.
//   * The actual th07 table addresses and field layout require a full reverse of
//     the th07 Player module, which is out of scope for this pass.
//
// Until the th07 layout is recovered, this header mirrors th06's struct shapes so
// that the rest of the codebase compiles. Values in BulletData.cpp are PLACEHOLDER
// (zeroed) and are EXPECTED to fail objdiff; they exist only to keep the namespace
// symbol `th07::BulletData` resolvable for the ExportDelinker pipeline.

#pragma once

#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "ZunMath.hpp"

namespace th07
{
namespace BulletData
{
// Mirrors th06::CharacterPowerBulletData. NOTE: th07 uses a 0x54-byte record (see
// FUN_00476785 stride); the field set below is th06's and is kept only so the
// namespace resolves. Replace once the th07 layout is recovered from the binary.
struct CharacterPowerBulletData
{
    i16 waitBetweenBullets;
    i16 bulletFrame;
    ZunVec2 motion;
    ZunVec2 size;
    f32 direction;
    f32 velocity;
    u16 damage;
    u8 spawnPositionIdx;
    u8 bulletType;
    i16 anmFileIdx;
    i16 bulletSoundIdx;
};
ZUN_ASSERT_SIZE(CharacterPowerBulletData, 0x24);

struct CharacterPowerData
{
    i32 numBullets;
    i32 power;
    CharacterPowerBulletData *bullets;
};
ZUN_ASSERT_SIZE(CharacterPowerData, 0xc);

DIFFABLE_EXTERN(CharacterPowerData, g_CharacterPowerDataReimuA[9]);
DIFFABLE_EXTERN(CharacterPowerData, g_CharacterPowerDataReimuB[9]);
DIFFABLE_EXTERN(CharacterPowerData, g_CharacterPowerDataMarisaA[9]);
DIFFABLE_EXTERN(CharacterPowerData, g_CharacterPowerDataMarisaB[9]);
} // namespace BulletData
} // namespace th07
