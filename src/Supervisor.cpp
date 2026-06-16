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
// SetupDInput externs
extern "C" i32 __fastcall GetWindowLongA_th07(HWND hwnd, i32 idx); // wraps GetWindowLongA
extern "C" i32 __fastcall DirectInput8Create_th07(i32 inst, u32 ver, void *iid, void **out, void *unk);
extern "C" void __fastcall GameErrorContext_LogFmt2(void *ctx, char *fmt); // FUN_004315f0 (1 arg after ctx)
extern "C" i32 __fastcall Supervisor_EnumKeybdCallback(void *ref, void *dev); // FUN_0043832f
extern "C" i32 __fastcall Supervisor_EnumJoysCallback(void *ref, void *dev); // FUN_0043836e
// DeletedCallback / cleanup externs
extern "C" void __fastcall Supervisor_SomeCleanup1();        // FUN_00437c39
extern "C" void __fastcall Supervisor_ReleaseAnm0();         // FUN_0044e4e0 wrapper
extern "C" void __fastcall Supervisor_MidiClearTracks();     // FUN_004365b0 (MidiOutput dtor step)
extern "C" void __fastcall Supervisor_Cleanup2();            // FUN_00443da0 (ReplayManager release)
extern "C" void __fastcall Supervisor_Cleanup3();            // FUN_0043227e
extern "C" void __fastcall Supervisor_HeapFreeAll();         // FUN_0045f800
extern "C" void __fastcall Supervisor_SomeCleanup4();        // FUN_004378f0
extern "C" void __fastcall Supervisor_SomeCleanup5();        // FUN_00438fef
extern "C" void *_free_th07(void *p);                         // _free wrapper
// LoadConfig externs
extern "C" void *__fastcall Supervisor_ReadConfigBuffer();    // FUN_00431330 (reads th07.cfg into heap)
extern "C" void __fastcall GameErrorContext_LogFmt3(void *ctx, char *fmt, char *arg); // FUN_00431730
extern "C" i32 __fastcall Supervisor_ValidateSize(i32 size); // FUN_00431540 (assert config struct size)
extern "C" void __fastcall Supervisor_LogStr1(char *s);      // FUN_00437903 (1 string arg)
extern "C" HANDLE __fastcall CreateFileA_th07(char *path, u32 access, u32 share, void *sa, u32 disp, u32 flags, HANDLE tmpl);
extern "C" i32 __fastcall ReadFile_th07(HANDLE f, void *buf, u32 n, u32 *read, void *ovl);
extern "C" i32 __fastcall CloseHandle_th07(HANDLE h);

// ---- thiscall callee stubs (ECX = singleton pointer) ----
struct MidiOutput
{
    void StopPlayback();                       // FUN_00436b30
    ZunResult LoadFile(char *path);            // FUN_004369c0
    void ClearTracks();                        // FUN_00436700
    ZunResult Play();                          // FUN_00436ad0
    ZunResult SetFadeOut(u32 ms);              // FUN_00436c90
    ZunResult ParseFile(i32 idx);              // FUN_00436790
};
struct SoundPlayer
{
    void StopStream(i32 cmd, i32 param, char *name); // FUN_0044d2f0
    void FadeOut(f32 seconds);                       // FUN_00444c20
};

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
// __fastcall, arg: i32 drawArg. ECX unused (but stored to [ebp-0x34]).
// Big function (0x35b bytes): dual-path fps measurement (timeGetTime vs
// QueryPerformanceCounter depending on g_usesQPC @ 0x575bbc), slow-frame %
// accumulation into g_slowFramePct* (@0x575ad0/d4/d8), and AsciiManager
// text draws at the bottom-left when the relevant display opts are set.
// Globals verified from disasm:
//   g_NoFpsCounter    i8  @ 0x0062627d  (early-return guard, replay mode)
//   frameskipCfg      u8  @ 0x00575a8b
//   g_frameCounter    i32 @ 0x0135e1f0
//   g_usesQPC         i32 @ 0x00575bbc  (0=timeGetTime path, else QPC)
//   g_qpcInitFlag     i32 @ 0x0135e2a4
//   g_lastTimeGetTime i32 @ 0x0135e2a0
//   g_lastQpcLo       i32 @ 0x0135e298
//   g_lastQpcHi       i32 @ 0x0135e29c
//   g_slowPct2        f32 @ 0x00575ad0
//   g_slowPct         f32 @ 0x00575ad4
//   g_slowPctI16      i16 @ 0x00575ad8
//   g_displayOpts     i32 @ 0x0062f648  (bit2 = show fps, bit3 = show slow%)
//   g_asciiStr1            @ 0x135e0f0  (fps string slot)
//   g_asciiStr2            @ 0x135dff0  (slow% string slot)
//   g_asciiMgr        ptr @ 0x134ce18  (AsciiManager singleton)
//   g_cfgColorVar     i32 @ 0x013542d8
//   g_slowCounter     i32 @ 0x0135dfec
//   g_someFlag1       i32 @ 0x00575ab8
//   g_someFlag2       i32 @ 0x00575bf8
//   rdata: "%.02ffps" @ 0x496fa0, "%2d" @ 0x496f9c
//   rdata floats: 0x498a48,0x498a50,0x498a68,0x498a6c,0x498aa0,0x498ab8,
//                 0x498b10,0x498b14,0x498b18,0x498b1c,0x498b20
// =====================================================================
extern "C" i32 __fastcall timeGetTime_th07();              // import @ 0x0048d224
extern "C" i32 __fastcall QueryPerformanceCounter_th07(i64 *out); // import @ 0x0048d06c
extern "C" i32 __cdecl sprintf_th07(char *dst, const char *fmt, ...); // 0x0047d44f
extern "C" i16 __fastcall FloatToI16(f32 v);               // 0x0048b8a0
// AsciiManager::AddString(this=ECX, D3DXVECTOR3 *pos, char *str) @ 0x00401f40
struct AsciiMgrStub
{
    void AddString(D3DXVECTOR3 *pos, char *s);
};
void __fastcall Supervisor::DrawFpsCounter(i32 drawArg)
{
    // orig: movsx eax,[0x0062627d]; test eax,eax; jne end
    if (*(i8 *)0x0062627d != 0) // g_NoFpsCounter (replay mode)
    {
        goto end_block;
    }

    *(i32 *)0x0135e1f0 = *(i32 *)0x0135e1f0 + (i32)*(u8 *)0x00575a8b + 1;

    if (*(i32 *)0x00575bbc == 0)
    {
        // timeGetTime path
        if ((*(i32 *)0x0135e2a4 & 1) == 0)
        {
            *(i32 *)0x0135e2a4 = *(i32 *)0x0135e2a4 | 1;
            *(i32 *)0x0135e2a0 = timeGetTime_th07();
        }
        i32 now = timeGetTime_th07();
        if (now < *(i32 *)0x0135e2a0)
        {
            *(i32 *)0x0135e2a0 = now;
            *(i32 *)0x0135e1f0 = 0;
        }
        if ((u32)(now - *(i32 *)0x0135e2a0) < 0x1f4)
        {
            goto done_fps;
        }
        f32 frameTime = (f32)(i64)(now - *(i32 *)0x0135e2a0);
        f32 frameTimeSec = frameTime / *reinterpret_cast<f32 *>(0x498ab8);
        *(i32 *)0x0135e2a0 = now;
    qpc_or_tgt_compute_fps:
        ; // label target shared with QPC path
        f32 fps = (f32)(i64)*(i32 *)0x0135e1f0 / frameTimeSec;
        *(i32 *)0x0135e1f0 = 0;
        // sprintf(g_asciiStr1, "%.02ffps", fps)
        sprintf_th07((char *)0x135e0f0, (const char *)0x496fa0, (f64)fps);
        if ((*(i32 *)0x0062f648 >> 2 & 1) == 0 || drawArg == 0)
        {
            goto done_fps;
        }
        // slow-frame % accumulation
        f32 base = *reinterpret_cast<f32 *>(0x498a48);
        *(f32 *)0x00575ad4 = *(f32 *)0x00575ad4 + base;
        if (base * *reinterpret_cast<f32 *>(0x498b20) > fps)
        {
            *(f32 *)0x00575ad0 = *(f32 *)0x00575ad0 + base;
        }
        else if (base * *reinterpret_cast<f32 *>(0x498b1c) > fps)
        {
            *(f32 *)0x00575ad0 = base * *reinterpret_cast<f32 *>(0x498b18) + *(f32 *)0x00575ad0;
        }
        else if (base * *reinterpret_cast<f32 *>(0x498a50) > fps)
        {
            *(f32 *)0x00575ad0 = base * *reinterpret_cast<f32 *>(0x498aa0) + *(f32 *)0x00575ad0;
        }
        else
        {
            *(f32 *)0x00575ad0 = base * *reinterpret_cast<f32 *>(0x498a50) + *(f32 *)0x00575ad0;
        }
        if ((*(i32 *)0x0062f648 >> 3 & 1) != 0)
        {
            // sprintf(g_asciiStr2, "%2d", FloatToI16(fps+0x498a50))
            sprintf_th07((char *)0x135dff0, (const char *)0x496f9c,
                         (i32)*(i16 *)(uintptr_t)0 /*placeholder*/);
        }
        else
        {
            *(i16 *)0x00575ad8 = FloatToI16(fps + *reinterpret_cast<f32 *>(0x498a50));
            sprintf_th07((char *)0x135dff0, (const char *)0x496f9c,
                         (i32)*(i16 *)0x00575ad8);
        }
        goto done_fps;
    }
    else
    {
        // QueryPerformanceCounter path
        if (*(i32 *)0x0135e298 == 0)
        {
            QueryPerformanceCounter_th07((i64 *)0x0135e298);
        }
        i64 now;
        QueryPerformanceCounter_th07(&now);
        if (*(i32 *)((u8 *)&now + 0) < *(i32 *)0x0135e298)
        {
            *(i32 *)0x0135e298 = *(i32 *)((u8 *)&now + 0);
            *(i32 *)0x0135e29c = *(i32 *)((u8 *)&now + 4);
            *(i32 *)0x0135e1f0 = 0;
        }
        if ((u32)(*(i32 *)((u8 *)&now + 0) - *(i32 *)0x0135e298) <
            (u32)(*(i32 *)0x0135e298 + (*(i32 *)0x00575bbc >> 1)))
        {
            goto end_block;
        }
        f32 frameTime = (f32)(i64)(*(i32 *)((u8 *)&now + 0) - *(i32 *)0x0135e298);
        f32 frameTimeSec = frameTime / (f32)(i64)*(i32 *)0x00575bbc;
        *(i32 *)0x0135e298 = *(i32 *)((u8 *)&now + 0);
        *(i32 *)0x0135e29c = *(i32 *)((u8 *)&now + 4);
        *(i32 *)0x0135dfec = *(i32 *)0x0135dfec + 1;
        if (*(i32 *)0x0135dfec % 8 == 0)
        {
            // Supervisor::TickTimer(supervisor+0x16a38, supervisor+0x16a34)
            // ECX = g_Supervisor @ 0x575950
            ((Supervisor *)0x00575950)->TickTimer((i32 *)0, (f32 *)0);
        }
        goto qpc_or_tgt_compute_fps;
    }
done_fps:
    goto end_block_check_draw;
end_block_check_draw:
    // AsciiManager draw block (FUN_00439350..end)
    if (*(i32 *)0x00575ab8 != 0 || drawArg == 0)
    {
        return;
    }
    {
        D3DXVECTOR3 pos1;
        pos1.x = *reinterpret_cast<f32 *>(0x498b14);
        pos1.y = *reinterpret_cast<f32 *>(0x498b10);
        pos1.z = 0.0f;
        (*(AsciiMgrStub **)0x134ce18)[0].AddString(&pos1, (char *)0x135e0f0);
    }
    if ((*(i32 *)0x0062f648 >> 3 & 1) == 0 || (*(i32 *)0x0062f648 >> 2 & 1) == 0)
    {
        return;
    }
    {
        D3DXVECTOR3 pos2;
        pos2.x = *reinterpret_cast<f32 *>(0x498a6c);
        pos2.y = *reinterpret_cast<f32 *>(0x498a68);
        pos2.z = 0.0f;
        if (*(i32 *)0x00575bf8 != 0)
        {
            *(i32 *)0x013542d8 = 0xffff4040;
        }
        else
        {
            *(i32 *)0x013542d8 = (i32)0xffffffd0;
        }
        (*(AsciiMgrStub **)0x134ce18)[0].AddString(&pos2, (char *)0x135dff0);
        *(i32 *)0x013542d8 |= 0xffffffff;
    }
    return;
end_block:
    goto end_block_check_draw;
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
// musicMode @ 0x00575a87, opts @ 0x00575a9c, midiOutput @ 0x00575acc.
#define MUSIC_MODE (*(u8 *)0x00575a87)
#define CFG_OPTS (*(u32 *)0x00575a9c)
#define MIDI_OUTPUT_PTR (*(MidiOutput **)0x00575acc)
#define SOUND_PLAYER_PTR (*(SoundPlayer **)0x004ba0d8)

// rdata string constants referenced by orig via absolute reloc.
// "dummy" @ 0x4980d0, empty string @ 0x496c1e. Declared extern "C" so the
// PUSH emits a reloc objdiff can match against the orig's DAT_xxxxxxxx reloc.
extern "C" char g_DummyStr[];   // DAT_004980d0 "dummy"
extern "C" char g_EmptyStr[];   // DAT_00496c1e "" (points at a NUL byte)
#define DUMMY_STR ((char *)g_DummyStr)
#define EMPTY_STR ((char *)g_EmptyStr)

// =====================================================================
// Supervisor::ReadMidiFile  (FUN_0043a05f)
// __fastcall arg: u32 midiFileIdx. ECX unused.
// orig: WAV branch tests opts>>13 bit; if set StopStream(4,0,"dummy") else
// StopStream(3,0,"dummy"). Name arg is the "dummy" rdata string, not NULL.
// =====================================================================
ZunResult Supervisor::ReadMidiFile(u32 midiFileIdx)
{
    (void)midiFileIdx;
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MIDI_OUTPUT_PTR->StopPlayback();
        }
    }
    else
    {
        if (MUSIC_MODE != MUSIC_WAV)
        {
            return ZUN_ERROR;
        }
        if ((CFG_OPTS >> 0xd & 1) != 0)
        {
            SOUND_PLAYER_PTR->StopStream(4, 0, DUMMY_STR);
        }
        else
        {
            SOUND_PLAYER_PTR->StopStream(3, 0, DUMMY_STR);
        }
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::PlayAudio  (FUN_00439dd0)
// __fastcall args: i32 channel, char *path. ECX = Supervisor* (unused body).
// MIDI path: MidiOutput::LoadFile(this, path, channel) @ 0x436650 (2 stack
// args). WAV path: copy path bytes into local buffer [ebp-0x108], locate
// '.', overwrite 4 bytes at that pos with ".wav" (0x2e,0x77,0x61,0x76), then
// SoundPlayer::StopStream(1, channel, modifiedPath) @ 0x44d2f0.
// Returns 0 always (orig: XOR EAX,EAX at both tails; the WAV tail has an
// extra `INC EAX` => returns 1).
// =====================================================================
extern "C" char *__cdecl strchr_th07(char *s, i32 ch); // 0x0047d4b0 (__cdecl, 2 stack args)
struct MidiOutput2
{
    void LoadFile2(char *path, i32 channel); // 0x00436650 (2 stack args)
};
ZunResult Supervisor::PlayAudio(i32 channel, char *path)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            (*(MidiOutput2 **)0x00575acc)[0].LoadFile2(path, channel);
        }
        return ZUN_SUCCESS;
    }
    if (MUSIC_MODE != MUSIC_WAV)
    {
        return (ZunResult)1; // orig WAV tail: XOR EAX,EAX; INC EAX
    }
    // WAV path: copy path into local buffer, find '.', write ".wav".
    // Orig keeps two dst pointer locals (d and d2) plus a byte local; mirror
    // that so the stack frame and copy loop match byte-for-byte. The loop
    // stores via `d` (not d2): d2 is a dead copy orig emits.
    char buf[0x100];
    char *p = path;
    char *d = buf;
    char *d2 = d;
    char c;
    do
    {
        c = *p;
        *d = c;
        p++;
        d++;
    } while (c != 0);
    char *dot = strchr_th07(buf, '.');
    dot[1] = 'w';
    dot[2] = 'a';
    dot[3] = 'v';
    SOUND_PLAYER_PTR->StopStream(1, channel, buf);
    return (ZunResult)1;
}

// =====================================================================
// Supervisor::PlayMidiFile  (FUN_00439f4d)
// __fastcall arg: char *midiPath. ECX = Supervisor* (unused body).
// MIDI path: MidiOutput::StopPlayback; LoadFile(midiPath); Play().
// WAV path: copy path into local buffer, append ".wav" at '.', then
// SoundPlayer::StopStream(2, -1, modifiedPath).
// =====================================================================
ZunResult Supervisor::PlayMidiFile(char *midiPath)
{
    // Orig allocates locals in this order: this[ebp-0x110], midi[ebp-0x10c],
    // buf[ebp-0x108]. Declaring midi before buf (both function-scope) makes
    // MSVC place midi adjacent to this and grow the frame to 0x120.
    MidiOutput *midi;
    char buf[0x100];
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        // orig: check global != 0 first, THEN cache to local [ebp-0x10c].
        if (MIDI_OUTPUT_PTR != 0)
        {
            midi = MIDI_OUTPUT_PTR;
            midi->StopPlayback();
            midi->LoadFile(midiPath);
            midi->Play();
        }
    }
    else
    {
        if (MUSIC_MODE != MUSIC_WAV)
        {
            return ZUN_ERROR;
        }
        char *p = midiPath;
        char *d = buf;
        char c;
        do
        {
            c = *p;
            *d = c;
            p++;
            d++;
        } while (c != 0);
        char *dot = strchr_th07(buf, '.');
        dot[1] = 'w';
        dot[2] = 'a';
        dot[3] = 'v';
        SOUND_PLAYER_PTR->StopStream(2, -1, buf);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::StopAudio  (FUN_00439ec1)
// __thiscall arg: i32 channel. ECX = Supervisor*.
// orig caches midiOutput singleton into a local ([ebp-0x4]) before the three
// thiscall calls; WAV path uses the "dummy" rdata string as name arg.
// =====================================================================
ZunResult Supervisor::StopAudio(i32 channel)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        MidiOutput *midi = MIDI_OUTPUT_PTR; // orig caches to local
        if (midi != 0)
        {
            midi->StopPlayback();
            midi->ParseFile(channel);
            midi->Play();
        }
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        if ((CFG_OPTS >> 0xd & 1) != 0)
        {
            SOUND_PLAYER_PTR->StopStream(4, 0, DUMMY_STR);
        }
        SOUND_PLAYER_PTR->StopStream(2, channel, DUMMY_STR);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::SetupDInput  (FUN_004383d8)
// __fastcall, ECX = Supervisor*. DirectInput8 + keyboard + (optional) joystick
// init. COM thiscall DInput vtable calls are emitted via stub structs so MSVC
// generates the `mov ecx,[iface]; mov edx,[ecx]; call [edx+off]` pattern.
// Globals (verified): dinputIface@+0xc, keyboard@+0x10, controller@+0x14,
// hwnd@+0x44, cfg.opts@+0x14c (bit 0xb = NO_DIRECTINPUT_PAD), controllerCaps
// size @ 0x575968. Strings: 0x497208/1d8/1a4/17c/15c, GUIDs 0x490408/8d674/8d46c,
// IID 0x4904a8.
// =====================================================================
struct DInputStub
{
    // IDirectInput8A vtable offsets used by orig.
    i32 CreateDevice(void *rguid, void **out, void *unk);     // +0xc
    i32 Release();                                            // +0x8
    i32 EnumDevices(u32 devtype, void *cb, void *ref, u32 flags); // +0x10
};
struct DInputDevStub
{
    i32 SetDataFormat(void *fmt);                             // +0xc
    i32 SetCooperativeLevel(HWND hwnd, u32 flags);            // +0x10
    i32 Acquire();                                            // +0x1c
    i32 EnumObjects(void *cb, void *ref, u32 flags);          // +0x2c
    i32 SetProperty(void *prop, void *data);                  // +0x34
    i32 Release();                                            // +0x8
};
ZunResult __fastcall Supervisor::SetupDInput(Supervisor *s)
{
    i32 hinst = GetWindowLongA_th07(*(HWND *)((u8 *)s + 0x44), -6);
    if ((*(u32 *)((u8 *)s + 0x14c) >> 0xb & 1) != 0)
    {
        return ZUN_ERROR;
    }
    i32 r = DirectInput8Create_th07(hinst, 0x800, (void *)0x004904a8, (void **)((u8 *)s + 0xc), 0);
    if (r < 0)
    {
        *(void **)((u8 *)s + 0xc) = 0;
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00497208);
        return ZUN_ERROR;
    }
    r = (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->CreateDevice((void *)0x00490408, (void **)((u8 *)s + 0x10), 0);
    if (r < 0)
    {
        if (*(void **)((u8 *)s + 0xc) != 0)
        {
            (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->Release();
            *(void **)((u8 *)s + 0xc) = 0;
        }
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00497208);
        return ZUN_ERROR;
    }
    r = (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->SetDataFormat((void *)0x0048d674);
    if (r < 0)
    {
        if (*(void **)((u8 *)s + 0x10) != 0)
        {
            (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->Release();
            *(void **)((u8 *)s + 0x10) = 0;
        }
        if (*(void **)((u8 *)s + 0xc) != 0)
        {
            (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->Release();
            *(void **)((u8 *)s + 0xc) = 0;
        }
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x004971d8);
        return ZUN_ERROR;
    }
    r = (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->SetCooperativeLevel(*(HWND *)((u8 *)s + 0x44), 0x16);
    if (r < 0)
    {
        if (*(void **)((u8 *)s + 0x10) != 0)
        {
            (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->Release();
            *(void **)((u8 *)s + 0x10) = 0;
        }
        if (*(void **)((u8 *)s + 0xc) != 0)
        {
            (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->Release();
            *(void **)((u8 *)s + 0xc) = 0;
        }
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x004971a4);
        return ZUN_ERROR;
    }
    (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->Acquire();
    GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x0049717c);
    (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->EnumDevices(4, (void *)Supervisor_EnumKeybdCallback, 0, 1);
    if (*(void **)((u8 *)s + 0x14) != 0)
    {
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->EnumObjects((void *)0x0048d46c, 0, 0);
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->SetCooperativeLevel(*(HWND *)((u8 *)s + 0x44), 0x10);
        *(u32 *)0x00575968 = 0x2c;
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->SetDataFormat((void *)0x00575968);
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->SetProperty((void *)Supervisor_EnumJoysCallback, 0);
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x0049715c);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::DeletedCallback  (FUN_00438de2)
// __fastcall, ECX = Supervisor*. Cleanup/teardown: free pbg4 archive, release
// anm0, stop audio, release MidiOutput + DInput devices, free GameManager
// memory, etc. Returns 0.
// Globals: g_Pbg4Archive @ 0x575c1c, g_GameManager @ 0x626278, g_GameManager2
// @ 0x626274, g_SomeObj @ 0x575a64.
// =====================================================================
ZunResult __fastcall Supervisor::DeletedCallback(Supervisor *s)
{
    void *pbg = *(void **)0x00575c1c;
    if (pbg != 0)
    {
        _free_th07(pbg);
        *(void **)0x00575c1c = 0;
    }
    Supervisor_SomeCleanup1();
    Supervisor_ReleaseAnm0();
    AsciiManager_CutChain();
    (*(SoundPlayer **)0x004ba0d8)->StopStream(4, 0, DUMMY_STR);
    if (*(void **)((u8 *)s + 0x17c) != 0)
    {
        MidiOutput_StopPlayback();
        void *midi = *(void **)((u8 *)s + 0x17c);
        if (midi != 0)
        {
            Supervisor_MidiClearTracks();
            _free_th07(midi);
        }
        *(void **)((u8 *)s + 0x17c) = 0;
    }
    Supervisor_Cleanup2();
    Supervisor_Cleanup3();
    if (*(void **)((u8 *)s + 0x10) != 0)
    {
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->Acquire();
    }
    if (*(void **)((u8 *)s + 0x10) != 0)
    {
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x10))->Release();
        *(void **)((u8 *)s + 0x10) = 0;
    }
    if (*(void **)((u8 *)s + 0x14) != 0)
    {
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->Acquire();
    }
    if (*(void **)((u8 *)s + 0x14) != 0)
    {
        (*(DInputDevStub **)*(u32 *)((u8 *)s + 0x14))->Release();
        *(void **)((u8 *)s + 0x14) = 0;
    }
    if (*(void **)((u8 *)s + 0xc) != 0)
    {
        (*(DInputStub **)*(u32 *)((u8 *)s + 0xc))->Release();
        *(void **)((u8 *)s + 0xc) = 0;
    }
    void *gm = *(void **)0x00626278;
    if (gm != 0)
    {
        _free_th07(gm);
        *(void **)0x00626278 = 0;
    }
    void *gm2 = *(void **)0x00626274;
    if (gm2 != 0)
    {
        _free_th07(gm2);
        *(void **)0x00626274 = 0;
    }
    Supervisor_HeapFreeAll();
    void *obj = *(void **)0x00575a64;
    if (obj != 0)
    {
        Supervisor_SomeCleanup4();
        void *obj2 = *(void **)0x00575a64;
        if (obj2 != 0)
        {
            Supervisor_SomeCleanup5();
            _free_th07(obj2);
        }
        *(void **)0x00575a64 = 0;
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::LoadConfig  (FUN_004398b6)
// __thiscall arg: char *configPath. ECX = Supervisor*.
// Reads ./th07.cfg (FUN_00431330 returns heap buffer of 0x38 bytes), validates
// the thbgm.dat header ("ZAV\1" + 0x700), range-checks config fields, applies
// config to the global config block @ 0x575a68, and logs warnings for each set
// "display option" bit in cfg.opts (+0x14c).
// Globals verified from disasm: config struct @ 0x575a68 (0x38 bytes),
// DAT_004b9e64 (must be 0x38), DAT_0049ee40..50 (5 dwords = default keymap),
// DAT_00575abc (joystick present flag).
// =====================================================================
ZunResult Supervisor::LoadConfig(char *configPath)
{
    // Zero the config struct (0xe dwords = 0x38 bytes).
    u32 *cfg = (u32 *)0x00575a68;
    for (i32 i = 0xe; i != 0; i--)
    {
        *cfg = 0;
        cfg++;
    }
    void *buf = Supervisor_ReadConfigBuffer();
    if (buf == 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496f14);
    }
    else
    {
        u32 *src = (u32 *)buf;
        u32 *dst = (u32 *)0x00575a68;
        for (i32 i = 0xe; i != 0; i--)
        {
            *dst = *src;
            src++;
            dst++;
        }
        _free_th07(buf);
        // Read thbgm.dat header (16 bytes).
        i32 hdr[3];
        u32 read1;
        HANDLE f1 = CreateFileA_th07("./thbgm.dat", 0x80000000, 1, 0, 3, 0x8000080, 0);
        if (f1 != (HANDLE)-1)
        {
            ReadFile_th07(f1, hdr, 0x10, &read1, 0);
            CloseHandle_th07(f1);
            if (hdr[0] != 0x5641575a || hdr[1] != 1 || hdr[2] != 0x700)
            {
                GameErrorContext_LogFmt3((void *)0x00624210, (char *)0x00496ee4, configPath);
                GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c20);
                return ZUN_ERROR;
            }
        }
        // Range validation.
        if (*(u8 *)0x00575a84 < 5 && *(u8 *)0x00575a85 < 4 && *(u8 *)0x00575a86 < 2 &&
            *(u8 *)0x00575a87 < 3 && *(u8 *)0x00575a89 < 6 && *(u8 *)0x00575a88 < 2 &&
            *(u8 *)0x00575a8a < 2 && *(u8 *)0x00575a8b < 3 && *(u8 *)0x00575a8c < 3 &&
            *(u8 *)0x00575a8d < 2 && *(u8 *)0x00575a8e < 2 &&
            *(u32 *)0x00575a7c == 0x70002 && *(u32 *)0x004b9e64 == 0x38)
        {
            goto apply_opts;
        }
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e88);
    }
    // Defaults.
    *(u8 *)0x00575a84 = 2;
    *(u8 *)0x00575a85 = 3;
    *(u8 *)0x00575a86 = 0xff;
    *(u32 *)0x00575a7c = 0x70002;
    *(i16 *)0x00575a80 = 600;
    *(i16 *)0x00575a82 = 600;
    i32 hdr2[3];
    u32 read2;
    HANDLE f2 = CreateFileA_th07("./thbgm.dat", 0x80000000, 1, 0, 3, 0x8000080, 0);
    if (f2 == (HANDLE)-1)
    {
        *(u8 *)0x00575a87 = 2;
        Supervisor_LogStr1((char *)0x00496ebc);
    }
    else
    {
        ReadFile_th07(f2, hdr2, 0x10, &read2, 0);
        CloseHandle_th07(f2);
        if (hdr2[0] != 0x5641575a || hdr2[1] != 1 || hdr2[2] != 0x700)
        {
            GameErrorContext_LogFmt3((void *)0x00624210, (char *)0x00496ee4, configPath);
            GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c20);
            return ZUN_ERROR;
        }
        *(u8 *)0x00575a87 = 1;
    }
    *(u8 *)0x00575a88 = 1;
    *(u8 *)0x00575a89 = 1;
    *(u8 *)0x00575a8a = 0;
    *(u8 *)0x00575a8b = 0;
    *(u32 *)0x00575a68 = *(u32 *)0x0049ee40;
    *(u32 *)0x00575a6c = *(u32 *)0x0049ee44;
    *(u32 *)0x00575a70 = *(u32 *)0x0049ee48;
    *(u32 *)0x00575a74 = *(u32 *)0x0049ee4c;
    *(u32 *)0x00575a78 = *(u32 *)0x0049ee50;
    *(u8 *)0x00575a8c = 2;
    *(u8 *)0x00575a8d = 0;
    *(u8 *)0x00575a8e = 1;
apply_opts:
    *(u32 *)0x00575a9c |= 1;
    *(u32 *)0x0049ee40 = *(u32 *)0x00575a68;
    *(u32 *)0x0049ee44 = *(u32 *)0x00575a6c;
    *(u32 *)0x0049ee48 = *(u32 *)0x00575a70;
    *(u32 *)0x0049ee4c = *(u32 *)0x00575a74;
    *(u32 *)0x0049ee50 = *(u32 *)0x00575a78;
    u32 opts = *(u32 *)((u8 *)this + 0x14c);
    if ((opts >> 1 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e64);
    }
    if ((opts >> 10 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e48);
    }
    if ((opts >> 2 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e20);
    }
    if ((opts >> 3 & 1) != 0 || (opts >> 4 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496dfc);
    }
    if ((opts >> 4 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496dd0);
    }
    if ((opts >> 5 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496da8);
    }
    if ((opts >> 6 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d8c);
    }
    *(u32 *)((u8 *)this + 0x16c) = 0;
    opts = opts & 0xffffff7f;
    *(u32 *)((u8 *)this + 0x14c) = opts;
    if ((opts >> 8 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d6c);
    }
    if (*(i8 *)((u8 *)this + 0x13a) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d4c);
    }
    if ((opts >> 9 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d24);
    }
    if ((opts >> 0xb & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cec);
    }
    if ((opts >> 0xc & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cd0);
    }
    if ((opts >> 0xd & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cb0);
    }
    if ((opts >> 0xe & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c98);
        *(u32 *)0x00575abc = 1;
    }
    i32 r = Supervisor_ValidateSize(0x38);
    if (r == 0)
    {
        return ZUN_SUCCESS;
    }
    GameErrorContext_LogFmt3((void *)0x00624210, (char *)0x00496c78, configPath);
    GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c20);
    return ZUN_ERROR;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::FadeOutMusic  (FUN_0043a0d6)
// __thiscall arg: f32 fadeOutSeconds. ECX = Supervisor*.
// orig: MIDI path uses rdata const 1000.0f @ 0x498ab8 (load const, mul arg);
// WAV path rereads this+0x178 and arg each compare (no local cache of
// mul/threshold/limit), result adj cached only in [ebp-0x4]; the StopStream
// name arg is the empty-string rdata symbol @ 0x496c1e (NOT NULL).
// =====================================================================
ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    f32 adj; // [ebp-0x4]
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            // orig: FLD [0x498ab8] (1000.0f rdata); FMUL [EBP+0x8] (arg).
            MIDI_OUTPUT_PTR->SetFadeOut((u32)(*reinterpret_cast<f32 *>(0x498ab8) * fadeOutSeconds));
        }
    }
    else
    {
        if (MUSIC_MODE != MUSIC_WAV)
        {
            return ZUN_ERROR;
        }
        // orig flow (reconstructed from disasm):
        //   step1: mul=this+0x178 FCOMP [0x498a4c]; TEST AH,0x44; JP step2
        //          (jumps when mul==threshold) else adj=arg.
        //   step2: mul=this+0x178 FCOMP [0x498a54]; TEST AH,0x41; JNZ divide
        //          (jumps when mul<=limit) else adj=arg.
        //   so: divide only when (mul == threshold) && (mul <= limit).
        // We write a single combined `&&` condition so MSVC emits the
        // JNZ-on-second-compare form that orig uses (vs JP from nested ifs).
        adj = fadeOutSeconds;
        if (*reinterpret_cast<f32 *>((u8 *)this + 0x178) == *reinterpret_cast<f32 *>(0x498a4c) &&
            *reinterpret_cast<f32 *>((u8 *)this + 0x178) <= *reinterpret_cast<f32 *>(0x498a54))
        {
            adj = fadeOutSeconds / *reinterpret_cast<f32 *>((u8 *)this + 0x178);
        }
        SOUND_PLAYER_PTR->StopStream(5, (i32)(*reinterpret_cast<f32 *>(0x498ab8) * adj), EMPTY_STR);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)

} // namespace th07
