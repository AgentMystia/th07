// MainMenu module for th07 (Perfect Cherry Blossom).
//
// Lifts the orig cluster (all verified from th07.exe disassembly):
//   FUN_0045c5d0  MainMenu::RegisterChain   (alloc 0xd158 obj, register chain)
//   FUN_0045c4c8  MainMenu::AddedCallback-thunk  -> FUN_0045bf15
//   FUN_0045bf15  MainMenu::AddedCallback   (load title01.anm, play BGM,
//                                            construct 0xe menu AnmVm sprites)
//   FUN_004554d6  MainMenu::OnCalc (per-frame state machine)
//   FUN_0045bd6c  MainMenu::OnDraw (per-frame sprite iteration + Draw3/NoRot)
//   FUN_0045c546  MainMenu::DeletedCallback
//
// The MainMenuObj struct (0xd158 bytes) names the fields the boot/draw path
// touches; the rest is reserved as unk_XXXX padding so sizeof stays locked.
// Field offsets verified against the disassembly of FUN_0045bf15 (sprite
// loop at +0xb0cc, stride 0x24c) and FUN_0045bd6c (reads +0xb0c4 sprite-array
// ptr, +0xd0f4 count, +0xd0f8 state, +0xb0c8 active-vm ptr).
#pragma once

#include "AnmVm.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
namespace MainMenu
{
ZunResult RegisterChain(i32 unused);

// MainMenu object (0xd158 bytes). 14 menu AnmVm sprites live at +0xb0cc
// (stride 0x24c). The bulk of the object is per-state scratch / menu-item
// label sprite pool (allocated separately by OnCalc state 0); named fields
// below cover only what the boot + draw path touches.
struct MainMenuObj
{
    i32 cursorIndex;            // +0x00 (menu cursor 0..7)
    i32 prevCursor;             // +0x04
    i32 subState;               // +0x08 (state-0 sub-state 0..3)
    i32 subStateFrame;          // +0x0c
    u8 unk_10[0x64 - 0x10];     // +0x10..0x64
    i32 savedState;             // +0x64 (previous menuState on transition)
    i32 returningFlag;          // +0x68
    u8 unk_6c[0xb0c0 - 0x6c];   // +0x6c..0xb0c0
    i32 frameCounter;           // +0xb0c0 (bumped each OnCalc)
    AnmVm *spriteArrayPtr;      // +0xb0c4 (label sprite pool; set by OnCalc s0)
    AnmVm *activeVm;            // +0xb0c8 (cursor sprite; set by AddedCallback)
    // 14 menu AnmVm sprites. 14 * 0x24c = 0x2028, so this array spans
    // +0xb0cc .. +0xd0f4 (ending exactly where spriteCount begins).
    AnmVm sprites[14];          // +0xb0cc .. +0xd0f4
    i32 spriteCount;            // +0xd0f4 (count passed to OnDraw loop)
    i32 menuState;              // +0xd0f8 (OnCalc dispatcher index)
    i32 stateFrame;             // +0xd0fc
    i32 demoTimer;              // +0xd100 (attract/demo frame counter)
    u8 tail_d104[0xd158 - 0xd104]; // +0xd104..0xd158
};
ZUN_ASSERT_SIZE(MainMenuObj, 0xd158);
} // namespace MainMenu
} // namespace th07
