// th07 BombData implementation.
//
// STATUS: SKELETON ONLY. See BombData.hpp for the rationale. This file
// provides the g_BombData table initializer matching the layout in
// th07.exe at 0x0049ec50 (12 entries, calc/draw pairs). The actual
// callback bodies are NOT ported — they require Player.hpp (struct layout)
// which has not yet been reverse-engineered for th07.
//
// Verified facts (all addresses read from th07.exe via ghidra-mcp):
//
//   g_BombData table address: 0x0049ec50 (.data section)
//   Entry size: 0x08 (two function pointers)
//   Entry count: 12 (NOT 4 like th06)
//   Total table size: 0x60 bytes
//
//   Entry layout (read directly from .data at 0x0049ec50):
//     [ 0] calc=FUN_00408710 draw=FUN_00408e10  (player+0x16a4c state, 8 bombs)
//     [ 1] calc=FUN_004091b0 draw=FUN_00409990  (uses 0.3f move speed mult,
//                                                duration 300, Master Spark-like)
//     [ 2] calc=FUN_00409dd0 draw=FUN_0040a280
//     [ 3] calc=FUN_0040a3a0 draw=FUN_0040a6b0
//     [ 4] calc=FUN_0040a7c0 draw=FUN_0040aba0
//     [ 5] calc=FUN_0040af10 draw=FUN_0040b5d0
//     [ 6] calc=FUN_0040b7d0 draw=FUN_0040bca0
//     [ 7] calc=FUN_0040be20 draw=FUN_0040c160
//     [ 8] calc=FUN_0040c2e0 draw=FUN_0040c970
//     [ 9] calc=FUN_0040ca50 draw=FUN_0040d3b0
//     [10] calc=FUN_0040d4c0 draw=FUN_0040d9a0
//     [11] calc=FUN_0040da80 draw=FUN_0040e280
//
//   Player struct offsets used by the bomb callbacks (verified from
//   decompilation of FUN_00408710, FUN_004091b0, FUN_00408e10):
//     +0x16a20  bombInfo.isInUse              (i32)
//     +0x16a28  bombInfo.duration             (i32, frames)
//     +0x16a30  bombInfo.timer.previous       (i32)
//     +0x16a34  bombInfo.timer.subFrame       (i32)
//     +0x16a38  bombInfo.timer.current        (i32)
//     +0x16a4c  bombInfo.perBombState[0]      (each entry 0x1428 bytes,
//                                             8 bombs * 0x1428 stride = 0xa140)
//     +0x16a08  invulnerabilityTimer          (i32)
//     +0x23f0  verticalMoveSpeedMultDuringBomb   (f32)
//     +0x23f4  horizontalMoveSpeedMultDuringBomb (f32)
//     +0x2408  playerState                    (u8)
//     +0x930   positionCenter                 (D3DXVECTOR3)
//     +0x9dc   bombRegionPositions[0]         (D3DXVECTOR3 per bomb, 0x20 stride)
//     +0x9e8   bombRegionSizes[0].x           (f32, 0x20 stride)
//     +0x9ec   bombRegionSizes[0].y           (f32, 0x20 stride)
//     +0x9f4   bombRegionDamages[0]           (i32, 0x20 stride)
//     +0x9f8   perBombLifetimeCounter[0]      (i32, 0x20 stride)
//
//   Helper functions called by the bomb callbacks:
//     FUN_00427b21  Gui::EndPlayerSpellcard-like (sets a 2-byte flag at
//                   GameManager+0x66da=1, +0x6b72=2)
//     FUN_00433a90  Gui::ShowBombNamePortrait-like
//     FUN_0042868d  Gui::ShowBombNamePortrait(anmIdx, stringIdx)
//     FUN_0043958d  ZunTimer::Tick (player+0x16a38, player+0x16a34)
//     FUN_00431930  utils::AddNormalizeAngle / RotateZ
//     FUN_00450d60  AnmVm::ExecuteAnmScript-like
//     FUN_00404f30  AnmManager::ExecuteAnmIdx
//     FUN_0041c1c0  EffectManager::SpawnParticles
//     FUN_004418b0  EffectManager::SetParticlePos/Velocity
//     FUN_0044c930  SoundPlayer::PlaySoundByIdx
//     FUN_0044b310  ScreenEffect::RegisterChain
//     FUN_004083f0  (called by draw callbacks; likely AnmManager flush + viewport)
//     FUN_0044f770  AnmVm::Draw or AnmManager::Draw
//
//   DarkenViewport body is INLINED into the bomb-draw dispatcher
//   FUN_00406de0 (not a separate function in th07). The dispatcher reads
//   player+0x514 (likely characterShotType or bombIsActive), and for the
//   active branch computes the darkness alpha from the bomb timer and
//   calls ScreenEffect::DrawSquare (FUN_00444a650 -- wait, DrawSquare is
//   at 0x44a650 per config/mapping.csv). The fade rect uses 32,16,416,464
//   like th06's DarkenViewport.

#include "BombData.hpp"

namespace th07
{
// Placeholder initializer matching the 12-entry g_BombData table.
// The real callback pointers (FUN_00408710 etc.) cannot be referenced by
// name until the corresponding th07 functions are implemented. Until then
// the entries are left as NULL so the table is allocated at the right size;
// objdiff will flag every entry as unmatched. Once the Player struct is
// ported, replace each slot with the real callback.
DIFFABLE_STATIC_ARRAY_ASSIGN(BombData, 12, g_BombData) = {
    {NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL},
    {NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL},
    {NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL},
};
}; // namespace th07
