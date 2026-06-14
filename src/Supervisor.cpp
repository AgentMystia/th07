// Supervisor.cpp - th07 Supervisor 模块
//
// 反编译自 th07.exe（ghidra 实读）。函数地址：
//   Supervisor::RegisterChain         FUN_00439000
//   Supervisor::OnUpdate              FUN_00437c70
//   Supervisor::OnDraw                FUN_0043831b
//   Supervisor::AddedCallback         FUN_00438986
//   Supervisor::DeletedCallback       FUN_00438de2
//   Supervisor::DrawFpsCounter        FUN_004390a5
//   Supervisor::TickTimer             FUN_0043958d
//   Supervisor::SetupDInput           FUN_004383d8
//   Supervisor::LoadConfig            FUN_004398b6
//   Supervisor::PlayAudio             FUN_00439dd0
//   Supervisor::StopAudio             FUN_00439ec1
//   Supervisor::PlayMidiFile          FUN_00439f4d
//   Supervisor::SetupMidiPlayback     (inline，并入调用点)
//   Supervisor::FadeOutMusic          FUN_0043a0d6
//   Supervisor::ReadMidiFile          FUN_0043a05f (按 musicMode==MIDI 处理)
//
// th07 vs th06 的重大差异（详见 Supervisor.hpp 头注释）。

#include "Supervisor.hpp"
#include "Chain.hpp"
#include "GameErrorContext.hpp"
#include "utils.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace th07
{
// === 全局静态 ===
DIFFABLE_STATIC(Supervisor, g_Supervisor)
DIFFABLE_STATIC(ControllerMapping, g_ControllerMapping)
DIFFABLE_STATIC(u16, g_LastFrameInput)
DIFFABLE_STATIC(u16, g_CurFrameInput)
DIFFABLE_STATIC(u16, g_IsEigthFrameOfHeldInput)
DIFFABLE_STATIC(u16, g_NumOfFramesInputsWereHeld)

// th07 归档对象（单实例，非 th06 的 16 个槽位数组）
DIFFABLE_STATIC(void *, g_Pbg4Archive)
DIFFABLE_STATIC(char *, g_Pbg4ArchiveName)

// 外部依赖（其它模块实现，此处只声明链接）
extern "C" void *g_AnmManager;          // DAT_004b9e44
extern void *g_TextBufferSurface;
struct Chain extern g_Chain;

// 这些常量字符串在原二进制里位于 .rdata，由对应函数引用。
extern const char TH_ERR_NO_WAVE_FILE[];       // "wave データが無いので、midi "
extern const char TH_ERR_CONFIG_CORRUPTED[];   // コンフィグデータ破損
extern const char TH_TH07_VER_FMT[];           // "th07_%.4x%c.ver"
extern const char TH_SCENE_FMT[];              // "scene %d -> %d\r\n"
extern const char TH_FPS_FMT[];                // "%.02ffps"
extern const char TH_ARCFILE_OPEN_FMT[];       // "info : %s open arcfile\r\n"
extern const char TH_ARCFILE_NOT_FOUND[];      // "info : %s not found\r\n"
extern const char TH_TH07_DAT[];               // "th07.dat"
extern const char TH_BGM_INIT_MID[];           // "bgm/init.mid"
extern const char TH_DATA_TITLE_LOGO[];        // "data/title/th07logo.jpg"
extern const char TH_DATA_TEXT_ANM[];          // "data/text.anm"
extern const char TH_BGM_THBGM_FMT[];          // "bgm/thbgm.fmt"
extern const char TH_THBGM_DAT[];              // "./thbgm.dat"

// ZUN 的浮点常量（实读自 .rdata）
#define FRAMERATE_THRESHOLD_099 0.99f
#define FRAMERATE_THRESHOLD_10 1.0f
#define FRAMERATE_THRESHOLD_05 0.5f

#pragma optimize("s", on)

// =====================================================================
// Supervisor::TickTimer  (FUN_0043958d)
// __thiscall, 2 个栈上参数 (frames*, subframes*)
// 与 th06 的差异：subframes 累加用的是 framerateMultiplier 而非 effective。
// =====================================================================
void Supervisor::TickTimer(i32 *frames, f32 *subframes)
{
    if (this->framerateMultiplier <= FRAMERATE_THRESHOLD_099)
    {
        *subframes = *subframes + this->framerateMultiplier;
        if (FRAMERATE_THRESHOLD_10 <= *subframes)
        {
            *frames = *frames + 1;
            *subframes = *subframes - FRAMERATE_THRESHOLD_10;
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
// __fastcall, ECX = Supervisor*
// th07 的状态机与 th06 大不相同（状态码、跳转结构全改）。
// 这里按 ghidra 反编译结构精确还原（goto 用于匹配 ZUN 的跳转表布局）。
// =====================================================================
ChainCallbackResult Supervisor::OnUpdate(Supervisor *s)
{
    // AnmManager 状态复位（th07 把 AnmManager 一组 current* 字段重置）
    // 在原二进制里这些是 *(byte*)(g_AnmManager + 0x2e4d2) = 0xff 等一串写入，
    // 属于 AnmManager 的 per-frame 重置，不是 Supervisor 自身字段。

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
    DebugPrint(TH_SCENE_FMT, s->wantedState, s->curState);

    // th07 状态分发：以 wantedState 为外层 switch。
    // 注意 th07 把 wantedState/curState 的角色与 th06 颠倒了（wantedState 是
    // 当前所在状态，curState 是目标状态），这里按二进制原样保留。
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
                    D3DDevice_DiscardBytes();
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
                D3DDevice_DiscardBytes();
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
                Chain_ReleaseAll();
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            if (cur == SUPERVISOR_STATE_MAINMENU)
            {
                s->curState = 0;
                Chain_ReleaseAll();
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
    // 每 4000 帧做一次 autosave（th07 新增）
    if (s->calcCount % 4000 == 3999)
    {
        if (AutosaveScore() != 0)
        {
            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::OnDraw  (FUN_0043831b)
// __fastcall, ECX = Supervisor*
// th07 OnDraw 直接调 DrawFpsCounter（不像 th06 还要重置 AnmManager 一组字段，
// 那组重置在 th07 移到了 OnUpdate 里）。
// =====================================================================
ChainCallbackResult Supervisor::OnDraw(Supervisor *s)
{
    DrawFpsCounter(0);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::DrawFpsCounter  (FUN_004390a5)
// __fastcall, ECX 不用；1 个栈参数 param (i32 drawArg)
// th07 大改：加了 QueryPerformanceCounter 双路径（vsyncEnabled 与否），加了
// slow-frame 计数器。static 局部放在 .data 段 0x0135e1f0 区域。
// =====================================================================
void Supervisor::DrawFpsCounter(i32 drawArg)
{
    static u32 g_NumFramesSinceLastTime = 0; // DAT_0135e1f0
    static DWORD g_LastTime = 0;             // DAT_0135e2a0 / DAT_0135e298
    static char g_FpsCounterBuffer[256];     // DAT_0135e0f0
    static char g_SlowBuffer[256];           // DAT_0135dff0

    if (g_NoFpsCounter != 0) // DAT_0062627d（replay 模式时置 1）
    {
        return;
    }

    g_NumFramesSinceLastTime = g_NumFramesSinceLastTime + 1 + (u32)g_Supervisor.cfg.frameskipConfig;

    if (g_Supervisor.vsyncDisabled == 0) // DAT_00575bbc：timeGetTime 路径
    {
        DWORD curTime = timeGetTime();
        if (curTime - g_LastTime >= 500)
        {
            f32 elapsed = (f32)(curTime - g_LastTime) / 1000.0f;
            f32 fps = (f32)g_NumFramesSinceLastTime / elapsed;
            g_LastTime = curTime;
            g_NumFramesSinceLastTime = 0;
            sprintf(g_FpsCounterBuffer, TH_FPS_FMT, (double)fps);

            if (drawArg != 0) // 仅游戏中统计 slow%
            {
                DrawFpsCounter_AccumSlow(fps);
            }
        }
    }
    else // QueryPerformanceCounter 路径
    {
        LARGE_INTEGER cur;
        QueryPerformanceCounter(&cur);
        // ... th07 在此路径里维护 perf-counter 基线与帧计数；为保 objdiff
        // 需要在后续完整反编译时补齐。当前先保留主路径骨架。
    }

    if (g_Supervisor.unkIsInEnding == 0 && drawArg != 0)
    {
        D3DXVECTOR3 pos;
        pos.x = 512.0f;
        pos.y = 464.0f;
        pos.z = 0.0f;
        AsciiManager_AddString(&pos, g_FpsCounterBuffer);

        if ((g_Supervisor.cfg.opts >> 3 & 1) != 0 && (g_Supervisor.cfg.opts >> 2 & 1) != 0)
        {
            D3DXVECTOR3 pos2;
            pos2.x = 640.0f;
            pos2.y = 608.0f;
            pos2.z = 0.0f;
            AsciiManager_AddString(&pos2, g_SlowBuffer);
        }
    }
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::RegisterChain  (FUN_00439000)
// __fastcall 无参；初始化 supervisor 状态 + 注册 calc/draw 链。
// =====================================================================
ZunResult Supervisor::RegisterChain()
{
    Supervisor *supervisor = &g_Supervisor;

    supervisor->wantedState = 0; // DAT_00575aa4
    supervisor->curState = -1;   // DAT_00575aa8
    supervisor->calcCount = 0;   // DAT_00575aa0

    ChainElem *chain = Chain_CreateElem((ChainCallback)Supervisor::OnUpdate);
    chain->arg = supervisor;
    chain->addedCallback = (ChainAddedCallback)Supervisor::AddedCallback;
    chain->deletedCallback = (ChainDeletedCallback)Supervisor::DeletedCallback;
    if (Chain_AddToCalcChain(chain, TH_CHAIN_PRIO_CALC_SUPERVISOR) != 0)
    {
        return ZUN_ERROR;
    }

    chain = Chain_CreateElem((ChainCallback)Supervisor::OnDraw);
    chain->arg = supervisor;
    Chain_AddToDrawChain(chain, TH_CHAIN_PRIO_DRAW_SUPERVISOR);

    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::SetupDInput  (FUN_004383d8)
// __fastcall, ECX = Supervisor*
// 与 th06 几乎一致：DirectInput8Create + 创建 keyboard/controller + EnumObjects
// 设轴范围。差别：错误字符串 + 调用约定（th07 全 __fastcall）。
// =====================================================================
ZunResult Supervisor::SetupDInput(Supervisor *supervisor)
{
    HINSTANCE hInst = (HINSTANCE)GetWindowLongA((HWND)supervisor->hwndGameWindow, GWL_HINSTANCE);

    if ((supervisor->cfg.opts >> GCOS_NO_DIRECTINPUT_PAD & 1) != 0)
    {
        return ZUN_ERROR;
    }

    if (DirectInput8Create(hInst, 0x800, IID_IDirectInput8A, (LPVOID *)&supervisor->dinputIface,
                           NULL) < 0)
    {
        supervisor->dinputIface = NULL;
        g_GameErrorContext.Log(TH_ERR_DIRECTINPUT_NOT_AVAILABLE);
        return ZUN_ERROR;
    }

    IDirectInput8A *di = (IDirectInput8A *)supervisor->dinputIface;
    if (di->CreateDevice(GUID_SysKeyboard, (IDirectInputDevice8A **)&supervisor->keyboard, NULL) < 0)
    {
        if (supervisor->dinputIface)
        {
            di->Release();
            supervisor->dinputIface = NULL;
        }
        g_GameErrorContext.Log(TH_ERR_DIRECTINPUT_NOT_AVAILABLE);
        return ZUN_ERROR;
    }

    IDirectInputDevice8A *kb = (IDirectInputDevice8A *)supervisor->keyboard;
    if (kb->SetDataFormat(&c_dfDIKeyboard) < 0)
    {
        if (supervisor->keyboard)
        {
            kb->Release();
            supervisor->keyboard = NULL;
        }
        if (supervisor->dinputIface)
        {
            di->Release();
            supervisor->dinputIface = NULL;
        }
        g_GameErrorContext.Log(TH_ERR_DIRECTINPUT_SETDATAFORMAT_NOT_AVAILABLE);
        return ZUN_ERROR;
    }

    if (kb->SetCooperativeLevel((HWND)supervisor->hwndGameWindow,
                                DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY) < 0)
    {
        if (supervisor->keyboard)
        {
            kb->Release();
            supervisor->keyboard = NULL;
        }
        if (supervisor->dinputIface)
        {
            di->Release();
            supervisor->dinputIface = NULL;
        }
        g_GameErrorContext.Log(TH_ERR_DIRECTINPUT_SETCOOPERATIVELEVEL_NOT_AVAILABLE);
        return ZUN_ERROR;
    }

    kb->Acquire();
    g_GameErrorContext.Log(TH_ERR_DIRECTINPUT_INITIALIZED);

    di->EnumDevices(DI8DEVCLASS_GAMECTRL, Supervisor_EnumGameControllersCb, NULL, DIEDFL_ATTACHEDONLY);
    if (supervisor->controller)
    {
        IDirectInputDevice8A *ctl = (IDirectInputDevice8A *)supervisor->controller;
        ctl->SetDataFormat(&c_dfDIJoystick2);
        ctl->SetCooperativeLevel((HWND)supervisor->hwndGameWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND);

        supervisor->controllerCaps.dwSize = 0x2c;
        ctl->GetCapabilities((DIDEVCAPS *)&supervisor->controllerCaps);
        ctl->EnumObjects(Supervisor_ControllerCallback, NULL, DIDFT_ALL);

        g_GameErrorContext.Log(TH_ERR_PAD_FOUND);
    }
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::TickTimer 等小工具（占位让链接通过；真实实现在上方）
// =====================================================================
// LoadPbg4 / ReleasePbg4 在 th07 里形态完全不同（全局单归档对象），由 AddedCallback
// 与 DeletedCallback 直接内联调用，不再像 th06 那样作为独立成员函数存在。

// =====================================================================
// Supervisor::AddedCallback  (FUN_00438986)
// __fastcall, ECX = Supervisor*
// th07 启动流程：QueryPerformanceFrequency 存到 supervisor+0x26c → Present logo
// 两次 → LoadPbg4(th07.dat) → LoadSurface(th07logo) → fps 校准 → SetupDInput →
// new MidiOutput → LoadAnm(text) → AsciiManager::RegisterChain → 加载 thbgm.fmt →
// SoundPlayer::LoadFormatFile → new 改名为 g_Pbg4Controller(0x14 字节) → 完。
// =====================================================================
ZunResult Supervisor::AddedCallback(Supervisor *s)
{
    QueryPerformanceFrequency((LARGE_INTEGER *)((u8 *)s + 0x26c));

    // logo 双缓冲 Present（th07 把 th06 的 CopySurfaceToBackBuffer 换成
    // d3dDevice 直接 BeginScene/CopyRects/EndScene/Present）。
    D3DDevice_BeginScene();
    D3DDevice_ClearColor(0, 0, 1, 0xff000000, 1.0f, 0);
    D3DDevice_EndScene();
    if (D3DDevice_Present(0, 0, 0, 0) < 0)
    {
        D3DDevice_Reset(&s->presentParameters);
    }

    D3DDevice_BeginScene();
    D3DDevice_ClearColor(0, 0, 1, 0xff000000, 1.0f, 0);
    D3DDevice_EndScene();
    if (D3DDevice_Present(0, 0, 0, 0) < 0)
    {
        D3DDevice_Reset(&s->presentParameters);
    }

    // LoadPbg4(th07.dat)
    if (LoadPbg4(TH_TH07_DAT) == 0)
    {
        return ZUN_ERROR;
    }

    AnmManager_LoadSurface(0, TH_DATA_TITLE_LOGO);
    s->unkIsInEnding = 1; // DAT_00575ab8 = 1（logo 阶段标志）

    if (s->vsyncDisabled == 0) // DAT_00575abc
    {
        if (CalibrateFramerate() != 0)
        {
            AnmManager_ReleaseSurface(0);
            return (ZunResult)0xfffffffe;
        }
    }
    else
    {
        // vsync 路径：循环 Present 几次预热
        for (i32 i = 0; i < 4; i++)
        {
            D3DDevice_BeginScene();
            AnmManager_CopySurface(0, 0, 0, 0, 0);
            D3DDevice_EndScene();
            if (D3DDevice_Present(0, 0, 0, 0) < 0)
            {
                D3DDevice_Reset(&s->presentParameters);
            }
        }
    }

    AnmManager_ReleaseSurface(0);
    s->unk198 = 0;            // +0x168
    s->unkIsInEnding = 0;     // +0x164（th07 这里清的是 0x164，对应 logo 标志复位）
    s->startupTimeBeforeMenuMusic = timeGetTime(); // +0x190

    // 把 startupTime 低 16 位塞进 rng seed（DAT_0049fe20 = 全局种子）
    g_RngSeed = (u16)(s->startupTimeBeforeMenuMusic & 0xffff);

    Supervisor::SetupDInput(s);

    if (s->midiOutput == NULL)
    {
        s->midiOutput = new MidiOutput(); // operator new(0x300) + 构造
    }
    if (s->midiOutput != NULL)
    {
        MidiOutput_LoadPlay(s->midiOutput, 0x1e, TH_BGM_INIT_MID);
    }

    SoundPlayer_InitSoundBuffers();
    if (AnmManager_LoadAnm(0, TH_DATA_TEXT_ANM, 0x700) != 0)
    {
        return ZUN_ERROR;
    }

    if (AsciiManager_RegisterChain() != 0)
    {
        g_GameErrorContext.Log(TH_ERR_ASCIIMANAGER_INIT_FAILED);
        return ZUN_ERROR;
    }

    AnmManager_SetupVertexBuffer();
    TextHelper_CreateTextBuffer();

    if (SoundPlayer_LoadFormatFile(TH_BGM_THBGM_FMT) != 0)
    {
        g_GameErrorContext.Fatal(TH_ERR_THBGM_FMT_LOAD_FAILED);
        return ZUN_ERROR;
    }

    // 注册文件系统回调（根据是否 demo 切 th07.dat / th07.dat 副本）
    FileSystem_SetArchiveName();
    FileSystem_RegisterCallbacks();

    // 性能统计结构体初始化（DAT_0062f4e0 = "PLST" 头）
    PerfStats_Init();

    // 创建 SoundPlayer 子对象（0x14 字节，vtable @ 0x496c0c）
    g_SoundStreamController = SoundStream_New();
    if (g_SoundStreamController != NULL)
    {
        SoundStream_Init(g_SoundStreamController);
    }

    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::DeletedCallback  (FUN_00438de2)
// __fastcall, ECX = Supervisor*
// th07 释放：释放归档 → AsciiManager::CutChain → SoundPlayer stop → 释放 midiOutput
// → keyboard/controller/dinputIface Release → 释放 SoundStream 子对象。
// =====================================================================
ZunResult Supervisor::DeletedCallback(Supervisor *s)
{
    ReleasePbg4();
    AnmManager_ReleaseVertexBuffer();
    AsciiManager_CutChain();
    SoundPlayer_StopBGM();

    if (s->midiOutput != NULL)
    {
        MidiOutput_StopPlayback(s->midiOutput);
        delete s->midiOutput;
        s->midiOutput = NULL;
    }

    ReplayManager_SaveReplay(NULL, NULL);
    TextHelper_ReleaseTextBuffer();

    if (s->keyboard != NULL)
    {
        ((IDirectInputDevice8A *)s->keyboard)->Unacquire();
        ((IDirectInputDevice8A *)s->keyboard)->Release();
        s->keyboard = NULL;
    }
    if (s->controller != NULL)
    {
        ((IDirectInputDevice8A *)s->controller)->Unacquire();
        ((IDirectInputDevice8A *)s->controller)->Release();
        s->controller = NULL;
    }
    if (s->dinputIface != NULL)
    {
        ((IDirectInput8A *)s->dinputIface)->Release();
        s->dinputIface = NULL;
    }

    if (g_SoundStreamController != NULL)
    {
        SoundStream_Destroy(g_SoundStreamController);
        free(g_SoundStreamController);
        g_SoundStreamController = NULL;
    }

    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::LoadConfig  (FUN_004398b6)
// __thiscall, ECX = Supervisor*, 栈参 = path (char*)
// th07 用 Win32 CreateFileA/ReadFile 读 th07.cfg，文件头校验变成
// "ZAV\x56" magic (0x5641575a) + version=1 + 0x700；不再像 th06 校验 LastFileSize。
// =====================================================================
ZunResult Supervisor::LoadConfig(char *path)
{
    GameConfiguration *data;
    memset(&g_Supervisor.cfg, 0, sizeof(GameConfiguration));

    data = (GameConfiguration *)FileSystem_OpenPath(path, 1);
    if (data == NULL)
    {
        g_GameErrorContext.Log(TH_ERR_CONFIG_NOT_FOUND);
    }
    else
    {
        g_Supervisor.cfg = *data;

        // 校验 thbgm.dat 文件头（th07 特有）
        HANDLE h = CreateFileA(TH_THBGM_DAT, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0x8000080, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            u32 magic, ver, hdr3;
            DWORD read;
            ReadFile(h, &magic, 0x10, &read, NULL);
            CloseHandle(h);
            if (magic != 0x5641575a || ver != 1 || hdr3 != 0x700)
            {
                g_GameErrorContext.Fatal(TH_ERR_THBGM_DAT_CORRUPT);
                return ZUN_ERROR;
            }
        }

        // 字段范围校验（th07 版本号 0x70002，thbgm 文件大小 0x38）
        if (g_Supervisor.cfg.lifeCount >= 5 || g_Supervisor.cfg.bombCount >= 4 ||
            g_Supervisor.cfg.colorMode16bit >= 2 || g_Supervisor.cfg.musicMode >= 3 ||
            g_Supervisor.cfg.defaultDifficulty >= 6 || g_Supervisor.cfg.playSounds >= 2 ||
            g_Supervisor.cfg.windowed >= 2 || g_Supervisor.cfg.frameskipConfig >= 3 ||
            g_Supervisor.cfg.unk24 >= 3 || g_Supervisor.cfg.unk25 >= 2 ||
            g_Supervisor.cfg.unk26 >= 2 || g_Supervisor.cfg.version != GAME_VERSION ||
            g_LastFileSize != 0x38)
        {
            goto corrupted;
        }
        goto valid;
    }

corrupted:
    g_GameErrorContext.Log(TH_ERR_CONFIG_CORRUPTED);
    g_Supervisor.cfg.lifeCount = 2;
    g_Supervisor.cfg.bombCount = 3;
    g_Supervisor.cfg.colorMode16bit = 0xff;
    g_Supervisor.cfg.version = GAME_VERSION;
    g_Supervisor.cfg.padXAxis = 600;
    g_Supervisor.cfg.padYAxis = 600;

    // th07 用 thbgm.dat 文件存在性决定 musicMode（th06 是 thXX_01.wav）
    {
        HANDLE h2 = CreateFileA(TH_THBGM_DAT, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, 0x8000080, NULL);
        if (h2 == INVALID_HANDLE_VALUE)
        {
            g_Supervisor.cfg.musicMode = MUSIC_MIDI;
            utils::DebugPrint(TH_ERR_NO_WAVE_FILE);
        }
        else
        {
            u32 m, v, h3;
            DWORD r;
            ReadFile(h2, &m, 0x10, &r, NULL);
            CloseHandle(h2);
            if (m != 0x5641575a || v != 1 || h3 != 0x700)
            {
                g_GameErrorContext.Fatal(TH_ERR_THBGM_DAT_CORRUPT);
                return ZUN_ERROR;
            }
            g_Supervisor.cfg.musicMode = MUSIC_WAV;
        }
    }
    g_Supervisor.cfg.playSounds = 1;
    g_Supervisor.cfg.defaultDifficulty = 1;
    g_Supervisor.cfg.windowed = 0;
    g_Supervisor.cfg.frameskipConfig = 0;
    g_Supervisor.cfg.controllerMapping = g_ControllerMapping;
    g_Supervisor.cfg.unk24 = 2;
    g_Supervisor.cfg.unk25 = 0;
    g_Supervisor.cfg.unk26 = 1;

valid:
    g_Supervisor.cfg.opts |= (1 << GCOS_USE_D3D_HW_TEXTURE_BLENDING);
    g_ControllerMapping = g_Supervisor.cfg.controllerMapping;
    free(data);

    // 各 opt 位的日志（与 th06 对应位一致，错误字符串略有不同）
    if ((g_Supervisor.cfg.opts >> GCOS_FORCE_16BIT_COLOR_MODE & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_USE_16BIT_TEXTURES);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_NO_FOG & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_NO_FOG);
    }
    if (((g_Supervisor.cfg.opts >> GCOS_CLEAR_BACKBUFFER_ON_REFRESH & 1) != 0) ||
        ((g_Supervisor.cfg.opts >> GCOS_DISPLAY_MINIMUM_GRAPHICS & 1) != 0))
    {
        g_GameErrorContext.Log(TH_ERR_FORCE_BACKBUFFER_CLEAR);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_DISPLAY_MINIMUM_GRAPHICS & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_DONT_RENDER_ITEMS);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_SUPPRESS_USE_OF_GOROUD_SHADING & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_NO_GOURAUD_SHADING);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_TURN_OFF_DEPTH_TEST & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_NO_DEPTH_TESTING);
    }
    g_Supervisor.vsyncDisabled = 0;
    if ((g_Supervisor.cfg.opts >> GCOS_FORCE_60FPS & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_FORCE_60FPS_MODE);
    }
    if (g_Supervisor.cfg.colorMode16bit != 0)
    {
        g_GameErrorContext.Log(TH_ERR_USE_16BIT_TEXTURES);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_LAUNCH_WINDOWED & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_LAUNCH_WINDOWED);
    }
    if ((g_Supervisor.cfg.opts >> GCOS_FORCE_REFERENCE_RASTERIZER & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_FORCE_REFERENCE_RASTERIZER);
        g_Supervisor.vsyncDisabled = 1;
    }
    if ((g_Supervisor.cfg.opts >> GCOS_NO_DIRECTINPUT_PAD & 1) != 0)
    {
        g_GameErrorContext.Log(TH_ERR_DO_NOT_USE_DIRECTINPUT);
    }

    if (FileSystem_WriteDataToFile(path, &g_Supervisor.cfg, sizeof(GameConfiguration)) != 0)
    {
        g_GameErrorContext.Fatal(TH_ERR_FILE_CANNOT_BE_EXPORTED, path);
        g_GameErrorContext.Fatal(TH_ERR_FOLDER_HAS_WRITE_PROTECT_OR_DISK_FULL);
        return ZUN_ERROR;
    }

    return ZUN_SUCCESS;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// =====================================================================
// Supervisor::ReadMidiFile / PlayMidiFile / PlayAudio / StopAudio /
// SetupMidiPlayback / FadeOutMusic
//
// th07 把这组音频函数重新编号，musicMode 的判定逻辑保留（OFF/WAV/MIDI），
// 但 WAV 分支通过 SoundPlayer 的"流式通道"播放（FUN_0044d2f0(channel,...)）。
// =====================================================================
ZunBool Supervisor::ReadMidiFile(u32 midiFileIdx, char *path)
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput_StopPlayback(g_Supervisor.midiOutput);
        }
        return FALSE;
    }
    return TRUE;
}

i32 Supervisor::PlayMidiFile(i32 midiFileIdx)
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput_StopPlayback(g_Supervisor.midiOutput);
            MidiOutput_LoadFile(g_Supervisor.midiOutput, (char *)(usize)midiFileIdx);
            MidiOutput_Play(g_Supervisor.midiOutput);
        }
        return FALSE;
    }
    return TRUE;
}

ZunResult Supervisor::PlayAudio(i32 channel, char *path)
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput_PlayTrack(g_Supervisor.midiOutput, channel, path);
        }
        return ZUN_SUCCESS;
    }
    if (g_Supervisor.cfg.musicMode == MUSIC_WAV)
    {
        char wavName[260];
        strcpy(wavName, path);
        char *ext = strrchr(wavName, '.');
        ext[1] = 'w';
        ext[2] = 'a';
        ext[3] = 'v';
        SoundPlayer_LoadStream(channel, wavName);
        return ZUN_SUCCESS;
    }
    return ZUN_ERROR;
}

ZunResult Supervisor::StopAudio(i32 channel)
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput_StopPlayback(g_Supervisor.midiOutput);
        }
        return ZUN_SUCCESS;
    }
    if (g_Supervisor.cfg.musicMode == MUSIC_WAV)
    {
        if ((g_Supervisor.cfg.opts >> GCOS_FORCE_REFERENCE_RASTERIZER & 1) == 0)
        {
            SoundPlayer_StopStream(3, 0, "dummy");
        }
        else
        {
            SoundPlayer_StopStream(4, 0, "dummy");
        }
        return ZUN_SUCCESS;
    }
    return ZUN_ERROR;
}

ZunResult Supervisor::SetupMidiPlayback()
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI || g_Supervisor.cfg.musicMode == MUSIC_WAV)
    {
        return ZUN_SUCCESS;
    }
    return ZUN_ERROR;
}

ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput_SetFadeOut(g_Supervisor.midiOutput, 1000.0f * fadeOutSeconds);
        }
        return ZUN_SUCCESS;
    }
    if (g_Supervisor.cfg.musicMode != MUSIC_WAV)
    {
        return ZUN_ERROR;
    }
    // th07 简化的 framerate 判定（实读自 0x0043a0d6 反汇编）：
    //   framerateMultiplier == 0.5f            -> 原值
    //   framerateMultiplier <= 1.0f && != 0.5  -> 原值
    //   else (>1.0f 或其它)                    -> fadeOutSeconds / framerateMultiplier
    if (this->framerateMultiplier != FRAMERATE_THRESHOLD_05 &&
        this->framerateMultiplier > FRAMERATE_THRESHOLD_10)
    {
        fadeOutSeconds = fadeOutSeconds / this->framerateMultiplier;
    }
    SoundPlayer_FadeOut(fadeOutSeconds);
    return ZUN_SUCCESS;
}

#pragma optimize("s", off)

}; // namespace th07
