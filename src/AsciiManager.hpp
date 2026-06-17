#pragma once

// AsciiManager module for th07 (Perfect Cherry Blossom).
//
// th07 substantially reworked this module relative to th06:
//   * The AsciiManager struct grew from 0xc1ac to 0x19e2c bytes (verified
//     by the offset of the final popup element: popups[723] at 0xa09c,
//     0x28 bytes each).
//   * AddedCallback only loads ascii.anm (ANM file 1) and capture.anm
//     (ANM file 4). Files 2 and 3 are loaded elsewhere and released by
//     DeletedCallback.
//   * DrawStrings is similar in spirit but writes through a different
//     AnmVm field layout and uses sprite offsets -1 / +0x7c instead of
//     th06's -0x15 / +0x61.
//   * DrawPopups (formerly DrawPopupsWith/WithoutHwVertexProcessing) is
//     collapsed into a single function and now also renders the in-game
//     score / point / graze counters.
//   * The pause / retry menu state machines are larger and live in two
//     differently-sized regions inside AsciiManager (gameMenu = 0x194c
//     bytes, retryMenu = 0x1268 bytes).
//
// All structure offsets below were verified against ghidra's decompilation
// of th07.exe. The internal AnmVm instances are kept as opaque 0x24c-byte
// buffers because th07's AnmVm definition is owned by the AnmManager
// module; AsciiManager only needs to know where they live.

#include "AnmVm.hpp"
#include "Chain.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>

namespace th07
{
#define TEXT_RIGHT_ARROW 0x7f

// A single on-screen ASCII string queued via AddString / AddFormatText.
// Layout verified against DrawStrings (FUN_004020b0) and AddString
// (FUN_00401f40).
struct AsciiManagerString
{
    char text[64];        // +0x00
    D3DXVECTOR3 position; // +0x40
    D3DCOLOR color;       // +0x4c
    D3DXVECTOR2 scale;    // +0x50
    u32 isSelected;       // +0x58
    u32 isGui;            // +0x5c
};
ZUN_ASSERT_SIZE(AsciiManagerString, 0x60);

// A damage / score popup. Layout verified against OnUpdate
// (FUN_004017e0) and CreatePopup1/2 (FUN_004024f0 / FUN_00402630).
struct AsciiManagerPopup
{
    char digits[8];       // +0x00
    D3DXVECTOR3 position; // +0x08
    D3DCOLOR color;       // +0x14
    i32 previous;         // +0x18 (ZunTimer.previous)
    f32 subFrame;         // +0x1c (ZunTimer.subFrame)
    i32 current;          // +0x20 (ZunTimer.current)
    u8 inUse;             // +0x24
    u8 characterCount;    // +0x25
    u8 _pad[2];           // +0x26
};
ZUN_ASSERT_SIZE(AsciiManagerPopup, 0x28);

// Forward declaration of the manager; the pause / retry menu methods are
// members so that RegisterChain / OnUpdate / OnDrawMenus can dispatch to
// them with the right `this` pointer.
struct AsciiManager;

// Pause / quit / retry menu state machine.
//
// th07 stores two of these inside AsciiManager at different offsets and
// with different sizes:
//   gameMenu  @ AsciiManager + 0x74e8, size 0x194c (10 animated sprites
//              plus a capture background plus curState/numFrames).
//   retryMenu @ AsciiManager + 0x8e34, size 0x1268 (5 animated sprites
//              plus a capture background plus curState/numFrames plus an
//              embedded "screenshake" AnmVm at +0x11c within retryMenu,
//              i.e. AsciiManager + 0x9e50, and a pendingInterrupt word
//              at AsciiManager + 0xa028).
//
// Because the two instances have different sizes we cannot give them a
// single struct type. Instead the menu methods take an AsciiManager *
// and read their state via the fixed offsets above; the underlying bytes
// are owned by the AsciiManager layout.
struct StageMenu
{
    StageMenu();
};

struct AsciiManager
{
    AsciiManager();

    static ChainCallbackResult OnUpdate(AsciiManager *s);
    static ChainCallbackResult OnDrawMenus(AsciiManager *s);
    static ChainCallbackResult OnDrawPopups(AsciiManager *s);
    static ZunResult AddedCallback(AsciiManager *s);
    static ZunResult DeletedCallback(AsciiManager *s);

    static ZunResult RegisterChain();
    static void CutChain();

    void InitializeVms();
    void InitializeMenuVms();

    void DrawStrings();
    void DrawPopups();

    void AddString(D3DXVECTOR3 *position, char *text);
    void AddFormatText(D3DXVECTOR3 *position, const char *fmt, ...);
    void CreatePopup1(D3DXVECTOR3 *position, i32 value, D3DCOLOR color);
    void CreatePopup2(D3DXVECTOR3 *position, i32 value, D3DCOLOR color);

    void SetColor(D3DCOLOR color)
    {
        this->color = color;
    }

    // ----- Field layout (offsets verified against th07.exe) -----
    //
    //   0x000   vm0            (AnmVm, 0x24c) - ASCII text rendering
    //   0x24c   vm1            (AnmVm, 0x24c) - popup rendering
    //   0x498   scoreLabelVm   (AnmVm, 0x24c)
    //   0x6e4   scoreDigitVm   (AnmVm, 0x24c)
    //   0x930   grazeLabelVm   (AnmVm, 0x24c)
    //   0xb7c   pointLabelVm[4](AnmVm[4], 0x930) - ends at 0x14ac
    //   0x14ac  unk14ac[0x10]  (16-byte tail/gap; strings start at 0x14bc)
    //   0x14bc  strings[256]   (AsciiManagerString[256], 0x6000)
    //   0x74bc  numStrings     (i32)
    //   0x74c0  color          (D3DCOLOR)
    //   0x74c4  scale.x        (f32)
    //   0x74c8  scale.y        (f32)
    //   0x74cc  isGui          (u32)
    //   0x74d0  isSelected     (u32)
    //   0x74d4  unk74d4        (u32)
    //   0x74d8  charWidth      (i32, = 14)
    //   0x74dc  nextPopupIndex1
    //   0x74e0  nextPopupIndex2
    //   0x74e4  _pad74e4       (4 bytes)
    //   0x74e8  gameMenu       (0x194c bytes - pause/quit menu)
    //   0x8e34  retryMenu      (0x1268 bytes - retry menu; contains the
    //                            screenshake AnmVm at +0x11c and the
    //                            pendingInterrupt word at +0x1f4)
    //   0xa09c  popups[723]    (AsciiManagerPopup[723], 0xfd90)
    AnmVm vm0;                                // +0x000 ASCII text rendering
    AnmVm vm1;                                // +0x24c popup rendering
    AnmVm scoreLabelVm;                       // +0x498
    AnmVm scoreDigitVm;                       // +0x6e4
    AnmVm grazeLabelVm;                       // +0x930
    AnmVm pointLabelVm[4];                    // +0xb7c (4 * 0x24c = 0x930 -> ends 0x14ac)
    u8 unk14ac[0x10];                         // +0x14ac 16-byte gap
    AsciiManagerString strings[256];          // +0x14bc (256 * 0x60 = 0x6000)
    i32 numStrings;                           // +0x74bc
    D3DCOLOR color;                           // +0x74c0
    D3DXVECTOR2 scale;                        // +0x74c4
    u32 isGui;                                // +0x74cc
    u32 isSelected;                           // +0x74d0
    u32 unk74d4;                              // +0x74d4
    i32 charWidth;                            // +0x74d8 (= 14)
    i32 nextPopupIndex1;                      // +0x74dc
    i32 nextPopupIndex2;                      // +0x74e0
    u32 unk74e4;                              // +0x74e4
    // Pause/quit menu state machine (0x194c bytes). The StageMenu methods take
    // an AsciiManager* and read this region via fixed offsets; interior layout
    // is opaque until those methods are reversed.
    u8 gameMenu[0x194c];                      // +0x74e8
    // Retry menu state machine (0x1268 bytes). Contains the screenshake AnmVm
    // at +0x11c within this block (== AsciiManager + 0x9e50) and the
    // pendingInterrupt word at +0x1f4 within (== AsciiManager + 0xa028).
    u8 retryMenu[0x1268];                     // +0x8e34
    AsciiManagerPopup popups[723];            // +0xa09c (723 * 0x28 = 0xfd90)
};
ZUN_ASSERT_SIZE(AsciiManager, 0x19e2c);

DIFFABLE_EXTERN(AsciiManager, g_AsciiManager);

// Chain elements registered by AsciiManager::RegisterChain.
DIFFABLE_EXTERN(ChainElem, g_AsciiManagerCalcChain);
DIFFABLE_EXTERN(ChainElem, g_AsciiManagerOnDrawMenusChain);
DIFFABLE_EXTERN(ChainElem, g_AsciiManagerOnDrawPopupsChain);
}; // namespace th07
