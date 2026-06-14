// ItemManager module for th07 (Perfect Cherry Blossom).
//
// WARNING: in th07 the item subsystem is NOT a standalone struct the way it is
// in th06. The 1100-item array, the per-frame counter and the intrusive
// singly-linked "in use" list are embedded inside th07::GameManager (DAT
// 00626278). The functions that operate on them take a GameManager pointer as
// ECX (__fastcall `this`) and read item state via fixed offsets:
//
//   items[1100]            GameManager + 0x000000   (each item = 0x288 bytes)
//   itemCount (i32)        GameManager + 0x00ae2ec
//   listHead.next (Item *) GameManager + 0x00ae2f0
//   listTailPtr (Item **)  GameManager + 0x00ae578
//   listCount (i32)        GameManager + 0x00ae574
//
// Because of this layout, the th07 functions here are declared as static
// helpers taking a GameManager * rather than as methods on a self-contained
// ItemManager struct. Only OnUpdate could be located with high confidence
// (FUN_00432990, called from the GameManager update path FUN_00425a50); the
// remaining th06-shaped entry points (constructor / SpawnItem / OnDraw /
// RemoveAllItems) could not be cleanly separated from the surrounding
// GameManager / Stage code without first recovering the full GameManager
// layout. This file is therefore a PARTIAL recovery and is intentionally
// confidence = low - it does NOT aim at objdiff 100%.
//
// All offsets below were verified against FUN_00432990's decompilation.

#pragma once

#include "AnmVm.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>

namespace th07
{
// Item type byte stored in Item::itemType (+0x27c). th07 has more types than
// th06 (which only defined 0..6); cases 7..9 are reached in OnUpdate's switch.
enum ItemType
{
    ITEM_POWER_SMALL,    // 0
    ITEM_POINT,          // 1
    ITEM_POWER_BIG,      // 2
    ITEM_BOMB,           // 3
    ITEM_FULL_POWER,     // 4
    ITEM_LIFE,           // 5  (case 5 in OnUpdate calls FUN_0042d83a)
    ITEM_POINT_BULLET,   // 6
    ITEM_DREAM_POINT,    // 7  (auto-collect-aware; uses graze bonus + ext)
    ITEM_EXTEND_NOTIFY,  // 8  (plays se_powerup / extend)
    ITEM_GRAZE_POINT,    // 9
    ITEM_NO_ITEM = 0xffffffff,
};

// Per-item state. Layout reconstructed from FUN_00432990 reads/writes:
//   +0x000  sprite            (AnmVm, 0x24c bytes)
//   +0x24c  currentPosition   (D3DXVECTOR3)
//   +0x258  startPosition     (D3DXVECTOR3)
//   +0x264  targetPosition    (D3DXVECTOR3)
//   +0x270  timer             (ZunTimer-style {previous, subFrame, current})
//   +0x27c  itemType          (u8, ItemType)
//   +0x27d  isInUse           (u8)
//   +0x27e  _pad27e           (u8)
//   +0x27f  state             (u8: 0=falling, 1=homing-to-player, 2=pop)
//   +0x280  autoCollectFlag   (u8: set when item is force-collected)
//   +0x284  listNext          (Item *, intrusive "in use" list link)
struct Item
{
    AnmVm sprite;
    D3DXVECTOR3 currentPosition;
    D3DXVECTOR3 startPosition;
    D3DXVECTOR3 targetPosition;
    AnmVmTimer timer;
    u8 itemType;
    u8 isInUse;
    u8 _pad27e;
    u8 state;
    u8 autoCollectFlag;
    u8 _pad281[3];
    Item *listNext;
};
ZUN_ASSERT_SIZE(Item, 0x288);

// Score table indexed by power-item-streak count. Located in th07.exe at
// 0x0049ecf8; verified byte-for-byte against memory read.
//   {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600,
//    700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
//    10000, 11000, 12000, 51200}
extern "C" i32 g_PowerItemScore[31];

// Power-up thresholds (currentPower must cross these to advance the weapon
// level). Located at 0x0049ed74. NOTE: th07 differs from th06 in the last two
// entries - th06 was {8,16,32,48,64,80,96,128,999,1,0}, th07 is
// {8,16,32,48,64,80,96,128,999,22,32}.
extern "C" i32 g_PowerUpThresholds[11];

// th07 does not expose a self-contained ItemManager type; these static helpers
// take the owning GameManager pointer. See file header for the rationale.
struct ItemManager
{
    // OnUpdate was located at FUN_00432990 (verified via g_PowerUpThresholds
    // xrefs at 0x0049ed74 and the 1100-item loop with 0x288 stride). The other
    // four th06-shaped entry points have not yet been located with confidence
    // and are intentionally left un-declared rather than guessed.
    static void OnUpdate(struct GameManager *mgr);
};
} // namespace th07
