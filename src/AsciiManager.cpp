// AsciiManager module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary. Pure C++ with a single unified code path:
// no #ifdef DIFFBUILD splits, no inline asm.
//
// Cross-module call conventions (all verified from orig disassembly):
//   g_AsciiManager        : global @ 0x134ce18
//   g_AsciiManagerCalcChain    : ChainElem @ 0x135dfac
//   g_AsciiManagerDrawMenusChain: ChainElem @ 0x134cdf4
//   g_AsciiManagerDrawPopupsChain: ChainElem @ 0x135dfcc
//   g_Chain               : __thiscall, ECX = &g_Chain @ 0x626218
//   g_AnmManager          : *pointer* stored @ 0x4b9e44; deref each use
//                           (LoadAnm @ FUN_0044df90, ReleaseAnm @ FUN_0044e4e0,
//                            DrawNoRotation @ FUN_0044f770)
//   AnmVm::Initialize     : __fastcall, FUN_00401170 (AnmVm* in ECX)
//   AnmVm::ResetInterpTimers: __fastcall, FUN_004011b0 (AnmVm* in ECX)
//   Supervisor::TickTimer : __thiscall, ECX = &g_Supervisor @ 0x575950
//   StageMenu::OnDrawGameMenu  : __fastcall, ECX = mgr+0x74e8, FUN_00403a20
//   StageMenu::OnDrawRetryMenu : __fastcall, ECX = mgr+0x8e34, FUN_00404560
//
// The AnmVm buffers are kept as opaque 0x24c-byte arrays because th07's AnmVm
// is owned by the AnmManager module; AsciiManager only needs to know field
// offsets within them.

#include "AsciiManager.hpp"

#include "Chain.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3dx8math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace th07
{
DIFFABLE_STATIC(AsciiManager, g_AsciiManager)
DIFFABLE_STATIC(ChainElem, g_AsciiManagerCalcChain)
DIFFABLE_STATIC(ChainElem, g_AsciiManagerOnDrawMenusChain)
DIFFABLE_STATIC(ChainElem, g_AsciiManagerOnDrawPopupsChain)

// ---- AnmManager thiscall callee stubs (struct methods emit PUSH/MOV ECX/CALL) ----
struct AnmMgrStub
{
    ZunResult LoadAnm_44df90(i32 idx, char *path, i32 offset);
    void ReleaseAnm_44e4e0(i32 idx);
    void DrawNoRotation_44f770(void *vm);
    void SetActiveSprite_44e8e0(void *vm, i32 spriteIdx);
    void SetAndExecuteScript_44ea20(void *vm, void *script);
    void FlushSprites_44f5c0();
};

// AnmVm helpers (fastcall, arg in ECX).
extern void __fastcall AnmVm_Initialize_401170(void *vm);   // FUN_00401170 (ctor)
extern void __fastcall AnmVm_Initialize_4010f0(void *vm);   // FUN_004010f0 (Initialize)
extern void __fastcall AnmVm_ResetInterpTimers_4011b0(void *vm);  // FUN_004011b0

// Supervisor::TickTimer  __thiscall, ECX = &g_Supervisor @ 0x575950.
struct Supervisor;
extern void __fastcall Supervisor_TickTimer_43958d(Supervisor *sup, i32 *current, f32 *subFrame);

// StageMenu methods (fastcall, this = mgr+0x74e8 / mgr+0x8e34).
extern void __fastcall StageMenu_OnDrawGameMenu_403a20(void *gameMenu);
extern void __fastcall StageMenu_OnDrawRetryMenu_404560(void *retryMenu);
extern void __fastcall StageMenu_OnUpdateGameMenu_402780(void *gameMenu);
extern void __fastcall StageMenu_OnUpdateRetryMenu_403b60(void *retryMenu);

// AsciiManager::ExecuteLabelVms (FUN_00401400)  __thiscall, drives the 7
// label AnmVms through AnmManager::ExecuteScript.
extern void __fastcall AsciiMgr_ExecuteLabelVms_401400(AsciiManager *mgr);

// AnmManager::ExecuteScript (FUN_00450d60)  __thiscall, PUSH vm; MOV ECX,anmMgr.
extern void AnmMgr_ExecuteScript_450d60(AnmMgrStub *anmMgr, void *vm);

// Singleton handles. Absolute addresses so MOV ECX,imm matches orig exactly.
#define ANM_MGR (*reinterpret_cast<AnmMgrStub **>(0x4b9e44))
#define ASCII_MGR (*reinterpret_cast<AsciiManager *>(0x134ce18))
#define ASCII_CALC_CHAIN (*reinterpret_cast<ChainElem *>(0x135dfac))
#define ASCII_DRAW_MENUS_CHAIN (*reinterpret_cast<ChainElem *>(0x134cdf4))
#define ASCII_DRAW_POPUPS_CHAIN (*reinterpret_cast<ChainElem *>(0x135dfcc))
#define SUP_PTR (reinterpret_cast<Supervisor *>(0x575950))

// Chain callback addresses (orig function entry points).
#define ASCII_ON_UPDATE_CB   ((ChainCallback)0x4017e0)
#define ASCII_ON_DRAW_MENUS_CB ((ChainCallback)0x401970)
#define ASCII_ON_DRAW_POPUPS_CB ((ChainCallback)0x4019e0)
#define ASCII_ADDED_CB       ((ChainAddedCallback)0x401d70)
#define ASCII_DELETED_CB     ((ChainDeletedCallback)0x401de0)

// GameManager globals used by OnUpdate.
#define GM_IS_IN_GAME_MENU (*reinterpret_cast<u8 *>(0x62f64c))
#define GM_IS_IN_RETRY_MENU (*reinterpret_cast<u8 *>(0x62f64d))
#define GM_FLAGS (*reinterpret_cast<u32 *>(0x62f648))

// Supervisor framerate multiplier @ 0x575ac8.
#define SUP_FRAMERATE_MULT (*reinterpret_cast<f32 *>(0x575ac8))

// Popup position offsets.
#define POPUP_OFFSET_X (*reinterpret_cast<f32 *>(0x62f864))
#define POPUP_OFFSET_Y (*reinterpret_cast<f32 *>(0x62f868))

// AnmVm script-table pointer inside AnmManager (scripts @ AnmManager+0x28ef0).
#define ANM_SCRIPTS_TABLE (*reinterpret_cast<i32 **>(reinterpret_cast<u8 *>(ANM_MGR) + 0x28ef0))

// th07 ANM file indices.
#define ANM_FILE_ASCII   1
#define ANM_FILE_ASCIIS  2
#define ANM_FILE_OTHER   3
#define ANM_FILE_CAPTURE 4
#define ANM_OFFSET_CAPTURE 0x724

// Chain priorities.
#define TH_CHAIN_PRIO_CALC_ASCIIMANAGER 1
#define TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_MENUS 0x10
#define TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_POPUPS 0x0b

#define ASCII_POPUPS_COUNT 0x2d3
#define ASCII_POPUPS_POPUP1_LIMIT 0x2cf
#define ASCII_POPUPS_POPUP2_BASE 0x2d0

// Convenience macro for poking a typed value into an opaque AnmVm byte buffer.
#define VM_FIELD(vmBuf, off, type) (*(type *)((u8 *)(vmBuf) + (off)))

// ===========================================================================
// AsciiManager::AsciiManager  (FUN_004014a0)
// Initializes all embedded AnmVms and primes the popups array.
// ===========================================================================
AsciiManager::AsciiManager()
{
    u8 *base = (u8 *)this;

    // The constructor calls AnmVm::ResetInterpTimers (FUN_004011b0) on each
    // embedded AnmVm, then memsets each to 0x24c bytes (ECX=0x93 dwords), and
    // sets word at +0x1d4 to 0xffff. Mirrored exactly.
    AnmVm_ResetInterpTimers_4011b0(base);
    memset(base, 0, 0x24c);
    *(u16 *)(base + 0x1d4) = 0xffff;

    AnmVm_ResetInterpTimers_4011b0(base + 0x24c);
    memset(base + 0x24c, 0, 0x24c);
    *(u16 *)(base + 0x24c + 0x1d4) = 0xffff;

    AnmVm_ResetInterpTimers_4011b0(base + 0x498);
    memset(base + 0x498, 0, 0x24c);
    *(u16 *)(base + 0x498 + 0x1d4) = 0xffff;

    AnmVm_ResetInterpTimers_4011b0(base + 0x6e4);
    memset(base + 0x6e4, 0, 0x24c);
    *(u16 *)(base + 0x6e4 + 0x1d4) = 0xffff;

    AnmVm_ResetInterpTimers_4011b0(base + 0x930);
    memset(base + 0x930, 0, 0x24c);
    *(u16 *)(base + 0x930 + 0x1d4) = 0xffff;

    // pointLabelVm[4] (base+0xb7c)  only AnmVm::Initialize, no memset.
    {
        u8 *p = base + 0xb7c;
        i32 n = 4;
        do
        {
            AnmVm_Initialize_401170(p);
            p += 0x24c;
            n -= 1;
        } while (n > 0);
    }

    // strings[256] (base+0x14bc)  pointless loop advancing the pointer.
    {
        u8 *p = base + 0x14bc;
        i32 n = 0x100;
        do
        {
            p += 0x60;
            n -= 1;
        } while (n > 0);
    }

    // StageMenu constructors (gameMenu @ +0x74e8, retryMenu @ +0x8e34).
    // extern void __fastcall StageMenuCtor_game_401690(void *gameMenu);
    // extern void __fastcall StageMenuCtor_retry_401720(void *retryMenu);
    // These are left as TODO stubs  not yet lifted.

    // Screenshake AnmVm @ +0x9e50.
    AnmVm_ResetInterpTimers_4011b0(base + 0x9e50);
    memset(base + 0x9e50, 0, 0x24c);
    *(u16 *)(base + 0x9e50 + 0x1d4) = 0xffff;

    // popups[723] @ +0xa09c  prime previous=-999, subFrame=0, current=0.
    {
        u8 *p = base + 0xa09c;
        i32 n = ASCII_POPUPS_COUNT;
        do
        {
            AsciiManagerPopup *popup = (AsciiManagerPopup *)p;
            popup->previous = -999;
            popup->subFrame = 0.0f;
            popup->current = 0;
            p += 0x28;
            n -= 1;
        } while (n > 0);
    }
}

StageMenu::StageMenu()
{
}

// ===========================================================================
// AsciiManager::OnUpdate  (FUN_004017e0)  __fastcall, AsciiManager* in ECX
// ===========================================================================
#pragma var_order(mgr, popup, i, timerPtr)
ChainCallbackResult AsciiManager::OnUpdate(AsciiManager *mgr)
{
    if (!GM_IS_IN_GAME_MENU && !GM_IS_IN_RETRY_MENU)
    {
        AsciiManagerPopup *popup = (AsciiManagerPopup *)((u8 *)mgr + 0xa09c);
        for (i32 i = 0; i < ASCII_POPUPS_COUNT; i++, popup++)
        {
            if (!popup->inUse)
            {
                continue;
            }
            // orig: FLD [0x498a50]; FMUL [0x575ac8]; FSUBR popup.position.y
            popup->position.y -= *(f32 *)0x498a50 * SUP_FRAMERATE_MULT;
            // Inline ZunTimer::Tick: timer @ popup+0x18 (previous/subFrame/current).
            u8 *timer = (u8 *)popup + 0x18;
            *(i32 *)(timer + 0) = *(i32 *)(timer + 8);   // previous = current
            Supervisor_TickTimer_43958d(SUP_PTR, (i32 *)(timer + 8), (f32 *)(timer + 4));
            if (*(i32 *)(timer + 8) > 60)
            {
                popup->inUse = 0;
            }
        }
    }
    else
    {
        if (GM_IS_IN_GAME_MENU)
        {
            StageMenu_OnUpdateGameMenu_402780((u8 *)mgr + 0x74e8);
        }
    }
    if (GM_IS_IN_RETRY_MENU)
    {
        StageMenu_OnUpdateRetryMenu_403b60((u8 *)mgr + 0x8e34);
    }

    AsciiMgr_ExecuteLabelVms_401400(mgr);

    if ((GM_FLAGS >> 1 & 1) != 0)
    {
        if (*(i16 *)((u8 *)mgr + 0xa028) == 0)
        {
            u8 *shakeVm = (u8 *)mgr + 0x9e50;
            u8 *anmMgr = (u8 *)ANM_MGR;
            *(u16 *)(shakeVm + 0x1d8) = 7;
            ANM_MGR->SetAndExecuteScript_44ea20(shakeVm, *(void **)(anmMgr + 0x28ef0 + 7 * 4));
        }
        AnmMgr_ExecuteScript_450d60(ANM_MGR, (u8 *)mgr + 0x9e50);
    }
    else
    {
        *(i16 *)((u8 *)mgr + 0xa028) = 0;
    }

    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::OnDrawMenus  (FUN_00401970)  __fastcall, AsciiManager* in ECX
// ===========================================================================
ChainCallbackResult AsciiManager::OnDrawMenus(AsciiManager *mgr)
{
    mgr->DrawStrings();
    mgr->numStrings = 0;
    StageMenu_OnDrawGameMenu_403a20((u8 *)mgr + 0x74e8);
    StageMenu_OnDrawRetryMenu_404560((u8 *)mgr + 0x8e34);
    if (*(i16 *)((u8 *)mgr + 0xa028) != 0)
    {
        ANM_MGR->DrawNoRotation_44f770((u8 *)mgr + 0x9e50);
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::OnDrawPopups  (FUN_004019e0)  __fastcall, AsciiManager* in ECX
// ===========================================================================
ChainCallbackResult AsciiManager::OnDrawPopups(AsciiManager *mgr)
{
    mgr->DrawPopups();
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::RegisterChain  (FUN_00401e30)  __cdecl
// ===========================================================================
ZunResult AsciiManager::RegisterChain()
{
    AsciiManager *mgr = &ASCII_MGR;

    // orig zeroes calcChain.callback/added/deleted (5 writes all = 0), then
    // sets arg = &g_AsciiManager. callback/added/deleted are NOT assigned
    // addresses here (they're set elsewhere, e.g. by the ChainElem ctor or
    // AddToCalcChain). Matching the literal zero-writes.
    ASCII_CALC_CHAIN.callback = 0;
    ASCII_CALC_CHAIN.addedCallback = 0;
    ASCII_CALC_CHAIN.deletedCallback = 0;
    ASCII_CALC_CHAIN.addedCallback = 0;
    ASCII_CALC_CHAIN.deletedCallback = 0;
    ASCII_CALC_CHAIN.arg = mgr;
    if (g_Chain.AddToCalcChain(&ASCII_CALC_CHAIN, TH_CHAIN_PRIO_CALC_ASCIIMANAGER) != 0)
    {
        return ZUN_ERROR;
    }

    ASCII_DRAW_MENUS_CHAIN.callback = 0;
    ASCII_DRAW_MENUS_CHAIN.addedCallback = 0;
    ASCII_DRAW_MENUS_CHAIN.deletedCallback = 0;
    g_Chain.AddToDrawChain(&ASCII_DRAW_MENUS_CHAIN, TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_MENUS);

    ASCII_DRAW_POPUPS_CHAIN.callback = 0;
    ASCII_DRAW_POPUPS_CHAIN.addedCallback = 0;
    ASCII_DRAW_POPUPS_CHAIN.deletedCallback = 0;
    g_Chain.AddToDrawChain(&ASCII_DRAW_POPUPS_CHAIN, TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_POPUPS);

    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::AddedCallback  (FUN_00401d70)  __fastcall, AsciiManager* in ECX
// ===========================================================================
ZunResult AsciiManager::AddedCallback(AsciiManager *s)
{
    memset(s, 0, 0x11194);

    if (ANM_MGR->LoadAnm_44df90(ANM_FILE_ASCII, (char *)0x498a28, 0) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    if (ANM_MGR->LoadAnm_44df90(ANM_FILE_CAPTURE, (char *)0x498a14, ANM_OFFSET_CAPTURE) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    s->InitializeVms();
    s->InitializeMenuVms();
    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::InitializeVms  (FUN_00401a00)  __thiscall
// ===========================================================================
#pragma var_order(base, vm1Ptr, anmMgr, tmpUnk74d4)
void AsciiManager::InitializeVms()
{
    u8 *base = (u8 *)this;

    // 6 REP STOSD memsets (dword counts from orig: 0x93/0x93/0x1800/0x653/0x407/0x1c3e).
    memset(base + 0x24c, 0, 0x93 * 4);    // vm1
    memset(base, 0, 0x93 * 4);             // vm0
    memset(base + 0x14bc, 0, 0x1800 * 4);  // strings
    memset(base + 0x74e8, 0, 0x653 * 4);   // gameMenu
    memset(base + 0x8e34, 0, 0x407 * 4);   // retryMenu
    memset(base + 0xa09c, 0, 0x1c3e * 4);  // popups

    this->numStrings = 0;
    this->isGui = 0;
    this->isSelected = 0;
    this->nextPopupIndex1 = 0;
    this->nextPopupIndex2 = 0;
    *(u32 *)(base + 0x74e4) = 0;
    this->color = 0xffffffff;
    this->scale.x = 1.0f;
    this->scale.y = 1.0f;

    // vm1.flags @ base+0x40c (= vm1+0x1c0) |= 0xc00
    *(u32 *)(base + 0x40c) |= 0xc00;
    u8 *vm1Ptr = base + 0x24c;
    AnmVm_Initialize_4010f0(vm1Ptr);
    ANM_MGR->SetActiveSprite_44e8e0(vm1Ptr, 0);
    AnmVm_Initialize_4010f0(base);
    ANM_MGR->SetActiveSprite_44e8e0(base, 0x20);

    // vm1.pos.z @ base+0x41c (= vm1+0x1d0) = 0.1f
    *(f32 *)(base + 0x41c) = 0.1f;
    this->isSelected = 0;
    this->charWidth = 14;

    // Dead store: copy u16 from unk74d4 into scoreLabelVm+0x172 (base+0x65e),
    // then write unk74d4 back to itself.
    {
        u32 tmpUnk74d4 = *(u32 *)(base + 0x74d4);
        *(u16 *)(base + 0x65e) = (u16)tmpUnk74d4;
        *(u32 *)(base + 0x74d4) = tmpUnk74d4;
    }
}

// ===========================================================================
// AsciiManager::InitializeMenuVms  (FUN_00401ba0)  __thiscall
// ===========================================================================
#pragma var_order(scoreLabelVm, anmMgr, scoreDigitVm, grazeLabelVm, pointLabelVm0, pointLabelVm1, pointLabelVm2, pointLabelVm3)
void AsciiManager::InitializeMenuVms()
{
    u8 *scoreLabelVm = (u8 *)this + 0x498;
    u8 *anmMgr = (u8 *)ANM_MGR;
    *(u16 *)(scoreLabelVm + 0x1d8) = 4;
    ANM_MGR->SetAndExecuteScript_44ea20(scoreLabelVm, *(void **)(anmMgr + 0x28ef0 + 4 * 4));

    u8 *scoreDigitVm = (u8 *)this + 0x6e4;
    *(u16 *)(scoreDigitVm + 0x1d8) = 3;
    ANM_MGR->SetAndExecuteScript_44ea20(scoreDigitVm, *(void **)(anmMgr + 0x28ef0 + 3 * 4));

    u8 *grazeLabelVm = (u8 *)this + 0x930;
    *(u16 *)(grazeLabelVm + 0x1d8) = 5;
    ANM_MGR->SetAndExecuteScript_44ea20(grazeLabelVm, *(void **)(anmMgr + 0x28ef0 + 5 * 4));

    u8 *pointLabelVm0 = (u8 *)this + 0xb7c;
    *(u16 *)(pointLabelVm0 + 0x1d8) = 6;
    ANM_MGR->SetAndExecuteScript_44ea20(pointLabelVm0, *(void **)(anmMgr + 0x28ef0 + 6 * 4));

    u8 *pointLabelVm1 = (u8 *)this + 0xdc8;
    *(u16 *)(pointLabelVm1 + 0x1d8) = 6;
    ANM_MGR->SetAndExecuteScript_44ea20(pointLabelVm1, *(void **)(anmMgr + 0x28ef0 + 6 * 4));

    u8 *pointLabelVm2 = (u8 *)this + 0x1014;
    *(u16 *)(pointLabelVm2 + 0x1d8) = 6;
    ANM_MGR->SetAndExecuteScript_44ea20(pointLabelVm2, *(void **)(anmMgr + 0x28ef0 + 6 * 4));

    u8 *pointLabelVm3 = (u8 *)this + 0x1260;
    *(u16 *)(pointLabelVm3 + 0x1d8) = 6;
    ANM_MGR->SetAndExecuteScript_44ea20(pointLabelVm3, *(void **)(anmMgr + 0x28ef0 + 6 * 4));
}

// ===========================================================================
// AsciiManager::DeletedCallback  (FUN_00401de0)  __fastcall, AsciiManager* in ECX
// ===========================================================================
ZunResult AsciiManager::DeletedCallback(AsciiManager *s)
{
    ANM_MGR->ReleaseAnm_44e4e0(ANM_FILE_ASCII);
    ANM_MGR->ReleaseAnm_44e4e0(ANM_FILE_ASCIIS);
    ANM_MGR->ReleaseAnm_44e4e0(ANM_FILE_CAPTURE);
    ANM_MGR->ReleaseAnm_44e4e0(ANM_FILE_OTHER);
    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::CutChain  (FUN_00401f10)  __cdecl
// ===========================================================================
void AsciiManager::CutChain()
{
    g_Chain.Cut(&ASCII_CALC_CHAIN);
    g_Chain.Cut(&ASCII_DRAW_MENUS_CHAIN);
}

// ===========================================================================
// AsciiManager::AddString  (FUN_00401f40)  __thiscall(position, text)
// ===========================================================================
void AsciiManager::AddString(D3DXVECTOR3 *position, char *text)
{
    // TODO(th07): full port of FUN_00401f40.
    if (this->numStrings >= 0x100)
    {
        return;
    }
    AsciiManagerString *curString = &this->strings[this->numStrings];
    this->numStrings += 1;
    strcpy(curString->text, text);
    curString->position = *position;
    curString->color = this->color;
    curString->scale.x = this->scale.x;
    curString->scale.y = this->scale.y;
    curString->isGui = this->isGui;
    curString->isSelected = this->isSelected;
}

// ===========================================================================
// AsciiManager::AddFormatText  (FUN_00402060)
// ===========================================================================
void AsciiManager::AddFormatText(D3DXVECTOR3 *position, const char *fmt, ...)
{
    char tmpBuffer[512];
    va_list args;

    va_start(args, fmt);
    vsprintf(tmpBuffer, fmt, args);
    AddString(position, tmpBuffer);
    va_end(args);
}

// ===========================================================================
// AsciiManager::DrawStrings  (FUN_004020b0)  __thiscall
// ===========================================================================
#pragma var_order(guiString, stringPtr, i, text, charWidth, thisBase, labelVmBase, labelIdx, diffTmp, absTmp)
void AsciiManager::DrawStrings()
{
    u8 *thisBase = (u8 *)this;
    i32 guiString = 1;
    AsciiManagerString *stringPtr = this->strings;

    // vm0.flags @ this+0x1c0 |= 1 (visible) and |= 0xc00 (anchor=TopLeft)
    *(u32 *)(thisBase + 0x1c0) |= 1;
    *(u32 *)(thisBase + 0x1c0) |= 0xc00;

    for (i32 i = 0; i < this->numStrings; i++, stringPtr++)
    {
        // vm0.pos @ this+0x1c8 = string->position
        *(D3DXVECTOR3 *)(thisBase + 0x1c8) = stringPtr->position;
        // vm0.scale @ this+0x18/0x1c = string->scale
        *(f32 *)(thisBase + 0x18) = stringPtr->scale.x;
        *(f32 *)(thisBase + 0x1c) = stringPtr->scale.y;
        f32 charWidth = (f32)this->charWidth * stringPtr->scale.x;

        if (guiString != (i32)stringPtr->isGui)
        {
            guiString = (i32)stringPtr->isGui;
            ANM_MGR->FlushSprites_44f5c0();
            if (guiString == 0)
            {
                // Full-screen viewport (0,0,640,480)
                *(u32 *)0x575a18 = 0;
                *(u32 *)0x575a1c = 0;
                *(u32 *)0x575a20 = 640;
                *(u32 *)0x575a24 = 480;
                (*(IDirect3DDevice8 **)0x575958)->SetViewport((D3DVIEWPORT8 *)0x575a18);
            }
            else
            {
                // Arcade-region viewport via GetArcadeRegionCoordinate calls
                // (FUN_0048b8a0 returns the coordinate; 4 calls for X/Y/W/H)
                extern i32 GetArcadeRegionCoordinate_48b8a0();
                *(i32 *)0x575a18 = GetArcadeRegionCoordinate_48b8a0();
                *(i32 *)0x575a1c = GetArcadeRegionCoordinate_48b8a0();
                *(i32 *)0x575a20 = GetArcadeRegionCoordinate_48b8a0();
                *(i32 *)0x575a24 = GetArcadeRegionCoordinate_48b8a0();
                (*(IDirect3DDevice8 **)0x575958)->SetViewport((D3DVIEWPORT8 *)0x575a18);
            }
        }

        u8 *text = (u8 *)stringPtr->text;
        while (*text != 0)
        {
            if (*text == '\n')
            {
                *(f32 *)(thisBase + 0x1cc) = *(f32 *)0x498a80 * stringPtr->scale.y + *(f32 *)(thisBase + 0x1cc);
                *(f32 *)(thisBase + 0x1c8) = stringPtr->position.x;
            }
            else if (*text == ' ')
            {
                *(f32 *)(thisBase + 0x1c8) = charWidth + *(f32 *)(thisBase + 0x1c8);
            }
            else
            {
                if (stringPtr->isSelected == 0)
                {
                    *(u32 *)(thisBase + 0x1e4) = (u32)((u8 *)ANM_MGR + 0x60 + (*text - 1) * 0x40);
                    *(u32 *)(thisBase + 0x1b8) = stringPtr->color;
                }
                else
                {
                    *(u32 *)(thisBase + 0x1e4) = (u32)((u8 *)ANM_MGR + 0x60 + (*text + 0x7c) * 0x40);
                    *(u32 *)(thisBase + 0x1b8) = 0xffffffff;
                }
                ANM_MGR->DrawNoRotation_44f770(thisBase);
                *(f32 *)(thisBase + 0x1c8) = charWidth + *(f32 *)(thisBase + 0x1c8);
            }
            text++;
        }
    }

    // Point-label color rendering loop (4 vms @ this+0xb7c + i*0x24c)
    // TODO(th07): point-label color logic from decompile (the second for loop).
}

// ===========================================================================
// AsciiManager::CreatePopup1  (FUN_004024f0)  __thiscall(pos, value, color)
// ===========================================================================
void AsciiManager::CreatePopup1(D3DXVECTOR3 *position, i32 value, D3DCOLOR color)
{
    AsciiManagerPopup *popup;
    i32 characterCount;

    if (this->nextPopupIndex1 >= ASCII_POPUPS_POPUP1_LIMIT)
    {
        this->nextPopupIndex1 = 0;
    }

    popup = &this->popups[this->nextPopupIndex1];
    popup->inUse = 1;
    characterCount = 0;

    if (value >= 0)
    {
        while (value)
        {
            popup->digits[characterCount++] = (char)(value % 10);
            value /= 10;
        }
    }
    else
    {
        popup->digits[characterCount++] = '\n';
    }

    if (characterCount == 0)
    {
        popup->digits[characterCount++] = '\0';
    }

    popup->characterCount = (u8)characterCount;
    popup->color = color;
    popup->current = 0;
    popup->subFrame = 0.0f;
    popup->previous = -999;
    popup->position = *position;
    popup->position.x += POPUP_OFFSET_X;
    popup->position.y += POPUP_OFFSET_Y;

    this->nextPopupIndex1++;
}

// ===========================================================================
// AsciiManager::CreatePopup2  (FUN_00402630)  __thiscall(pos, value, color)
// ===========================================================================
void AsciiManager::CreatePopup2(D3DXVECTOR3 *position, i32 value, D3DCOLOR color)
{
    AsciiManagerPopup *popup;
    i32 characterCount;

    if (this->nextPopupIndex2 >= 3)
    {
        this->nextPopupIndex2 = 0;
    }

    popup = &this->popups[ASCII_POPUPS_POPUP2_BASE + this->nextPopupIndex2];
    popup->inUse = 1;
    characterCount = 0;

    if (value >= 0)
    {
        while (value)
        {
            popup->digits[characterCount++] = (char)(value % 10);
            value /= 10;
        }
    }
    else
    {
        popup->digits[characterCount++] = '\n';
    }

    if (characterCount == 0)
    {
        popup->digits[characterCount++] = '\0';
    }

    popup->characterCount = (u8)characterCount;
    popup->color = color;
    popup->current = 0;
    popup->subFrame = 0.0f;
    popup->previous = -999;
    popup->position = *position;
    popup->position.x += POPUP_OFFSET_X;
    popup->position.y += POPUP_OFFSET_Y;

    this->nextPopupIndex2++;
}

// ===========================================================================
// AsciiManager::DrawPopups  (FUN_00404690)  __thiscall
// ===========================================================================
void AsciiManager::DrawPopups()
{
    // TODO(th07): full port of FUN_00404690 (0x7d9 bytes, score/point/graze
    // counter rendering + popup iteration).
}
}; // namespace th07
