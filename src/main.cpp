// WinMain / boot entry for th07 (Perfect Cherry Blossom).
//
// The orig game's true entry point is FUN_00434020 (called from the MSVC CRT
// startup stub at 0x0047ea7d). FUN_00434020 is a large boot function that:
//   1. Saves hInstance into g_Supervisor.hInstance (DAT_00575950).
//   2. Captures SystemParametersInfo (screensaver/keyboard/keyboard-cursor
//      settings) so they can be restored on exit.
//   3. Calls CheckForRunningGameInstance (FUN_00435bd0) and LoadConfig
//      (FUN_004398b6 "th07.cfg").
//   4. Enters a do/while "session" loop that, per iteration:
//      - Calls the window-creation + D3D-device-init + display-mode-setup
//        trio (FUN_00434a40 / FUN_00434a80 / FUN_00434bd0).
//      - Allocates a 0x17e560-byte AnmManager (operator new + ctor
//        FUN_0044d3e0), stores it into DAT_004b9e44.
//      - Calls Supervisor::RegisterChain (FUN_00439000).
//      - Runs the main message loop (PeekMessage + TestCooperativeLevel +
//        Supervisor::OnUpdate via the chain + Present).
//      - On exit, tears down AnmManager/D3D/window and either reboots the
//        session (local_28 == 2) or returns.
//
// This file currently provides a MINIMAL skeleton that lets the project link
// to a `normal`-build th07e.exe. The body is intentionally a stub: it calls
// the window/D3D init helpers and Supervisor::RegisterChain but does NOT yet
// reproduce the orig's exact teardown/reboot loop. Full reverse-engineering
// of FUN_00434020 is tracked as a separate task.

#include "Supervisor.hpp"
#include "Chain.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <windows.h>

namespace th07
{
// Boot-helper externs (orig VAs verified from disasm of FUN_00434020). These
// are not yet reversed as C++ classes; the linker resolves them to stubs in
// stubs.cpp until their owning modules are implemented.
extern "C" i32 __fastcall Supervisor_Bootstrap();         // FUN_00434a40 (initial setup)
extern "C" i32 __fastcall Supervisor_CreateWindow();      // FUN_00434a80 (window + D3D)
extern "C" i32 __fastcall Supervisor_InitD3D();           // FUN_00434bd0 (device init)
extern "C" i32 __fastcall Supervisor_RunSession();        // FUN_004346e0 (per-frame run)
extern "C" void __fastcall Supervisor_Teardown();         // FUN_00433e90 (cleanup)
}

// WinMain  entry point for /subsystem:windows. The MSVC CRT startup code
// parses the command line and calls here with the standard 4 args.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Stamp hInstance into the Supervisor singleton (orig DAT_00575950 = this).
    *(void **)&th07::g_Supervisor = (void *)hInstance;

    // TODO: lift the full boot loop from FUN_00434020. For now this skeleton
    // calls the documented init sequence so the exe links and reaches
    // Supervisor::RegisterChain; deeper correctness is pending RE.
    if (th07::Supervisor_Bootstrap() != 0)
    {
        return 1;
    }
    if (th07::Supervisor_CreateWindow() != 0)
    {
        return 1;
    }
    if (th07::Supervisor_InitD3D() != 0)
    {
        return 1;
    }

    // Register the supervisor chain; this is what drives OnUpdate/OnDraw and
    // triggers AddedCallback (the boot-time D3D+content init we lifted).
    if (th07::Supervisor::RegisterChain() != 0)
    {
        return 1;
    }

    // Minimal Windows message pump until the supervisor signals exit. The
    // orig loop is much richer (TestCooperativeLevel, device-loss recovery,
    // chain driving via g_Chain.RunCalcChain/RunDrawChain, Present); those
    // land when FUN_00434020 is fully reversed.
    while (th07::g_Supervisor.curState != -2)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (th07::Supervisor_RunSession() != 0)
        {
            break;
        }
    }

    th07::Supervisor_Teardown();
    return 0;
}
