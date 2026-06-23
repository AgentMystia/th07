// Supervisor module for th07 (Perfect Cherry Blossom).
// Pure C++ with a single unified code path: no naked asm, no #ifndef DIFFBUILD
// splits. #pragma var_order may be used to control MSVC stack layout matching.

#include "Supervisor.hpp"
#include "AsciiManager.hpp"
#include "SoundPlayer.hpp"
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
extern "C" void __fastcall Callback_4547f(void *anmMgr, i32 textureIdx, char *path);   // 0x004547b0 AnmManager::LoadTexture
extern "C" void __fastcall Callback_4547f_2arg(void *cfg, void *buf);    // 0x004547b0 alt. 2-arg form (LoadConfig)
extern "C" void __fastcall Callback_454fc(void *a);              // 0x004454fc
extern "C" void __fastcall ListNode_Ctor(void *p);               // 0x004362a0 (ECX = new'd buf)
extern "C" void __fastcall Callback_378d0(void *p);              // 0x004378d0 (ECX = obj2)
// AnmManager boot-draw helpers called from AddedCallback (orig FUN_00454a10 /
// FUN_00454aa0). Replaces the previous raw-address casts -- those jumped into
// unmapped space and crashed the boot at the AddedCallback logo-draw step.
extern "C" void __fastcall AnmMgr_ReleaseTextureSlot_454a10(void *anm, i32 idx); // 0x00454a10
extern "C" void __fastcall AnmMgr_BootDrawLogo_454aa0(void *anm, i32 idx,
                                                       i32 a2, i32 a3, i32 a4, i32 a5); // 0x00454aa0

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
#define SOUND_PLAYER_PTR (&g_SoundPlayer)
#define DUMMY_STR "dummy"
#define EMPTY_STR "debug"

// ---- typed externs for rdata float constants and game-state globals.
//      Addresses are recovered on the objdiff side via SYMBOL_MAP entries
//      in scripts/generate_objdiff_objs.py. MSVC emits [glob] memory operands
//      matching orig's addressing, so no #ifdef DIFFBUILD split is needed.
extern "C" const f32 g_SupervisorC0x498a48;   // rdata float
extern "C" const f32 g_SupervisorC0x498a4c;
extern "C" const f32 g_SupervisorC0x498a50;
extern "C" const f32 g_SupervisorC0x498a54;   // 1.0f
extern "C" const f32 g_SupervisorC0x498a58;
extern "C" const f32 g_SupervisorC0x498a68;
extern "C" const f32 g_SupervisorC0x498a6c;
extern "C" const f32 g_SupervisorC0x498aa0;
extern "C" const f32 g_SupervisorC0x498ab8;   // 60.0f
extern "C" const f32 g_SupervisorC0x498b10;
extern "C" const f32 g_SupervisorC0x498b14;
extern "C" const f32 g_SupervisorC0x498b18;
extern "C" const f32 g_SupervisorC0x498b1c;
extern "C" const f32 g_SupervisorC0x498b20;
extern "C" u16 g_SupervisorG0x49fe20;          // rng seed source
extern "C" u32 g_SupervisorG0x4b9e64;          // input crc check
extern "C" i32 g_SupervisorG0x4bdaa0;          // BGM state
extern "C" void *g_SupervisorG0x575a64;        // supervisor list-node sink
extern "C" i32 g_SupervisorG0x575bbc;          // supervisor tail scratch
extern "C" i32 g_SupervisorG0x575bf8;
extern "C" u8  g_SupervisorG0x575c0c;
extern "C" i32 g_SupervisorG0x575c10;
extern "C" i32 g_SupervisorG0x575c14;
extern "C" u32 g_SupervisorG0x62f4e0;          // wav format table (0x58 dwords)
extern "C" u16 g_SupervisorG0x62f4e4;          // nChannels
extern "C" u16 g_SupervisorG0x62f4e6;          // mirror
extern "C" u8  g_SupervisorG0x62f4e8;
extern "C" i32 g_SupervisorG0x62f85c;
extern "C" i32 g_SupervisorG0x13542d8;         // effect color slot
extern "C" i32 g_SupervisorG0x135dfec;         // ascii/effect counters
extern "C" i32 g_SupervisorG0x135e1f0;
extern "C" i32 g_SupervisorG0x135e298;
extern "C" i32 g_SupervisorG0x135e29c;
extern "C" i32 g_SupervisorG0x135e2a0;
extern "C" i32 g_SupervisorG0x135e2a4;
extern "C" char g_SupervisorG0x135dff0[];      // AsciiManager scratch buffer
extern "C" char g_SupervisorG0x135e0f0[];      // AsciiManager scratch buffer
extern "C" void *g_SupervisorG0x496c0c;        // rdata pointer
extern "C" void *g_SupervisorG0x4980c4;        // rdata pointer (ECX for Callback_44c20)
extern "C" void *g_SupervisorG0x4bd994;        // scratch (thbgm descriptor table)
extern "C" i8  g_GameManagerRankForceFlag;     // GameManager +0xd (inside flag0c i32)

// ---- P1.3 boot-path globals + helpers (FUN_00434020 / a40 / a80 / bd0 /
//      46e0 / 3e90). These live as standalone .data/.bss globals, not as
//      Supervisor struct fields; referenced by the boot loop, the window/D3D
//      init helpers, and RunSession.
// Boot/window state:
//   g_SupervisorWindow_575c20: HWND created by CreateWindow (FUN_00434a80).
//   g_SupervisorExStyle_575c28 / g_SupervisorStyle_575c2c: cached window
//     style bits for re-creation on mode change.
//   g_SupervisorExitFlag_575c24: nonzero -> main loop terminates (set at end
//     of InitD3D and cleared on reboot).
//   g_SupervisorBootVar_575c30: 0xe2 written before entering the session
//     loop (likely a vsync/sleep config).
//   g_SupervisorPerfFreq_575c34: QueryPerformanceFrequency result.
//   g_SupervisorSysParam_575c40/44/48: SystemParametersInfo snapshots
//     (screensaver/keyboard speed/cursor) restored on exit.
//   g_SupervisorLoadResult_575c3c: nonzero when cfg load requested windowed
//     override for InitD3D.
//   g_SupervisorIsForeground_575a8a: cfg.windowed byte (also accessed via
//     g_Supervisor.cfg.windowed).
//   g_SupervisorFrameskipCfg_575a8b: cfg.frameskipConfig byte.
//   g_SupervisorColorMode_575a86: cfg.colorMode16bit byte.
//   g_SupervisorCfgOpts_575a9c: cfg.opts dword.
//   g_SupervisorUnkFlag_575a8c, _575abc, _575ab4, _575ac0, _575ac4, _575adc,
//     _575ae8, _575b40, _575b78: assorted per-frame / boot flags touched by
//     InitD3D and the session loop.
//   g_GameErrorContext_624210: the 0x2008-byte GameErrorContext buffer; the
//     boot loop writes its address into the chain head at DAT_00626210.
//   g_GameErrorContextHead_626210: chain head pointer.
//   g_SupervisorQpcLast_135e208/_c: last QueryPerformanceCounter sample.
//   g_SupervisorTimeLast_135e200: last timeGetTime sample.
//   g_SupervisorFrameCounter_135e1f8: per-frame monotonic counter bumped by
//     RunSession.
//   g_SupervisorPresentParams_575a30: present-parameters buffer (copy of the
//     local one built by InitD3D, used by device-reset paths).
//   g_SupervisorViewport_575a18: the D3D viewport set after device creation.
//   g_SupervisorUnkMatrix1_575990 / _5759d0: scratch matrices for the boot-
//     time D3DXMatrixPerspectiveLH / D3DXMatrixLookAtLH calls.
//   g_SupervisorAnmMgrSlot_4b9e44: the AnmManager singleton pointer slot.
//   g_SupervisorReplayActive_62f4e0: nonzero when a replay is recording /
//     playing back (flushed on session exit).
extern "C" void *g_SupervisorWindow_575c20;
extern "C" i32  g_SupervisorExStyle_575c28;
extern "C" i32  g_SupervisorStyle_575c2c;
extern "C" i32  g_SupervisorExitFlag_575c24;
extern "C" i32  g_SupervisorBootVar_575c30;
extern "C" i64  g_SupervisorPerfFreq_575c34;
extern "C" i32  g_SupervisorSysParam_575c40;
extern "C" i32  g_SupervisorSysParam_575c44;
extern "C" i32  g_SupervisorSysParam_575c48;
extern "C" u8   g_SupervisorLoadResult_575c3c;
extern "C" u8   g_SupervisorColorMode_575a86;
extern "C" u8   g_SupervisorIsForeground_575a8a;
extern "C" u8   g_SupervisorFrameskipCfg_575a8b;
extern "C" u32  g_SupervisorCfgOpts_575a9c;
extern "C" u8   g_SupervisorUnkFlag_575a8c;
extern "C" u8   g_SupervisorWindowedOverride_575abc;
extern "C" i32  g_SupervisorUnkAb4_575ab4;
extern "C" i32  g_SupervisorUnkAc0_575ac0;
extern "C" i32  g_SupervisorHasHwVertexProc_575ac4;
extern "C" i32  g_SupervisorFrameFlags_575adc;
extern "C" i32  g_SupervisorUnkAe8_575ae8;
extern "C" u32  g_SupervisorVramMegs_575b40;
extern "C" u32  g_SupervisorUnkB78_575b78;
extern "C" char  g_GameErrorContext_624210[0x2008];
extern "C" void *g_GameErrorContextHead_626210;
extern "C" i32  g_SupervisorQpcLastLo_135e208;
extern "C" i32  g_SupervisorQpcLastHi_135e20c;
extern "C" f64  g_SupervisorTimeLast_135e200;
extern "C" i32  g_SupervisorFrameCounter_135e1f8;
extern "C" u32  g_SupervisorPresentParams_575a30[13];
extern "C" u32  g_SupervisorViewport_575a18[8];
extern "C" u32  g_SupervisorUnkMatrix1_575990[16];
extern "C" u32  g_SupervisorUnkMatrix2_5759d0[16];
extern "C" void *g_SupervisorHwndMirror_575994;
extern "C" void *g_SupervisorAnmMgrSlot_4b9e44;
// Error message title string (rdata pointer at orig 0x497c78). Reached via
// absolute address by the boot loop's MessageBoxA call in Teardown.
extern "C" char *g_SupervisorErrTitle_497c78;
// Per-frame input/pad flag (orig 0x575c0c). Stamped each frame by RunSession.
extern "C" u8 g_SupervisorFrameInputFlag_575c0c;
// Note: g_SupervisorReplayActive shares DAT_0062f4e0 with the existing
// g_SupervisorG0x62f4e0 declared earlier in this file (u32). Reuse that name.
// HINSTANCE mirror of g_Supervisor.hInstance (orig 0x575950), since the boot
// helpers reach the value via the absolute address.
// IDirect3D8 iface mirror (orig 0x575954).
// D3D device pointer slot (orig 0x575958). Reused from the existing
// link_globals.cpp definition (g_SupervisorD3dDevice_575958); the boot
// helpers reach it via absolute address with a VTBL macro cast.
extern "C" f64  g_PlayerAbsorbSizeZero;            // rdata double 0.0 at 0x498a90 (also used as QPC threshold / absorb-size threshold)
extern "C" f64  g_SupervisorFrametime_498bc0;   // rdata double (~0.01666)
extern "C" f64  g_SupervisorFrametime2_498bc8;  // rdata double (~0.015)

// P1.3 rdata-string slots reached via absolute address by the boot loop's
// GameErrorLog/Fatal calls (orig .rdata addresses; content lives in orig).

// ---- P1.3 boot-path helper externs (not yet lifted; resolved by stubs in
//      link_stubs.cpp for the normal build). Each one's FUN_ anchor is
//      preserved in the comment for the next RE pass.
extern "C" i32  __fastcall Supervisor_LoadConfig_004398b6(char *path); // LoadConfig
extern "C" i32  __fastcall Supervisor_CheckAlreadyRunning_00435bd0();  // CheckForRunningGameInstance
extern "C" void __fastcall Supervisor_InitGameErrorCtx_00435ec0();     // pre-boot error ctx init
extern "C" void __fastcall Supervisor_GameErrorLog_004315f0(void *ctx, char *msg); // Log
extern "C" void __fastcall Supervisor_GameErrorFatal_00431730(void *ctx, char *msg); // Fatal
extern "C" void __fastcall Supervisor_FlushGameError_00431540(i32 size); // Flush
extern "C" void __fastcall Supervisor_SeedRngFromPerf_00435e30();      // QPC seed
// WndProc thunk: minimal DefWindowProcA forwarder so CreateWindowExA
// succeeds. The full orig WndProc (FUN_00434490) handles WM_CLOSE/
// WM_ACTIVATE/WM_SETCURSOR + IME; lifting it is a separate task.
extern "C" LRESULT WINAPI Supervisor_WndProc_Thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
extern "C" void __fastcall Supervisor_PreSessionInit_004345c0();        // per-frame pre-run
extern "C" i32  __fastcall Supervisor_RunFrameOnce_0042fd60();         // chain calc/draw+present
extern "C" void __fastcall Supervisor_DrainChain_0044c9c0();           // chain cleanup pass
extern "C" void __fastcall Supervisor_AnmMgrReset_0044b830();          // pre-alloc reset
extern "C" void __fastcall Supervisor_AnmMgrReleaseVm_0044d620();      // AnmVm release
extern "C" void __fastcall Supervisor_AnmMgrDtorCall_0044b560(void *hwnd); // pre-ctor drain
extern "C" void __fastcall Supervisor_SuspendMusic_00430290();         // BGM pause
extern "C" void __fastcall Supervisor_ShutdownAudio_004312c0();        // audio teardown
extern "C" void __fastcall Supervisor_ResetDisplayMode_00435230();     // display-mode reset
extern "C" void __fastcall Supervisor_DeviceLostHandler_00433f20();    // device-lost recovery
extern "C" void __fastcall Supervisor_DeviceNotResetHandler_004356a0(); // device-reset recovery
extern "C" void __fastcall Supervisor_FlushReplay_0044a302();          // replay data flush
extern "C" void __fastcall Supervisor_TeardownFinal_00430060();        // final teardown
extern "C" void __fastcall Supervisor_AnmVmInit_0044f580();            // AnmVm zero/init
extern "C" void __fastcall Supervisor_AnmMgrFlush_0044f5c0();          // FlushVertexBuffer
extern "C" void __fastcall Supervisor_BgmFrameTick_0043a207();         // bgm per-frame tick
extern "C" void __fastcall Supervisor_MidiFrameTick_0042fe20();        // midi per-frame tick
extern "C" void __fastcall Supervisor_PostD3DInit_0044a520();          // post-CreateDevice
extern "C" f32  __fastcall Supervisor_GetRefreshRate_0048b920();       // display refresh rate
extern "C" void __fastcall D3DXMatrixLookAtLH_004621a0(void *out, void *eye, void *at, void *up);
extern "C" void __fastcall D3DXMatrixPerspectiveLH_00461dd8(void *out, f32 w, f32 h, f32 zn, f32 zf);

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
    if (*(f32 *)((u8 *)this + 0x178) < g_SupervisorC0x498a58)
    {
        *subframes = *subframes + *(f32 *)((u8 *)this + 0x178);
        if (g_SupervisorC0x498a54 <= *subframes)
        {
            *frames = *frames + 1;
            *subframes = *subframes - g_SupervisorC0x498a54;
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
    g_SupervisorG0x575c0c = 0xff;
    if (g_SoundPlayer.backgroundMusic != 0)
    {
        CStreamingSound_UpdateFadeOut();
    }

    if (g_GameManagerRankForceFlag == 0)
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
        Supervisor_LogStr1("scene %d -> %d\r\n", s->wantedState, s->curState);

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
                    ((IDirect3DDevice8 *)g_Supervisor.d3dDevice)->ResourceManagerDiscardBytes(0);
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
                    ((i32 *)0x62f52c)[g_GameManager.difficulty * 0xb]++;
                    GameManager::CutChain();
                    if (MainMenu::RegisterChain(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    break;
                case 0xa:
                    GameManager::CutChain();
                    if ((g_GameManager.statusBitfield & 1) == 0 && g_GameManager.difficulty < 4)
                        g_SupervisorG0x62f85c = 0;
                    else
                        g_SupervisorG0x62f85c = g_SupervisorG0x62f85c - 1;
                    ReplayManager::SaveReplay((char*)0, (char*)0);
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                case 0xb:
                    g_Supervisor.curState = 3;
                    GameManager::CutChain();
                    g_SupervisorG0x62f85c = g_SupervisorG0x62f85c - 1;
                    if (GameManager::RegisterChain() != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                    s->curState = 2;
                    break;
                case 0xc:
                    g_Supervisor.curState = 3;
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
                ((IDirect3DDevice8 *)g_Supervisor.d3dDevice)->ResourceManagerDiscardBytes(0);
                if (MainMenu::RegisterChain(0) != 0) return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
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
        if (Supervisor_AutosaveScore("0100b", g_SupervisorG0x575c14, g_SupervisorG0x575c10) != 0)
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

    if (g_GameManagerRankForceFlag != 0) goto end_check;
    g_SupervisorG0x135e1f0 += (i32)g_Supervisor.cfg.frameskipConfig + 1;

    if (g_SupervisorG0x575bbc == 0)
    {
        if ((g_SupervisorG0x135e2a4 & 1) == 0)
        {
            g_SupervisorG0x135e2a4 |= 1;
            g_SupervisorG0x135e2a0 = timeGetTime();
        }
        now = timeGetTime();
        if (now < g_SupervisorG0x135e2a0)
        {
            g_SupervisorG0x135e2a0 = now;
            g_SupervisorG0x135e1f0 = 0;
        }
        if ((u32)(now - g_SupervisorG0x135e2a0) < 0x1f4) goto done_fps;
        frameTime = (f32)(i64)(now - g_SupervisorG0x135e2a0);
        frameTimeSec = frameTime / g_SupervisorC0x498ab8;
        g_SupervisorG0x135e2a0 = now;
    compute_fps:
        fps = (f32)(i64)g_SupervisorG0x135e1f0 / frameTimeSec;
        g_SupervisorG0x135e1f0 = 0;
        sprintf_th07(g_SupervisorG0x135e0f0, "%.02ffps", (f64)fps);
        if ((g_GameManager.statusBitfield >> 2 & 1) == 0 || drawArg == 0) goto done_fps;
        base = g_SupervisorC0x498a48;
        g_Supervisor.d3dDeviceCaps184 += base;
        if (base * g_SupervisorC0x498b20 > fps)
            g_Supervisor.d3dDeviceCaps180 += base;
        else if (base * g_SupervisorC0x498b1c > fps)
            g_Supervisor.d3dDeviceCaps180 = base * g_SupervisorC0x498b18 + g_Supervisor.d3dDeviceCaps180;
        else if (base * g_SupervisorC0x498a50 > fps)
            g_Supervisor.d3dDeviceCaps180 = base * g_SupervisorC0x498aa0 + g_Supervisor.d3dDeviceCaps180;
        else
            g_Supervisor.d3dDeviceCaps180 = base * g_SupervisorC0x498a50 + g_Supervisor.d3dDeviceCaps180;
        if ((g_GameManager.statusBitfield >> 3 & 1) == 0)
        {
            g_Supervisor.unk188 = FloatToI16(fps + g_SupervisorC0x498a50);
            sprintf_th07(g_SupervisorG0x135dff0, "%2d", (i32)g_Supervisor.unk188);
        }
        else
            sprintf_th07(g_SupervisorG0x135dff0, "%2d", (i32)g_Supervisor.unk188);
        goto done_fps;
    }
    else
    {
        i64 qpcNow;
        if (g_SupervisorG0x135e298 == 0)
            QueryPerformanceCounter((LARGE_INTEGER *)0x0135e298);
        QueryPerformanceCounter((LARGE_INTEGER *)&qpcNow);
        if (*(i32 *)((u8 *)&qpcNow + 0) < g_SupervisorG0x135e298)
        {
            g_SupervisorG0x135e298 = *(i32 *)((u8 *)&qpcNow + 0);
            g_SupervisorG0x135e29c = *(i32 *)((u8 *)&qpcNow + 4);
            g_SupervisorG0x135e1f0 = 0;
        }
        if ((u32)(*(i32 *)((u8 *)&qpcNow + 0) - g_SupervisorG0x135e298) <
            (u32)(g_SupervisorG0x135e298 + (g_SupervisorG0x575bbc >> 1))) goto end_check;
        frameTime = (f32)(i64)(*(i32 *)((u8 *)&qpcNow + 0) - g_SupervisorG0x135e298);
        frameTimeSec = frameTime / (f32)(i64)g_SupervisorG0x575bbc;
        g_SupervisorG0x135e298 = *(i32 *)((u8 *)&qpcNow + 0);
        g_SupervisorG0x135e29c = *(i32 *)((u8 *)&qpcNow + 4);
        g_SupervisorG0x135dfec += 1;
        if (g_SupervisorG0x135dfec % 8 == 0)
            g_Supervisor.TickTimer((i32 *)0, (f32 *)0);
        goto compute_fps;
    }
done_fps:
end_check:
    if (g_Supervisor.unkIsInEnding != 0 || drawArg == 0) return;
    {
        D3DXVECTOR3 pos1;
        pos1.x = g_SupervisorC0x498b14;
        pos1.y = g_SupervisorC0x498b10;
        pos1.z = 0.0f;
        g_AsciiManager.AddString(&pos1, g_SupervisorG0x135e0f0);
    }
    if ((g_GameManager.statusBitfield >> 3 & 1) == 0 || (g_GameManager.statusBitfield >> 2 & 1) == 0) return;
    {
        D3DXVECTOR3 pos2;
        pos2.x = g_SupervisorC0x498a6c;
        pos2.y = g_SupervisorC0x498a68;
        pos2.z = 0.0f;
        if (g_SupervisorG0x575bf8 != 0)
            g_SupervisorG0x13542d8 = 0xffff4040;
        else
            g_SupervisorG0x13542d8 = 0xffffffd0;
        g_AsciiManager.AddString(&pos2, g_SupervisorG0x135dff0);
        g_SupervisorG0x13542d8 |= -1;
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
//   3. Runs Supervisor::Callback6 (version/archive file probe)  bail on fail.
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
    dev = (IDirect3DDevice8 *)g_Supervisor.d3dDevice;
    dev->BeginScene();
    dev->Clear(0, 0, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0);
    dev->EndScene();
    if (dev->Present(0, 0, 0, 0) < 0)
    {
        dev->Reset((D3DPRESENT_PARAMETERS *)&g_SupervisorPresentParams_575a30[0]);
    }
    dev->BeginScene();
    dev->Clear(0, 0, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0);
    dev->EndScene();
    if (dev->Present(0, 0, 0, 0) < 0)
    {
        dev->Reset((D3DPRESENT_PARAMETERS *)&g_SupervisorPresentParams_575a30[0]);
    }

    // 3. Callback6 (version/archive probe). Bail -1 on fail.
    if (Supervisor_Callback6() != 0)
    {
        return (ZunResult)-1;
    }

    // 4. AnmManager::LoadTexture(this, 0, "data/title/th07logo.jpg") @ 0x4547b0.
    Callback_4547f((void *)g_AnmManager, 0, "data/title/th07logo.jpg");
    g_Supervisor.unkIsInEnding = 1;

    // 5. Either single-frame logo path or four-frame boot fade.
    if (g_Supervisor.vsyncDisabled == 0)
    {
        if (Supervisor_Callback7() != 0)
        {
            AnmMgr_ReleaseTextureSlot_454a10((void *)g_AnmManager, 0);
            return (ZunResult)-2;
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            dev->BeginScene();
            AnmMgr_BootDrawLogo_454aa0((void *)g_AnmManager, 0, 0, 0, 0, 0);
            dev->EndScene();
            if (dev->Present(0, 0, 0, 0) < 0)
            {
                dev->Reset((D3DPRESENT_PARAMETERS *)&g_SupervisorPresentParams_575a30[0]);
            }
        }
    }
    AnmMgr_ReleaseTextureSlot_454a10((void *)g_AnmManager, 0);

    // 6. Reset frame counters + stamp startup time.
    *(i32 *)((u8 *)s + 0x168) = 0;
    *(i32 *)((u8 *)s + 0x164) = 0;
    *(u32 *)((u8 *)s + 0x190) = timeGetTime();
    g_SupervisorG0x49fe20 = *(u16 *)((u8 *)s + 0x190);
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
        MidiOutput_Play(*(void **)((u8 *)s + 0x17c), 0x1e, "bgm/init.mid");
    }

    // 8. SoundPlayer Callback_C7d0; AnmManager::LoadAnm(0, "data/text.anm", 0x700).
    SoundPlayer_Callback_C7d0((void *)&g_SoundPlayer);
    if (AnmManager_LoadAnm(g_AnmManager, 0, "data/text.anm", 0x700) != 0)
    {
        return (ZunResult)-1;
    }

    // 9. Callback_01e30 (error-logged failure path).
    if (Callback_01e30() != 0)
    {
        g_GameErrorContext.Log("%s", "error : \225\266\216\232\202\314\217\211\212\372\211\273\202\311\216\270\224s\202\265\202\334\202\265\202\275\015\012");
        return (ZunResult)-1;
    }

    // 10. AnmManager::Callback_D630; Callback_3225b; SoundPlayer::LoadFmt.
    AnmManager_Callback_D630(g_AnmManager);
    Callback_3225b();
    if (SoundPlayer_LoadFmt((void *)&g_SoundPlayer, "bgm/thbgm.fmt") != 0)
    {
        g_GameErrorContext.Log("%s", "error : BGM \202\314\217\211\212\372\211\273\202\311\216\270\224s\202\265\202\334\202\265\202\275\015\012");
        return (ZunResult)-1;
    }

    // 11. thbgm descriptor table install (depends on alt-mode + opts bit 0xd).
    if (g_SupervisorG0x4bdaa0 == 0)
    {
        if ((g_Supervisor.cfg.opts >> 0xd & 1) == 0)
        {
            SoundPlayer_Callback_C020((void *)&g_SoundPlayer, "thbgm.dat");
        }
        else
        {
            memcpy((void *)g_SupervisorG0x4bd994 /* orig 0x4bd994 */, (void *)"thbgm.dat", 10);
        }
    }
    else
    {
        if ((g_Supervisor.cfg.opts >> 0xd & 1) == 0)
        {
            SoundPlayer_Callback_C020((void *)&g_SoundPlayer, "th07.dat");
        }
        else
        {
            memcpy((void *)g_SupervisorG0x4bd994 /* orig 0x4bd994 */, (void *)"th07.dat", 9);
        }
    }

    // 12. Callback_44c20 returns a CFG handle; prime WAVEFORMATEX at 0x62f4e0.
    cfgHandle = Callback_44c20((void *)&g_SupervisorG0x4980c4);
    memset(&g_SupervisorG0x62f4e0, 0, 0x58 * 4);
    g_SupervisorG0x62f4e4 = 0x160;          // nChannels
    g_SupervisorG0x62f4e6 = g_SupervisorG0x62f4e4; // mirror (orig re-reads)
    g_SupervisorG0x62f4e0 = 0x54534c50;     // 'PLST' magic
    g_SupervisorG0x62f4e8 = 1;
    Callback_4547f_2arg(cfgHandle, &g_SupervisorG0x62f4e0);
    Callback_454fc(cfgHandle);

    // 13. Allocate a 0x14-byte list node, install vtable, store + init.
    listNode = operator_new_th07(0x14);
    if (listNode != 0)
    {
        ListNode_Ctor(listNode);
        *(void **)listNode = (void *)&g_SupervisorG0x496c0c;
    }
    else
    {
        listNode = 0;
    }
    g_SupervisorG0x575a64 = listNode;
    if (g_SupervisorG0x575a64 != 0)
    {
        Callback_378d0(g_SupervisorG0x575a64);
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
    gm = g_GameManager.scoreSub;
    if (gm != NULL) { _free_th07(gm); g_GameManager.scoreSub = NULL; }
    gm2 = g_GameManager.playerSub;
    if (gm2 != NULL) { _free_th07(gm2); g_GameManager.playerSub = NULL; }
    Supervisor_HeapFreeAll();
    obj2 = g_SupervisorG0x575a64;
    if (obj2 != NULL)
    {
        Supervisor_SomeCleanup4();
        obj2 = g_SupervisorG0x575a64;
        if (obj2 != NULL)
        {
            Supervisor_SomeCleanup5();
            _free_th07(obj2);
        }
        g_SupervisorG0x575a64 = NULL;
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
            g_Supervisor.cfg.version == GAME_VERSION && g_SupervisorG0x4b9e64 == 0x38)
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
        g_Supervisor.vsyncDisabled = 1;
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
        if (*(f32 *)((u8 *)this + 0x178) == g_SupervisorC0x498a4c)
        {
            if (*(f32 *)((u8 *)this + 0x178) <= g_SupervisorC0x498a54)
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

// =============================================================================
// P0 link-pass stubs (global namespace). MidiOutput / ReplayManager are
// referenced here as global-namespace classes via local fwd-decls (the real
// th07::MidiOutput in MidiOutput.hpp is a different mangled symbol). Defined
// outside namespace th07 to match the caller's mangling.
// =============================================================================
ZunResult MidiOutput::LoadFile(char *) { return ZUN_SUCCESS; }
ZunResult MidiOutput::ParseFile(int) { return ZUN_SUCCESS; }
ZunResult MidiOutput::Play() { return ZUN_SUCCESS; }
ZunResult MidiOutput::SetFadeOut(unsigned int) { return ZUN_SUCCESS; }
ZunResult MidiOutput::StopPlayback() { return ZUN_SUCCESS; }
void ReplayManager::SaveReplay(char *, char *) { }
extern "C" void *_free_th07(void *p) { return 0; }

// =============================================================================
// P1.3 boot-path functions (FUN_00434020 / a40 / a80 / bd0 / 46e0 / 3e90).
// These are global-namespace extern "C" free functions (orig is NOT
// __thiscall; they reach the Supervisor singleton via the absolute
// DAT_00575950 base). Not in objdiff tracking (no orig .obj / not named in
// mapping.csv), so the priority is game-behavior correctness + normal-build
// linkability, not byte-exact objdiff.
// =============================================================================

// Vtable-indexed call helpers. The orig disasm emits the pattern
//   CALL dword ptr [EAX + vtable_off]
// where EAX = *(void**)com_interface. We mirror that by reading the
// vtable pointer then indexing by byte offset, then DEREFERENCING the
// slot to obtain the function pointer. The cast to u8* is required
// because MSVC 7.0 forbids pointer arithmetic on void* (C2036).
//
// BUG FIX (2026-06-23): the old macro returned (vtable + off), i.e. the
// ADDRESS of the slot, not the function pointer IN the slot. The cast to
// a function pointer then made `call eax` jump into the middle of the
// vtable itself (0x7B41CE00 = vtable+0x20), which is Wine's
// __wine_unimplemented region -> SIGILL. The slot must be dereferenced
// with one more indirection.
//
// IMPORTANT: All D3D8 / Direct3D8 COM methods are __stdcall (callee-clean),
// matching the Win32 ABI. Orig disasm pushes every argument on the stack
// (including 'this') and never ADDs ESP after the call -- the callee pops
// them. Using __fastcall for these calls is a bug: __fastcall passes the
// first 2 args in ECX/EDX and only spills the rest to the stack, so the
// callee (which expects N*4 bytes on the stack) pops a wrong amount and
// corrupts ESP.
#define VTBL(comIf, off) (*(void **)(*(u8 **)(comIf) + (off)))

// Bootstrap (FUN_00434a40). Creates the IDirect3D8 interface (D3D runtime
// version 0x78 = 120 = DirectX 8.0 header). Returns true on failure (i.e.
// when the D3D8 create returned NULL), false on success. On failure logs a
// fatal error to the game-error context.
extern "C" i32 __fastcall Supervisor_Bootstrap()
{
    g_SupervisorD3D8_575954 = Direct3DCreate8(120);
    if (g_SupervisorD3D8_575954 == 0)
    {
        Supervisor_GameErrorFatal_00431730(&g_GameErrorContext_624210, "Direct3D \203I\203u\203W\203F\203N\203g\202\315\211\275\214\314\202\251\215\354\220\254\217o\227\210\202\310\202\251\202\301\202\275\r\n");
        return 1;
    }
    return 0;
}

// CreateWindow (FUN_00434a80). __fastcall(HINSTANCE). Registers the "BASE"
// window class with the orig wndproc (FUN_00434490) and creates the game
// window. Windowed mode: fixed 0x280x0x1e0 (640x480). Fullscreen mode:
// computes the size from system metrics (border/caption + client 640x480).
// On success: stores HWND into both g_SupervisorWindow_575c20 and the
// g_SupervisorHwndMirror_575994 slot, seeds the rng from QPC, returns 0.
// On failure returns 1.
extern "C" i32 __fastcall Supervisor_CreateWindow(void *hInstance)
{
    WNDCLASSA wc;
    i32 windowWidth;
    i32 windowHeight;

    // Zero the WNDCLASSA (orig does a 10-dword clear loop on the struct).
    ZeroMemory(&wc, sizeof(wc));

    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.lpfnWndProc = (WNDPROC)Supervisor_WndProc_Thunk;
    g_SupervisorExStyle_575c28 = 1;
    g_SupervisorStyle_575c2c = 0;
    wc.lpszClassName = "BASE";
    wc.hInstance = (HINSTANCE)hInstance;
    RegisterClassA(&wc);

    if (g_SupervisorIsForeground_575a8a == 0)
    {
        // Windowed: fixed 640x480.
        windowWidth = 0x280;
        windowHeight = 0x1e0;
        g_SupervisorWindow_575c20 = (void *)CreateWindowExA(0, "BASE", (LPCSTR)"\223\214\225\373\227d\201X\226\262\201@\201` Perfect Cherry Blossom. ver 1.00b",
            0xcf0000, 0, 0, 0x280, 0x1e0, 0, 0, (HINSTANCE)hInstance, 0);
    }
    else
    {
        // Fullscreen: add frame extents around the 640x480 client.
        windowWidth = GetSystemMetrics(SM_CXBORDER) * 2 + 0x280;
        windowHeight = GetSystemMetrics(SM_CYCAPTION) + 0x1e0 + GetSystemMetrics(SM_CYBORDER) * 2;
        g_SupervisorWindow_575c20 = (void *)CreateWindowExA(0, "BASE", (LPCSTR)"\223\214\225\373\227d\201X\226\262\201@\201` Perfect Cherry Blossom. ver 1.00b",
            0x100a0000, (i32)0x80000000, (i32)0x80000000, windowWidth, windowHeight,
            0, 0, (HINSTANCE)hInstance, 0);
    }
    g_SupervisorHwndMirror_575994 = g_SupervisorWindow_575c20;
    if (g_SupervisorWindow_575c20 != 0)
    {
        Supervisor_SeedRngFromPerf_00435e30();
        return 0;
    }
    return 1;
}

// Teardown / error-context flush (FUN_00433e90). __fastcall(char *buf).
// If the error buffer has been written to (i.e. buf[0x2000] != buf, the
// self-marker GameErrorContext uses to mean "empty"), logs a separator,
// optionally shows a MessageBox (if buf[0x2004] flag set), then flushes the
// buffer to disk and resets its size.
extern "C" void __fastcall Supervisor_Teardown(char *buf)
{
    char *p;

    if (*(char **)(buf + 0x2000) != buf)
    {
        Supervisor_GameErrorLog_004315f0(buf,
            "---------------------------------------------------------- \r\n");
        if (buf[0x2004] != 0)
        {
            MessageBoxA(0, buf, g_SupervisorErrTitle_497c78, MB_ICONHAND);
        }
        p = buf;
        while (*p != 0)
        {
            p++;
        }
        Supervisor_FlushGameError_00431540((i32)p - (i32)(buf + 1));
    }
}

// RunSession (FUN_004346e0). __fastcall(th07::Supervisor *s). The per-frame
// driver. Drives the calc/draw chain, presents the frame with vsync, and
// paces the framerate via QueryPerformanceCounter (or timeGetTime fallback).
// Returns: 0 = continue, 1 = exit session, 2 = reboot session.
extern "C" i32 __fastcall Supervisor_RunSession(th07::Supervisor *s)
{
    f64 frameElapsed;
    f64 timeElapsed;
    LARGE_INTEGER qpc;
    DWORD timeNow;
    i32 frameResult;
    u32 *viewport;
    void *dev;

    dev = s->d3dDevice;
    if (dev == 0)
    {
        return 0;
    }

    if (s->unk188 != 0)
    {
        goto mid_loop_check;
    }
frame_loop:
    do
    {
        while (1)
        {
            // When enough sub-frames have been ticked, do the actual Present
            // + chain draw + bgm/midi ticks for the composed frame.
            if ((i32)(u32)g_SupervisorFrameskipCfg_575a8b <= (i32)s->unk188)
            {
                // TestCooperativeLevel (vtable +0x88 / 0x22*4).
                ((HRESULT(__stdcall *)(void *))(VTBL(dev, 0x88)))(dev);
                Supervisor_AnmVmInit_0044f580();
                g_SupervisorFrameInputFlag_575c0c = 0xff;
                Supervisor_BgmFrameTick_0043a207();
                Supervisor_MidiFrameTick_0042fe20();
                Supervisor_AnmMgrFlush_0044f5c0();
                // Present(src, dst, dirty, plug) -- vtable +0xf4 / 0x3d*4.
                ((void (__fastcall *)(void *, void *, void *, void *, void *))(
                    VTBL(dev, 0xf4)))(dev, 0, 0, 0, 0);
                // EndScene -- vtable +0x8c / 0x23*4.
                ((void (__stdcall *)(void *))(VTBL(dev, 0x8c)))(dev);
            }
            Supervisor_AnmMgrFlush_0044f5c0();
            // Reset the viewport to the full 640x480 client.
            viewport = &g_SupervisorViewport_575a18[0];
            viewport[0] = 0;
            viewport[1] = 0;
            viewport[2] = 0x280;
            viewport[3] = 0x1e0;
            // SetViewport -- vtable +0xa0 / 0x28*4.
            ((void (__stdcall *)(void *, void *))(VTBL(dev, 0xa0)))(
                dev, &g_SupervisorViewport_575a18);

            frameResult = Supervisor_RunFrameOnce_0042fd60();
            Supervisor_DrainChain_0044c9c0();
            if (frameResult == 0)
            {
                return 1;
            }
            if (frameResult == -1)
            {
                return 2;
            }
            *(u8 *)&s->unk188 = (u8)s->unk188 + 1;
        mid_loop_check:
            if ((g_SupervisorIsForeground_575a8a != 0 || g_SupervisorWindowedOverride_575abc != 0) &&
                s->unk188 != 0)
            {
                break;
            }
        check_exit:
            if (g_SupervisorIsForeground_575a8a != 0)
            {
                return 0;
            }
            if (g_SupervisorWindowedOverride_575abc != 0)
            {
                return 0;
            }
            if ((i32)(u32)g_SupervisorFrameskipCfg_575a8b < (i32)s->unk188)
            {
                goto reset_subframe;
            }
            Supervisor_PreSessionInit_004345c0();
        }

        // Frame-rate pacing. Use QPC if available, else timeGetTime.
        if (*(i32 *)&g_SupervisorPerfFreq_575c34 != 0)
        {
            QueryPerformanceCounter(&qpc);
            frameElapsed = (f64)(qpc.u.LowPart - g_SupervisorQpcLastLo_135e208) /
                           (f64)*(i32 *)&g_SupervisorPerfFreq_575c34;
            if (frameElapsed < g_PlayerAbsorbSizeZero)
            {
                g_SupervisorQpcLastLo_135e208 = qpc.u.LowPart;
                g_SupervisorQpcLastHi_135e20c = qpc.u.HighPart;
            }
            if (frameElapsed < g_SupervisorFrametime2_498bc8 &&
                g_SupervisorLoadResult_575c3c == 0)
            {
                goto check_exit;
            }
            while (g_SupervisorFrametime2_498bc8 <= frameElapsed)
            {
                frameElapsed = frameElapsed - g_SupervisorFrametime2_498bc8;
                g_SupervisorQpcLastLo_135e208 =
                    g_SupervisorQpcLastLo_135e208 + *(i32 *)&g_SupervisorPerfFreq_575c34 / 0x3c;
            }
            if ((i32)(u32)g_SupervisorFrameskipCfg_575a8b < (i32)s->unk188)
            {
                break;
            }
            goto frame_loop;
        }
        // timeGetTime fallback path.
        timeBeginPeriod(1);
        timeNow = timeGetTime();
        timeElapsed = (f64)timeNow;
        if (timeElapsed < g_SupervisorTimeLast_135e200)
        {
            g_SupervisorTimeLast_135e200 = timeElapsed;
        }
        timeElapsed = g_SupervisorTimeLast_135e200 - timeElapsed;
        if (timeElapsed < 0)
        {
            timeElapsed = -timeElapsed;
        }
        timeEndPeriod(1);
        if (timeElapsed < g_SupervisorFrametime_498bc0 && g_SupervisorLoadResult_575c3c == 0)
        {
            goto check_exit;
        }
        while (g_SupervisorFrametime_498bc0 <= timeElapsed)
        {
            timeElapsed = timeElapsed - g_SupervisorFrametime_498bc0;
            g_SupervisorTimeLast_135e200 = g_SupervisorTimeLast_135e200 + g_SupervisorFrametime_498bc0;
        }
    } while ((i32)(u8)s->unk188 <= (i32)(u32)g_SupervisorFrameskipCfg_575a8b);
reset_subframe:
    Supervisor_PreSessionInit_004345c0();
    *(u8 *)&s->unk188 = 0;
    g_SupervisorFrameCounter_135e1f8 = g_SupervisorFrameCounter_135e1f8 + 1;
    return 0;
}

// InitD3D (FUN_00434bd0). Builds the D3DPRESENT_PARAMETERS, attempts
// CreateDevice in several fallback orderings (hardware vertex processing
// -> software T&L -> reference rasterizer), then primes the viewport /
// projection matrices and registers the chain. Returns 0 on success, 1 on
// failure (with the D3D8 iface released).
extern "C" i32 __fastcall Supervisor_InitD3D()
{
    D3DPRESENT_PARAMETERS_FAKE localPP;
    i32 iVar1;
    u32 *dst;
    u32 *srcP;
    i32 fallbackTried;
    char localMsgBuf[0x2000];
    f32 refreshRate;
    // local_20 is the present-params Windowed field (orig local_20 lives at
    // [ebp-0x20] = the 12th dword of the 13-dword present-params struct,
    // i.e. localPP.fields[11]). Previously declared as a separate local,
    // which meant CreateDevice always saw Windowed=0 (fullscreen) and tried
    // to allocate huge surfaces -> Wine OOM. Now write directly to fields[11].
    u32 local_10;
    D3DXVECTOR3 eye;
    D3DXVECTOR3 at;
    D3DXVECTOR3 up;

    fallbackTried = 0;

    // Zero the local present-parameters (orig does a 13-dword clear loop).
    ZeroMemory(&localPP, sizeof(localPP));

    // Query the current display mode (GetAdapterDisplayMode -- vtable +0x20).
    ((void (__stdcall *)(void *, i32, void *))(VTBL(g_SupervisorD3D8_575954, 0x20)))(
        g_SupervisorD3D8_575954, 0, &local_10);

    if (g_SupervisorIsForeground_575a8a == 0)
    {
        // Windowed mode.
        if ((g_SupervisorCfgOpts_575a9c >> 2 & 1) == 1)
        {
            // Force 16-bit color mode.
            localPP.BackBufferFormat = 0x17; // D3DFMT_R5G6B5
            g_SupervisorColorMode_575a86 = 1;
        }
        else if ((i8)g_SupervisorColorMode_575a86 == -1)
        {
            localPP.BackBufferFormat = 0x16; // D3DFMT_A8R8G8B8
            g_SupervisorColorMode_575a86 = 0;
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "\217\211\211\361\213N\223\256\201A\211\346\226\312\202\360 32Bits \202\305\217\211\212\372\211\273\202\265\202\334\202\265\202\275\r\n");
        }
        else if (g_SupervisorColorMode_575a86 == 0)
        {
            localPP.BackBufferFormat = 0x16;
        }
        else
        {
            localPP.BackBufferFormat = 0x17;
        }
        if (g_SupervisorLoadResult_575c3c != 0)
        {
            g_SupervisorWindowedOverride_575abc = 1;
        }
        if (g_SupervisorWindowedOverride_575abc == 0)
        {
            // Default windowed present params.
            localPP.FullScreen_PresentationInterval = 0x3c; // D3DPRESENT_INTERVAL_DEFAULT (60Hz)
            localPP.Windowed = 1;
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "\203\212\203t\203\214\203b\203V\203\205\203\214\201[\203g\202\36060Hz\202\311\225\317\215X\202\360\216\212\202\335\202\334\202\267\r\n");
            localPP.SwapEffect = (g_SupervisorUnkFlag_575a8c == 0) ? 2 : 4;
        }
        else
        {
            // Windowed override (user requested).
            localPP.FullScreen_PresentationInterval = 0;   // Default interval
            localPP.SwapEffect = 3;   // D3DSWAPEFFECT_COPY_VSYNC
            localPP.Windowed = 0x80000000; // windowed-override marker (non-zero = windowed)
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "VSync\224\361\223\257\212\372\211\302\224\\\202\251\202\307\202\244\202\251\202\360\216\212\202\335\202\334\202\267\r\n");
        }
    }
    else
    {
        // Fullscreen: reuse the format from the display mode query.
        localPP.BackBufferFormat = local_10;
        localPP.SwapEffect = 3;
        localPP.MultiSampleType = 1; // orig: local_3c=1 in fullscreen path
    }

    // Common present params: 640x480 backbuffer, depth/stencil, one backbuffer.
    localPP.BackBufferWidth = 0x280;
    localPP.BackBufferHeight = 0x1e0;
    localPP.BackBufferCount = 1;
    localPP.AutoDepthStencilFormat = 0x50; // D3DFMT_D24S8
    localPP.EnableAutoDepthStencil = 1;
    localPP.Flags = 1;            // orig local_24=1
    g_SupervisorFrameFlags_575adc = g_SupervisorFrameFlags_575adc | 2;
    g_SupervisorHasHwVertexProc_575ac4 = 1;

    // CreateDevice retry loop. Try hardware vertex processing first, then
    // software T&L, then reference rasterizer.
    do
    {
        if ((g_SupervisorCfgOpts_575a9c >> 9 & 1) == 0)
        {
            // CreateDevice(adapter, deviceType, hwnd, behaviorFlags, pp, &dev)
            // -- vtable +0x3c / 0xf*4. Try hardware vertex processing (0x40).
            iVar1 = ((i32(__stdcall *)(void *, i32, i32, void *, i32, void *, void *))(
                VTBL(g_SupervisorD3D8_575954, 0x3c)))(g_SupervisorD3D8_575954, 0, 1,
                g_SupervisorWindow_575c20, 0x40, &localPP, (void *)&g_SupervisorD3dDevice_575958);
            if (iVar1 >= 0)
            {
                Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "T&L HAL \202\305\223\256\215\354\202\265\202\334\201`\202\267\r\n");
                g_SupervisorFrameFlags_575adc = g_SupervisorFrameFlags_575adc | 1;
                goto device_ok;
            }
            if (fallbackTried != 0)
            {
                Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "T&L HAL \202\315\216g\227p\202\305\202\253\202\310\202\242\202\346\202\244\202\305\202\267\r\n");
            }
            // Fallback: software vertex processing (0x20).
            iVar1 = ((i32(__stdcall *)(void *, i32, i32, void *, i32, void *, void *))(
                VTBL(g_SupervisorD3D8_575954, 0x3c)))(g_SupervisorD3D8_575954, 0, 1,
                g_SupervisorWindow_575c20, 0x20, &localPP, (void *)&g_SupervisorD3dDevice_575958);
            if (iVar1 < 0)
            {
                if (fallbackTried != 0)
                {
                    Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "HAL \202\340\216g\227p\202\305\202\253\202\310\202\242\202\346\202\244\202\305\202\267\r\n");
                }
                goto ref_or_fail;
            }
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "HAL \202\305\223\256\215\354\202\265\202\334\202\267\r\n");
        device_ok:
            g_SupervisorFrameFlags_575adc = g_SupervisorFrameFlags_575adc & ~1u;
        copy_pp_and_setup:
            // Copy the local present-params into the persistent
            // g_SupervisorPresentParams_575a30 buffer.
            dst = &g_SupervisorPresentParams_575a30[0];
            srcP = (u32 *)&localPP;
            for (iVar1 = 0xd; iVar1 != 0; iVar1--)
            {
                *dst = *srcP;
                dst++;
                srcP++;
            }
            // Build the view + projection matrices.
            //   eye = (320, -240, -240/refreshRate)
            //   at  = (320, -240, 0)
            //   up  = (0, 1, 0)
            //   proj= D3DXMatrixPerspectiveLH(w=0x3f060a92, h=0x3faaaaab,
            //                                 zn=100, zf=10000)
            refreshRate = 240.0f / Supervisor_GetRefreshRate_0048b920();
            eye.x = 320.0f; eye.y = -240.0f; eye.z = -refreshRate;
            at.x = 320.0f; at.y = -240.0f; at.z = 0.0f;
            up.x = 0.0f; up.y = 1.0f; up.z = 0.0f;
            D3DXMatrixLookAtLH_004621a0(&th07::g_Supervisor.viewMatrix, &eye, &at, &up);
            {
                f32 projW = *(f32 *)0x498b20; // rdata (0x3f060a92)
                f32 projH = *(f32 *)0x498b24; // rdata (0x3faaaaab)
                D3DXMatrixPerspectiveLH_00461dd8(&th07::g_Supervisor.projectionMatrix,
                    projW, projH, 100.0f, 10000.0f);
            }
            ((void (__stdcall *)(void *, i32, void *))(VTBL(g_SupervisorD3dDevice_575958, 0x94)))(
                g_SupervisorD3dDevice_575958, 2, &th07::g_Supervisor.viewMatrix);
            ((void (__stdcall *)(void *, i32, void *))(VTBL(g_SupervisorD3dDevice_575958, 0x94)))(
                g_SupervisorD3dDevice_575958, 3, &th07::g_Supervisor.projectionMatrix);
            // SetViewport -- vtable +0xa0.
            ((void (__stdcall *)(void *, void *))(VTBL(g_SupervisorD3dDevice_575958, 0xa0)))(
                g_SupervisorD3dDevice_575958, &g_SupervisorViewport_575a18);
            // SetMaterial -- vtable +0x1c.
            ((void (__stdcall *)(void *, void *))(VTBL(g_SupervisorD3dDevice_575958, 0x1c)))(
                g_SupervisorD3dDevice_575958, (void *)&g_SupervisorUnkAe8_575ae8);

            if ((g_SupervisorCfgOpts_575a9c & 1) == 0 && (g_SupervisorUnkB78_575b78 & 0x40) == 0)
            {
                Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "D3DTEXOPCAPS_ADD \202\360\203T\203|\201[\203g\202\265\202\304\202\242\202\334\202\271\202\361\201A\220F\211\301\216Z\203G\203~\203\205\203\214\201[\203g\203\202\201[\203h\r\n");
                g_SupervisorCfgOpts_575a9c = g_SupervisorCfgOpts_575a9c | 1;
            }
            if (g_SupervisorVramMegs_575b40 < 0x101)
            {
                Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "512 \210\310\217\343\202\314\203e\203N\203X\203`\203\203\202\360\203T\203|\201[\203g\202\265\202\304\202\242\202\334\202\271\202\361\201B\226w\202\307\202\314\212G\202\252\203{\203P\202\304\225\\\216\246\r\n");
            }
            Supervisor_ResetDisplayMode_00435230();
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, localMsgBuf);

            // Try to enable 16-bit double-buffering if not already tried.
            if ((g_SupervisorCfgOpts_575a9c >> 2 & 1) == 0 && fallbackTried != 0)
            {
                // CheckDeviceFormat -- vtable +0x28.
                iVar1 = ((i32(__stdcall *)(void *, i32, i32, i32, i32, i32, i32))(
                    VTBL(g_SupervisorD3D8_575954, 0x28)))(g_SupervisorD3D8_575954, 0, 1,
                    localPP.BackBufferFormat, 0, 3, 0x15);
                if (iVar1 == 0)
                {
                    g_SupervisorFrameFlags_575adc = g_SupervisorFrameFlags_575adc | 4;
                }
                else
                {
                    g_SupervisorFrameFlags_575adc = g_SupervisorFrameFlags_575adc & ~4u;
                    g_SupervisorCfgOpts_575a9c = g_SupervisorCfgOpts_575a9c | 4;
                    Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "D3DFMT_A8R8G8B8 \202\360\203T\203|\201[\203g\202\265\202\304\202\242\202\334\202\271\202\361\201A\214\270\220F\203\202\201[\203h\202\305\223\256\215\354\202\265\202\334\202\267\r\n");
                }
            }
            Supervisor_DeviceNotResetHandler_004356a0();
            Supervisor_PostD3DInit_0044a520();
            g_SupervisorHasHwVertexProc_575ac4 = 0;
            g_SupervisorExitFlag_575c24 = 0;
            return 0;
        }
    ref_or_fail:
        // Last-ditch: reference rasterizer (deviceType 2).
        iVar1 = ((i32(__stdcall *)(void *, i32, i32, void *, i32, void *, void *))(
            VTBL(g_SupervisorD3D8_575954, 0x3c)))(g_SupervisorD3D8_575954, 0, 2,
            g_SupervisorWindow_575c20, 0x20, &localPP, (void *)&g_SupervisorD3dDevice_575958);
        if (iVar1 >= 0)
        {
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "REF \202\305\223\256\215\354\202\265\202\334\202\267\202\252\201A\217d\202\265\202\254\202\304\213\260\202\347\202\255\203Q\201[\203\200\202\311\202\310\202\350\202\334\202\271\202\361...\r\n");
            fallbackTried = 0;
            goto device_ok;
        }
        if (g_SupervisorWindowedOverride_575abc == 0)
        {
            // First failure: downgrade to windowed and retry.
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "\203\212\203t\203\214\203b\203V\203\205\203\214\201[\203g\202\252\225\317\215X\202\305\202\253\202\334\202\271\202\361\r\n");
            localPP.FullScreen_PresentationInterval = 0;
            g_SupervisorHasHwVertexProc_575ac4 = 0;
            fallbackTried = 1;
        }
        else
        {
            // Already tried the fallback: hard fail.
            if (localPP.Windowed != 0x80000000)
            {
                Supervisor_GameErrorFatal_00431730(&g_GameErrorContext_624210, "Direct3D \202\314\217\211\212\372\211\273\202\311\216\270\224s\201A\202\261\202\352\202\315\203Q\201[\203\200\202\315\217o\227\210\202\334\202\271\202\361\r\n");
                if (g_SupervisorD3D8_575954 != 0)
                {
                    ((void (__stdcall *)(void *))(VTBL(g_SupervisorD3D8_575954, 8)))(
                        g_SupervisorD3D8_575954);
                    g_SupervisorD3D8_575954 = 0;
                }
                return 1;
            }
            Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "\224\361\223\257\212\372\215X\220V\202\340\215s\202\246\202\334\202\271\202\361\201B\210\352\224\324\211\230\202\242\203\202\201[\203h\202\311\225\317\215X\202\265\202\334\202\267\r\n");
            Supervisor_GameErrorFatal_00431730(&g_GameErrorContext_624210, "*** \203\212\203t\203\214\203b\203V\203\205\203\214\201[\203g\202\36060Hz\202\311\225\317\215X\202\267\202\351\202\261\202\306\202\360\220\204\217\247\202\265\202\334\202\267 ***\r\n");
            localPP.Windowed = 1;
            localPP.SwapEffect = 3;
        }
    } while (1);
}


