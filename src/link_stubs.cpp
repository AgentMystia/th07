// Auto-generated link stubs (Phase C, P0 link pass). Resolves cross-
// module `@Name@N` fastcall and `_Name` cdecl references that no source
// module defines yet. Each stub is a no-op returning 0; the stack-
// cleanup byte count from the @ suffix is preserved by declaring the
// equivalent number of dummy args so MSVC emits the matching `RET N`.
// th07 uses __fastcall for callbacks (AGENTS.md section 6) so the stubs are
// declared __fastcall too -- this yields the `@Name@N` mangling the
// callers expect (no leading underscore, unlike __stdcall).
//
// Normal-build only; link_stubs.obj is excluded from objdiff (not in
// objdiff.json / config/ghidra_ns_to_obj.csv).

#include "inttypes.hpp"
#include "ZunResult.hpp"

#include <windows.h>

#include "Chain.hpp"
#include "AsciiManager.hpp"

extern "C" {
    void __fastcall AnmManager_Callback_D630(i32 a0) { }
    void __fastcall AnmManager_FlushSprites(i32 a0) { }
    i32  __fastcall AnmManager_LoadAnm(i32 a0, i32 a1, i32 a2, i32 a3) { return 0; }
    void __fastcall AnmMgr_ExecuteAnmIdx(i32 a0, i32 a1) { }
    void __fastcall AnmVm_Die(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall AnmVm_ExecuteScript(i32 a0) { }
    void __fastcall BulletMgr_RemoveAllBullets(i32 a0) { }
    void __fastcall CMyFont_GetPixelFormat(i32 a0) { }
    void __fastcall CStreamingSound_UpdateFadeOut(void) { }
    // Callback_01e30 (orig FUN_00401e30) is actually AsciiManager::RegisterChain.
    // The real impl is in AsciiManager.cpp -- call it so the ASCII chain
    // registers + the FPS counter / menu text get callbacks.
    i32  __fastcall Callback_01e30(void) {
        return (i32)th07::AsciiManager::RegisterChain();
    }
    void __fastcall Callback_3225b(void) { }
    void __fastcall Callback_378d0(i32 a0) { }
    void * __fastcall Callback_44c20(i32 a0) { return (void *)1; }   // non-NULL handle
    void __fastcall Callback_4547f(i32 a0, i32 a1, i32 a2) { }
    void __fastcall Callback_4547f_2arg(i32 a0, i32 a1) { }
    void __fastcall Callback_454fc(i32 a0) { }
    void __fastcall Controller_GetInput(void) { }
    void __fastcall EffectManager_SpawnEffectObj(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall EffectManager_SpawnEffectObj2(i32 a0, i32 a1, i32 a2, i32 a3) { }
    void __fastcall Effect_EnemyDamage(i32 a0, i32 a1, i32 a2) { }
    void __fastcall EnemyManager_IsGameActive(i32 a0) { }
    void __fastcall EnemyManager_ResetBorderState(i32 a0) { }
    void __fastcall EnemyManager_ResetStage(i32 a0) { }
    void __fastcall FloatToI16(i32 a0) { }
    void __fastcall GameManager_BorderSetup(i32 a0) { }
    void __fastcall GameManager_CheckState(i32 a0) { }
    void __fastcall GameManager_GameStateFlag(i32 a0) { }
    void __fastcall GameManager_SetStageState(i32 a0, i32 a1) { }
    void __fastcall GameManager_TintSprites(i32 a0) { }
    void __fastcall Gui_EndPlayerSpellcard2(void) { }
    void __fastcall Gui_EndSpellcard(void) { }
    void __fastcall Gui_HasMessage(void) { }
    void __fastcall Gui_Reset(void) { }
    void __fastcall Gui_ShowBombName(i32 a0, i32 a1) { }
    void __fastcall Gui_ShowBombPortrait2(void) { }
    void __fastcall Item_FullPowerSetup(i32 a0, i32 a1) { }
    void __fastcall Item_SpawnItem(i32 a0, i32 a1, i32 a2) { }
    void __fastcall ListNode_Ctor(i32 a0) { }
    void __fastcall MidiOutput_Ctor(i32 a0) { }
    i32  __fastcall MidiOutput_Play(i32 a0, i32 a1, i32 a2) { return 0; }
    void __fastcall MidiOutput_StopPlayback(void) { }
    i32 __fastcall Pbg4_NodePick(i32 a0) { return 0; }
    void __fastcall Pbg4_NodePush(i32 a0, i32 a1) { }
    void __fastcall Pbg4_NodeShrink(i32 a0, i32 a1) { }
    void __fastcall Player_ResetOptionSpriteScale(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall Rng_GetRandomU32(i32 a0) { }
    void __fastcall SoundPlayer_Callback_C020(i32 a0, i32 a1) { }
    void __fastcall SoundPlayer_Callback_C7d0(i32 a0) { }
    i32  __fastcall SoundPlayer_LoadFmt(i32 a0, i32 a1) { return 0; }
    void __fastcall Sound_PlayEffect(i32 a0, i32 a1) { }
    void __fastcall Supervisor_AutosaveScore(i32 a0, i32 a1, i32 a2) { }
    void __fastcall Supervisor_BombPreDraw(void) { }
    i32  __fastcall Supervisor_Callback6(void) { return 0; }
    i32  __fastcall Supervisor_Callback7(void) { return 0; }
    void __fastcall Supervisor_Callback_Fun383d8(i32 a0) { }
    void __fastcall Supervisor_Cleanup3(void) { }
    void __fastcall Supervisor_ClearAnmScriptChain(void) { }
    void __fastcall Supervisor_D3DDiscard(i32 a0) { }
    void __fastcall Supervisor_HeapFreeAll(void) { }
    void __fastcall Supervisor_MidiClearTracks(void) { }
    void __fastcall Supervisor_ReadConfigBuffer(i32 a0, i32 a1) { }
    void __fastcall Supervisor_ReleaseAnm0(void) { }
    void __fastcall Supervisor_SetAnmFlag(i32 a0, i32 a1) { }
    void __fastcall Supervisor_SetPlayerPosFlag(i32 a0) { }
    void __fastcall Supervisor_SomeCleanup1(void) { }
    void __fastcall Supervisor_SomeCleanup4(void) { }
    void __fastcall Supervisor_SomeCleanup5(void) { }
    void __fastcall Supervisor_TickTimer(i32 a0, i32 a1) { }
    void __fastcall Supervisor_TickTimer2(i32 a0, i32 a1) { }
    void __fastcall Supervisor_ValidateSize(i32 a0) { }
    void __fastcall ZunAngleNormalize(i32 a0, i32 a1) { }
    void __fastcall ZunCos(i32 a0) { }
    void __fastcall ZunSin(i32 a0) { }
    // operator_new_th07 returns void* (caller derefs). Return non-NULL so the
    // "allocation succeeded" branch runs; the ctor stubs are no-ops so the
    // bogus pointer is never really written through meaningfully.
    void * __fastcall operator_new_th07(i32 a0) { extern void *malloc(unsigned int); return malloc((unsigned int)a0); }
    void __fastcall utils_AddNormalizeAngle(i32 a0, i32 a1) { }
    void __fastcall utils_GetArcadeRegionMaxX(void) { }
    void __fastcall utils_IsInBounds(i32 a0, i32 a1, i32 a2, i32 a3) { }
    void __fastcall utils_RandF32(void) { }
    void __fastcall utils_RandF32_2(void) { }
    // AnmManager::ExecuteScript cross-module helpers (no-op stubs for the
    // normal build; objdiff resolves them via SYMBOL_MAP to the orig FUN_s).
    i32 __fastcall AnmMgr_Ftol_0048b8a0(f32 val) { return (i32)val; }
    f32 __fastcall AnmMgr_AngleNormalize_00431930(f32 angle, f32 base) { return angle; }
    void __fastcall AnmMgr_LogError_004394c7(i32 severity) { (void)severity; }
    void __fastcall AnmMgr_TickTimer_0043958d(i32 *current, f32 *subFrame)
    {
        if (current) *current = *current + 1;
        if (subFrame) *subFrame = 0.0f;
    }
    f32 __fastcall AnmMgr_RngRandI_0048bb0a(void) { return 0.0f; }
    f32 __fastcall AnmMgr_RngRangeF_0048bb40(void) { return 0.0f; }
    f32 __fastcall AnmMgr_RngRangeF2_0048bbf0(void) { return 0.0f; }
    f32 __fastcall AnmMgr_RngRange_0048b920(void) { return 0.0f; }
    f32 __fastcall AnmMgr_RngRandI_0047eca0(void) { return 0.0f; }
    f32 __fastcall AnmMgr_RngRandI_0048ba20(void) { return 0.0f; }
    // P1.3 boot-path helper stubs (not yet lifted; resolved by these stubs
    // for the normal build until each is reversed). FUN_ anchors preserved.
    i32  __fastcall Supervisor_LoadConfig_004398b6(char *path) { (void)path; return 0; }
    // CheckAlreadyRunning: orig FUN_00435bd0 creates the named mutex
    // "Touhou YouYouMu App"; returns -1 only if it already existed
    // (ERROR_ALREADY_EXISTS). For the normal build we replicate the happy
    // path: create the mutex, return 0 to let the boot continue.
    extern "C" void *g_SupervisorAppMutex_135e1f4;
    i32  __fastcall Supervisor_CheckAlreadyRunning_00435bd0() {
        g_SupervisorAppMutex_135e1f4 = CreateMutexA(0, 1, "Touhou YouYouMu App");
        if (GetLastError() == 0xb7) return -1;
        if (g_SupervisorAppMutex_135e1f4 == 0) return -1;
        return 0;
    }
    void __fastcall Supervisor_InitGameErrorCtx_00435ec0() { }
    void __fastcall Supervisor_GameErrorLog_004315f0(void *ctx, char *msg) { (void)ctx; (void)msg; }
    void __fastcall Supervisor_GameErrorFatal_00431730(void *ctx, char *msg) { (void)ctx; (void)msg; }
    void __fastcall Supervisor_FlushGameError_00431540(i32 size) { (void)size; }
    void __fastcall Supervisor_SeedRngFromPerf_00435e30() { }
    // Supervisor_RegisterWndProc_00434490 is the game's window procedure
    // (FUN_00434490). The orig handles WM_CLOSE/WM_QUERYENDSESSION/
    //WM_ENDSESSION (sets exit flag), WM_ACTIVATE (tracks foreground +
    // cursor visibility), WM_SETCURSOR, and an app-defined WM_APP+0x3c8
    // for IME. Until the full WndProc is lifted we forward every message
    // to DefWindowProcA -- this satisfies the requirement that a WNDCLASS
    // lpfnWndProc returns LRESULT with stdcall calling convention. The old
    // no-op void-returning stub crashed inside CreateWindowExA because
    // WM_NCCREATE/WM_CREATE expect a non-zero BOOL return.
    LRESULT WINAPI Supervisor_WndProc_Thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    void __fastcall Supervisor_PreSessionInit_004345c0() { }
    // RunFrameOnce: orig FUN_0042fd60 is Chain::RunCalcChain (ECX=&g_Chain
    // at the call site in RunSession @ 0x004347df). Returns the number of
    // chain callbacks executed; 0 only when a callback returns
    // EXIT_GAME_SUCCESS, -1 on EXIT_GAME_ERROR. The old no-op stub returned
    // 0 unconditionally, which RunSession misread as "exit game success"
    // and aborted the boot loop. Delegate to the real Chain method.
    i32  __fastcall Supervisor_RunFrameOnce_0042fd60() {
        return th07::g_Chain.RunCalcChain();
    }
    void __fastcall Supervisor_DrainChain_0044c9c0() { }
    void __fastcall Supervisor_AnmMgrReset_0044b830() { }
    void __fastcall Supervisor_AnmMgrReleaseVm_0044d620() { }
    void __fastcall Supervisor_AnmMgrDtorCall_0044b560(void *hwnd) { (void)hwnd; }
    void __fastcall Supervisor_SuspendMusic_00430290() { }
    void __fastcall Supervisor_ShutdownAudio_004312c0() { }
    void __fastcall Supervisor_ResetDisplayMode_00435230() { }
    void __fastcall Supervisor_DeviceLostHandler_00433f20() { }
    void __fastcall Supervisor_DeviceNotResetHandler_004356a0() { }
    void __fastcall Supervisor_FlushReplay_0044a302() { }
    void __fastcall Supervisor_TeardownFinal_00430060() { }
    void __fastcall Supervisor_AnmVmInit_0044f580() { }
    void __fastcall Supervisor_AnmMgrFlush_0044f5c0() { }
    void __fastcall Supervisor_BgmFrameTick_0043a207() { }
    void __fastcall Supervisor_MidiFrameTick_0042fe20() { }
    void __fastcall Supervisor_PostD3DInit_0044a520() { }
    f32  __fastcall Supervisor_GetRefreshRate_0048b920() { return 60.0f; }
    void __fastcall D3DXMatrixLookAtLH_004621a0(void *out, void *eye, void *at, void *up)
    { (void)out; (void)eye; (void)at; (void)up; }
    void __fastcall D3DXMatrixPerspectiveLH_00461dd8(void *out, f32 w, f32 h, f32 zn, f32 zf)
    { (void)out; (void)w; (void)h; (void)zn; (void)zf; }
    // P1.3 WinMain (FUN_00434020) helper: AnmManager ctor (FUN_0044d3e0).
    void __fastcall AnmManager_Ctor_0044d3e0(void *anmMgr) { (void)anmMgr; }
    // FUN_00454a10 / FUN_00454aa0: AnmManager boot-draw helpers, called from
    // Supervisor::AddedCallback after the logo texture load. They release/
    // draw the per-sprite texture + vertex-buffer slots. As no-op stubs they
    // keep the boot advancing (the logo just won't draw); the real impls
    // live in the AnmManager module, still to be lifted.
    void __fastcall AnmMgr_ReleaseTextureSlot_454a10(void *anm, i32 idx)
    { (void)anm; (void)idx; }
    void __fastcall AnmMgr_BootDrawLogo_454aa0(void *anm, i32 idx,
                                               i32 a2, i32 a3, i32 a4, i32 a5)
    { (void)anm; (void)idx; (void)a2; (void)a3; (void)a4; (void)a5; }
    // SoundPlayer wave-play variant -- orig FUN_0044d2f0. The full impl loads
    // and plays a .wav through the streaming-sound stack; normal-build stub
    // is a no-op until the SoundPlayer wave path is lifted.
    void __fastcall SoundPlayer_PlayWaveVariant(i32 a, void *b, char *path)
    { (void)a; (void)b; (void)path; }
} // extern "C"

// The following were originally objdiff-only extern "C" data slots (zero-init
// ints that just reserve the symbol so objdiff maps to the orig exe's import
// thunks). The NORMAL build calls them as functions, so for the normal build
// we provide real wrappers around the CRT. The data-slot form is kept only
// under DIFFBUILD so objdiff remains unaffected.
#ifndef DIFFBUILD
extern "C" {
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
    // File-backed debug log helpers used by the normal-build boot path. Wrap
    // CRT fopen/fprintf/fclose so they work from anywhere in the binary.
    void *__cdecl th07_fopen_w(const char *path, const char *mode) { return fopen(path, mode); }
    void __cdecl th07_fprintf(void *fp, const char *fmt, ...) {
        va_list a; va_start(a, fmt); vfprintf((FILE *)fp, fmt, a); va_end(a);
        fflush((FILE *)fp);
    }
    void __cdecl th07_fclose(void *fp) { if (fp) fclose((FILE *)fp); }
    // _sprintf_th07: orig imports msvcrt sprintf. Wrap vsnprintf-backed sprintf.
    int __cdecl sprintf_th07(char *dst, const char *fmt, ...) { va_list a; va_start(a, fmt); int r = vsprintf(dst, fmt, a); va_end(a); return r; }
    // _strchr_th07: orig imports msvcrt strchr.
    char *__cdecl strchr_th07(char *s, int ch) { return strchr(s, ch); }
    // _Supervisor_LogStr1: orig imports a printf-like logger; normal build just
    // no-ops (output goes nowhere; the game does not read it back).
    void __cdecl Supervisor_LogStr1(const char *fmt, ...) { (void)fmt; }
    // _new_0047d441 / _delete_0047d43c: operator new/delete thunks (orig sizes
    // 0x4d441/0x4d43c). Normal build uses malloc/free so the allocators stay
    // consistent with operator_new_th07 above.
    void *__cdecl new_0047d441(unsigned int size) { return malloc(size); }
    void __cdecl delete_0047d43c(void *p) { free(p); }
}
#else
// _Supervisor_LogStr1: extern "C" data slot (zero-init)
extern "C" int Supervisor_LogStr1 = 0;
// _delete_0047d43c: extern "C" data slot (zero-init)
extern "C" int delete_0047d43c = 0;
// _new_0047d441: extern "C" data slot (zero-init)
extern "C" int new_0047d441 = 0;
// _sprintf_th07: extern "C" data slot (zero-init)
extern "C" int sprintf_th07 = 0;
// _strchr_th07: extern "C" data slot (zero-init)
extern "C" int strchr_th07 = 0;
#endif
