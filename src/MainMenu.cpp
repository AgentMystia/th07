// MainMenu module for th07 (Perfect Cherry Blossom).
//
// Lifts the orig cluster (all verified from th07.exe disassembly):
//   FUN_0045c5d0  MainMenu::RegisterChain   (alloc 0xd158 obj, register chain)
//   FUN_0045c4c8  MainMenu::AddedCallback-thunk  -> FUN_0045bf15
//   FUN_0045bf15  MainMenu::AddedCallback   (load title01.anm, play th07_01.mid)
//   FUN_004554d6  MainMenu::OnCalc (per-frame)
//   FUN_0045bd6c  MainMenu::OnDraw (per-frame)
//   FUN_0045c546  MainMenu::DeletedCallback
//
// RegisterChain is lifted faithfully. AddedCallback lifts the boot-critical
// tail (title01.anm load + bgm/th07_01.mid play) faithfully; the giant
// phantasm.jpg boot-fade loop (DAT_0062f894 == 0 first-time path) is left as
// a skip because it needs the full AnmManager draw stack to be useful. The
// calc/draw callbacks are minimal CONTINUE returns pending the full
// menu-state-machine lift (the 0xe menu sprites, input handling, transitions).
#include "MainMenu.hpp"

#include "AnmManager.hpp"
#include "Chain.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <windows.h>

namespace th07
{
namespace MainMenu
{

// ---------------------------------------------------------------------------
// Orig DAT slots referenced by FUN_0045bf15. Declared as extern "C" globals
// in link_globals.cpp; the boot-path reads them.
//   0x00575aac  g_SupervisorAac       -- some accumulated state (0 on first boot)
//   0x00575a87  g_SupervisorMusicMode -- 1 = WAVE, 2 = MIDI (set by cfg read)
//   0x00575acc  g_SupervisorMidiSlot  -- MidiOutput* allocated lazily by boot
//   0x0062f648  g_MainMenuFlagBits    -- menu-control flag word (boot skips
//                                        BGM when bit 1 is set)
// ---------------------------------------------------------------------------
extern "C" i32  g_SupervisorAac_575aac;
extern "C" u8   g_SupervisorMusicMode_575a87;
extern "C" void *g_SupervisorMidiSlot_575acc;
extern "C" i32  g_MainMenuFlagBits_62f648;

// AnmManager::LoadAnm -- 0x0044df90. Normal-build stub returns 0 (success).
extern "C" i32 __fastcall AnmManager_LoadAnm(void *anm, i32 a, char *path, i32 b);
// operator new thunk -- 0x0047d441. Normal build wraps malloc.
extern "C" void *__fastcall operator_new_th07(u32 size);
// MidiOutput::Play -- 0x00436650. this=MidiOutput*, trackIdx, path.
extern "C" i32 __fastcall MidiOutput_Play(void *p, i32 a, char *path);
// SoundPlayer wave-play -- 0x0044d2f0. mode, trackCtx, path.
extern "C" void __fastcall SoundPlayer_PlayWaveVariant(i32 a, void *b, char *path);

// MainMenu object. Orig: 0xd158 bytes. The bulk is an array of 0xe menu
// sprites (stride 0x24c) + per-state scratch + the two embedded ChainElem
// pointers at +0xd104 (calc) and +0xd108 (draw). Kept as a byte blob until
// the full menu-state-machine struct is lifted.
struct MainMenuObj
{
    u8 raw[0xd158];
};

// ---------------------------------------------------------------------------
// AddedCallback (orig FUN_0045bf15, simplified). The orig is a 0x5b0-byte
// function that also loops 900 frames loading data/title/phantasm.jpg with a
// boot-fade, sets up the 0xe menu sprites, picks an entry state from
// g_SupervisorAac_575aac, and configures per-sprite blend modes. For the
// normal-build demo we lift only the asset-load + BGM-play tail:
//   - AnmManager::LoadAnm(0x20, "data/title01.anm", 0x900)
//   - if music mode initialised and not in all-clear (5) state, play
//     bgm/th07_01.mid (MIDI) or bgm/th07_01.wav (WAVE) via the BGM dispatcher.
// Returns ZUN_SUCCESS on happy path, -1 if the anm load fails.
// ---------------------------------------------------------------------------
static ZunResult __fastcall AddedCallback(MainMenuObj *mm)
{
    i32 iVar;

    (void)mm;

    // 0x20 -> anmIdx slot for the menu. "data/title01.anm", 0x900 sprites.
    iVar = AnmManager_LoadAnm((void *)g_AnmManager, 0x20, "data/title01.anm", 0x900);
    if (iVar != 0)
    {
        return (ZunResult)-1;
    }

    // BGM dispatch: only when bit 1 of the menu flag word is clear.
    if ((g_MainMenuFlagBits_62f648 >> 1 & 1) == 0)
    {
        if (g_SupervisorAac_575aac != 5) // 5 == all-clear state (skip BGM)
        {
            // FUN_00439dd0 (orig): musicMode 2 (MIDI) -> MidiOutput::Play;
            // mode 1 (WAVE) -> rewrite .mid -> .wav + SoundPlayer play.
            if (g_SupervisorMusicMode_575a87 == 2)
            {
                if (g_SupervisorMidiSlot_575acc != 0)
                {
                    MidiOutput_Play(g_SupervisorMidiSlot_575acc, 8, "bgm/th07_01.mid");
                }
            }
            else if (g_SupervisorMusicMode_575a87 == 1)
            {
                SoundPlayer_PlayWaveVariant(1, (void *)0, "bgm/th07_01.wav");
            }
        }
    }

    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// OnCalc / OnDraw / DeletedCallback -- minimal CONTINUE returns. Orig
// FUN_004554d6 / FUN_0045bd6c / FUN_0045c546 drive the menu state machine +
// sprite animation (the 0xe menu sprites, input handling, transitions,
// phantasm/title animations). Lifting these is the bulk of the remaining
// menu work; returning CONTINUE keeps the element on the chain so the boot
// loop keeps Presenting frames.
// ---------------------------------------------------------------------------
static ChainCallbackResult __fastcall OnCalc(MainMenuObj *mm)
{
    (void)mm;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

static ChainCallbackResult __fastcall OnDraw(MainMenuObj *mm)
{
    (void)mm;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

static ZunResult __fastcall DeletedCallback(MainMenuObj *mm)
{
    (void)mm;
    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// RegisterChain (orig FUN_0045c5d0). Allocates the 0xd158-byte MainMenu
// object, zeroes it, and installs a calc chain element (priority 3) whose
// addedCallback fires AddedCallback immediately, plus a draw chain element.
// The calc element's addedCallback is what loads title01.anm + starts BGM.
// ---------------------------------------------------------------------------
ZunResult RegisterChain(i32 unused)
{
    (void)unused;

    MainMenuObj *mm;
    ChainElem *calcElem;
    ChainElem *drawElem;

    mm = (MainMenuObj *)operator_new_th07(0xd158);
    if (mm == 0)
    {
        mm = 0;
    }
    if (mm != 0)
    {
        memset(mm, 0, 0xd158);
    }

    // Calc chain element (priority 3). addedCallback fires on registration.
    calcElem = g_Chain.CreateElem((ChainCallback)OnCalc);
    calcElem->arg = mm;
    calcElem->addedCallback = (ChainAddedCallback)AddedCallback;
    calcElem->deletedCallback = (ChainDeletedCallback)DeletedCallback;
    if (g_Chain.AddToCalcChain(calcElem, 3) != 0)
    {
        return (ZunResult)-1;
    }

    // Draw chain element (priority 0).
    drawElem = g_Chain.CreateElem((ChainCallback)OnDraw);
    drawElem->arg = mm;
    g_Chain.AddToDrawChain(drawElem, 0);

    return ZUN_SUCCESS;
}

} // namespace MainMenu
} // namespace th07
