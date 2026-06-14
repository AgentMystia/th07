// th07::GameManager — in-game state manager (largest module).
// See GameManager.hpp for the full address map and struct notes.
//
// Implemented here (first pass): RegisterChain, CutChain, OnDraw.
// TODO (later passes): OnUpdate, AddedCallback, DeletedCallback, OnItemUpdate,
// CalculateChecksum, IsGameActive — these need more of the struct reversed.

#include "GameManager.hpp"
#include <cstring>

namespace th07
{
DIFFABLE_STATIC(GameManager, g_GameManager)

// ---- cross-module callees reached by OnUpdate/DeletedCallback/AddedCallback ----
// Fixed addresses in the original; signatures derived from each call site's
// disassembly. These gain real definitions when their owning modules land
// (Supervisor/Controller/SoundPlayer). float args travel in ST0 (th07 idiom).
extern i32 __fastcall RandFloatToInt_0048b8a0(f32 randFloat);        // float in ST0; returns i64, caller takes EAX
extern i32 __fastcall IsGameActive_0042ad66(void *activeGame);       // ECX = 0x49fbf0
extern i32 __fastcall CalculateChecksum_0042d7be(GameManager *gm);   // ECX = gameManager
extern void __fastcall PauseSound_0044d2f0(void *sndCtx, i32 a, i32 b, const char *name); // ECX=0x4ba0d8
extern void __fastcall SoundCmd_0044c930(void *sndCtx, i32 a, i32 b);                      // ECX=0x4ba0d8
extern void __fastcall SupUpdateTimeAccumB_0043a3f4(void *supervisor);                      // ECX=0x575950
extern void __fastcall FadeOutMusic_0043a0d6(void *supervisor, f32 seconds);                // ECX=0x575950
extern void __fastcall SoundCmd_0044b310(i32 a, i32 b, i32 c, i32 d, i32 e);                // ECX=2,EDX=0x78

// IDirect3DDevice8 stub — only the Clear slot (vtable offset 0x90) is used here.
struct D3DDeviceStub;
struct D3DDeviceStubVtbl
{
    void *methods[0x90 / 4];
    void (__stdcall *Clear)(D3DDeviceStub *pDevice, u32 count, const void *pRects, u32 flags,
                            u32 color, f32 z, u32 stencil);
};
struct D3DDeviceStub
{
    D3DDeviceStubVtbl *lpVtbl;
};
// 0x00575958 holds the IDirect3DDevice8* (= g_Supervisor.d3dDevice). Reached via
// raw cast so OnUpdate needs no cross-module symbol dependency.

// ---- DeletedCallback callees ----
extern void __fastcall SupOnDelete_0043a05f(void *sup);           // ECX=0x575950
extern void __fastcall MidiCloseAll_436b30(void *dev);           // ECX=midi dev
extern void __fastcall MidiPlay_436ad0(void *dev);               // ECX=midi dev
extern i32 __fastcall ProcessSoundQueue_0044c9c0(void *snd);     // ECX=0x4ba0d8
extern void __fastcall ReleaseItems_004075d0();
extern void __fastcall EffectItemTeardown_427760();
extern void __fastcall AnmMgrTeardown_442b10();
extern void __fastcall ReleaseEnemies_423050();
extern void __fastcall DispParamsDtor_40e4f0(void *disp);        // ECX=0x1347938
extern void __fastcall ReleaseBosses_41d150();
extern void __fastcall ReleaseChainCtrl_42d53d();
extern void __fastcall BgmFadeBook_443d30();
extern void __fastcall SupUpdateTimeAccumA_43a27f(void *sup);    // ECX=0x575950
extern void __fastcall EffectMgrReset_401a00(void *eff);         // ECX=0x134ce18

// MidiOutput::Open is __thiscall (this in ECX, idx on stack, RET 4) — model it as
// a struct method so MSVC emits PUSH idx; MOV ECX,this; CALL.
struct MidiDevStub
{
    void Open_436790(u32 idx);
};

// ---- AddedCallback callees (signatures from AddedCallback disasm) ----
extern i32 __fastcall CheckSomething_004429d0(void *arg);          // ECX=0
extern void __cdecl ErrorLog_004315f0(void *ctx, void *msg);       // 2 stack args
extern void __fastcall FirstInit_0042e1f8();
extern u32 __fastcall Rng_GetRandomU32_004318d0(void *rng);        // ECX=0x49fe20
extern void *__cdecl Malloc_0047d39d(u32 size);
extern void *__cdecl OperatorNew_0047d441(u32 size);
extern void __cdecl Free_0047d43c(void *ptr);
extern void __cdecl Free2_0047d285(void *ptr);
extern void __fastcall ScoreSubInit_0042e3da();
extern void __fastcall RngAdvance_004012b0(void *ctx);             // ECX=0x626270 or gm
extern void __fastcall RngConsume_00401390(void *ctx, i32 v);      // ECX=0x626270, v on stack
extern void __fastcall AnmColorSetup_0042d657(GameManager *gm);
extern i32 __fastcall StageInitCheck_0042e634();
extern void __fastcall StageColor_0042e38c(GameManager *gm);
extern void __fastcall GrantExtend_0042e81b(void *record, i32 val); // ECX=record, EDX=val
extern void __fastcall FullPowerSetup_00443aa0(i32 a, void *b);    // ECX=a, EDX=b
extern i32 __fastcall InitItemMgr_004074c0(i32 playCount);         // ECX=playCount
extern i32 __fastcall InitEffect_004276a0(void *data);             // ECX=0x498524
extern i32 __fastcall InitEnemy_00422f40(i32 a, i32 b);            // ECX, EDX
extern i32 __fastcall LoadStageData_0040e420(void *disp, void *stageData); // ECX=0x1347938
extern i32 __fastcall InitBoss_0041d0a0();
extern i32 __fastcall InitStageGame_0042d136();
extern void __fastcall PlayAudio_00439dd0(void *sup, i32 channel, void *path); // ECX=sup
extern void __fastcall StopAudio_00439ec1(void *sup, i32 channel); // ECX=sup
extern void __fastcall DrawFpsCounter_004390a5(void *arg);          // ECX=0
extern void __cdecl DebugPrint_00437903(const char *fmt, ...);
extern u32 __stdcall timeGetTime(void);

// GameManager::RegisterChain  FUN_0042f3c5  (0x97 bytes)
// Registers g_GameManager into the global Chain (g_Chain @ 0x626218) via two
// embedded ChainElem nodes (update @ +0x9644, draw @ +0x9664), priority 2.
ZunResult GameManager::RegisterChain()
{
    g_GameManager.updateChainNode.callback = (ChainCallback)OnUpdate;
    // MSVC /Od emits `and [mem],0` for the `= 0` and `mov [mem],imm` for the callback
    // address — matching the orig's redundant zero-then-set sequence exactly.
    g_GameManager.updateChainNode.addedCallback = 0;
    g_GameManager.updateChainNode.deletedCallback = 0;
    g_GameManager.updateChainNode.addedCallback = (ChainAddedCallback)AddedCallback;
    g_GameManager.updateChainNode.deletedCallback = (ChainDeletedCallback)DeletedCallback;
    g_GameManager.updateChainNode.arg = &g_GameManager;
    g_GameManager.frameCounter = 0;

    if (g_Chain.AddToCalcChain(&g_GameManager.updateChainNode, 2) != 0)
    {
        return ZUN_ERROR;
    }

    g_GameManager.drawChainNode.callback = (ChainCallback)OnDraw;
    g_GameManager.drawChainNode.addedCallback = 0;
    g_GameManager.drawChainNode.deletedCallback = 0;
    g_GameManager.drawChainNode.arg = &g_GameManager;
    g_Chain.AddToDrawChain(&g_GameManager.drawChainNode, 2);

    return ZUN_SUCCESS;
}

// GameManager::CutChain  FUN_0042f45d  (0x4d bytes)
// Detaches both chain nodes, then clamps the running score to 999999999 and
// syncs the displayed score to it (scoreSub reached via GameManager+0x8 pointer).
void GameManager::CutChain()
{
    g_Chain.Cut(&g_GameManager.updateChainNode);
    g_Chain.Cut(&g_GameManager.drawChainNode);

    if ((u32)g_GameManager.scoreSub->score >= 1000000000)
    {
        g_GameManager.scoreSub->score = 999999999;
    }
    g_GameManager.scoreSub->guiScore = g_GameManager.scoreSub->score;
}

// GameManager::OnDraw  FUN_0042e1d4  (0x23 bytes)
// Trivial draw-tick: if the pause-request flag (+0x93dc) is set, latch it to 2.
ChainCallbackResult __fastcall GameManager::OnDraw(GameManager *gameManager)
{
    if (gameManager->unk_93dc != 0)
    {
        gameManager->unk_93dc = 2;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// GameManager::OnUpdate  FUN_0042d8d5  (~2303 bytes, the per-frame calc callback)
// Handles: pause-request edge detection, 4-float RNG readout into Supervisor
// scratch, rank-up tier gating, paused-state BGM fade + timeout, CRC re-seed,
// ScoreSub seed sanity range-checks, the active-bit recompute, D3D Clear,
// score smoothing toward the running total, and a second rank-force tier.
// Returns CONTINUE (1) on a normal tick, BREAK (3) to skip downstream calc
// for this frame (rank/pause gating).
#pragma auto_inline(off)
ChainCallbackResult __fastcall GameManager::OnUpdate(GameManager *gameManager)
{
    // ---- Block 1: pause-request detection (edge on input bit 3) ----
    if (gameManager->unk_93dd == 0 &&
        gameManager->unk_93dc == 0 &&
        ((gameManager->statusBitfield >> 1) & 1) == 0 &&
        *(i8 *)((u8 *)gameManager + 0xd) == 0 &&
        (*(u16 *)0x4b9e4c & 8) != 0 &&
        (*(u16 *)0x4b9e4c & 8) != (*(u16 *)0x4b9e54 & 8))
    {
        gameManager->unk_93dc = 1;
        g_GameManager.anmColorSetup[0] = *(f32 *)0x498a84;
        g_GameManager.anmColorSetup[1] = *(f32 *)0x498a80;
        g_GameManager.anmColorSetup[2] = *(f32 *)0x498a6c;
        g_GameManager.anmColorSetup[3] = *(f32 *)0x498a68;
        *(i32 *)((u8 *)&g_GameManager + 0x93d0) = 1;
        if (g_GameManager.playCount != 6 || *(i32 *)0x49fbf0 >= 0x12c)
        {
            PauseSound_0044d2f0((void *)0x4ba0d8, 6, 0, (const char *)0x498a40);
        }
        SoundCmd_0044c930((void *)0x4ba0d8, 0x25, 0);
        SupUpdateTimeAccumB_0043a3f4((void *)0x575950);
    }

    // ---- Block 2: convert anmColorSetup[0..3] to ints (Supervisor scratch),
    //      reset two floats, mark AnmManager dirty byte ----
    *(i32 *)0x575a18 = RandFloatToInt_0048b8a0(g_GameManager.anmColorSetup[0]);
    *(i32 *)0x575a1c = RandFloatToInt_0048b8a0(g_GameManager.anmColorSetup[1]);
    *(i32 *)0x575a20 = RandFloatToInt_0048b8a0(g_GameManager.anmColorSetup[2]);
    *(i32 *)0x575a24 = RandFloatToInt_0048b8a0(g_GameManager.anmColorSetup[3]);
    *(f32 *)0x575a28 = 0.0f;
    *(f32 *)0x575a2c = 1.0f;
    *(u8 *)((u8 *)*(void **)0x4b9e44 + 0x2e4d4) |= 0xff;

    // ---- Block 3: rank-up gating (practice/replay + active game). rankCounter++
    //      then tier-based modulo check; BREAK skips downstream calc this frame ----
    if ((((g_GameManager.statusBitfield >> 3) & 1) != 0) &&
        *(u8 *)0x62f64f == 1 &&
        IsGameActive_0042ad66((void *)0x49fbf0) == 0)
    {
        gameManager->rankCounter++;
        if ((i32)*(i16 *)0x575ad8 < 0x14)
        {
            if (gameManager->rankCounter % 3 != 0)
            {
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if ((i32)*(i16 *)0x575ad8 >= 0x14 && (i32)*(i16 *)0x575ad8 < 0x1e)
        {
            if (gameManager->rankCounter % 2 != 0)
            {
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if ((i32)*(i16 *)0x575ad8 >= 0x1e && (i32)*(i16 *)0x575ad8 < 0x28)
        {
            if (gameManager->rankCounter % 3 == 0)
            {
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if ((i32)*(i16 *)0x575ad8 >= 0x28 && (i32)*(i16 *)0x575ad8 < 0x32)
        {
            if (gameManager->rankCounter % 6 == 0)
            {
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
    }

    // ---- Block 4: paused-state handling (statusBitfield bit1 set) ----
    if (((gameManager->statusBitfield >> 1) & 1) != 0)
    {
        if (*(u16 *)0x4b9e4c != 0 && *(u16 *)0x4b9e4c != *(u16 *)0x4b9e54)
        {
            *(i32 *)0x575aa8 = 1;
        }
        gameManager->pauseFrameCounter++;
        if ((*(u8 *)((u8 *)gameManager + 0x93de) == 0 && gameManager->pauseFrameCounter == 0x1fa4) ||
            (*(u8 *)((u8 *)gameManager + 0x93de) == 1 && gameManager->pauseFrameCounter == 0x1b6c) ||
            (*(u8 *)((u8 *)gameManager + 0x93de) == 2 && gameManager->pauseFrameCounter == 0x120c))
        {
            SoundCmd_0044b310(2, 0x78, 0, 0, 0);
            FadeOutMusic_0043a0d6((void *)0x575950, *(f32 *)0x498a8c);
        }
        if ((*(u8 *)((u8 *)gameManager + 0x93de) == 0 && (i32)gameManager->pauseFrameCounter > 0x201b) ||
            (*(u8 *)((u8 *)gameManager + 0x93de) == 1 && (i32)gameManager->pauseFrameCounter > 0x1be3) ||
            (*(u8 *)((u8 *)gameManager + 0x93de) == 2 && (i32)gameManager->pauseFrameCounter >= 0x1284))
        {
            *(i32 *)0x575aa8 = 1;
            return CHAIN_CALLBACK_RESULT_BREAK;
        }
    }

    // ---- Block 5: CRC re-seed + checksum -> stage-time float (via g_GameManager global) ----
    g_GameManager.scoreSub->crcAcc = g_GameManager.scoreSub->crcReseed38;
    {
        i32 checksum = CalculateChecksum_0042d7be(gameManager);
        *(f32 *)0x62f884 = (f32)checksum + (f32)g_GameManager.scoreSub->stageTimeBase98;
    }

    // ---- Block 6: ScoreSub seed sanity range-checks (ints in [0x198f,0x1a02f]) ----
    {
        u32 i;
        for (i = 0; i < 7; i++)
        {
            if (gameManager->scoreSub->seedIds7[i] < 0x198f ||
                gameManager->scoreSub->seedIds7[i] > 0x1a02f)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
        for (i = 0; i < 2; i++)
        {
            if (gameManager->scoreSub->randF3260[i] < *(f32 *)0x498bac ||
                gameManager->scoreSub->randF3260[i] > *(f32 *)0x498c7c)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
    }

    // ---- Block 7: recompute chain-active bit (bit2 of statusBitfield) ----
    {
        u32 notPaused;
        if (gameManager->unk_93dd == 0 && gameManager->unk_93dc == 0)
        {
            notPaused = 1;
        }
        else
        {
            notPaused = 0;
        }
        gameManager->statusBitfield = (gameManager->statusBitfield & 0xfffffffb) | ((notPaused & 1) << 2);
    }

    // ---- Block 8: more ScoreSub sanity range-checks ----
    {
        u32 i;
        for (i = 0; i < 2; i++)
        {
            if (gameManager->scoreSub->randF3254[i] < *(f32 *)0x498bac ||
                gameManager->scoreSub->randF3254[i] > *(f32 *)0x498c7c)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
        for (i = 0; i < 8; i++)
        {
            if (gameManager->scoreSub->seedIds8[i] < 0x198f ||
                gameManager->scoreSub->seedIds8[i] > 0x1a02f)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
    }

    // ---- Block 9: D3D device Clear (z-buffer) via vtable[0x90] ----
    (*(D3DDeviceStub **)0x575958)->lpVtbl->Clear((*(D3DDeviceStub **)0x575958), 0, 0, 2,
                                                 *(u32 *)0x1347fe4, 1.0f, 0);

    // ---- Block 10: pause-state skip (unk_93dc latched 1/2 or unk_93dd set) ----
    if (gameManager->unk_93dc == 1 || gameManager->unk_93dc == 2 || gameManager->unk_93dd != 0)
    {
        return CHAIN_CALLBACK_RESULT_BREAK;
    }

    // ---- Block 11: score smoothing (displayed guiScore chases running score) ----
    if (gameManager->scoreSub->score >= 1000000000)
    {
        gameManager->scoreSub->score = 999999999;
    }
    if (gameManager->scoreSub->guiScore != gameManager->scoreSub->score)
    {
        if (gameManager->scoreSub->score < gameManager->scoreSub->guiScore)
        {
            gameManager->scoreSub->score = gameManager->scoreSub->guiScore;
        }
        {
            u32 delta = (gameManager->scoreSub->score - gameManager->scoreSub->guiScore) >> 5;
            if (delta >= 0x8d55e)
            {
                delta = 0x8d55e;
            }
            else if (delta == 0)
            {
                delta = 1;
            }
            if (gameManager->scoreSub->scoreDelta < delta)
            {
                gameManager->scoreSub->scoreDelta = delta;
            }
            if (gameManager->scoreSub->guiScore + gameManager->scoreSub->scoreDelta > gameManager->scoreSub->score)
            {
                gameManager->scoreSub->scoreDelta =
                    gameManager->scoreSub->score - gameManager->scoreSub->guiScore;
            }
            gameManager->scoreSub->guiScore += gameManager->scoreSub->scoreDelta;
            if (gameManager->scoreSub->guiScore >= gameManager->scoreSub->score)
            {
                gameManager->scoreSub->scoreDelta = 0;
                gameManager->scoreSub->guiScore = gameManager->scoreSub->score;
            }
            if (gameManager->scoreSub->highScore < gameManager->scoreSub->guiScore)
            {
                gameManager->scoreSub->highScore = gameManager->scoreSub->guiScore;
                gameManager->scoreSub->highScoreStatusByte = gameManager->scoreSub->srcStatusByte20;
            }
        }
    }

    // ---- Block 12: final ScoreSub sanity range-checks ----
    {
        u32 i;
        for (i = 0; i < 3; i++)
        {
            if (gameManager->scoreSub->randF3370[i] < *(f32 *)0x498bac ||
                gameManager->scoreSub->randF3370[i] > *(f32 *)0x498c7c)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
        for (i = 0; i < 2; i++)
        {
            if (gameManager->scoreSub->randF3280[i] < *(f32 *)0x498bac ||
                gameManager->scoreSub->randF3280[i] > *(f32 *)0x498c7c)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
        for (i = 0; i < 5; i++)
        {
            if (gameManager->scoreSub->seedIds5[i] < 0x198f ||
                gameManager->scoreSub->seedIds5[i] > 0x1a02f)
            {
                *(f32 *)0x62f884 = *(f32 *)0x498c80;
            }
        }
    }

    // ---- Block 13: rank-force tier (gated by playerSub->bonusExtendGate) ----
    if (g_GameManager.playerSub->bonusExtendGate != 0)
    {
        *(u8 *)0x62627d = 0;
        gameManager->rankCounter++;
        if (*(i32 *)0x9a9a80 >= 0x140)
        {
            if (gameManager->rankCounter % 3 == 0)
            {
                *(u8 *)0x62627d = 1;
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if (*(i32 *)0x9a9a80 < 0x140 && *(i32 *)0x9a9a80 >= 0xe0)
        {
            if (gameManager->rankCounter % 4 == 0)
            {
                *(u8 *)0x62627d = 1;
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if (*(i32 *)0x9a9a80 < 0xe0 && *(i32 *)0x9a9a80 >= 0x80)
        {
            if (gameManager->rankCounter % 5 == 0)
            {
                *(u8 *)0x62627d = 1;
                return CHAIN_CALLBACK_RESULT_BREAK;
            }
        }
        if (*(i32 *)0x9a9a80 < 0x80)
        {
            gameManager->rankCounter = 0;
        }
    }

    gameManager->frameCounter++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}
#pragma auto_inline(on)

// GameManager::DeletedCallback  FUN_0042f2e4  (0xe1 bytes)
// Chain "OnDelete" for the update node. Cross-subsystem teardown: MIDI shutdown
// (if Supervisor state==2 and a midi device is open), streaming-sound queue
// drain, release of every subsystem singleton, play-time accumulator update,
// clear of the chain-registered bit, EffectManager reset, and clear of two
// GameManager liveness fields. GameManager* is only used for the bit2 clear.
#pragma auto_inline(off)
ZunResult __fastcall GameManager::DeletedCallback(GameManager *gameManager)
{
    SupOnDelete_0043a05f((void *)0x575950);

    if (*(u8 *)0x575a87 == 2 && *(void **)0x575acc != 0)
    {
        MidiDevStub *midi = *(MidiDevStub **)0x575acc;
        MidiCloseAll_436b30(midi);
        midi->Open_436790(0x1e);
        MidiPlay_436ad0(midi);
    }

    do
    {
    } while (ProcessSoundQueue_0044c9c0((void *)0x4ba0d8) != 0);

    ReleaseItems_004075d0();
    EffectItemTeardown_427760();
    AnmMgrTeardown_442b10();
    ReleaseEnemies_423050();
    DispParamsDtor_40e4f0((void *)0x1347938);
    ReleaseBosses_41d150();
    ReleaseChainCtrl_42d53d();
    BgmFadeBook_443d30();

    if ((*(u32 *)0x62f648 >> 3 & 1) == 0)
    {
        SupUpdateTimeAccumB_0043a3f4((void *)0x575950);
    }

    *(u32 *)0x575ae4 = 0;
    SupUpdateTimeAccumA_43a27f((void *)0x575950);

    gameManager->statusBitfield &= 0xfffffffbu;

    EffectMgrReset_401a00((void *)0x134ce18);

    *(u8 *)0x62627d = 0;
    *(i32 *)0x62f858 = 0;

    return ZUN_SUCCESS;
}
#pragma auto_inline(on)

// RngConsume_00401390 takes its arg on the stack (__thiscall: this in ECX, arg
// pushed, RET 4) — model it as a struct method so MSVC emits PUSH arg; MOV ECX.
// (Default member calling convention is __thiscall under /Gd.)
struct RngStub
{
    void Consume_00401390(i32 v);
};

// GameManager::AddedCallback  FUN_0042e83e  (~2726 bytes, the chain OnAdd callback)
// First-time init: allocates playerSub(0x38)+scoreSub(0xC8), memcpy's the player
// init template, seeds score fields, sets per-difficulty extend thresholds,
// grants initial extends, runs the subsystem init cascade, loads stage data,
// starts BGM. Reinit path (curState==3): reuses scoreSub, re-syncs guiScore.
// Returns ZUN_SUCCESS / ZUN_ERROR.
#pragma auto_inline(off)
ZunResult __fastcall GameManager::AddedCallback(GameManager *gameManager)
{
    *(i32 *)0x575c08 = 0;
    gameManager->difficultyMask = 1u << gameManager->difficulty;
    *(u8 *)((u8 *)gameManager + 0x93d7) =
        (u8)(*(u8 *)((u8 *)gameManager + 0x93d6) + *(u8 *)((u8 *)gameManager + 0x93d5) * 2);
    *(u32 *)0x575ae4 = timeGetTime();
    *(f32 *)0x575ac8 = 1.0f;

    if (*(i32 *)0x575aa8 == 3)
    {
        gameManager->scoreSub->guiScore = gameManager->scoreSub->score;
        gameManager->scoreSub->scoreDelta = 0;
        if (CheckSomething_004429d0((void *)0) != 0)
        {
            ErrorLog_004315f0((void *)0x624210, (void *)0x498064);
            return ZUN_ERROR;
        }
    }
    else
    {
        FirstInit_0042e1f8();
        if (gameManager->playerSub != 0)
        {
            Free_0047d43c(gameManager->playerSub);
            gameManager->playerSub = 0;
        }
        if (gameManager->scoreSub != 0)
        {
            Free_0047d43c(gameManager->scoreSub);
            gameManager->scoreSub = 0;
        }
        {
            u32 r = Rng_GetRandomU32_004318d0((void *)0x49fe20);
            gameManager->scratchBuf = Malloc_0047d39d(r % 0xffff + 0x10);
        }
        gameManager->playerSub = (PlayerSub *)OperatorNew_0047d441(0x38);
        gameManager->scoreSub = (ScoreSub *)OperatorNew_0047d441(0xc8);
        ScoreSubInit_0042e3da();
        memcpy(gameManager->playerSub, (void *)0x575a68, 0x38);
        Free2_0047d285(gameManager->scratchBuf);
        *(u8 *)((u8 *)gameManager + 0x93d4) = 0;
        gameManager->extendThresholdB = gameManager->scoreSub->mainSeedScoreBase;
        gameManager->initScoreMirror = gameManager->scoreSub->mainSeedScoreBase;
        if (*(i32 *)0x626280 >= 4)
        {
            gameManager->playerSub->playerCountMode = 2;
        }
        if ((*(u32 *)0x62f648 & 1) != 0)
        {
            gameManager->playerSub->playerCountMode = 8;
        }
        if (CheckSomething_004429d0((void *)0) != 0)
        {
            ErrorLog_004315f0((void *)0x624210, (void *)0x498064);
            return ZUN_ERROR;
        }
        if ((*(u32 *)0x62f648 >> 3 & 1) == 0)
        {
            g_GameManager.scoreSub->playerCountF32 = (f32)gameManager->playerSub->playerCountMode;
            RngAdvance_004012b0((void *)0x626270);
            ((RngStub *)(void *)0x626270)
                ->Consume_00401390(RandFloatToInt_0048b8a0(*(f32 *)((u8 *)*(void **)0x575948 + 4)));
        }
        AnmColorSetup_0042d657(gameManager);
        gameManager->scoreSub->pointItemAutoBonus = 0.0f;
        RngAdvance_004012b0(gameManager);
        gameManager->counter9628 = 0;
        gameManager->scoreSub->guiScore = 0;
        gameManager->scoreSub->score = 0;
        gameManager->scoreSub->scoreDelta = 0;
        gameManager->scoreSub->highScore = 100000;
        gameManager->scoreSub->srcStatusByte20 = 0;
        gameManager->scoreSub->counter18 = 0;
        gameManager->scoreSub->lifePieceProgress = 0;
        if ((i32)gameManager->difficulty < 4)
        {
            gameManager->scoreSub->lifePieceThreshold = 0x32;
        }
        else
        {
            gameManager->scoreSub->lifePieceThreshold = 200;
            gameManager->playerSub->bonusExtendGate = 0;
        }
        gameManager->scoreSub->lifePieceTier = 0;
        if (StageInitCheck_0042e634() != 0)
        {
            return ZUN_ERROR;
        }
        StageColor_0042e38c(gameManager);
        gameManager->scoreSub->counter50 = 0;
        RngAdvance_004012b0(gameManager);
        gameManager->scoreSub->counter6c = 0;
        RngAdvance_004012b0(gameManager);
        gameManager->scoreSub->counter1c = 0;

        if ((*(u32 *)0x62f648 & 1) == 0)
        {
            i32 diff = gameManager->difficulty;
            if (diff == 0)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 200000;
            }
            else if (diff == 1)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 200000;
            }
            else if (diff == 2)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 250000;
            }
            else if (diff == 3)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 300000;
            }
            else if (diff == 4)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 400000;
                gameManager->extendThresholdB = gameManager->scoreSub->mainSeedScoreBase + 200000;
            }
            else if (diff == 5)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 400000;
                gameManager->extendThresholdB = gameManager->scoreSub->mainSeedScoreBase + 300000;
            }
        }
        else
        {
            i32 diff = gameManager->difficulty;
            if (diff == 0)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 200000;
            }
            else if (diff == 1)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 200000;
            }
            else if (diff == 2)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 250000;
            }
            else if (diff == 3)
            {
                gameManager->extendThresholdA = gameManager->scoreSub->mainSeedScoreBase + 300000;
            }
            {
                i32 pc = gameManager->playCount + 1;
                if (pc == 2)
                {
                    gameManager->extendThresholdB = gameManager->extendThresholdA;
                }
                else if (pc == 3)
                {
                    gameManager->extendThresholdA += 50000;
                    gameManager->extendThresholdB = gameManager->extendThresholdA;
                }
                else if (pc == 4)
                {
                    gameManager->extendThresholdA += 100000;
                    gameManager->extendThresholdB = gameManager->extendThresholdA;
                }
                else if (pc == 5)
                {
                    gameManager->extendThresholdA += 150000;
                    gameManager->extendThresholdB = gameManager->extendThresholdA;
                }
                else if (pc == 6)
                {
                    gameManager->extendThresholdA += 200000;
                    gameManager->extendThresholdB = gameManager->extendThresholdA;
                }
            }
        }

        if ((*(u32 *)0x62f648 >> 3 & 1) == 0)
        {
            if (gameManager->playerSub->bonusExtendGate == 0)
            {
                GrantExtend_0042e81b((void *)((u8 *)0x62f50c + *(i32 *)0x626280 * 0x2c), 0xf423f);
                GrantExtend_0042e81b((void *)0x62f614, 0xf423f);
                GrantExtend_0042e81b((void *)((u8 *)0x62f510 + *(i32 *)0x626280 * 0x2c +
                                               *(u8 *)((u8 *)gameManager + 0x93d7) * 4),
                                     0xf423f);
                GrantExtend_0042e81b((void *)((u8 *)0x62f618 + *(u8 *)((u8 *)gameManager + 0x93d7) * 4),
                                     0xf423f);
                if (*(i32 *)0x575aa8 == 10)
                {
                    GrantExtend_0042e81b((void *)((u8 *)0x62f528 + *(i32 *)0x626280 * 0x2c), 0xf423f);
                    GrantExtend_0042e81b((void *)0x62f630, 0xf423f);
                }
                if ((*(u32 *)0x62f648 & 1) != 0)
                {
                    GrantExtend_0042e81b((void *)((u8 *)0x62f534 + *(i32 *)0x626280 * 0x2c), 0xf423f);
                    GrantExtend_0042e81b((void *)0x62f63c, 0xf423f);
                }
            }
        }
        else
        {
            gameManager->playerSub->bonusExtendGate = 0;
        }
    }

    gameManager->counter9640 = 0;
    gameManager->scoreSub->grazeCount = 0;
    gameManager->scoreSub->counter14 = 0;
    *(u8 *)((u8 *)gameManager + 0x93dc) = 0;
    gameManager->playCount++;

    if ((*(u32 *)0x62f648 >> 3 & 1) == 0)
    {
        u32 idx = *(u8 *)0x62f647;
        if (gameManager->scoreSub->srcStatusByte20 == 0)
        {
            u8 *rec = (u8 *)gameManager + 0x8454 + *(i32 *)0x626280 + idx * 0x1c;
            if ((u32)*rec < (u32)gameManager->playCount - 1)
            {
                *rec = (u8)(gameManager->playCount - 1);
            }
        }
        {
            u8 *rec = (u8 *)gameManager + 0x845a + *(i32 *)0x626280 + idx * 0x1c;
            if ((u32)*rec < (u32)gameManager->playCount - 1)
            {
                *rec = (u8)(gameManager->playCount - 1);
            }
        }
    }

    if (((gameManager->statusBitfield & 1) != 0) && gameManager->playCount != 1)
    {
        gameManager->scoreSub->pointItemAutoBonus = *(f32 *)0x498b24;
        RngAdvance_004012b0(gameManager);
    }

    if ((*(u32 *)0x62f648 >> 3 & 1) != 0)
    {
        StageColor_0042e38c(gameManager);
        FullPowerSetup_00443aa0(1, (void *)0x62f654);
        {
            u16 saved = *(u16 *)0x49fe20;
            RngAdvance_004012b0(gameManager);
            *(u16 *)0x49fe20 = saved;
        }
    }

    gameManager->randSeedWord = *(u16 *)0x49fe20;

    if (InitItemMgr_004074c0(gameManager->playCount) != 0)
    {
        ErrorLog_004315f0((void *)0x624210, (void *)0x498038);
        return ZUN_ERROR;
    }
    if (InitEffect_004276a0((void *)0x498524) != 0)
    {
        ErrorLog_004315f0((void *)0x624210, (void *)0x498010);
        return ZUN_ERROR;
    }
    {
        i32 pc = gameManager->playCount;
        if (InitEnemy_00422f40(*(i32 *)((u8 *)0x49f588 + pc * 8),
                               *(i32 *)((u8 *)0x49f58c + pc * 8)) != 0)
        {
            ErrorLog_004315f0((void *)0x624210, (void *)0x497f4c);
            return ZUN_ERROR;
        }
    }
    if (LoadStageData_0040e420((void *)0x1347938,
                               (void *)*(u32 *)((u8 *)0x49f560 + gameManager->playCount * 4)) != 0)
    {
        ErrorLog_004315f0((void *)0x624210, (void *)0x497e84);
        return ZUN_ERROR;
    }
    if (InitBoss_0041d0a0() != 0)
    {
        ErrorLog_004315f0((void *)0x624210, (void *)0x497e58);
        return ZUN_ERROR;
    }
    if (InitStageGame_0042d136() != 0)
    {
        ErrorLog_004315f0((void *)0x624210, (void *)0x497e30);
        return ZUN_ERROR;
    }
    if ((*(u32 *)0x62f648 >> 3 & 1) == 0)
    {
        FullPowerSetup_00443aa0(0, (void *)0x497e1c);
    }
    PlayAudio_00439dd0((void *)0x575950, 0, (void *)((u8 *)*(void **)0x1347f9c + 0x290));
    PlayAudio_00439dd0((void *)0x575950, 1, (void *)((u8 *)*(void **)0x1347f9c + 0x310));
    if (gameManager->playCount == 6)
    {
        SupOnDelete_0043a05f((void *)0x575950);
        PlayAudio_00439dd0((void *)0x575950, 2, (void *)0x4986b4);
    }
    else
    {
        StopAudio_00439ec1((void *)0x575950, 0);
    }
    do
    {
    } while (ProcessSoundQueue_0044c9c0((void *)0x4ba0d8) != 0);

    *(u8 *)((u8 *)gameManager + 0x93dd) = 0;
    gameManager->statusBitfield |= 4;

    if (*(i32 *)0x575aa8 != 3)
    {
        *(f32 *)0x575ad0 = 0.0f;
        *(f32 *)0x575ad4 = 0.0f;
    }

    *(u8 *)((u8 *)gameManager + 0xc) = 0;
    gameManager->scoreSub->score = 0;
    gameManager->statusBitfield &= 0xffffffefu;

    EffectMgrReset_401a00((void *)0x134ce18);
    *(u8 *)0x62627d = 0;
    DrawFpsCounter_004390a5((void *)0);
    DebugPrint_00437903((const char *)0x497e08, *(u16 *)0x49fe20, *(i32 *)0x49fe24);

    return ZUN_SUCCESS;
}
#pragma auto_inline(on)

} // namespace th07
