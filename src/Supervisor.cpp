// Supervisor module for th07 (Perfect Cherry Blossom).
// Pure C++ with a single unified code path: no naked asm, no #ifndef DIFFBUILD
// splits. #pragma var_order may be used to control MSVC stack layout matching.

#include "Supervisor.hpp"
#include "AsciiManager.hpp"
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Ending.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "MainMenu.hpp"
#include "MusicRoom.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "utils.hpp"

#include <windows.h>
#include <mmsystem.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

// Stubs for modules that aren't yet reverse-engineered. Their headers now
// exist (src/MainMenu.hpp etc.) and provide RegisterChain signatures; the
// bodies are no-ops that return ZUN_SUCCESS. ReplayManager::SaveReplay still
// needs a forward decl since ReplayManager.cpp doesn't expose it as a class
// method yet.
struct ReplayManager { static void SaveReplay(char *a, char *b); };

// Extern "C" stubs for functions not yet implemented as C++ classes.
extern "C" u16 __fastcall Controller_GetInput();
extern "C" void __fastcall CStreamingSound_UpdateFadeOut();
extern "C" void *_free_th07(void *p);
extern "C" void *memset(void *, int, size_t);
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" i16 __fastcall FloatToI16(f32 v);
extern "C" char *__cdecl strchr_th07(char *s, i32 ch);
extern "C" i32 __cdecl sprintf_th07(char *dst, const char *fmt, ...);
extern "C" void __fastcall Supervisor_SomeCleanup1();
extern "C" void __fastcall Supervisor_ReleaseAnm0();
extern "C" void __fastcall Supervisor_MidiClearTracks();
extern "C" void __fastcall Supervisor_Cleanup3();
extern "C" void __fastcall Supervisor_HeapFreeAll();
extern "C" void __fastcall Supervisor_SomeCleanup4();
extern "C" void __fastcall Supervisor_SomeCleanup5();
extern "C" void __cdecl Supervisor_LogStr1(char *fmt, ...);
extern "C" i32 __fastcall Supervisor_D3DDiscard(i32 mode);
extern "C" i32 __fastcall Supervisor_AutosaveScore(char *p1, i32 p2, i32 p3);
extern "C" void __fastcall MidiOutput_StopPlayback();
extern "C" void *__fastcall Supervisor_ReadConfigBuffer(char *path, i32 flag);
extern "C" i32 __fastcall Supervisor_ValidateSize(i32 size);

// AddedCallback callees (orig VAs verified from disasm of FUN_00438986).
// Most are __thiscall with ECX = a singleton pointer; declared here as plain
// externs so MSVC emits a direct call (objdiff tolerates the reloc). All
// pointer params use void* since th07:: types aren't visible at file scope.
extern "C" i32 __fastcall Supervisor_Callback6();                 // 0x00438668
extern "C" i32 __fastcall Supervisor_Callback7();                 // 0x004386f3
extern "C" void __fastcall Supervisor_Callback_Fun383d8(void *s); // 0x004383d8 (reads controller)
extern "C" void *__fastcall operator_new_th07(u32 size);          // 0x0047d441
extern "C" void __fastcall MidiOutput_Ctor(void *p);              // 0x00436450 (ECX = new'd buf)
extern "C" i32 __fastcall MidiOutput_Play(void *p, i32 a, char *path); // 0x00436650
extern "C" void __fastcall SoundPlayer_Callback_C7d0(void *p);   // 0x0044c7d0 (ECX = 0x4ba0d8)
extern "C" i32 __fastcall AnmManager_LoadAnm(void *anm, i32 a, char *path, i32 b); // 0x0044df90
extern "C" i32 __fastcall Callback_01e30();                       // 0x00401e30
extern "C" void __fastcall AnmManager_Callback_D630(void *anm);   // 0x0044d630
extern "C" void __fastcall Callback_3225b();                      // 0x0043225b
extern "C" i32 __fastcall SoundPlayer_LoadFmt(void *p, char *path); // 0x0044bff0 (ECX = 0x4ba0d8)
extern "C" void __fastcall SoundPlayer_Callback_C020(void *p, char *src); // 0x0044c020 (ECX = 0x4ba0d8)
extern "C" void *__fastcall Callback_44c20(void *cfg);           // 0x00444c20 (ECX = 0x4980c4)
extern "C" void __fastcall Callback_4547f(void *a, void *cfg);   // 0x0044547f
extern "C" void __fastcall Callback_454fc(void *a);              // 0x004454fc
extern "C" void __fastcall ListNode_Ctor(void *p);               // 0x004362a0 (ECX = new'd buf)
extern "C" void __fastcall Callback_378d0(void *p);              // 0x004378d0 (ECX = obj2)

// Stub structs for thiscall callees (ECX = singleton pointer).
struct MidiOutput {
    ZunResult StopPlayback();
    ZunResult LoadFile(char *path);
    void ClearTracks();
    ZunResult Play();
    ZunResult SetFadeOut(u32 ms);
    ZunResult ParseFile(i32 idx);
};
struct SoundPlayer {
    void SoundQueueAdd(i32 cmd, i32 param, char *name);
};

// Macros for audio globals.
#define MUSIC_MODE (g_Supervisor.cfg.musicMode)
#define CFG_OPTS (g_Supervisor.cfg.opts)
#define MIDI_OUTPUT_PTR ((MidiOutput *)g_Supervisor.midiOutput)
#define SOUND_PLAYER_PTR (*(SoundPlayer **)0x004ba0d8)
#define DUMMY_STR ((char *)0x004980d0)
#define EMPTY_STR ((char *)0x00496c1e)

namespace th07
{
DIFFABLE_STATIC(Supervisor, g_Supervisor);
DIFFABLE_STATIC(ControllerMapping, g_ControllerMapping);
DIFFABLE_STATIC(u16, g_LastFrameInput);
DIFFABLE_STATIC(u16, g_CurFrameInput);
DIFFABLE_STATIC(u16, g_IsEigthFrameOfHeldInput);
DIFFABLE_STATIC(u16, g_NumOfFramesInputsWereHeld);

extern "C" struct Chain g_Chain;

#pragma optimize("s", on)

// =====================================================================
// Supervisor::TickTimer (FUN_0043958d)
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
// Supervisor::OnUpdate (FUN_00437c70)
// =====================================================================
#pragma var_order(anmm0, anmm1, anmm2, anmm3, anmm4, anmm5, anmm6, anmm7, anmm8, wanted, cur1, cur5, cur2, cur6, cur8, cur9)
ChainCallbackResult __fastcall Supervisor::OnUpdate(Supervisor *s)
{
    u8 *anm;
    i32 wanted;
    i32 cur1, cur5, cur2, cur6, cur8, cur9;

    // AnmManager per-frame reset.
    anm = (u8 *)g_AnmManager;
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
    *(f32 *)((u8 *)g_AnmManager + 0x1c) = 0.0f;
    *(f32 *)((u8 *)g_AnmManager + 0x18) = 0.0f;
    *(u8 *)0x00575c0c = 0xff;
    if (*(void **)0x004bda94 != 0)
    {
        CStreamingSound_UpdateFadeOut();
    }

    if (*(i8 *)0x0062627d == 0)
    {
        g_LastFrameInput = g_CurFrameInput;
        g_CurFrameInput = Controller_GetInput();
        g_IsEigthFrameOfHeldInput = 0;
        if (g_LastFrameInput == g_CurFrameInput)
        {
            if (0x1e <= g_NumOfFramesInputsWereHeld)
            {
                g_IsEigthFrameOfHeldInput = (u16)(g_NumOfFramesInputsWereHeld % 8 == 0);
                if (0x26 < g_NumOfFramesInputsWereHeld)
                {
                    g_NumOfFramesInputsWereHeld = 0x1e;
                }
            }
            g_NumOfFramesInputsWereHeld = g_NumOfFramesInputsWereHeld + 1;
        }
        else
        {
            g_NumOfFramesInputsWereHeld = 0;
        }
    }
    else
    {
        g_CurFrameInput = g_CurFrameInput | Controller_GetInput();
    }

    s->wantedState2 = s->wantedState;
    if (s->wantedState != s->curState)
    {
        wanted = s->wantedState;
        Supervisor_LogStr1((char *)0x497230, s->wantedState, s->curState);

        switch (wanted)
        {
        case 0:
            goto reinit_mainmenu_d3d;
        case 1:
            cur1 = s->curState;
            switch (cur1)
            {
            case -1: return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 2:
                if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            case 4: return CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR;
            case 5:
                if (MusicRoom::RegisterChain(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            case 8:
                if (Ending::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            case 9:
                GameManager::CutChain();
                if (MainMenu::RegisterChain(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            }
            break;
        case 2:
            cur2 = s->curState;
            if (cur2 <= 7)
            {
                switch (cur2)
                {
                case 7:
                    GameManager::CutChain();
                    s->curState = 0;
                    ReplayManager::SaveReplay((char*)0, (char*)0);
                    s->curState = 1;
                    (*(IDirect3DDevice8 **)0x00575958)->ResourceManagerDiscardBytes(0);
                    if (Supervisor_D3DDiscard(1) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    break;
                case -1: return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                case 1:
                    GameManager::CutChain();
                    s->curState = 0;
                    ReplayManager::SaveReplay((char*)0, (char*)0);
                    goto reinit_mainmenu_d3d;
                case 3:
                    GameManager::CutChain();
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                case 6:
                    GameManager::CutChain();
                    if (MusicRoom::RegisterChain(1) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    break;
                }
            }
            else
            {
                switch (cur2)
                {
                case 9:
                    ((i32 *)0x62f52c)[*(i32 *)0x00626280 * 0xb]++;
                    GameManager::CutChain();
                    if (MainMenu::RegisterChain(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    break;
                case 0xa:
                    GameManager::CutChain();
                    if ((*(u32 *)0x0062f648 & 1) == 0 && *(i32 *)0x00626280 < 4)
                        *(i32 *)0x0062f85c = 0;
                    else
                        *(i32 *)0x0062f85c = *(i32 *)0x0062f85c - 1;
                    ReplayManager::SaveReplay((char*)0, (char*)0);
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                case 0xb:
                    *(i32 *)0x00575aa8 = 3;
                    GameManager::CutChain();
                    *(i32 *)0x0062f85c = *(i32 *)0x0062f85c - 1;
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                case 0xc:
                    *(i32 *)0x00575aa8 = 3;
                    GameManager::CutChain();
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                }
            }
            break;
        case 5:
            cur5 = s->curState;
            switch (cur5)
            {
            case -1: return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
            reinit_mainmenu_d3d:
                s->curState = 1;
                (*(IDirect3DDevice8 **)0x00575958)->ResourceManagerDiscardBytes(0);
                if (Supervisor_D3DDiscard(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            }
            break;
        case 6:
            cur6 = s->curState;
            switch (cur6)
            {
            case -1:
                ReplayManager::SaveReplay((char*)0, (char*)0);
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                ReplayManager::SaveReplay((char*)0, (char*)0);
                goto reinit_mainmenu_d3d;
            }
            break;
        case 8:
            cur8 = s->curState;
            switch (cur8)
            {
            case -1: return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                goto reinit_mainmenu_d3d;
            }
            break;
        case 9:
            cur9 = s->curState;
            switch (cur9)
            {
            case -1: return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                goto reinit_mainmenu_d3d;
            case 6:
                if (MusicRoom::RegisterChain(1) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                break;
            }
            break;
        }

        g_IsEigthFrameOfHeldInput = 0;
        g_LastFrameInput = 0;
        g_CurFrameInput = 0;
    }

    s->wantedState = s->curState;
    s->calcCount++;
    if (s->calcCount % 4000 == 3999)
    {
        if (Supervisor_AutosaveScore((char *)0x497228, *(i32 *)0x00575c14, *(i32 *)0x00575c10) != 0)
        {
            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::OnDraw (FUN_0043831b)
// =====================================================================
ChainCallbackResult __fastcall Supervisor::OnDraw(Supervisor *s)
{
    Supervisor::DrawFpsCounter(1);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)

// =====================================================================
// Supervisor::DrawFpsCounter (FUN_004390a5)
// =====================================================================
#pragma var_order(now, frameTime, frameTimeSec, fps, base)
void __fastcall Supervisor::DrawFpsCounter(i32 drawArg)
{
    DWORD now;
    f32 frameTime, frameTimeSec, fps, base;

    if (*(i8 *)0x0062627d != 0) goto end_check;
    *(i32 *)0x0135e1f0 += (i32)*(u8 *)0x00575a8b + 1;

    if (*(i32 *)0x00575bbc == 0)
    {
        if ((*(i32 *)0x0135e2a4 & 1) == 0)
        {
            *(i32 *)0x0135e2a4 |= 1;
            *(i32 *)0x0135e2a0 = timeGetTime();
        }
        now = timeGetTime();
        if (now < *(i32 *)0x0135e2a0)
        {
            *(i32 *)0x0135e2a0 = now;
            *(i32 *)0x0135e1f0 = 0;
        }
        if ((u32)(now - *(i32 *)0x0135e2a0) < 0x1f4) goto done_fps;
        frameTime = (f32)(i64)(now - *(i32 *)0x0135e2a0);
        frameTimeSec = frameTime / *(f32 *)0x00498ab8;
        *(i32 *)0x0135e2a0 = now;
    compute_fps:
        fps = (f32)(i64)*(i32 *)0x0135e1f0 / frameTimeSec;
        *(i32 *)0x0135e1f0 = 0;
        sprintf_th07((char *)0x135e0f0, "%.02ffps", (f64)fps);
        if ((*(i32 *)0x0062f648 >> 2 & 1) == 0 || drawArg == 0) goto done_fps;
        base = *(f32 *)0x00498a48;
        *(f32 *)0x00575ad4 += base;
        if (base * *(f32 *)0x00498b20 > fps)
            *(f32 *)0x00575ad0 += base;
        else if (base * *(f32 *)0x00498b1c > fps)
            *(f32 *)0x00575ad0 = base * *(f32 *)0x00498b18 + *(f32 *)0x00575ad0;
        else if (base * *(f32 *)0x00498a50 > fps)
            *(f32 *)0x00575ad0 = base * *(f32 *)0x00498aa0 + *(f32 *)0x00575ad0;
        else
            *(f32 *)0x00575ad0 = base * *(f32 *)0x00498a50 + *(f32 *)0x00575ad0;
        if ((*(i32 *)0x0062f648 >> 3 & 1) == 0)
        {
            *(i16 *)0x00575ad8 = FloatToI16(fps + *(f32 *)0x00498a50);
            sprintf_th07((char *)0x135dff0, "%2d", (i32)*(i16 *)0x00575ad8);
        }
        else
            sprintf_th07((char *)0x135dff0, "%2d", (i32)*(i16 *)0x00575ad8);
        goto done_fps;
    }
    else
    {
        i64 qpcNow;
        if (*(i32 *)0x0135e298 == 0)
            QueryPerformanceCounter((LARGE_INTEGER *)0x0135e298);
        QueryPerformanceCounter((LARGE_INTEGER *)&qpcNow);
        if (*(i32 *)((u8 *)&qpcNow + 0) < *(i32 *)0x0135e298)
        {
            *(i32 *)0x0135e298 = *(i32 *)((u8 *)&qpcNow + 0);
            *(i32 *)0x0135e29c = *(i32 *)((u8 *)&qpcNow + 4);
            *(i32 *)0x0135e1f0 = 0;
        }
        if ((u32)(*(i32 *)((u8 *)&qpcNow + 0) - *(i32 *)0x0135e298) <
            (u32)(*(i32 *)0x0135e298 + (*(i32 *)0x00575bbc >> 1))) goto end_check;
        frameTime = (f32)(i64)(*(i32 *)((u8 *)&qpcNow + 0) - *(i32 *)0x0135e298);
        frameTimeSec = frameTime / (f32)(i64)*(i32 *)0x00575bbc;
        *(i32 *)0x0135e298 = *(i32 *)((u8 *)&qpcNow + 0);
        *(i32 *)0x0135e29c = *(i32 *)((u8 *)&qpcNow + 4);
        *(i32 *)0x0135dfec += 1;
        if (*(i32 *)0x0135dfec % 8 == 0)
            g_Supervisor.TickTimer((i32 *)0, (f32 *)0);
        goto compute_fps;
    }
done_fps:
end_check:
    if (*(i32 *)0x00575ab8 != 0 || drawArg == 0) return;
    {
        D3DXVECTOR3 pos1;
        pos1.x = *(f32 *)0x00498b14;
        pos1.y = *(f32 *)0x00498b10;
        pos1.z = 0.0f;
        (*(AsciiManager **)0x0134ce18)->AddString(&pos1, (char *)0x135e0f0);
    }
    if ((*(i32 *)0x0062f648 >> 3 & 1) == 0 || (*(i32 *)0x0062f648 >> 2 & 1) == 0) return;
    {
        D3DXVECTOR3 pos2;
        pos2.x = *(f32 *)0x00498a6c;
        pos2.y = *(f32 *)0x00498a68;
        pos2.z = 0.0f;
        if (*(i32 *)0x00575bf8 != 0)
            *(i32 *)0x013542d8 = 0xffff4040;
        else
            *(i32 *)0x013542d8 = 0xffffffd0;
        (*(AsciiManager **)0x0134ce18)->AddString(&pos2, (char *)0x135dff0);
        *(i32 *)0x013542d8 |= -1;
    }
}

// =====================================================================
// Supervisor::RegisterChain (FUN_00439000)
// =====================================================================
#pragma var_order(chain, calcResult, supervisor)
ZunResult Supervisor::RegisterChain()
{
    Supervisor *supervisor = &g_Supervisor;
    ChainElem *chain;
    i32 calcResult;

    supervisor->wantedState = 0;
    supervisor->curState = -1;
    supervisor->calcCount = 0;

    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnUpdate);
    chain->arg = supervisor;
    chain->addedCallback = (ChainAddedCallback)Supervisor::AddedCallback;
    chain->deletedCallback = (ChainDeletedCallback)Supervisor::DeletedCallback;
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

// =============================================================================
// Supervisor::AddedCallback  --  FUN_00438986  (__fastcall, Supervisor* in ECX)
//
// Boot-critical one-time D3D + content initialisation, run by the chain when
// the supervisor element is first added. It:
//   1. Records QueryPerformanceFrequency into this+0x26c.
//   2. Clears the back buffer + front buffer to opaque black (BeginScene /
//      Clear / EndScene / Present) twice, calling Reset between failures.
//   3. Runs Supervisor::Callback6 (version/archive file probe) — bail on fail.
//   4. Loads data/title/th07logo.jpg as an Anm texture (idx 0).
//   5. Either calls Supervisor::Callback7 (single-frame logo path) or, if
//      DAT_00575abc is set, loops four BeginScene/AnmDraw/EndScene/Present
//      iterations (the four-frame boot-fade). Then AnmManager::Callback at 0.
//   6. Zeroes this+0x168 and this+0x164, stamps timeGetTime() into this+0x190,
//      mirrors its low 16 bits into [0x49fe20] (g_LastFrameTimeLow16), then
//      calls Supervisor::Callback_383d8.
//   7. Lazily allocates a 0x300-byte MidiOutput (operator new + ctor at
//      0x00436450), stores it into this+0x17c, and if non-NULL plays
//      bgm/init.mid (track 0x1e).
//   8. SoundPlayer(0x4ba0d8) Callback_C7d0, then AnmManager::LoadAnm(0,
//      "data/text.anm", 0x700); on failure bail.
//   9. Callback_01e30 (ZunResult check, error log on fail).
//  10. AnmManager::Callback_D630, Callback_3225b, then
//      SoundPlayer::LoadFmt(0x4ba0d8, "bgm/thbgm.fmt"); on failure bail.
//  11. Picks one of two thbgm.dat descriptor tables (0x496fac or 0x497150)
//      depending on DAT_004bdaa0 (alternate-game-mode flag) and
//      cfg.opts bit 0xd (fast-load flag); either copies it verbatim into the
//      0x4bd994..0x4bd99c slot or calls SoundPlayer::Callback_C020 to install
//      it through the legacy path.
//  12. Callback_44c20 (CFG_0x4980c4) returns a handle; zeroes the 0x58-dword
//      wave-format block at 0x62f4e0, primes it as a WAVEFORMATEX (tag 0x0001,
//      0x160 channels, 0x160 samples/sec), calls Callback_4547f/454fc.
//  13. Allocates a 0x14-byte list node (operator new + ListNode_Ctor), stamps
//      vtable 0x496c0c into [node], stores it into DAT_00575a64 (g_Supervisor
//      obj2 slot), and calls Callback_378d0 on it.
//
// Returns ZUN_SUCCESS (0) on the happy path, 0xfffffffe on the early
// logo-load abort, 0xffffffff on any other failure.
// =============================================================================
#pragma var_order(s, dev, i, newObj, midiNewObj, cfgHandle, listNode)
ZunResult __fastcall Supervisor::AddedCallback(Supervisor *s)
{
    IDirect3DDevice8 *dev;
    i32 i;
    void *newObj, *midiNewObj, *cfgHandle, *listNode;

    // 1. QueryPerformanceFrequency(this + 0x26c).
    QueryPerformanceFrequency((LARGE_INTEGER *)((u8 *)s + 0x26c));

    // 2. Back buffer clear, twice (BeginScene/Clear/EndScene/Present + retry).
    dev = *(IDirect3DDevice8 **)0x00575958;
    dev->BeginScene();
    dev->Clear(0, 0, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0);
    dev->EndScene();
    if (dev->Present(0, 0, 0, 0) < 0)
    {
        dev->Reset((D3DPRESENT_PARAMETERS *)0x575a30);
    }
    dev->BeginScene();
    dev->Clear(0, 0, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0);
    dev->EndScene();
    if (dev->Present(0, 0, 0, 0) < 0)
    {
        dev->Reset((D3DPRESENT_PARAMETERS *)0x575a30);
    }

    // 3. Callback6 (version/archive probe). Bail -1 on fail.
    if (Supervisor_Callback6() != 0)
    {
        return (ZunResult)-1;
    }

    // 4. AnmManager::LoadTexture(this, 0, "data/title/th07logo.jpg") @ 0x4547b0.
    ((void (*)(AnmManager *, i32, char *))0x004547b0)(g_AnmManager, 0, (char *)0x497038);
    *(i32 *)0x00575ab8 = 1;

    // 5. Either single-frame logo path or four-frame boot fade.
    if (*(i32 *)0x00575abc == 0)
    {
        if (Supervisor_Callback7() != 0)
        {
            ((void (*)(AnmManager *, i32))0x00454a10)(g_AnmManager, 0);
            return (ZunResult)-2;
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            dev->BeginScene();
            ((void (*)(AnmManager *, i32, i32, i32, i32))0x00454aa0)(g_AnmManager, 0, 0, 0, 0);
            dev->EndScene();
            if (dev->Present(0, 0, 0, 0) < 0)
            {
                dev->Reset((D3DPRESENT_PARAMETERS *)0x575a30);
            }
        }
    }
    ((void (*)(AnmManager *, i32))0x00454a10)(g_AnmManager, 0);

    // 6. Reset frame counters + stamp startup time.
    *(i32 *)((u8 *)s + 0x168) = 0;
    *(i32 *)((u8 *)s + 0x164) = 0;
    *(u32 *)((u8 *)s + 0x190) = timeGetTime();
    *(u16 *)0x0049fe20 = *(u16 *)((u8 *)s + 0x190);
    Supervisor_Callback_Fun383d8(s);

    // 7. Lazy MidiOutput allocation + bgm/init.mid.
    if (*(void **)((u8 *)s + 0x17c) == 0)
    {
        newObj = operator_new_th07(0x300);
        if (newObj != 0)
        {
            MidiOutput_Ctor(newObj);
            midiNewObj = newObj;
        }
        else
        {
            midiNewObj = 0;
        }
        *(void **)((u8 *)s + 0x17c) = midiNewObj;
    }
    if (*(void **)((u8 *)s + 0x17c) != 0)
    {
        MidiOutput_Play(*(void **)((u8 *)s + 0x17c), 0x1e, (char *)0x497028);
    }

    // 8. SoundPlayer Callback_C7d0; AnmManager::LoadAnm(0, "data/text.anm", 0x700).
    SoundPlayer_Callback_C7d0((void *)0x004ba0d8);
    if (AnmManager_LoadAnm(g_AnmManager, 0, (char *)0x497018, 0x700) != 0)
    {
        return (ZunResult)-1;
    }

    // 9. Callback_01e30 (error-logged failure path).
    if (Callback_01e30() != 0)
    {
        g_GameErrorContext.Log("%s", (char *)0x496ff0);
        return (ZunResult)-1;
    }

    // 10. AnmManager::Callback_D630; Callback_3225b; SoundPlayer::LoadFmt.
    AnmManager_Callback_D630(g_AnmManager);
    Callback_3225b();
    if (SoundPlayer_LoadFmt((void *)0x004ba0d8, (char *)0x496fe0) != 0)
    {
        g_GameErrorContext.Log("%s", (char *)0x496fb8);
        return (ZunResult)-1;
    }

    // 11. thbgm descriptor table install (depends on alt-mode + opts bit 0xd).
    if (*(i32 *)0x004bdaa0 == 0)
    {
        if ((*(u32 *)0x00575a9c >> 0xd & 1) == 0)
        {
            SoundPlayer_Callback_C020((void *)0x004ba0d8, (char *)0x496fac);
        }
        else
        {
            memcpy((void *)0x004bd994, (void *)0x496fac, 10);
        }
    }
    else
    {
        if ((*(u32 *)0x00575a9c >> 0xd & 1) == 0)
        {
            SoundPlayer_Callback_C020((void *)0x004ba0d8, (char *)0x497150);
        }
        else
        {
            memcpy((void *)0x004bd994, (void *)0x497150, 9);
        }
    }

    // 12. Callback_44c20 returns a CFG handle; prime WAVEFORMATEX at 0x62f4e0.
    cfgHandle = Callback_44c20((void *)0x004980c4);
    memset((void *)0x0062f4e0, 0, 0x58 * 4);
    *(u16 *)0x0062f4e4 = 0x160;          // nChannels
    *(u16 *)0x0062f4e6 = *(u16 *)0x0062f4e4; // mirror (orig re-reads)
    *(u32 *)0x0062f4e0 = 0x54534c50;     // 'PLST' magic
    *(u8 *)0x0062f4e8 = 1;
    Callback_4547f(cfgHandle, (void *)0x0062f4e0);
    Callback_454fc(cfgHandle);

    // 13. Allocate a 0x14-byte list node, install vtable, store + init.
    listNode = operator_new_th07(0x14);
    if (listNode != 0)
    {
        ListNode_Ctor(listNode);
        *(void **)listNode = (void *)0x00496c0c;
    }
    else
    {
        listNode = 0;
    }
    *(void **)0x00575a64 = listNode;
    if (*(void **)0x00575a64 != 0)
    {
        Callback_378d0(*(void **)0x00575a64);
    }

    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::ReadMidiFile (FUN_0043a05f)
// =====================================================================
#pragma var_order(this_save)
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
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        if ((CFG_OPTS >> 0xd & 1) != 0)
            SOUND_PLAYER_PTR->SoundQueueAdd(4, 0, DUMMY_STR);
        else
            SOUND_PLAYER_PTR->SoundQueueAdd(3, 0, DUMMY_STR);
    }
    else
    {
        return ZUN_ERROR;
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::PlayAudio (FUN_00439dd0)
// =====================================================================
ZunResult Supervisor::PlayAudio(i32 channel, char *path)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MidiOutput *midi = MIDI_OUTPUT_PTR;
            midi->StopPlayback();
            midi->LoadFile(path);
            midi->Play();
        }
        return ZUN_SUCCESS;
    }
    if (MUSIC_MODE != MUSIC_WAV)
    {
        return (ZunResult)1;
    }
    {
        char buf[0x100];
        char *p = path;
        char *d = buf;
        char *d2 = d;
        char c;
        do {
            c = *p;
            *d = c;
            p++;
            d++;
        } while (c != 0);
        (void)d2;
        char *dot = strchr_th07(buf, '.');
        dot[1] = 'w'; dot[2] = 'a'; dot[3] = 'v';
        SOUND_PLAYER_PTR->SoundQueueAdd(1, channel, buf);
    }
    return (ZunResult)1;
}

// =====================================================================
// Supervisor::StopAudio (FUN_00439ec1)
// =====================================================================
#pragma var_order(midi)
ZunResult Supervisor::StopAudio(i32 channel)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        MidiOutput *midi = MIDI_OUTPUT_PTR;
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
            SOUND_PLAYER_PTR->SoundQueueAdd(4, 0, DUMMY_STR);
        SOUND_PLAYER_PTR->SoundQueueAdd(2, channel, DUMMY_STR);
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::PlayMidiFile (FUN_00439f4d)
// =====================================================================
ZunResult Supervisor::PlayMidiFile(char *midiPath)
{
    char buf[0x100];
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MidiOutput *midi = MIDI_OUTPUT_PTR;
            midi->StopPlayback();
            midi->LoadFile(midiPath);
            midi->Play();
        }
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        char *p = midiPath;
        char *d = buf;
        char *d2 = d;
        char c;
        do { c = *p; *d = c; p++; d++; } while (c != 0);
        (void)d2;
        char *dot = strchr_th07(buf, '.');
        dot[1] = 'w'; dot[2] = 'a'; dot[3] = 'v';
        SOUND_PLAYER_PTR->SoundQueueAdd(2, -1, buf);
    }
    else
    {
        return ZUN_ERROR;
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::SetupDInput (FUN_004383d8)
// =====================================================================
#pragma var_order(hinst)
ZunResult __fastcall Supervisor::SetupDInput(Supervisor *s)
{
    i32 hinst;
    hinst = GetWindowLongA((HWND)s->hwndGameWindow, GWL_HINSTANCE);
    if ((s->cfg.opts >> GCOS_NO_DIRECTINPUT_PAD & 1) != 0)
    {
        return ZUN_ERROR;
    }
    if (DirectInput8Create((HINSTANCE)hinst, DIRECTINPUT_VERSION,
                           IID_IDirectInput8A, (LPVOID *)&s->dinputIface, NULL) < 0)
    {
        s->dinputIface = NULL;
        g_GameErrorContext.Log("%s", "DirectInput not available");
        return ZUN_ERROR;
    }
    if (((IDirectInput8A *)s->dinputIface)->CreateDevice(GUID_SysKeyboard,
                                                          (LPDIRECTINPUTDEVICE8A *)&s->keyboard, NULL) < 0)
    {
        if (s->dinputIface) { ((IDirectInput8A *)s->dinputIface)->Release(); s->dinputIface = NULL; }
        g_GameErrorContext.Log("%s", "DirectInput not available");
        return ZUN_ERROR;
    }
    if (((IDirectInputDevice8A *)s->keyboard)->SetDataFormat(&c_dfDIKeyboard) < 0)
    {
        if (s->keyboard) { ((IDirectInputDevice8A *)s->keyboard)->Release(); s->keyboard = NULL; }
        if (s->dinputIface) { ((IDirectInput8A *)s->dinputIface)->Release(); s->dinputIface = NULL; }
        g_GameErrorContext.Log("%s", "SetDataFormat failed");
        return ZUN_ERROR;
    }
    if (((IDirectInputDevice8A *)s->keyboard)->SetCooperativeLevel((HWND)s->hwndGameWindow,
                                                                    DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY) < 0)
    {
        if (s->keyboard) { ((IDirectInputDevice8A *)s->keyboard)->Release(); s->keyboard = NULL; }
        if (s->dinputIface) { ((IDirectInput8A *)s->dinputIface)->Release(); s->dinputIface = NULL; }
        g_GameErrorContext.Log("%s", "SetCooperativeLevel failed");
        return ZUN_ERROR;
    }
    ((IDirectInputDevice8A *)s->keyboard)->Acquire();
    g_GameErrorContext.Log("%s", "DirectInput initialized");
    ((IDirectInput8A *)s->dinputIface)->EnumDevices(DI8DEVCLASS_GAMECTRL,
                                                      (LPDIENUMDEVICESCALLBACKA)0x0043832f, NULL, DIEDFL_ATTACHEDONLY);
    if (s->controller)
    {
        ((IDirectInputDevice8A *)s->controller)->EnumObjects((LPDIENUMDEVICEOBJECTSCALLBACK)0x0048d46c, NULL, DIDFT_ALL);
        ((IDirectInputDevice8A *)s->controller)->SetCooperativeLevel((HWND)s->hwndGameWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        ((IDirectInputDevice8A *)s->controller)->SetDataFormat(&c_dfDIJoystick2);
        g_GameErrorContext.Log("%s", "Pad found");
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::DeletedCallback (FUN_00438de2)
// =====================================================================
#pragma var_order(pbg, midi, gm, gm2, obj2)
ZunResult __fastcall Supervisor::DeletedCallback(Supervisor *s)
{
    void *pbg, *midi, *gm, *gm2, *obj2;

    pbg = g_Pbg4Archive;
    if (pbg != NULL)
    {
        _free_th07(pbg);
        g_Pbg4Archive = NULL;
    }
    Supervisor_SomeCleanup1();
    Supervisor_ReleaseAnm0();
    AsciiManager::CutChain();
    SOUND_PLAYER_PTR->SoundQueueAdd(4, 0, DUMMY_STR);
    if (s->midiOutput != NULL)
    {
        MidiOutput_StopPlayback();
        midi = s->midiOutput;
        if (midi != NULL)
        {
            Supervisor_MidiClearTracks();
            _free_th07(midi);
        }
        s->midiOutput = NULL;
    }
    ReplayManager::SaveReplay(NULL, NULL);
    Supervisor_Cleanup3();
    if (s->keyboard != NULL)
        ((IDirectInputDevice8A *)s->keyboard)->Unacquire();
    if (s->keyboard != NULL)
    {
        ((IDirectInputDevice8A *)s->keyboard)->Release();
        s->keyboard = NULL;
    }
    if (s->controller != NULL)
        ((IDirectInputDevice8A *)s->controller)->Unacquire();
    if (s->controller != NULL)
    {
        ((IDirectInputDevice8A *)s->controller)->Release();
        s->controller = NULL;
    }
    if (s->dinputIface != NULL)
    {
        ((IDirectInput8A *)s->dinputIface)->Release();
        s->dinputIface = NULL;
    }
    gm = *(void **)0x00626278;
    if (gm != NULL) { _free_th07(gm); *(void **)0x00626278 = NULL; }
    gm2 = *(void **)0x00626274;
    if (gm2 != NULL) { _free_th07(gm2); *(void **)0x00626274 = NULL; }
    Supervisor_HeapFreeAll();
    obj2 = *(void **)0x00575a64;
    if (obj2 != NULL)
    {
        Supervisor_SomeCleanup4();
        obj2 = *(void **)0x00575a64;
        if (obj2 != NULL)
        {
            Supervisor_SomeCleanup5();
            _free_th07(obj2);
        }
        *(void **)0x00575a64 = NULL;
    }
    return ZUN_SUCCESS;
}

// =====================================================================
// Supervisor::LoadConfig (FUN_004398b6)
// =====================================================================
#pragma var_order(buf, f1, read1, hdr1_0, hdr1_1, hdr1_2, f2, read2, hdr2_0, hdr2_1, hdr2_2, _pad1, _pad2)
ZunResult Supervisor::LoadConfig(char *configPath)
{
    void *buf;
    HANDLE f1, f2;
    u32 read1, read2;
    i32 hdr1_0, hdr1_1, hdr1_2;
    i32 hdr2_0, hdr2_1, hdr2_2;
    i32 _pad1, _pad2;

    memset(&g_Supervisor.cfg, 0, sizeof(GameConfiguration));
    buf = Supervisor_ReadConfigBuffer(configPath, 1);
    if (buf == NULL)
    {
        g_GameErrorContext.Log("%s", "Config not found");
    }
    else
    {
        memcpy(&g_Supervisor.cfg, buf, sizeof(GameConfiguration));
        _free_th07(buf);
        f1 = CreateFileA("./thbgm.dat", GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (f1 != INVALID_HANDLE_VALUE)
        {
            ReadFile(f1, &hdr1_0, 0x10, (DWORD *)&read1, NULL);
            CloseHandle(f1);
            if (hdr1_0 != 0x5641575a || hdr1_1 != 1 || hdr2_2 != 0x700)
            {
                g_GameErrorContext.Fatal("%s %s", "Invalid bgm dat", configPath);
                (void)g_GameErrorContext.Fatal("%s", "Write protect or disk full");
                return ZUN_ERROR;
            }
        }
        if (g_Supervisor.cfg.lifeCount < 5 && g_Supervisor.cfg.bombCount < 4 &&
            g_Supervisor.cfg.colorMode16bit < 2 && g_Supervisor.cfg.musicMode < 3 &&
            g_Supervisor.cfg.defaultDifficulty < 6 && g_Supervisor.cfg.playSounds < 2 &&
            g_Supervisor.cfg.windowed < 2 && g_Supervisor.cfg.frameskipConfig < 3 &&
            *(u8 *)((u8 *)&g_Supervisor.cfg + 0x24) < 3 && *(u8 *)((u8 *)&g_Supervisor.cfg + 0x25) < 2 &&
            *(u8 *)((u8 *)&g_Supervisor.cfg + 0x26) < 2 &&
            g_Supervisor.cfg.version == GAME_VERSION && *(u32 *)0x004b9e64 == 0x38)
        {
            goto apply_opts;
        }
        g_GameErrorContext.Log("%s", "Config corrupted");
    }
    g_Supervisor.cfg.lifeCount = 2;
    g_Supervisor.cfg.bombCount = 3;
    g_Supervisor.cfg.colorMode16bit = 0xff;
    g_Supervisor.cfg.version = GAME_VERSION;
    g_Supervisor.cfg.padXAxis = 600;
    g_Supervisor.cfg.padYAxis = 600;
    f2 = CreateFileA("./thbgm.dat", GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f2 == INVALID_HANDLE_VALUE)
    {
        g_Supervisor.cfg.musicMode = 2;
        Supervisor_LogStr1("%s", "No wave file");
    }
    else
    {
        ReadFile(f2, &hdr2_0, 0x10, (DWORD *)&read2, NULL);
        CloseHandle(f2);
        if (hdr2_0 != 0x5641575a || hdr2_1 != 1 || hdr2_2 != 0x700)
        {
            g_GameErrorContext.Fatal("%s %s", "Invalid bgm dat", configPath);
            (void)g_GameErrorContext.Fatal("%s", "Write protect or disk full");
            return ZUN_ERROR;
        }
        g_Supervisor.cfg.musicMode = 1;
    }
    g_Supervisor.cfg.playSounds = 1;
    g_Supervisor.cfg.defaultDifficulty = 1;
    g_Supervisor.cfg.windowed = 0;
    g_Supervisor.cfg.frameskipConfig = 0;
    g_Supervisor.cfg.controllerMapping = g_ControllerMapping;
    *(u8 *)((u8 *)&g_Supervisor.cfg + 0x24) = 2;
    *(u8 *)((u8 *)&g_Supervisor.cfg + 0x25) = 0;
    *(u8 *)((u8 *)&g_Supervisor.cfg + 0x26) = 1;
apply_opts:
    g_Supervisor.cfg.opts |= 1;
    g_ControllerMapping = g_Supervisor.cfg.controllerMapping;
    if ((g_Supervisor.cfg.opts >> 1 & 1) != 0)
        g_GameErrorContext.Log("%s", "No vertex buffer");
    if ((g_Supervisor.cfg.opts >> 10 & 1) != 0)
        g_GameErrorContext.Log("%s", "No fog");
    if ((g_Supervisor.cfg.opts >> 2 & 1) != 0)
        g_GameErrorContext.Log("%s", "16-bit textures");
    if ((g_Supervisor.cfg.opts >> 3 & 1) != 0 || (g_Supervisor.cfg.opts >> 4 & 1) != 0)
        g_GameErrorContext.Log("%s", "Force backbuffer clear");
    if ((g_Supervisor.cfg.opts >> 4 & 1) != 0)
        g_GameErrorContext.Log("%s", "Don't render items");
    if ((g_Supervisor.cfg.opts >> 5 & 1) != 0)
        g_GameErrorContext.Log("%s", "No gouraud shading");
    if ((g_Supervisor.cfg.opts >> 6 & 1) != 0)
        g_GameErrorContext.Log("%s", "No depth testing");
    *(u32 *)((u8 *)&g_Supervisor + 0x16c) = 0;
    g_Supervisor.cfg.opts &= 0xffffff7f;
    if ((g_Supervisor.cfg.opts >> 8 & 1) != 0)
        g_GameErrorContext.Log("%s", "No texture color compositing");
    if (*(i8 *)((u8 *)&g_Supervisor + 0x13a) != 0)
        g_GameErrorContext.Log("%s", "Launch windowed");
    if ((g_Supervisor.cfg.opts >> 9 & 1) != 0)
        g_GameErrorContext.Log("%s", "Force reference rasterizer");
    if ((g_Supervisor.cfg.opts >> 0xb & 1) != 0)
        g_GameErrorContext.Log("%s", "Do not use DirectInput");
    if ((g_Supervisor.cfg.opts >> 0xc & 1) != 0)
        g_GameErrorContext.Log("%s", "No color compression");
    if ((g_Supervisor.cfg.opts >> 0xd & 1) != 0)
        g_GameErrorContext.Log("%s", "Force 60fps");
    if ((g_Supervisor.cfg.opts >> 0xe & 1) != 0)
    {
        g_GameErrorContext.Log("%s", "Force 60fps mode");
        *(u32 *)0x00575abc = 1;
    }
    if (Supervisor_ValidateSize(0x38) == 0)
    {
        return ZUN_SUCCESS;
    }
    (void)g_GameErrorContext.Fatal("%s %s", "File cannot be exported", configPath);
    (void)g_GameErrorContext.Fatal("%s", "Write protect or disk full");
    return ZUN_ERROR;
}

// =====================================================================
// Supervisor::FadeOutMusic (FUN_0043a0d6)
// =====================================================================
#pragma var_order(adj)
ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    f32 adj;
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
        {
            MIDI_OUTPUT_PTR->SetFadeOut((u32)(1000.0f * fadeOutSeconds));
        }
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        adj = fadeOutSeconds;
        if (*(f32 *)((u8 *)this + 0x178) == *(f32 *)0x00498a4c)
        {
            if (*(f32 *)((u8 *)this + 0x178) <= *(f32 *)0x00498a54)
            {
                adj = fadeOutSeconds / *(f32 *)((u8 *)this + 0x178);
            }
        }
        SOUND_PLAYER_PTR->SoundQueueAdd(5, (i32)(1000.0f * adj), EMPTY_STR);
    }
    else
    {
        return ZUN_ERROR;
    }
    return ZUN_SUCCESS;
}

} // namespace th07
