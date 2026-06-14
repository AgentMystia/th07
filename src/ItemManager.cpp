// ItemManager module for th07 (Perfect Cherry Blossom) - PARTIAL recovery.
//
// See ItemManager.hpp for the high-level status: in th07 the item subsystem
// lives inside GameManager (DAT 00626278). Only OnUpdate (FUN_00432990) has
// been located with confidence so far. The full function is ~4300 bytes and
// depends on a dozen GameManager-local helpers that are still unnamed, so a
// faithful, objdiff-100% port is out of scope for this pass. What is recorded
// here is the verified structure layout (Item size 0x288, the location of
// g_PowerItemScore / g_PowerUpThresholds, and the GameManager offsets at which
// the item array + intrusive list live) so the next pass can start from
// ground truth instead of re-deriving it.

#include "ItemManager.hpp"

namespace th07
{
// Verified addresses (read directly from th07.exe via Ghidra):
//   g_PowerItemScore      @ 0x0049ecf8  (31 i32s, 0x7c bytes)
//   g_PowerUpThresholds   @ 0x0049ed74  (11 i32s, 0x2c bytes)
//   g_GameManager         @ 0x00626278  (the `this` passed to OnUpdate)
//   OnUpdate              @ 0x00432990  (size 0x10c9, caller FUN_00425a50)
//
// The item array starts at GameManager + 0; itemCount is at GameManager +
// 0x00ae2ec; the intrusive in-use list head is at GameManager + 0x00ae2f0 and
// its tail pointer is at GameManager + 0x00ae578; the in-use list element
// count is at GameManager + 0x00ae574.

// Stub: the real body is ~4300 bytes and pulls in a large cluster of
// GameManager-local helpers (FUN_0042f4aa = IncreaseSubrank,
// FUN_0042f526 = DecreaseSubrank, FUN_004024f0 = AsciiManager::CreatePopup1,
// FUN_00402630 = AsciiManager::CreatePopup2, FUN_0044c930 = SoundPlayer::Play,
// FUN_0043e4e0 = Player::CalcItemBoxCollision, FUN_00433b20 =
// TurnAllItemsIntoPoints, FUN_0042d612 = Gui::ShowFullPowerMode, etc.). Porting
// it to objdiff-100% requires those helpers' signatures first.
void ItemManager::OnUpdate(struct GameManager *mgr)
{
    (void)mgr;
}
} // namespace th07
