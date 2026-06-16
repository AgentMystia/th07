// Supervisor module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary. Plain C++ so it ports cleanly to the SDL2
// build; orig (DIFFBUILD) addresses are confined to #ifdef DIFFBUILD macros.
//
// Function addresses (verified from ghidra):
//   Supervisor::RegisterChain   FUN_00439000  __cdecl  no args
//   Supervisor::OnUpdate        FUN_00437c70  __fastcall ECX=Supervisor*
//   Supervisor::OnDraw          FUN_0043831b  __fastcall ECX=Supervisor*
//   Supervisor::AddedCallback   FUN_00438986  __fastcall ECX=Supervisor*
//   Supervisor::DeletedCallback FUN_00438de2  __fastcall ECX=Supervisor*
//   Supervisor::DrawFpsCounter  FUN_004390a5  __fastcall arg: i32 drawArg
//   Supervisor::TickTimer       FUN_0043958d  __thiscall args: i32*, f32*
//   Supervisor::SetupDInput     FUN_004383d8  __fastcall ECX=Supervisor*
//   Supervisor::LoadConfig      FUN_004398b6  __thiscall arg: char*
//   Supervisor::PlayAudio       FUN_00439dd0  __fastcall args: i32, char*
//   Supervisor::StopAudio       FUN_00439ec1  __thiscall arg: i32
//   Supervisor::PlayMidiFile    FUN_00439f4d  __fastcall arg: char*
//   Supervisor::FadeOutMusic    FUN_0043a0d6  __thiscall arg: f32
//   Supervisor::ReadMidiFile    FUN_0043a05f  __fastcall arg: u32

#include "Supervisor.hpp"

#include "Chain.hpp"
#include "GameErrorContext.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "utils.hpp"

#include <windows.h>
#include <mmsystem.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

namespace th07
{
DIFFABLE_STATIC(Supervisor, g_Supervisor);
DIFFABLE_STATIC(ControllerMapping, g_ControllerMapping);
DIFFABLE_STATIC(u16, g_LastFrameInput);
DIFFABLE_STATIC(u16, g_CurFrameInput);
DIFFABLE_STATIC(u16, g_IsEigthFrameOfHeldInput);
DIFFABLE_STATIC(u16, g_NumOfFramesInputsWereHeld);

// .rdata string constants referenced by orig (defined in a data module later).
extern const char TH_FPS_FMT[];            // "%.02ffps"
extern const char TH_SCENE_FMT[];          // "scene %d -> %d\r\n"

// ---- cross-module stubs (full impls land when those modules reverse) ----
// Declared as extern C so MSVC emits a reloc CALL that objdiff tolerates.
extern "C" u16 __fastcall Controller_GetInput();                  // FUN_00430b50
extern "C" void __fastcall DebugPrint_th(const char *fmt, ...);         // FUN_0045e4f0
extern "C" ZunResult __fastcall GameManager_RegisterChain();      // FUN_0042f3c5
extern "C" void __fastcall GameManager_CutChain();                // FUN_0042f45d
extern "C" ZunResult __fastcall MainMenu_RegisterChain();         // FUN_0041e820
extern "C" ZunResult __fastcall MusicRoom_RegisterChain();        // FUN_0044a302
extern "C" ZunResult __fastcall Ending_RegisterChain();           // FUN_0043b4db
extern "C" ZunResult __fastcall ResultScreen_RegisterChain(i32 b);// FUN_0041c1b0
extern "C" ZunResult __fastcall Supervisor_AutosaveScore();       // FUN_0043a569
extern "C" void __fastcall SoundPlayer_StopStream(i32 cmd, i32 p, char *name); // FUN_0044d2f0
extern "C" void __fastcall SoundPlayer_FadeOut(f32 seconds);      // FUN_00444c20
extern "C" ZunResult __fastcall SoundPlayer_InitSoundBuffers();   // FUN_0044c7d0
extern "C" ZunResult __fastcall SoundPlayer_LoadBgmFmtFile(char *p); // FUN_0044bff0
extern "C" ZunResult __fastcall AsciiManager_RegisterChain();     // FUN_00401e30
extern "C" void __fastcall AsciiManager_CutChain();               // FUN_00401f10
extern "C" void __fastcall AsciiManager_AddString(D3DXVECTOR3 *pos, char *s); // FUN_00401f40
extern "C" ZunResult __fastcall AnmManager_LoadAnm(i32 idx, char *path, i32 base); // FUN_0044df90
extern "C" void __fastcall AnmManager_ReleaseAnm(i32 idx);        // FUN_0044e4e0
extern "C" void __fastcall MidiOutput_StopPlayback();             // FUN_00436b30
extern "C" ZunResult __fastcall MidiOutput_LoadFile(char *path);  // FUN_004369c0
extern "C" void __fastcall MidiOutput_ClearTracks();              // FUN_00436700
extern "C" ZunResult __fastcall MidiOutput_SetFadeOut(u32 ms);    // FUN_00436c90
extern "C" void __fastcall CStreamingSound_UpdateFadeOut();       // FUN_0045dad0
extern "C" i32 __fastcall GetPrivateProfileInt_th07(char *app, char *key, i32 def, char *file); // FUN_00431330
extern "C" void __fastcall Supervisor_D3DDiscard();               // FUN_0045c5d0 (D3DDevice reset)
extern "C" void __fastcall Supervisor_ChainReleaseAll();          // FUN_00443da0 (ReplayManager release)
extern "C" void __fastcall Supervisor_SomePulseFlag();            // FUN_00404fe0 (used by EffectManager)

// Chain singleton (orig g_Chain @ 0x00626218).
extern "C" struct Chain g_Chain;

#pragma optimize("s", on)

// =====================================================================
// Supervisor::TickTimer  (FUN_0043958d)
// __thiscall, args: frames*, subframes*. ECX = Supervisor*.
// orig reads framerateMultiplier from this+0x178 directly each time (no local).
// =====================================================================
void Supervisor::TickTimer(i32 *frames, f32 *subframes)
{
    if (*(f32 *)((u8 *)this + 0x178) < *(f32 *)0x00498a58)
    {
        *subframes = *subframes + *(f32 *)((u8 *)this + 0x178);
        if (*(f32 *)0x00498a54 <= *subframes)
        {
            *frames = *frames + 1;
            *subframes = *subframes - *(f32 *)0x00498a54;
        }
    }
    else
    {
        *frames = *frames + 1;
    }
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::OnUpdate  (FUN_00437c70)
// __fastcall, ECX = Supervisor*. Per-frame state machine + input poll.
// Orig resets a cluster of AnmManager current* fields at the top.
// =====================================================================
ChainCallbackResult __fastcall Supervisor::OnUpdate(Supervisor *s)
{
    // AnmManager per-frame field reset (orig: byte writes to g_AnmManager+0x2e4d0..d4
    // and dword zero of +0xc..+0x14, +0x4; *0x80808080 to +0; fldz to +0x18/+0x1c).
    u8 *anm = *(u8 **)0x004b9e44;
    anm[0x2e4d2] = 0xff;
    *(u32 *)(anm + 0x2e4d8) = 0;
    *(u32 *)(anm + 0x2e4cc) = 0;
    anm[0x2e4d1] = 0xff;
    anm[0x2e4d0] = 0xff;
    anm[0x2e4d3] = 0xff;
    *(u32 *)(anm + 0xc) = 0;
    *(u32 *)(anm + 0x10) = 0;
    *(u32 *)(anm + 0x8) = 0;
    *(u32 *)(anm + 0x14) = 0;
    anm[0x2e4d4] = 0xff;
    *(u32 *)(anm + 0x4) = 0;
    *(u32 *)(anm + 0x0) = 0x80808080;
    *(f32 *)(anm + 0x1c) = 0.0f;
    *(f32 *)(anm + 0x18) = 0.0f;
    *(u8 *)0x00575c0c = 0xff;
    if (*(void **)0x004bda94 != 0)
    {
        CStreamingSound_UpdateFadeOut();
    }


    g_LastFrameInput = g_CurFrameInput;
    g_CurFrameInput = Controller_GetInput();
    g_IsEigthFrameOfHeldInput = 0;
    if (g_LastFrameInput == g_CurFrameInput)
    {
        if (0x1d < g_NumOfFramesInputsWereHeld)
        {
            g_IsEigthFrameOfHeldInput = (u16)(g_NumOfFramesInputsWereHeld % 8 == 0);
            if (0x25 < g_NumOfFramesInputsWereHeld)
            {
                g_NumOfFramesInputsWereHeld = 0x1e;
            }
        }
        g_NumOfFramesInputsWereHeld++;
    }
    else
    {
        g_NumOfFramesInputsWereHeld = 0;
    }

    if (s->wantedState == s->curState)
    {
        goto update_calccount;
    }

    s->wantedState2 = s->wantedState;
    DebugPrint_th(TH_SCENE_FMT, s->wantedState, s->curState);

    {
        i32 wanted = s->wantedState;
        i32 cur = s->curState;

        if (wanted == SUPERVISOR_STATE_INIT)
        {
            goto reinit_mainmenu;
        }
        else if (wanted == SUPERVISOR_STATE_MAINMENU)
        {
            if (cur == -1)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            if (cur == SUPERVISOR_STATE_GAMEMANAGER)
            {
                if (GameManager_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
            else if (cur == SUPERVISOR_STATE_EXITERROR)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR;
            }
            else if (cur == SUPERVISOR_STATE_RESULTSCREEN)
            {
                if (ResultScreen_RegisterChain(NULL) != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
            else if (cur == SUPERVISOR_STATE_MUSICROOM)
            {
                if (MusicRoom_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
            else if (cur == SUPERVISOR_STATE_ENDING)
            {
                if (Ending_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
        }
        else if (wanted == SUPERVISOR_STATE_GAMEMANAGER)
        {
            if (cur < SUPERVISOR_STATE_RESULTSCREEN_FROMGAME)
            {
                if (cur == SUPERVISOR_STATE_MAINMENU_REPLAY)
                {
                    GameManager_CutChain();
                    s->curState = 0;
                    Supervisor_D3DDiscard();
                    s->curState = SUPERVISOR_STATE_MAINMENU;
                    if (MainMenu_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                }
                else
                {
                    if (cur == -1)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    if (cur == SUPERVISOR_STATE_MAINMENU)
                    {
                        GameManager_CutChain();
                        s->curState = 0;
                        goto reinit_mainmenu;
                    }
                    if (cur == SUPERVISOR_STATE_GAMEMANAGER_REINIT)
                    {
                        GameManager_CutChain();
                        if (GameManager_RegisterChain() != 0)
                        {
                            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                        }
                        s->curState = SUPERVISOR_STATE_GAMEMANAGER;
                    }
                    else if (cur == SUPERVISOR_STATE_MUSICROOM)
                    {
                        GameManager_CutChain();
                        if (MusicRoom_RegisterChain() != 0)
                        {
                            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                        }
                    }
                }
            }
            else if (cur == SUPERVISOR_STATE_ENDING)
            {
                GameManager_CutChain();
                if (Ending_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
            else if (cur == SUPERVISOR_STATE_ENDING_B)
            {
                s->curState = SUPERVISOR_STATE_GAMEMANAGER_REINIT;
                GameManager_CutChain();
                if (GameManager_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                s->curState = SUPERVISOR_STATE_GAMEMANAGER;
            }
            else if (cur == SUPERVISOR_STATE_ENDING_C)
            {
                s->curState = SUPERVISOR_STATE_GAMEMANAGER_REINIT;
                GameManager_CutChain();
                if (GameManager_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                s->curState = SUPERVISOR_STATE_GAMEMANAGER;
            }
        }
        else if (wanted == SUPERVISOR_STATE_RESULTSCREEN)
        {
            if (cur == -1)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            if (cur == SUPERVISOR_STATE_MAINMENU)
            {
                s->curState = 0;
            reinit_mainmenu:
                s->curState = SUPERVISOR_STATE_MAINMENU;
                Supervisor_D3DDiscard();
                if (MainMenu_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
        }
        else if (wanted == SUPERVISOR_STATE_MUSICROOM)
        {
            if (cur == -1)
            {
                Supervisor_ChainReleaseAll();
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            if (cur == SUPERVISOR_STATE_MAINMENU)
            {
                s->curState = 0;
                Supervisor_ChainReleaseAll();
                goto reinit_mainmenu;
            }
        }
        else if (wanted == SUPERVISOR_STATE_RESULTSCREEN_FROMGAME)
        {
            i32 c = s->curState;
            if (c == -1)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            if (c == SUPERVISOR_STATE_MAINMENU)
            {
                s->curState = 0;
                goto reinit_mainmenu;
            }
            if (c == SUPERVISOR_STATE_MUSICROOM && ResultScreen_RegisterChain(TRUE) != 0)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
        }
    }

    g_IsEigthFrameOfHeldInput = 0;
    g_LastFrameInput = 0;
    g_CurFrameInput = 0;

update_calccount:
    s->wantedState = s->curState;
    s->calcCount++;
    if (s->calcCount % 4000 == 3999)
    {
        if (Supervisor_AutosaveScore() != 0)
        {
            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)
#pragma optimize("s", on)


#pragma optimize("s", off)
#pragma optimize("s", on)



#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::OnDraw  (FUN_0043831b)
// __fastcall, ECX = Supervisor*. Calls DrawFpsCounter(1); returns 1.
// NOTE: orig passes arg=1 (xor ecx,ecx; inc ecx), NOT 0.
// =====================================================================
ChainCallbackResult __fastcall Supervisor::OnDraw(Supervisor *s)
{
    Supervisor::DrawFpsCounter(1);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::DrawFpsCounter  (FUN_004390a5)
// __fastcall, arg: i32 drawArg. ECX unused.
// Big function (0x35b bytes): fps measurement (timeGetTime vs QPC dual
// path), slow-frame % accumulation, AsciiManager draw. Stubbed for now:
// the early-return guard (g_NoFpsCounter) is implemented; the body lands
// in a follow-up pass.
// =====================================================================
void __fastcall Supervisor::DrawFpsCounter(i32 drawArg)
{
    // orig: movsx eax,[0x0062627d]; test eax,eax; jne end
    if (*(i8 *)0x0062627d != 0) // g_NoFpsCounter (replay mode)
    {
        return;
    }
    // TODO: full body (fps calc + slow% + AsciiManager draw)
}

#pragma optimize("s", off)
// __cdecl, no args. supervisor singleton @ 0x00575950. g_Chain @ 0x00626218.
// orig caches supervisor to [ebp-0xc] and reads it back each statement.
// =====================================================================
ZunResult Supervisor::RegisterChain()
{
    Supervisor *supervisor = (Supervisor *)0x00575950;
    i32 calcResult;
    ChainElem *chain;

    supervisor->wantedState = 0;   // +0x154
    supervisor->curState = -1;     // +0x158
    supervisor->calcCount = 0;     // +0x150

    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnUpdate);
    chain->arg = supervisor;
    chain->addedCallback = (ChainAddedCallback)0;
    chain->deletedCallback = (ChainDeletedCallback)0;
    calcResult = g_Chain.AddToCalcChain(chain, 0);
    if (calcResult != 0)
    {
        return (ZunResult)calcResult;
    }

    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnDraw);
    chain->arg = supervisor;
    g_Chain.AddToDrawChain(chain, 0xf);

    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor audio globals (absolute, matching orig DAT_ addresses).
// musicMode @ 0x00575a87, opts @ 0x00575a9c (= g_Supervisor.cfg.opts),
// midiOutput @ 0x00575acc (= g_Supervisor.midiOutput).
// =====================================================================
#define MUSIC_MODE (*(u8 *)0x00575a87)
#define CFG_OPTS (*(u32 *)0x00575a9c)
#define MIDI_OUTPUT_PTR (*(void **)0x00575acc)

// =====================================================================
// Supervisor::ReadMidiFile  (FUN_0043a05f)
// __fastcall, arg: u32 midiFileIdx. ECX unused (orig has no this ref).
// orig: if musicMode==MIDI(2) && midiOutput!=0: MidiOutput_StopPlayback.
//       elif musicMode==WAV(1): SoundPlayer_StopStream(3 or 4 by opts bit13).
//       else: return -1.
// =====================================================================
ZunResult __fastcall Supervisor_ReadMidiFile(u32 midiFileIdx)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MidiOutput_StopPlayback();
        }
    }
    else
    {
        if (MUSIC_MODE != MUSIC_WAV)
        {
            return ZUN_ERROR;
        }
        if ((CFG_OPTS >> 0xd & 1) == 0)
        {
            SoundPlayer_StopStream(3, 0, (char *)0);
        }
        else
        {
            SoundPlayer_StopStream(4, 0, (char *)0);
        }
    }
    return ZUN_SUCCESS;
}

ZunResult Supervisor::ReadMidiFile(u32 idx)
{
    return Supervisor_ReadMidiFile(idx);
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::StopAudio  (FUN_00439ec1)
// __thiscall, arg: i32 channel. ECX = Supervisor*.
// =====================================================================
ZunResult Supervisor::StopAudio(i32 channel)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MidiOutput_StopPlayback();
            MidiOutput_LoadFile((char *)channel);
            // orig: MidiOutput::Play() — stubbed via MidiOutput_LoadFile return path
        }
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        if ((CFG_OPTS >> 0xd & 1) != 0)
        {
            SoundPlayer_StopStream(4, 0, "dummy");
        }
        SoundPlayer_StopStream(2, channel, "dummy");
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::FadeOutMusic  (FUN_0043a0d6)
// __thiscall, arg: f32 fadeOutSeconds. ECX = Supervisor*.
// =====================================================================
ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MidiOutput_SetFadeOut((u32)(fadeOutSeconds * 1000.0f));
        }
    }
    else
    {
        if (MUSIC_MODE != MUSIC_WAV)
        {
            return ZUN_ERROR;
        }
        f32 adj = fadeOutSeconds;
        f32 mul = *(f32 *)((u8 *)this + 0x178);
        f32 threshold = *(f32 *)0x00498a4c; // DAT_00498a4c
        f32 limit = *(f32 *)0x00498a54;     // DAT_00498a54
        if (mul != threshold && mul <= limit)
        {
            adj = fadeOutSeconds / mul;
        }
        SoundPlayer_StopStream(5, (i32)(adj * 1000.0f), (char *)0);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)

} // namespace th07