// WinMain / boot entry for th07 (Perfect Cherry Blossom).
//
// The orig game's true entry point is FUN_00434020 (called from the MSVC CRT
// startup stub at 0x0047ea7d). FUN_00434020 is the boot function that drives
// the full session lifecycle:
//
//   1. Stamps hInstance into g_Supervisor.hInstance (DAT_00575950).
//   2. Captures SystemParametersInfo snapshots (SPI screensaver / keyboard
//      delay / keyboard speed) so they can be restored on exit; then
//      disables each (passing 0 to set them off).
//   3. Calls CheckForRunningGameInstance (FUN_00435bd0); if it returns -1
//      (another instance is running) the boot aborts. Otherwise LoadConfig
//      ("th07.cfg") runs; if it returns 0 (first run / no cfg) the boot
//      primes the error context and QueryPerformanceFrequency, then jumps
//      straight to the init trio.
//   4. Enters an outer do/while "reboot" loop. Each iteration:
//      a) Teardown phase: release any existing AnmManager (call its dtor,
//         free the allocation), release the D3D device + D3D8 iface,
//         destroy the window, show the cursor. If local_28 != 2 (i.e. not
//         a reboot request), restore the SystemParametersInfo settings,
//         re-enable IME, run the error-context flush (Supervisor_Teardown)
//         and return 0. Otherwise reset the error context, pump any pending
//         window messages, and fall through to the init phase.
//      b) Init phase: Bootstrap -> CreateWindow -> InitD3D. If any one
//         returns nonzero, loop back to the teardown phase (retry).
//      c) Allocation: operator_new(0x17e560) for the AnmManager; on success
//         run its ctor (FUN_0044d3e0) and store the result into the
//         g_SupervisorAnmMgrSlot_4b9e44 global. Then hide the cursor +
//         disable IME (windowed mode only).
//      d) RegisterChain (FUN_00439000). On success (0): enter the main
//         message loop (PeekMessage + TestCooperativeLevel + RunSession).
//         The loop exits when g_SupervisorExitFlag_575c24 becomes nonzero,
//         or when TestCooperativeLevel / RunSession signal device loss /
//         session end. The result code (0/2/-1) is stored in local_28 for
//         the next outer-loop iteration.
//      e) Post-session: flush replay data if a replay was active, run
//         Supervisor_TeardownFinal, drain any pending chain callbacks,
//         then loop back to (a) for either a reboot (local_28==2) or the
//         final exit.
//
// The boot loop references many standalone .data/.bss globals (window HWND,
// present-params buffer, QPC samples, frame counter, ...) declared as typed
// `extern "C"` in Supervisor.cpp. This file reaches them via the same
// declarations so the normal-build linker resolves them to one definition.

#include "Supervisor.hpp"
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "FileSystem.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <windows.h>

// Boot-helper externs (orig VAs verified from disasm of FUN_00434020). These
// are now lifted as real C++ implementations in Supervisor.cpp (P1.3); the
// signatures match the orig call sites (RunSession takes the Supervisor
// singleton in ECX, CreateWindow takes the HINSTANCE in ECX, Teardown takes
// the error-context buffer in ECX). All extern "C" -- declared at global
// scope (not inside namespace th07) so C-linkage mangling is correct.
extern "C" i32 __fastcall Supervisor_Bootstrap();
extern "C" i32 __fastcall Supervisor_CreateWindow(void *hInstance);
extern "C" i32 __fastcall Supervisor_InitD3D();
extern "C" i32 __fastcall Supervisor_RunSession(th07::Supervisor *s);
extern "C" void __fastcall Supervisor_Teardown(char *errBuf);

// Debug-log helpers (file-backed) used by the normal-build boot path. Bodies
// live in link_stubs.cpp; CRT wrappers around fopen/fprintf/fclose.
extern "C" void *__cdecl th07_fopen_w(const char *path, const char *mode);
extern "C" void __cdecl th07_fprintf(void *fp, const char *fmt, ...);
extern "C" void __cdecl th07_fclose(void *fp);
// Standalone alias of cfg.musicMode (DAT_00575a87). Set alongside
// g_Supervisor.cfg.musicMode so both MidiOutput::ReadFileData (which reads
// cfg.musicMode) and MainMenu's BGM dispatcher (which reads this byte) agree.
extern "C" u8 g_SupervisorMusicMode_575a87;

// Standalone boot globals (defined in link_globals.cpp; declared here so
// WinMain can reach them with typed C++).
extern "C" void *g_SupervisorWindow_575c20;
extern "C" i32  g_SupervisorExitFlag_575c24;
extern "C" i32  g_SupervisorSysParam_575c40;
extern "C" i32  g_SupervisorSysParam_575c44;
extern "C" i32  g_SupervisorSysParam_575c48;
extern "C" i32  g_SupervisorBootVar_575c30;
extern "C" i64  g_SupervisorPerfFreq_575c34;
extern "C" u8   g_SupervisorIsForeground_575a8a;
extern "C" u8   g_SupervisorWindowedOverride_575abc;
extern "C" char  g_GameErrorContext_624210[0x2008];
extern "C" void *g_GameErrorContextHead_626210;
extern "C" void *g_SupervisorAnmMgrSlot_4b9e44;
extern "C" i32  g_SupervisorReplayActive_62f4e0;
// (Alias of g_SupervisorG0x62f4e0 -- same DAT_0062f4e0; the wav-format-table
// global doubles as the replay-active flag for the boot loop's flush check.)
extern "C" u32  g_SupervisorPresentParams_575a30[13];

// Boot-loop helper externs (resolved by stubs in link_stubs.cpp; each
// FUN_ anchor preserved for the next RE pass).
extern "C" i32  __fastcall Supervisor_LoadConfig_004398b6(char *path);
extern "C" i32  __fastcall Supervisor_CheckAlreadyRunning_00435bd0();
extern "C" void __fastcall Supervisor_InitGameErrorCtx_00435ec0();
extern "C" void __fastcall Supervisor_GameErrorLog_004315f0(void *ctx, char *msg);
extern "C" void __fastcall Supervisor_GameErrorFatal_00431730(void *ctx, char *msg);
extern "C" void __fastcall Supervisor_FlushGameError_00431540(i32 size);
extern "C" void __fastcall Supervisor_AnmMgrReset_0044b830();
extern "C" void __fastcall Supervisor_AnmMgrReleaseVm_0044d620();
extern "C" void __fastcall Supervisor_AnmMgrDtorCall_0044b560(void *hwnd);
extern "C" void __fastcall Supervisor_SuspendMusic_00430290();
extern "C" void __fastcall Supervisor_ShutdownAudio_004312c0();
extern "C" void __fastcall Supervisor_FlushReplay_0044a302();
extern "C" void __fastcall Supervisor_TeardownFinal_00430060();
extern "C" i32  __fastcall Supervisor_DrainChain_0044c9c0();
extern "C" void __fastcall Supervisor_DeviceLostHandler_00433f20();
extern "C" void __fastcall Supervisor_DeviceNotResetHandler_004356a0();
extern "C" void *__fastcall operator_new_th07(u32 size);
extern "C" void __fastcall AnmManager_Ctor_0044d3e0(void *anmMgr);

// P1.3 rdata-string slots reached via absolute address by the boot loop's
// GameErrorLog/Fatal calls (orig .rdata addresses; content lives in orig).
// Declared as char* (pointer to the first char of each string).

// WINNLSEnableIME: undocumented user32 export (ordinal). Orig calls it via
// import stub. Declared here so the linker resolves it against user32.lib
// (wine provides the export; MSVC's user32.lib has it as a private symbol).
extern "C" BOOL WINAPI WINNLSEnableIME(HWND, BOOL);
// fallback no-op if user32.lib doesn't export it; defined only if the link
// fails (it doesn't under wine).
#ifdef TH07_NO_WINNLSEnableIME
BOOL WINAPI WINNLSEnableIME(HWND, BOOL) { return FALSE; }
#endif

// WinMain entry point for /subsystem:windows. Mirrors FUN_00434020.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    void *anmMgr;
    void *existingAnmMgr;
    i32 iVar1;
    i32 local_28;
    i32 local_8;
    i32 i;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    local_28 = 0;
    g_SupervisorHInstance_575950 = (void *)hInstance;
    *(void **)&th07::g_Supervisor = (void *)hInstance;

    // Capture SystemParametersInfo snapshots (SPI_GETSCREENSAVETIMEOUT=0x10,
    // SPI_GETKEYBOARDDELAY=0x16, SPI_GETKEYBOARDSPEED=0x0a... orig uses 0x53/0x54
    // which are SPI_GETSCREENSAVEACTIVE / SPI_GETKEYBOARDPREF variants).
    SystemParametersInfoA(0x10, 0, &g_SupervisorSysParam_575c40, 0);
    SystemParametersInfoA(0x53, 0, &g_SupervisorSysParam_575c44, 0);
    SystemParametersInfoA(0x54, 0, &g_SupervisorSysParam_575c48, 0);
    // Disable each (passing 0 with SPIF_SENDCHANGE=2).
    SystemParametersInfoA(0x11, 0, (PVOID)0, 2);
    SystemParametersInfoA(0x55, 0, (PVOID)0, 2);
    SystemParametersInfoA(0x56, 0, (PVOID)0, 2);

    iVar1 = Supervisor_CheckAlreadyRunning_00435bd0();
    if (iVar1 != -1)
    {
        iVar1 = Supervisor_LoadConfig_004398b6("th07.cfg");
        if (iVar1 == 0)
        {
            Supervisor_InitGameErrorCtx_00435ec0();
            QueryPerformanceFrequency((LARGE_INTEGER *)&g_SupervisorPerfFreq_575c34);
            goto init_phase;
        }
    }

    // Outer reboot loop.
    do
    {
        // ---- teardown phase ----
        Supervisor_AnmMgrReset_0044b830();
        existingAnmMgr = g_SupervisorAnmMgrSlot_4b9e44;
        if (existingAnmMgr == 0)
        {
            existingAnmMgr = 0;
        }
        else
        {
            Supervisor_AnmMgrReleaseVm_0044d620();
            free(existingAnmMgr);
        }
        g_SupervisorAnmMgrSlot_4b9e44 = 0;

        // Release the D3D device (Reset + Release).
        if (g_SupervisorD3dDevice_575958 != 0)
        {
            // (*dev)->Reset(presentParams) -- vtable +0x38.
            ((void (__stdcall *)(void *, void *))( *(void **)(*(u8 **)g_SupervisorD3dDevice_575958 + 0x38) ))(
                g_SupervisorD3dDevice_575958, (void *)&g_SupervisorPresentParams_575a30);
        }
        if (g_SupervisorD3dDevice_575958 != 0)
        {
            // (*dev)->Release() -- vtable +8.
            ((u32 (__stdcall *)(void *))( *(void **)(*(u8 **)g_SupervisorD3dDevice_575958 + 8) ))(
                g_SupervisorD3dDevice_575958);
            g_SupervisorD3dDevice_575958 = 0;
        }
        // Release the D3D8 iface.
        if (g_SupervisorD3D8_575954 != 0)
        {
            ((u32 (__stdcall *)(void *))( *(void **)(*(u8 **)g_SupervisorD3D8_575954 + 8) ))(
                g_SupervisorD3D8_575954);
            g_SupervisorD3D8_575954 = 0;
        }
        // Destroy the window.
        if (g_SupervisorWindow_575c20 != 0)
        {
            ShowWindow((HWND)g_SupervisorWindow_575c20, 0);
            MoveWindow((HWND)g_SupervisorWindow_575c20, 0, 0, 0, 0, 0);
            DestroyWindow((HWND)g_SupervisorWindow_575c20);
            g_SupervisorWindow_575c20 = 0;
        }
        ShowCursor(1);

        // If not a reboot request: restore system settings + exit.
        if (local_28 != 2)
        {
            Supervisor_FlushGameError_00431540(0x38);
            SystemParametersInfoA(0x11, g_SupervisorSysParam_575c40, (PVOID)0, 2);
            SystemParametersInfoA(0x55, g_SupervisorSysParam_575c44, (PVOID)0, 2);
            SystemParametersInfoA(0x56, g_SupervisorSysParam_575c48, (PVOID)0, 2);
            WINNLSEnableIME(0, 1);
            Supervisor_Teardown(&g_GameErrorContext_624210[0]);
            return 0;
        }

        // Reset the error context for the new session.
        g_GameErrorContextHead_626210 = &g_GameErrorContext_624210;
        g_GameErrorContext_624210[0] = 0;
        Supervisor_GameErrorLog_004315f0(&g_GameErrorContext_624210, "\215\304\213N\223\256\202\360\227v\202\267\202\351\203I\203v\203V\203\207\203\223\202\252\225\317\215X\202\263\202\352\202\275\202\314\202\305\215\304\213N\223\256\202\265\r\n");
        if (g_SupervisorIsForeground_575a8a == 0)
        {
            WINNLSEnableIME(0, 1);
        }
        // Drain any pending window messages before re-init.
        for (i = 0; i < 0x3c; i++)
        {
            if (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

    init_phase:
        // ---- init phase: Bootstrap -> CreateWindow -> InitD3D ----
        iVar1 = Supervisor_Bootstrap();
        if (iVar1 != 0)
        {
            continue; // retry teardown+init
        }
        iVar1 = Supervisor_CreateWindow((void *)hInstance);
        if (iVar1 != 0)
        {
            continue;
        }
        iVar1 = Supervisor_InitD3D();
        if (iVar1 != 0)
        {
            continue;
        }

        // ---- allocate AnmManager ----
        Supervisor_AnmMgrDtorCall_0044b560(g_SupervisorWindow_575c20);
        Supervisor_SuspendMusic_00430290();
        Supervisor_ShutdownAudio_004312c0();
        anmMgr = operator_new_th07(0x17e560);
        if (anmMgr == 0)
        {
            anmMgr = 0;
        }
        else
        {
            AnmManager_Ctor_0044d3e0(anmMgr);
        }
        g_SupervisorAnmMgrSlot_4b9e44 = anmMgr;
        th07::g_AnmManager = (th07::AnmManager *)anmMgr;

        // Open th07.dat before any packed resource load. The orig binary
        // opens the archive lazily inside FUN_0045fb50 (Pbg4Archive::Open,
        // called the first time FileSystem::OpenPath hits an archive entry);
        // we just do it eagerly here so the normal build has a single,
        // well-defined open point. A failed open is non-fatal -- disk-only
        // resources still load via the CreateFileA fallback in OpenPath.
#ifndef DIFFBUILD
        {
            void *dbg = th07_fopen_w("boot_debug.log", "w");
            if (dbg) th07_fprintf(dbg, "[main] opening th07.dat\n");
            i32 arcOk = th07::g_ArchiveEntries.archive.Open((char *)"th07.dat");
            if (dbg) th07_fprintf(dbg, "[main] archive.Open=%d entryCount=%u\n",
                                  arcOk, th07::g_ArchiveEntries.archive.entryCount);
            // Probe key assets the boot needs.
            u32 szA = th07::g_ArchiveEntries.archive.GetEntrySize((char *)"ascii.anm");
            u32 szT = th07::g_ArchiveEntries.archive.GetEntrySize((char *)"title01.anm");
            u32 szM = th07::g_ArchiveEntries.archive.GetEntrySize((char *)"bgm/init.mid");
            u32 szF = th07::g_ArchiveEntries.archive.GetEntrySize((char *)"bgm/thbgm.fmt");
            if (dbg)
            {
                th07_fprintf(dbg, "[main] ascii.anm=%u title01.anm=%u init.mid=%u thbgm.fmt=%u\n",
                             szA, szT, szM, szF);
                th07_fclose(dbg);
            }
        }
#endif

        // Normal-build demo: force MIDI music mode so bgm/*.mid files load
        // and play through MidiOutput. The orig game reads this from th07.cfg
        // (byte at cfg+0x1f); our cfg reader is a stub, so default to MIDI.
        // Two symbols alias the same byte in the orig binary (cfg+0x1f and
        // the standalone DAT_00575a87) but they are distinct in our normal
        // build, so set both.
#ifndef DIFFBUILD
        th07::g_Supervisor.cfg.musicMode = th07::MUSIC_MIDI;
        g_SupervisorMusicMode_575a87 = th07::MUSIC_MIDI;
#endif

        // Hide cursor + disable IME in windowed mode.
        if (g_SupervisorIsForeground_575a8a == 0)
        {
            WINNLSEnableIME(0, 0);
            ShowCursor(0);
        }

        // ---- register the chain + run the main message loop ----
        iVar1 = th07::Supervisor::RegisterChain();
        if (iVar1 == 0)
        {
            local_28 = 0;
            g_SupervisorBootVar_575c30 = 0xe2;
            while (g_SupervisorExitFlag_575c24 == 0)
            {
                if (!PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
                {
                    // TestCooperativeLevel -- vtable +0xc.
                    local_8 = ((i32 (__stdcall *)(void *))( *(void **)(*(u8 **)g_SupervisorD3dDevice_575958 + 0xc) ))(
                        g_SupervisorD3dDevice_575958);
                    if (local_8 == 0)
                    {
                        local_28 = Supervisor_RunSession(&th07::g_Supervisor);
                        if (local_28 != 0)
                        {
                            break;
                        }
                        th07::g_Supervisor.frameBasedStuffFlags =
                            th07::g_Supervisor.frameBasedStuffFlags & ~0x10u;
                    }
                    else if (local_8 == (i32)0x8876b869) /* D3DERR_DEVICEREMOVED or similar DX8 status */ // D3DERR_DEVICELOST
                    {
                        Supervisor_DeviceLostHandler_00433f20();
                        iVar1 = ((i32 (__stdcall *)(void *, void *))( *(void **)(*(u8 **)g_SupervisorD3dDevice_575958 + 0x38) ))(
                            g_SupervisorD3dDevice_575958, (void *)&g_SupervisorPresentParams_575a30);
                        if (iVar1 != 0)
                        {
                            break;
                        }
                        Supervisor_DeviceNotResetHandler_004356a0();
                        th07::g_Supervisor.wantedState2 = 3;
                        th07::g_Supervisor.frameBasedStuffFlags =
                            th07::g_Supervisor.frameBasedStuffFlags | 0x10u;
                    }
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }
        else if (iVar1 == -1)
        {
            local_28 = -1;
        }
        else
        {
            local_28 = 2;
        }

        // ---- post-session cleanup ----
        if (g_SupervisorReplayActive_62f4e0 != 0)
        {
            Supervisor_FlushReplay_0044a302();
        }
        Supervisor_TeardownFinal_00430060();
        do
        {
            iVar1 = Supervisor_DrainChain_0044c9c0();
        } while (iVar1 != 0);
    } while (1);

    return 0;
}

