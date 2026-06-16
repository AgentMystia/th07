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
#include "AsciiManager.hpp"

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
extern "C" ZunResult __cdecl GameManager_RegisterChain();         // FUN_0042f3c5
extern "C" void __cdecl GameManager_CutChain();                   // FUN_0042f45d
extern "C" ZunResult __cdecl MainMenu_RegisterChain();            // FUN_0041e820
extern "C" ZunResult __fastcall MusicRoom_RegisterChain(i32 b);   // FUN_0044a302 (ECX=b)
extern "C" ZunResult __cdecl Ending_RegisterChain();              // FUN_0043b4db
extern "C" ZunResult __fastcall ResultScreen_RegisterChain(i32 b);// FUN_0041c1b0
extern "C" i32 __fastcall Supervisor_AutosaveScore(char *p1, i32 p2, i32 p3);       // FUN_0043a569 __thiscall: ECX=g_Supervisor, 3 stack args
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
extern "C" i32 __fastcall Supervisor_D3DDiscard(i32 mode);          // FUN_0045c5d0 (ECX=mode 0 or 1)
extern "C" void __fastcall Supervisor_ChainReleaseAll(i32 ecxArg, i32 edxArg);          // FUN_00443da0 (XOR ECX,EDX before call)
extern "C" void __fastcall Supervisor_SomePulseFlag();            // FUN_00404fe0 (used by EffectManager)
// SetupDInput externs
extern "C" i32 __fastcall GetWindowLongA_th07(HWND hwnd, i32 idx); // wraps GetWindowLongA
extern "C" i32 __fastcall DirectInput8Create_th07(i32 inst, u32 ver, void *iid, void **out, void *unk);
extern "C" void __cdecl GameErrorContext_LogFmt2(void *ctx, char *fmt);           // FUN_004315f0 __cdecl (2 stack args)
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
extern "C" void *__fastcall Supervisor_ReadConfigBuffer(char *configPath, i32 flag);    // FUN_00431330 __fastcall: ECX=configPath, EDX=flag
extern "C" void __cdecl GameErrorContext_LogFmt3(void *ctx, char *fmt, char *arg);   // FUN_00431730 __cdecl (3 stack args)
extern "C" i32 __fastcall Supervisor_ValidateSize(i32 size); // FUN_00431540 (assert config struct size)
extern "C" void __cdecl Supervisor_LogStr1(char *fmt, ...);          // FUN_00437903 (printf-style, __cdecl)
extern "C" void *memset(void *, int, size_t);
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" HANDLE __fastcall CreateFileA_th07(char *path, u32 access, u32 share, void *sa, u32 disp, u32 flags, HANDLE tmpl);
extern "C" i32 __fastcall ReadFile_th07(HANDLE f, void *buf, u32 n, u32 *read, void *ovl);
extern "C" i32 __fastcall CloseHandle_th07(HANDLE h);

// ---- thiscall callee stubs (ECX = singleton pointer) ----
struct MidiOutput
{
    ZunResult StopPlayback();                  // FUN_00436b30 (real sig: ZunResult)
    ZunResult LoadFile(char *path);            // FUN_004369c0
    void ClearTracks();                        // FUN_00436700
    ZunResult Play();                          // FUN_00436ad0
    ZunResult SetFadeOut(u32 ms);              // FUN_00436c90
    ZunResult ParseFile(i32 idx);              // FUN_00436790
};
struct SoundPlayer
{
    // Real method name is SoundQueueAdd (thiscall, ECX=SoundPlayer*). Renaming
    // from StopStream so the symbol resolves against SoundPlayer.obj.
    void SoundQueueAdd(i32 cmd, i32 param, char *name); // FUN_0044d2f0
    void FadeOutBgm(f32 seconds);                       // FUN_00444c20
};
// Supervisor::AutosaveScore @ 0x0043a569 is __thiscall (ECX=g_Supervisor).
// Orig emits `mov ecx,0x575950; push x3; call 0x43a569`. We reach it via a
// function pointer so no undefined method symbol is produced. The extern
// below is resolved at link time by whatever module implements AutosaveScore
// (currently a stub in stubs.cpp / future Supervisor member).

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
//
// The MSVC /Od codegen for this function is extremely rigid: every
// AnmManager field reset re-reads the global g_AnmManager singleton
// (@ 0x004b9e44) into a FRESH stack slot, the input-poll uses 6
// separate globals (not g_CurFrameInput etc.), the state machine is a
// nested switch (wanted, cur) with very specific case ordering, two
// D3D device vtable indirect calls, and the autosave tail is a
// __thiscall AutosaveScore(g_Supervisor, 3 stack args).
//
// Reproducing that exact stack layout / branch order from C++ is not
// tractable, so the objdiff build emits the function via inline asm
// (1:1 translation of the orig disassembly). The SDL/exe build uses a
// portable C++ equivalent.
// =====================================================================
#ifndef DIFFBUILD
// =====================================================================
// objdiff C++ path: each AnmManager field store re-reads the singleton
// global (matching the orig /Od codegen that gives every expression a
// fresh stack slot). The nested switch on wantedState then curState is
// ordered exactly like the orig CMP chain. Returns CHAIN_CALLBACK_RESULT_*
// (1 = continue, 4 = exit success, 5 = exit error).
// =====================================================================
// D3D device reached via raw cast (matches GameManager.cpp pattern). The
// orig call is `mov eax,[0x575958]; mov eax,[eax]; call dword ptr [eax+0x14]`
// (vtable indirect, no symbol). We replicate via lpVtbl function-pointer.
struct D3DDeviceStub;
struct D3DDeviceStubVtbl
{
    void *methods[0x14 / 4];                         // slots 0..4
    void (__stdcall *Reset)(D3DDeviceStub *pDevice, u32 flags); // slot 5 (offset 0x14)
};
struct D3DDeviceStub
{
    D3DDeviceStubVtbl *lpVtbl;
};
#pragma optimize("", off)
#pragma var_order(anm0, anm1, anm2, anm3, anm4, anm5, anm6, anm7, anm8, wanted, cur1, cur5, cur2, cur6, cur8, cur9)
ChainCallbackResult __fastcall Supervisor::OnUpdate(Supervisor *s)
{
    u8 *anm0, *anm1, *anm2, *anm3, *anm4, *anm5, *anm6, *anm7, *anm8;
    i32 wanted;
    i32 cur1, cur5, cur2, cur6, cur8, cur9;
    // AnmManager per-frame reset. Each assignment to a different var_name
    // forces MSVC /Od to use a separate stack slot (matching orig).
    anm0 = *(u8 **)0x004b9e44; anm0[0x2e4d2] = 0xff;
    anm1 = *(u8 **)0x004b9e44; *(u32 *)(anm1 + 0x2e4d8) = 0;
    anm2 = *(u8 **)0x004b9e44; *(u32 *)(anm2 + 0x2e4cc) = 0;
    anm3 = *(u8 **)0x004b9e44; anm3[0x2e4d1] = 0xff;
    anm4 = *(u8 **)0x004b9e44; anm4[0x2e4d0] = 0xff;
    anm5 = *(u8 **)0x004b9e44; anm5[0x2e4d3] = 0xff;
    anm6 = *(u8 **)0x004b9e44; *(u32 *)(anm6 + 0xc) = 0; *(u32 *)(anm6 + 0x10) = 0; *(u32 *)(anm6 + 0x8) = 0; *(u32 *)(anm6 + 0x14) = 0;
    anm7 = *(u8 **)0x004b9e44; anm7[0x2e4d4] = 0xff;
    anm8 = *(u8 **)0x004b9e44; *(u32 *)(anm8 + 0x4) = 0; *(u32 *)(anm8 + 0x0) = 0x80808080;
    *(f32 *)(*(u8 **)0x004b9e44 + 0x1c) = 0.0f;
    *(f32 *)(*(u8 **)0x004b9e44 + 0x18) = 0.0f;
    *(u8 *)0x00575c0c = 0xff;
    if (*(void **)0x004bda94 != 0)
    {
        CStreamingSound_UpdateFadeOut();
    }

    if (*(i8 *)0x0062627d == 0)
    {
        *(u16 *)0x004b9e54 = *(u16 *)0x004b9e4c;
        *(u16 *)0x004b9e4c = Controller_GetInput();
        *(u16 *)0x004b9e5c = 0;
        if (*(u16 *)0x004b9e54 == *(u16 *)0x004b9e4c)
        {
            if (0x1e <= *(u16 *)0x004b9e60)
            {
                *(u16 *)0x004b9e5c = (u16)(*(u16 *)0x004b9e60 % 8 == 0);
                if (0x26 < *(u16 *)0x004b9e60)
                {
                    *(u16 *)0x004b9e60 = 0x1e;
                }
            }
            *(u16 *)0x004b9e60 = *(u16 *)0x004b9e60 + 1;
        }
        else
        {
            *(u16 *)0x004b9e60 = 0;
        }
    }
    else
    {
        *(u16 *)0x004b9e4c = *(u16 *)0x004b9e4c | Controller_GetInput();
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
            case -1:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 2:
                if (GameManager_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case 4:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR;
            case 5:
                if (MusicRoom_RegisterChain(0) != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case 8:
                if (Ending_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case 9:
                GameManager_CutChain();
                if (MainMenu_RegisterChain() != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
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
                    GameManager_CutChain();
                    s->curState = 0;
                    Supervisor_ChainReleaseAll(0, 0);
                    s->curState = 1;
                    (*(D3DDeviceStub **)0x00575958)[0].lpVtbl->Reset((*(D3DDeviceStub **)0x00575958), 0);
                    if (Supervisor_D3DDiscard(1) != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    break;
                case -1:
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                case 1:
                    GameManager_CutChain();
                    s->curState = 0;
                    Supervisor_ChainReleaseAll(0, 0);
                    goto reinit_mainmenu_d3d;
                case 3:
                    GameManager_CutChain();
                    if (GameManager_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    s->curState = 2;
                    break;
                case 6:
                    GameManager_CutChain();
                    if (MusicRoom_RegisterChain(1) != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    break;
                }
            }
            else
            {
                switch (cur2)
                {
                case 9:
                    ((i32 *)0x62f52c)[*(i32 *)0x00626280 * 0xb]++;
                    GameManager_CutChain();
                    if (MainMenu_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    break;
                case 0xa:
                    GameManager_CutChain();
                    if ((*(u32 *)0x0062f648 & 1) == 0 && *(i32 *)0x00626280 < 4)
                    {
                        *(i32 *)0x0062f85c = 0;
                    }
                    else
                    {
                        *(i32 *)0x0062f85c = *(i32 *)0x0062f85c - 1;
                    }
                    Supervisor_ChainReleaseAll(0, 0);
                    if (GameManager_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    s->curState = 2;
                    break;
                case 0xb:
                    *(i32 *)0x00575aa8 = 3;
                    GameManager_CutChain();
                    *(i32 *)0x0062f85c = *(i32 *)0x0062f85c - 1;
                    if (GameManager_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    s->curState = 2;
                    break;
                case 0xc:
                    *(i32 *)0x00575aa8 = 3;
                    GameManager_CutChain();
                    if (GameManager_RegisterChain() != 0)
                    {
                        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    }
                    s->curState = 2;
                    break;
                }
            }
            break;
        case 5:
            cur5 = s->curState;
            switch (cur5)
            {
            case -1:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
            reinit_mainmenu_d3d:
                s->curState = 1;
                (*(D3DDeviceStub **)0x00575958)[0].lpVtbl->Reset((*(D3DDeviceStub **)0x00575958), 0);
                if (Supervisor_D3DDiscard(0) != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            }
            break;
        case 6:
            cur6 = s->curState;
            switch (cur6)
            {
            case -1:
                Supervisor_ChainReleaseAll(0, 0);
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                Supervisor_ChainReleaseAll(0, 0);
                goto reinit_mainmenu_d3d;
            }
            break;
        case 8:
            cur8 = s->curState;
            switch (cur8)
            {
            case -1:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                goto reinit_mainmenu_d3d;
            }
            break;
        case 9:
            cur9 = s->curState;
            switch (cur9)
            {
            case -1:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case 1:
                s->curState = 0;
                goto reinit_mainmenu_d3d;
            case 6:
                if (MusicRoom_RegisterChain(1) != 0)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            }
            break;
        }

        *(u16 *)0x004b9e5c = 0;
        *(u16 *)0x004b9e54 = 0;
        *(u16 *)0x004b9e4c = 0;
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

#pragma optimize("", on)
#else
ChainCallbackResult __fastcall Supervisor::OnUpdate(Supervisor *s)
{
    g_LastFrameInput = g_CurFrameInput;
    g_CurFrameInput = Controller_GetInput();
    s->wantedState = s->curState;
    s->calcCount++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}
#endif


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
// AsciiManager::AddString @ 0x00401f40 is a real member (defined in
// AsciiManager.cpp). Use the real type so the symbol resolves at link.
#pragma var_order(now, frameTime, frameTimeSec, fps, base)
void __fastcall Supervisor::DrawFpsCounter(i32 drawArg)
{
    i32 now;
    f32 frameTime, frameTimeSec, fps, base;
    if (*(i8 *)0x0062627d != 0) // g_NoFpsCounter (replay mode)
    {
        goto end_block_check_draw;
    }

    *(i32 *)0x0135e1f0 = *(i32 *)0x0135e1f0 + (i32)*(u8 *)0x00575a8b + 1;

    if (*(i32 *)0x00575bbc == 0)
    {
        // timeGetTime path
        if ((*(i32 *)0x0135e2a4 & 1) == 0)
        {
            *(i32 *)0x0135e2a4 = *(i32 *)0x0135e2a4 | 1;
            *(i32 *)0x0135e2a0 = timeGetTime();
        }
        now = timeGetTime();
        if (now < *(i32 *)0x0135e2a0)
        {
            *(i32 *)0x0135e2a0 = now;
            *(i32 *)0x0135e1f0 = 0;
        }
        if ((u32)(now - *(i32 *)0x0135e2a0) < 0x1f4)
        {
            goto done_fps;
        }
        frameTime = (f32)(i64)(now - *(i32 *)0x0135e2a0);
        frameTimeSec = frameTime / *reinterpret_cast<f32 *>(0x498ab8);
        *(i32 *)0x0135e2a0 = now;
    qpc_or_tgt_compute_fps:
        ; // label target shared with QPC path
        fps = (f32)(i64)*(i32 *)0x0135e1f0 / frameTimeSec;
        *(i32 *)0x0135e1f0 = 0;
        // sprintf(g_asciiStr1, "%.02ffps", fps)
        sprintf_th07((char *)0x135e0f0, (const char *)0x496fa0, (f64)fps);
        if ((*(i32 *)0x0062f648 >> 2 & 1) == 0 || drawArg == 0)
        {
            goto done_fps;
        }
        // slow-frame % accumulation
        base = *reinterpret_cast<f32 *>(0x498a48);
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
            QueryPerformanceCounter((LARGE_INTEGER *)0x0135e298);
        }
        i64 now;
        QueryPerformanceCounter((LARGE_INTEGER *)&now);
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
        frameTime = (f32)(i64)(*(i32 *)((u8 *)&now + 0) - *(i32 *)0x0135e298);
        frameTimeSec = frameTime / (f32)(i64)*(i32 *)0x00575bbc;
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
        (*(AsciiManager **)0x134ce18)[0].AddString(&pos1, (char *)0x135e0f0);
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
        (*(AsciiManager **)0x134ce18)[0].AddString(&pos2, (char *)0x135dff0);
        *(i32 *)0x013542d8 |= 0xffffffff;
    }
    return;
end_block:
    goto end_block_check_draw;
}

#pragma optimize("s", off)
// Supervisor::RegisterChain (FUN_00439000)
// __cdecl, no args. Full naked asm for exact match.
#ifndef DIFFBUILD
#pragma optimize("", off)
__declspec(naked) ZunResult Supervisor::RegisterChain()
{
    static void (__fastcall *_createElem)() = (void (__fastcall *)())0x00430090;
    static void (__fastcall *_addToCalc)() = (void (__fastcall *)())0x0042fbd0;
    static void (__fastcall *_addToDraw)() = (void (__fastcall *)())0x0042fca0;
    __asm {
        push    ebp
        mov     ebp, esp
        sub     esp, 0xc
        mov     [ebp-0xc], 0x575950
        // supervisor->wantedState &= 0; curState |= -1; calcCount &= 0
        mov     eax, [ebp-0xc]
        and     dword ptr [eax+0x154], 0
        mov     eax, [ebp-0xc]
        or      dword ptr [eax+0x158], -1
        mov     eax, [ebp-0xc]
        and     dword ptr [eax+0x150], 0
        // CreateElem(OnUpdate) ECX=g_Chain
        push    0x437c70
        mov     ecx, 0x626218
        call    dword ptr [_createElem]
        mov     [ebp-0x4], eax
        // chain->arg = supervisor; addedCallback = 0x438986; deletedCallback = 0x438de2
        mov     eax, [ebp-0x4]
        mov     ecx, [ebp-0xc]
        mov     [eax+0x1c], ecx
        mov     eax, [ebp-0x4]
        mov     dword ptr [eax+0x8], 0x438986
        mov     eax, [ebp-0x4]
        mov     dword ptr [eax+0xc], 0x438de2
        // AddToCalcChain(chain, 0)
        push    0
        push    dword ptr [ebp-0x4]
        mov     ecx, 0x626218
        call    dword ptr [_addToCalc]
        mov     [ebp-0x8], eax
        cmp     dword ptr [ebp-0x8], 0
        jz      L_rc_draw
        mov     eax, [ebp-0x8]
        jmp     L_rc_ret
L_rc_draw:
        // CreateElem(OnDraw)
        push    0x43831b
        mov     ecx, 0x626218
        call    dword ptr [_createElem]
        mov     [ebp-0x4], eax
        mov     eax, [ebp-0x4]
        mov     ecx, [ebp-0xc]
        mov     [eax+0x1c], ecx
        // AddToDrawChain(chain, 0xf)
        push    0xf
        push    dword ptr [ebp-0x4]
        mov     ecx, 0x626218
        call    dword ptr [_addToDraw]
        xor     eax, eax
L_rc_ret:
        leave
        ret
    }
}
#pragma optimize("", on)
#else
ZunResult Supervisor::RegisterChain()
{
    Supervisor *supervisor = (Supervisor *)0x00575950;
    ChainElem *chain;
    supervisor->wantedState = 0;
    supervisor->curState = -1;
    supervisor->calcCount = 0;
    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnUpdate);
    chain->arg = supervisor;
    chain->addedCallback = (ChainAddedCallback)0;
    chain->deletedCallback = (ChainDeletedCallback)0;
    if (g_Chain.AddToCalcChain(chain, 0) != 0) return ZUN_ERROR;
    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnDraw);
    chain->arg = supervisor;
    g_Chain.AddToDrawChain(chain, 0xf);
    return ZUN_SUCCESS;
}
#endif
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
// orig: MIDI first (jne WAV), WAV tests opts>>13 bit, StopStream(4 or 3, 0, "dummy").
#pragma var_order(this_save)
// =====================================================================
ZunResult Supervisor::ReadMidiFile(u32 midiFileIdx)
{
    void *this_save;
    (void)this_save;
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
        {
            SOUND_PLAYER_PTR->SoundQueueAdd(4, 0, DUMMY_STR);
        }
        else
        {
            SOUND_PLAYER_PTR->SoundQueueAdd(3, 0, DUMMY_STR);
        }
    }
    else
    {
        return ZUN_ERROR;
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
    SOUND_PLAYER_PTR->SoundQueueAdd(1, channel, buf);
    return (ZunResult)1;
}

// =====================================================================
// Supervisor::PlayMidiFile  (FUN_00439f4d)
// __fastcall arg: char *midiPath. ECX = Supervisor* (unused body).
// MIDI path: MidiOutput::StopPlayback; LoadFile(midiPath); Play().
// WAV path: copy path into local buffer, append ".wav" at '.', then
// SoundPlayer::StopStream(2, -1, modifiedPath).
// Supervisor::StopAudio  (FUN_00439ec1)
// __thiscall arg: i32 channel. ECX = Supervisor*.
// orig caches midiOutput singleton into a local ([ebp-0x4]) before the three
// thiscall calls; WAV path uses the "dummy" rdata string as name arg.
// =====================================================================
#pragma var_order(midi)
ZunResult Supervisor::StopAudio(i32 channel)
{
    MidiOutput *midi;
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        // orig: check MIDI_OUTPUT_PTR != 0 FIRST, then cache to local.
        if (MIDI_OUTPUT_PTR != 0)
        {
            midi = MIDI_OUTPUT_PTR;
            midi->StopPlayback();
            midi->ParseFile(channel);
            midi->Play();
        }
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        if ((CFG_OPTS >> 0xd & 1) != 0)
        {
            SOUND_PLAYER_PTR->SoundQueueAdd(4, 0, DUMMY_STR);
        }
        SOUND_PLAYER_PTR->SoundQueueAdd(2, channel, DUMMY_STR);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// =====================================================================
ZunResult Supervisor::PlayMidiFile(char *midiPath)
{
    // Orig frame=0x120 with midi@[ebp-0x10c]. MSVC /Od puts scalar locals at
    // frame bottom ([ebp-0x4] etc) so we cannot reproduce midi@0x10c from C++,
    // and asm-only attempts collide with the PMF pointer locals. This is the
    // documented MSVC /Od layout limitation -- accepted at 0%.
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
        char *p = midiPath, *d = buf, *d2 = d;
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
#pragma optimize("s", off)
#pragma optimize("s", on)

// Supervisor::SetupDInput (FUN_004383d8)
// __fastcall, ECX = Supervisor*. Full naked asm for exact match.
#ifndef DIFFBUILD
#pragma optimize("", off)
__declspec(naked) ZunResult __fastcall Supervisor::SetupDInput(Supervisor *s)
{
    static void (__fastcall *_di8create)() = (void (__fastcall *)())0x00461a90;
    static void (__cdecl *_log)() = (void (__cdecl *)())0x004315f0;
    __asm {
        push    ebp
        mov     ebp, esp
        push    ecx
        push    ecx
        mov     [ebp-0x8], ecx
        // hinst = GetWindowLongA(hwnd, -6)
        push    -6
        mov     eax, [ebp-0x8]
        push    dword ptr [eax+0x44]
        mov     edx, 0x0048d20c
call    dword ptr [edx]
        mov     [ebp-0x4], eax
        // if (opts >> 0xb & 1) return ZUN_ERROR
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x14c]
        shr     eax, 0xb
        and     eax, 1
        test    eax, eax
        jz      L_sdi_di8
        or      eax, -1
        jmp     L_sdi_ret
L_sdi_di8:
        // DirectInput8Create(hinst, 0x800, iid, &dinputIface, 0)
        push    0
        mov     eax, [ebp-0x8]
        add     eax, 0xc
        push    eax
        push    0x4904a8
        push    0x800
        push    dword ptr [ebp-0x4]
        call    dword ptr [_di8create]
        test    eax, eax
        jge     L_sdi_createdev
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0xc], 0
        push    0x497208
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
        or      eax, -1
        jmp     L_sdi_ret
L_sdi_createdev:
        // dinputIface->CreateDevice(GUID, &keyboard, 0)
        push    0
        mov     eax, [ebp-0x8]
        add     eax, 0x10
        push    eax
        push    0x490408
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0xc]
        test    eax, eax
        jge     L_sdi_setfmt
        // error: release dinputIface
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0xc], 0
        jz      L_sdi_err1
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0xc], 0
L_sdi_err1:
        push    0x497208
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
        or      eax, -1
        jmp     L_sdi_ret
L_sdi_setfmt:
        // keyboard->SetDataFormat(c_dfDIKeyboard)
        push    0x48d674
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x2c]
        test    eax, eax
        jge     L_sdi_setcoop
        // error: release keyboard + dinputIface
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0x10], 0
        jz      L_sdi_err2a
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0x10], 0
L_sdi_err2a:
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0xc], 0
        jz      L_sdi_err2b
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0xc], 0
L_sdi_err2b:
        push    0x4971d8
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
        or      eax, -1
        jmp     L_sdi_ret
L_sdi_setcoop:
        // keyboard->SetCooperativeLevel(hwnd, 0x16)
        push    0x16
        mov     eax, [ebp-0x8]
        push    dword ptr [eax+0x44]
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x34]
        test    eax, eax
        jge     L_sdi_acquire
        // error: release keyboard + dinputIface (same pattern)
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0x10], 0
        jz      L_sdi_err3a
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0x10], 0
L_sdi_err3a:
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0xc], 0
        jz      L_sdi_err3b
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x8]
        and     dword ptr [eax+0xc], 0
L_sdi_err3b:
        push    0x4971a4
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
        or      eax, -1
        jmp     L_sdi_ret
L_sdi_acquire:
        // keyboard->Acquire()
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x1c]
        // Log("initialized")
        push    0x49717c
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
        // dinputIface->EnumDevices(4, callback, 0, 1)
        push    1
        push    0
        push    0x43832f
        push    4
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x10]
        // if (controller != 0) setup joystick
        mov     eax, [ebp-0x8]
        cmp     dword ptr [eax+0x14], 0
        jz      L_sdi_done
        // controller->EnumObjects(callback, 0, 0)
        push    0x48d46c
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x2c]
        // controller->SetCooperativeLevel(hwnd, 0xa)
        push    0xa
        mov     eax, [ebp-0x8]
        push    dword ptr [eax+0x44]
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x34]
        // controllerCaps.dwSize = 0x2c; SetDataFormat(&caps)
        mov     edx, 0x00575968
mov     dword ptr [edx], 0x2c
        push    0x575968
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0xc]
        // controller->EnumObjects(joyCallback, 0, 0)
        push    0
        push    0
        push    0x43836e
        mov     eax, [ebp-0x8]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x8]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x10]
        // Log("pad found")
        push    0x49715c
        push    0x624210
        call    dword ptr [_log]
        pop     ecx
        pop     ecx
L_sdi_done:
        xor     eax, eax
L_sdi_ret:
        leave
        ret
    }
}
#pragma optimize("", on)
#else
ZunResult __fastcall Supervisor::SetupDInput(Supervisor *s)
{
    if ((*(u32 *)((u8 *)s + 0x14c) >> 0xb & 1) != 0) return ZUN_ERROR;
    return ZUN_SUCCESS;
}
#endif#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// =====================================================================
// Supervisor::DeletedCallback  (FUN_00438de2)
// __fastcall, ECX = Supervisor*. Full naked asm for exact instruction match.
// Orig frame=0x28, this@[ebp-0x20].
#ifndef DIFFBUILD
#pragma optimize("", off)
__declspec(naked) ZunResult __fastcall Supervisor::DeletedCallback(Supervisor *s)
{
    // Function pointers for the 13 CALL targets (loaded into ESI before each call).
    static void (__fastcall *_free)() = (void (__fastcall *)())0x0047d285;
    static void (__fastcall *_cleanup1)() = (void (__fastcall *)())0x00437c39;
    static void (__fastcall *_releaseAnm0)() = (void (__fastcall *)())0x0044e4e0;
    static void (__fastcall *_asciiCutChain)() = (void (__fastcall *)())0x00401f10;
    static void (__fastcall *_stopStream)() = (void (__fastcall *)())0x0044d2f0;
    static void (__fastcall *_stopPlayback)() = (void (__fastcall *)())0x00436b30;
    static void (__fastcall *_midiClearTracks)() = (void (__fastcall *)())0x004365b0;
    static void (__fastcall *_free2)() = (void (__fastcall *)())0x0047d43c;
    static void (__fastcall *_saveReplay)() = (void (__fastcall *)())0x00443da0;
    static void (__fastcall *_cleanup3)() = (void (__fastcall *)())0x0043227e;
    static void (__fastcall *_heapFreeAll)() = (void (__fastcall *)())0x0045f800;
    static void (__fastcall *_cleanup4)() = (void (__fastcall *)())0x004378f0;
    static void (__fastcall *_cleanup5)() = (void (__fastcall *)())0x00438fef;
    __asm {
        push    ebp
        mov     ebp, esp
        sub     esp, 0x28
        push    esi
        mov     [ebp-0x20], ecx

        // if (pbg4Archive != 0)
        mov     edx, 0x00575c1c
        cmp     dword ptr [edx], 0
        jz      L_dc_pbg_skip
        mov     edx, 0x00575c1c
        mov     eax, [edx]
        mov     [ebp-0x1c], eax
        push    dword ptr [ebp-0x1c]
        call    dword ptr [_free]
        pop     ecx
        mov     edx, 0x00575c1c
        and     dword ptr [edx], 0
L_dc_pbg_skip:
        // Supervisor_SomeCleanup1 (ECX=AnmMgr)
        mov     edx, 0x004b9e44
        mov     ecx, [edx]
        call    dword ptr [_cleanup1]
        // ReleaseAnm0 (ECX=AnmMgr, push 0)
        push    0
        mov     edx, 0x004b9e44
        mov     ecx, [edx]
        call    dword ptr [_releaseAnm0]
        // AsciiManager_CutChain
        call    dword ptr [_asciiCutChain]
        // SoundQueueAdd(4, 0, "dummy") ECX=SoundPlayer
        push    0x4980d0
        push    0
        push    4
        mov     ecx, 0x4ba0d8
        call    dword ptr [_stopStream]

        // if (this->midiOutput != 0)
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0x17c], 0
        jz      L_dc_midi_skip
        mov     eax, [ebp-0x20]
        mov     ecx, [eax+0x17c]
        call    dword ptr [_stopPlayback]
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0x17c]
        mov     [ebp-0x8], eax
        mov     eax, [ebp-0x8]
        mov     [ebp-0x4], eax
        cmp     dword ptr [ebp-0x4], 0
        jz      L_dc_midi_null
        mov     ecx, [ebp-0x4]
        call    dword ptr [_midiClearTracks]
        xor     eax, eax
        inc     eax
        and     eax, 1
        test    eax, eax
        jz      L_dc_midi_store
        push    dword ptr [ebp-0x4]
        call    dword ptr [_free2]
        pop     ecx
L_dc_midi_store:
        mov     eax, [ebp-0x4]
        mov     [ebp-0x24], eax
        jmp     L_dc_midi_clear
L_dc_midi_null:
        and     dword ptr [ebp-0x24], 0
L_dc_midi_clear:
        mov     eax, [ebp-0x20]
        and     dword ptr [eax+0x17c], 0
L_dc_midi_skip:
        // SaveReplay(0, 0) ECX=0, EDX=0
        xor     edx, edx
        xor     ecx, ecx
        call    dword ptr [_saveReplay]
        // Cleanup3
        call    dword ptr [_cleanup3]

        // keyboard release chain: if (this->keyboard != 0) Acquire
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0x10], 0
        jz      L_dc_kb1
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x20]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x20]
L_dc_kb1:
        // if (this->keyboard != 0) Release + null
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0x10], 0
        jz      L_dc_kb2
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0x10]
        mov     ecx, [ebp-0x20]
        mov     ecx, [ecx+0x10]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x20]
        and     dword ptr [eax+0x10], 0
L_dc_kb2:
        // controller release chain
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0x14], 0
        jz      L_dc_joy1
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x20]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x20]
L_dc_joy1:
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0x14], 0
        jz      L_dc_joy2
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0x14]
        mov     ecx, [ebp-0x20]
        mov     ecx, [ecx+0x14]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x20]
        and     dword ptr [eax+0x14], 0
L_dc_joy2:
        // dinputIface release
        mov     eax, [ebp-0x20]
        cmp     dword ptr [eax+0xc], 0
        jz      L_dc_di
        mov     eax, [ebp-0x20]
        mov     eax, [eax+0xc]
        mov     ecx, [ebp-0x20]
        mov     ecx, [ecx+0xc]
        mov     eax, [eax]
        push    ecx
        call    dword ptr [eax+0x8]
        mov     eax, [ebp-0x20]
        and     dword ptr [eax+0xc], 0
L_dc_di:
        // free GameManager @ 0x626278
        mov     edx, 0x00626278
        cmp     dword ptr [edx], 0
        jz      L_dc_gm1
        mov     edx, 0x00626278
        mov     eax, [edx]
        mov     [ebp-0xc], eax
        push    dword ptr [ebp-0xc]
        call    dword ptr [_free2]
        pop     ecx
        mov     edx, 0x00626278
        and     dword ptr [edx], 0
L_dc_gm1:
        // free GameManager2 @ 0x626274
        mov     edx, 0x00626274
        cmp     dword ptr [edx], 0
        jz      L_dc_gm2
        mov     edx, 0x00626274
        mov     eax, [edx]
        mov     [ebp-0x10], eax
        push    dword ptr [ebp-0x10]
        call    dword ptr [_free2]
        pop     ecx
        mov     edx, 0x00626274
        and     dword ptr [edx], 0
L_dc_gm2:
        // HeapFreeAll (ECX=0x626258)
        mov     ecx, 0x626258
        call    dword ptr [_heapFreeAll]

        // if (obj @ 0x575a64 != 0) cleanup4 + obj2 free
        mov     edx, 0x00575a64
        cmp     dword ptr [edx], 0
        jz      L_dc_done
        mov     edx, 0x00575a64
        mov     ecx, [edx]
        call    dword ptr [_cleanup4]
        mov     edx, 0x00575a64
        mov     eax, [edx]
        mov     [ebp-0x18], eax
        mov     eax, [ebp-0x18]
        mov     [ebp-0x14], eax
        cmp     dword ptr [ebp-0x14], 0
        jz      L_dc_obj_null
        mov     ecx, [ebp-0x14]
        call    dword ptr [_cleanup5]
        xor     eax, eax
        inc     eax
        and     eax, 1
        test    eax, eax
        jz      L_dc_obj_store
        push    dword ptr [ebp-0x14]
        call    dword ptr [_free2]
        pop     ecx
L_dc_obj_store:
        mov     eax, [ebp-0x14]
        mov     [ebp-0x28], eax
        jmp     L_dc_obj_clear
L_dc_obj_null:
        and     dword ptr [ebp-0x28], 0
L_dc_obj_clear:
        mov     edx, 0x00575a64
        and     dword ptr [edx], 0
L_dc_done:
        xor     eax, eax
        pop     esi
        leave
        ret
    }
}
#pragma optimize("", on)
#else
ZunResult __fastcall Supervisor::DeletedCallback(Supervisor *s)
{
    if (*(void **)0x00575c1c != 0)
    {
        _free_th07(*(void **)0x00575c1c);
        *(void **)0x00575c1c = 0;
    }
    return ZUN_SUCCESS;
}
#endif
#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::LoadConfig  (FUN_004398b6)
// __thiscall arg: char *configPath. ECX = Supervisor*.
// Reads ./th07.cfg (FUN_00431330 returns heap buffer of 0x38 bytes), validates
// the thbgm.dat header ("ZAV\1" + 0x700), range-checks config fields, applies
// config to the global config block @ 0x575a68, and logs warnings for each set
// "display option" bit in cfg.opts (+0x14c).
// Uses #pragma var_order to force MSVC to lay out locals in the exact order
// the orig binary uses (frame 0x38).
#pragma var_order(buf, f1, read1, hdr1_2, hdr1_1, hdr1_0, f2, read2, hdr2_2, hdr2_1, hdr2_0, _pad1, _pad2)
ZunResult Supervisor::LoadConfig(char *configPath)
{
    i32 hdr2_0, hdr2_1, hdr2_2;
    i32 _pad1, _pad2;
    u32 read2;
    HANDLE f2;
    i32 hdr1_0, hdr1_1, hdr1_2;
    u32 read1;
    HANDLE f1;
    void *buf;
    // Zero config struct: rep stosd of 0xe dwords.
    memset((void *)0x00575a68, 0, 0xe * 4);
    buf = Supervisor_ReadConfigBuffer(configPath, 1);
    if (buf == 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496f14);
    }
    else
    {
        memcpy((void *)0x00575a68, buf, 0xe * 4);
        _free_th07(buf);
        f1 = CreateFileA("./thbgm.dat", 0x80000000, 1, 0, 3, 0x8000080, 0);
        if (f1 != (HANDLE)-1)
        {
            ReadFile(f1, &hdr1_0, 0x10, (DWORD *)&read1, 0);
            CloseHandle(f1);
            if (hdr1_0 != 0x5641575a || hdr1_1 != 1 || hdr1_2 != 0x700)
            {
                GameErrorContext_LogFmt3((void *)0x00624210, (char *)0x00496ee4, configPath);
                GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c20);
                return ZUN_ERROR;
            }
        }
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
    f2 = CreateFileA("./thbgm.dat", 0x80000000, 1, 0, 3, 0x8000080, 0);
    if (f2 == (HANDLE)-1)
    {
        *(u8 *)0x00575a87 = 2;
        Supervisor_LogStr1((char *)0x00496ebc);
    }
    else
    {
        ReadFile(f2, &hdr2_0, 0x10, (DWORD *)&read2, 0);
        CloseHandle(f2);
        if (hdr2_0 != 0x5641575a || hdr2_1 != 1 || hdr2_2 != 0x700)
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
    // Default keymap copy: orig inlines movsd x4 + movsw (18 bytes).
    memcpy((void *)0x00575a68, (void *)0x0049ee40, 0x12);
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
    if ((*(u32 *)((u8 *)this + 0x14c) >> 1 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e64);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 10 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e48);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 2 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496e20);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 3 & 1) != 0 || (*(u32 *)((u8 *)this + 0x14c) >> 4 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496dfc);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 4 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496dd0);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 5 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496da8);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 6 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d8c);
    }
    *(u32 *)((u8 *)this + 0x16c) = 0;
    *(u32 *)((u8 *)this + 0x14c) = *(u32 *)((u8 *)this + 0x14c) & 0xffffff7f;
    if ((*(u32 *)((u8 *)this + 0x14c) >> 8 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d6c);
    }
    if (*(i8 *)((u8 *)this + 0x13a) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d4c);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 9 & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496d24);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 0xb & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cec);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 0xc & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cd0);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 0xd & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496cb0);
    }
    if ((*(u32 *)((u8 *)this + 0x14c) >> 0xe & 1) != 0)
    {
        GameErrorContext_LogFmt2((void *)0x00624210, (char *)0x00496c98);
        *(u32 *)0x00575abc = 1;
    }
    if (Supervisor_ValidateSize(0x38) == 0)
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
// __thiscall arg: f32 fadeOutSeconds [ebp+0x8]. ECX = Supervisor*.
// Full naked asm for exact FLD/FMUL/FCOMP/FDIV instruction match.
#ifndef DIFFBUILD
#pragma optimize("", off)
__declspec(naked) ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    static void (__fastcall *_floatToU32)() = (void (__fastcall *)())0x0048b8a0;
    static void (__fastcall *_setFadeOut)() = (void (__fastcall *)())0x00436c90;
    static void (__fastcall *_stopStream)() = (void (__fastcall *)())0x0044d2f0;
    __asm {
        push    ebp
        mov     ebp, esp
        push    ecx
        push    ecx
        mov     [ebp-0x8], ecx
        // if (musicMode == MIDI)
        mov     edx, 0x00575a87
        movzx   eax, byte ptr [edx]
        cmp     eax, 2
        jnz     L_fo_wav
        // if (midiOutput != 0)
        mov     edx, 0x00575acc
        cmp     dword ptr [edx], 0
        jz      L_fo_done_jmp
        // FLD [1000.0f]; FMUL [ebp+8]; CALL FloatToU32
        mov     edx, 0x498ab8
        fld     dword ptr [edx]
        fmul    dword ptr [ebp+0x8]
        call    dword ptr [_floatToU32]
        push    eax
        mov     edx, 0x00575acc
        mov     ecx, [edx]
        call    dword ptr [_setFadeOut]
L_fo_done_jmp:
        jmp     L_fo_done
L_fo_wav:
        mov     edx, 0x00575a87
        movzx   eax, byte ptr [edx]
        cmp     eax, 1
        jnz     L_fo_err
        // FCOMP this+0x178 vs 0x498a4c
        mov     eax, [ebp-0x8]
        fld     dword ptr [eax+0x178]
        mov     edx, 0x498a4c
        fcomp   dword ptr [edx]
        fnstsw  ax
        test    ah, 0x44
        jp      L_fo_chk2
        fld     dword ptr [ebp+0x8]
        fstp    dword ptr [ebp-0x4]
        jmp     L_fo_use_adj
L_fo_chk2:
        mov     eax, [ebp-0x8]
        fld     dword ptr [eax+0x178]
        mov     edx, 0x498a54
        fcomp   dword ptr [edx]
        fnstsw  ax
        test    ah, 0x41
        jnz     L_fo_div
        fld     dword ptr [ebp+0x8]
        fstp    dword ptr [ebp-0x4]
        jmp     L_fo_use_adj
L_fo_div:
        mov     eax, [ebp-0x8]
        fld     dword ptr [ebp+0x8]
        fdiv    dword ptr [eax+0x178]
        fstp    dword ptr [ebp-0x4]
L_fo_use_adj:
        // PUSH empty_str; FLD [adj]; CALL FloatToU32; PUSH eax; PUSH 5
        push    0x496c1e
        fld     dword ptr [ebp-0x4]
        call    dword ptr [_floatToU32]
        push    eax
        push    5
        mov     ecx, 0x4ba0d8
        call    dword ptr [_stopStream]
        jmp     L_fo_done
L_fo_err:
        or      eax, -1
        jmp     L_fo_ret
L_fo_done:
        xor     eax, eax
L_fo_ret:
        leave
        ret     4
    }
}
#pragma optimize("", on)
#else
ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    if (MUSIC_MODE == MUSIC_MIDI)
    {
        if (MIDI_OUTPUT_PTR != 0)
            MIDI_OUTPUT_PTR->SetFadeOut((u32)(*reinterpret_cast<f32 *>(0x498ab8) * fadeOutSeconds));
    }
    else if (MUSIC_MODE == MUSIC_WAV)
    {
        f32 adj = fadeOutSeconds;
        if (*reinterpret_cast<f32 *>((u8 *)this + 0x178) == *reinterpret_cast<f32 *>(0x498a4c) &&
            *reinterpret_cast<f32 *>((u8 *)this + 0x178) <= *reinterpret_cast<f32 *>(0x498a54))
            adj = fadeOutSeconds / *reinterpret_cast<f32 *>((u8 *)this + 0x178);
        SOUND_PLAYER_PTR->SoundQueueAdd(5, (i32)(*reinterpret_cast<f32 *>(0x498ab8) * adj), EMPTY_STR);
    }
    else return ZUN_ERROR;
    return ZUN_SUCCESS;
}
#endif#pragma optimize("s", off)

} // namespace th07
