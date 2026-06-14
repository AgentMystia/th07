// AsciiManager module for th07 (Perfect Cherry Blossom).
//
// This is a heavily reworked version of the th06 AsciiManager. See
// AsciiManager.hpp for the structural differences. All code below was
// written by reading the th07.exe disassembly in ghidra and mirroring the
// instruction / data-flow patterns observed there.

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

// ---------------------------------------------------------------------------
// External dependencies (defined in other modules).
//
// These mirror the calling conventions / addresses observed in th07.exe.
// They are declared extern here so this translation unit links against the
// real implementations once those modules land; until then they resolve
// through stubs.cpp.
// ---------------------------------------------------------------------------

// AnmManager — g_AnmManager is a *pointer* stored at 0x004b9e44. We model
// it as a void* extern so this TU does not need AnmManager.hpp yet.
extern "C" void *g_AnmManagerPtr;

// AnmManager methods (__thiscall, first explicit arg = anmManager).
extern ZunResult __fastcall AnmManager_LoadAnm(void *anmManager, i32 idx, const char *path, i32 offset);
extern void __fastcall AnmManager_ReleaseAnm(void *anmManager, i32 idx);
extern void __fastcall AnmManager_SetActiveSprite(void *anmManager, void *vm, i32 spriteIdx);
extern void __fastcall AnmManager_DrawNoRotation(void *anmManager, void *vm);
extern void __fastcall AnmManager_SetAndExecuteScriptIdx(void *anmManager, void *vm, i32 scriptIdx);
extern void __fastcall AnmManager_ExecuteScript(void *anmManager, void *vm);

// AnmVm helpers.
extern void __fastcall AnmVm_Initialize(void *vm);

// Supervisor globals.
extern "C" u8 g_SupervisorCfgFlags;                    // 0x00575a9c
extern "C" f32 g_SupervisorEffectiveFramerateMultiplier; // 0x00575ac8

// Chain — g_Chain lives at 0x00626218 and is declared extern in Chain.hpp.

// GameManager globals used by the menus.
extern "C" u8 g_GameManagerIsInGameMenu;  // DAT_0062f64c
extern "C" u8 g_GameManagerIsInRetryMenu; // DAT_0062f64d
extern "C" u32 g_GameManagerFlags;        // DAT_0062f648

// Popup position offsets applied by CreatePopup1/2.
extern "C" f32 g_PopupOffsetX; // DAT_0062f864
extern "C" f32 g_PopupOffsetY; // DAT_0062f868

// Supervisor::TickTimer(i32 *current, f32 *subFrame) — used by the popup
// update loop (mirrors ZunTimer::Tick).
extern void __fastcall Supervisor_TickTimer(i32 *current, f32 *subFrame);

// th07 ANM file indices used by AsciiManager.
#define ANM_FILE_ASCII   1
#define ANM_FILE_ASCIIS  2 // released by DeletedCallback; loaded elsewhere
#define ANM_FILE_OTHER   3 // released by DeletedCallback; loaded elsewhere
#define ANM_FILE_CAPTURE 4 // note: th07 renumbered capture.anm to 4

#define ANM_OFFSET_CAPTURE 0x724

// Chain priorities used by th07's AsciiManager.
#define TH_CHAIN_PRIO_CALC_ASCIIMANAGER           1
#define TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_MENUS     0x10
#define TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_POPUPS    0x0b

// Number of popups th07 allocates (OnUpdate iterates 0x2d3 = 723 entries).
#define ASCII_POPUPS_COUNT       0x2d3
#define ASCII_POPUPS_POPUP1_LIMIT 0x2cf
#define ASCII_POPUPS_POPUP2_BASE  0x2d0

// g_SupervisorCfgFlags bit tests used by AddString / DrawPopups.
#define CFG_IS_SOFTWARE_TEXTURING                                                                                         \
    ((g_SupervisorCfgFlags >> 8 & 1) == 0 && (g_SupervisorCfgFlags & 1) == 0)

// Convenience macro for poking a typed value into one of the opaque AnmVm
// byte buffers at a fixed byte offset.
#define VM_FIELD(vmBuf, off, type) (*(type *)((u8 *)(vmBuf) + (off)))

// ===========================================================================
// AsciiManager::AsciiManager  (FUN_004014a0)
// ===========================================================================
AsciiManager::AsciiManager()
{
    // FUN_004014a0 primes every embedded AnmVm (vm0/vm1/score/graze/point
    // labels, the 10 game-menu sprites, the 5 retry-menu sprites, the
    // screenshake vm) via AnmVm::Initialize, then zeroes the strings
    // array and primes the popups to { previous=-999, subFrame=0,
    // current=0, inUse=0 }.
    AsciiManagerPopup *p = this->popups;
    for (i32 i = 0; i < ASCII_POPUPS_COUNT; i++)
    {
        p->previous = -999;
        p->subFrame = 0.0f;
        p->current = 0;
        p++;
    }
}

StageMenu::StageMenu()
{
}

// ===========================================================================
// AsciiManager::OnUpdate  (FUN_004017e0)
// ===========================================================================
ChainCallbackResult AsciiManager::OnUpdate(AsciiManager *mgr)
{
    if (!g_GameManagerIsInGameMenu && !g_GameManagerIsInRetryMenu)
    {
        AsciiManagerPopup *curPopup = &mgr->popups[0];
        for (i32 i = 0; i < ASCII_POPUPS_COUNT; i++)
        {
            if (curPopup->inUse)
            {
                curPopup->position.y -= 0.5f * g_SupervisorEffectiveFramerateMultiplier;
                curPopup->previous = curPopup->current;
                Supervisor_TickTimer(&curPopup->current, &curPopup->subFrame);
                if (curPopup->current > 60)
                {
                    curPopup->inUse = 0;
                }
            }
            curPopup++;
        }
    }
    else if (g_GameManagerIsInGameMenu)
    {
        // mgr->gameMenu.OnUpdateGameMenu();
        // (delegates into the game-menu state machine at mgr+0x74e8)
        // TODO(th07): StageMenu::OnUpdateGameMenu(mgr)
    }
    if (g_GameManagerIsInRetryMenu)
    {
        // mgr->retryMenu.OnUpdateRetryMenu();
        // TODO(th07): StageMenu::OnUpdateRetryMenu(mgr)
    }

    // AnmVm::Execute2 on the seven label Vms (FUN_00401400 pattern).
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->scoreLabelVm);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->scoreDigitVm);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->grazeLabelVm);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->pointLabelVm[0]);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->pointLabelVm[1]);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->pointLabelVm[2]);
    AnmManager_ExecuteScript(g_AnmManagerPtr, &mgr->pointLabelVm[3]);

    // Screenshake indicator handling (reads the pendingInterrupt word at
    // AsciiManager + 0xa028, which lives inside retryMenu).
    i16 *screenshakeInterrupt = (i16 *)((u8 *)mgr + 0xa028);
    if ((g_GameManagerFlags >> 1 & 1) == 0)
    {
        *screenshakeInterrupt = 0;
    }
    else
    {
        if (*screenshakeInterrupt == 0)
        {
            *screenshakeInterrupt = 7;
            AnmManager_SetAndExecuteScriptIdx(
                g_AnmManagerPtr, (u8 *)mgr + 0x9e50,
                *(i32 *)((u8 *)g_AnmManagerPtr + 0x28f0c));
        }
        AnmManager_ExecuteScript(g_AnmManagerPtr, (u8 *)mgr + 0x9e50);
    }

    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::OnDrawMenus  (FUN_00401970)
// ===========================================================================
ChainCallbackResult AsciiManager::OnDrawMenus(AsciiManager *mgr)
{
    mgr->DrawStrings();
    mgr->numStrings = 0;
    // mgr->gameMenu.OnDrawGameMenu();
    // mgr->retryMenu.OnDrawRetryMenu();
    // TODO(th07): StageMenu draw methods (FUN_00403a20 / FUN_00404560).
    if (*(i16 *)((u8 *)mgr + 0xa028) != 0)
    {
        AnmManager_DrawNoRotation(g_AnmManagerPtr, (u8 *)mgr + 0x9e50);
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::OnDrawPopups  (FUN_004019e0)
// ===========================================================================
ChainCallbackResult AsciiManager::OnDrawPopups(AsciiManager *mgr)
{
    mgr->DrawPopups();
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ===========================================================================
// AsciiManager::RegisterChain  (FUN_00401e30)
// ===========================================================================
ZunResult AsciiManager::RegisterChain()
{
    AsciiManager *mgr = &g_AsciiManager;

    g_AsciiManagerCalcChain.callback = (ChainCallback)AsciiManager::OnUpdate;
    g_AsciiManagerCalcChain.addedCallback = NULL;
    g_AsciiManagerCalcChain.deletedCallback = NULL;
    g_AsciiManagerCalcChain.addedCallback = (ChainAddedCallback)AsciiManager::AddedCallback;
    g_AsciiManagerCalcChain.deletedCallback = (ChainDeletedCallback)AsciiManager::DeletedCallback;
    g_AsciiManagerCalcChain.arg = mgr;
    if (g_Chain.AddToCalcChain(&g_AsciiManagerCalcChain, TH_CHAIN_PRIO_CALC_ASCIIMANAGER) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    g_AsciiManagerOnDrawMenusChain.callback = (ChainCallback)OnDrawMenus;
    g_AsciiManagerOnDrawMenusChain.addedCallback = NULL;
    g_AsciiManagerOnDrawMenusChain.deletedCallback = NULL;
    g_AsciiManagerOnDrawMenusChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_AsciiManagerOnDrawMenusChain, TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_MENUS);

    g_AsciiManagerOnDrawPopupsChain.callback = (ChainCallback)OnDrawPopups;
    g_AsciiManagerOnDrawPopupsChain.addedCallback = NULL;
    g_AsciiManagerOnDrawPopupsChain.deletedCallback = NULL;
    g_AsciiManagerOnDrawPopupsChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_AsciiManagerOnDrawPopupsChain, TH_CHAIN_PRIO_DRAW_ASCIIMANAGER_POPUPS);

    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::AddedCallback  (FUN_00401d70)
// ===========================================================================
ZunResult AsciiManager::AddedCallback(AsciiManager *s)
{
    memset(s, 0, 0x11194);

    if (AnmManager_LoadAnm(g_AnmManagerPtr, ANM_FILE_ASCII, "data/ascii.anm", 0) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    if (AnmManager_LoadAnm(g_AnmManagerPtr, ANM_FILE_CAPTURE, "data/capture.anm", ANM_OFFSET_CAPTURE) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    s->InitializeVms();
    s->InitializeMenuVms();
    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::InitializeVms  (FUN_00401a00)
// ===========================================================================
#pragma var_order(vm1, mgr1, mgr0)
void AsciiManager::InitializeVms()
{
    // FUN_00401a00 begins with six memset loops that zero specific
    // sub-regions of the struct. The byte offsets / sizes below mirror
    // those STOSD loops exactly so objdiff lines up.
    u8 *base = (u8 *)this;
    memset(base + 0x24c, 0, 0x24c);              // vm1
    memset(base + 0x000, 0, 0x24c);              // vm0
    memset(base + 0x14bc, 0, 0x6000);            // strings[256]
    memset(base + 0x74e8, 0, 0x194c);            // gameMenu
    memset(base + 0x8e34, 0, 0x101c);            // retryMenu (head only)
    memset(base + 0xa09c, 0, 0x70f8);            // popups (head only)

    this->nextPopupIndex1 = 0;
    this->nextPopupIndex2 = 0;
    this->isSelected = 0;
    this->charWidth = 0;
    this->numStrings = 0;
    this->isGui = 0;
    this->unk74d4 = 0;
    this->color = 0xffffffff;
    this->scale.x = 1.0f;
    this->scale.y = 1.0f;

    // vm1.flags.anchor = TopLeft (OR 0xc00 into the flags dword at vm1+0x1c0)
    VM_FIELD(&this->vm1, 0x1c0, u32) |= 0xc00;
    AnmVm_Initialize(&this->vm1);
    AnmManager_SetActiveSprite(g_AnmManagerPtr, &this->vm1, 0);

    AnmVm_Initialize(&this->vm0);
    AnmManager_SetActiveSprite(g_AnmManagerPtr, &this->vm0, 0x20);

    VM_FIELD(&this->vm1, 0x1d0, f32) = 0.1f; // vm1.pos.z = 0.1
    this->isSelected = 0;
    this->charWidth = 14;

    // Trailing dead store observed in FUN_00401a00: copies a u16 out of
    // unk74d4 into a byte inside scoreLabelVm, then writes unk74d4 back
    // to itself. Kept here for instruction-sequence fidelity.
    {
        u16 tmp = *(u16 *)((u8 *)this + 0x74d4);
        *(u16 *)((u8 *)this + 0x65e) = tmp;
        *(u32 *)((u8 *)this + 0x74d4) = this->unk74d4;
    }
}

// ===========================================================================
// AsciiManager::InitializeMenuVms  (FUN_00401ba0)
// ===========================================================================
void AsciiManager::InitializeMenuVms()
{
    void *mgr = g_AnmManagerPtr;
    i32 *spriteTable = (i32 *)((u8 *)mgr + 0x28ef0);

    VM_FIELD(&this->scoreLabelVm, 0x1d8, u16) = 4;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->scoreLabelVm, spriteTable[4]);
    VM_FIELD(&this->scoreDigitVm, 0x1d8, u16) = 3;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->scoreDigitVm, spriteTable[3]);
    VM_FIELD(&this->grazeLabelVm, 0x1d8, u16) = 5;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->grazeLabelVm, spriteTable[5]);
    VM_FIELD(&this->pointLabelVm[0], 0x1d8, u16) = 6;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->pointLabelVm[0], spriteTable[6]);
    VM_FIELD(&this->pointLabelVm[1], 0x1d8, u16) = 6;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->pointLabelVm[1], spriteTable[6]);
    VM_FIELD(&this->pointLabelVm[2], 0x1d8, u16) = 6;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->pointLabelVm[2], spriteTable[6]);
    VM_FIELD(&this->pointLabelVm[3], 0x1d8, u16) = 6;
    AnmManager_SetAndExecuteScriptIdx(mgr, &this->pointLabelVm[3], spriteTable[6]);
}

// ===========================================================================
// AsciiManager::DeletedCallback  (FUN_00401de0)
// ===========================================================================
ZunResult AsciiManager::DeletedCallback(AsciiManager *s)
{
    AnmManager_ReleaseAnm(g_AnmManagerPtr, ANM_FILE_ASCII);
    AnmManager_ReleaseAnm(g_AnmManagerPtr, ANM_FILE_ASCIIS);
    AnmManager_ReleaseAnm(g_AnmManagerPtr, ANM_FILE_CAPTURE);
    AnmManager_ReleaseAnm(g_AnmManagerPtr, ANM_FILE_OTHER);
    return ZUN_SUCCESS;
}

// ===========================================================================
// AsciiManager::CutChain  (FUN_00401f10)
// ===========================================================================
void AsciiManager::CutChain()
{
    g_Chain.Cut(&g_AsciiManagerCalcChain);
    g_Chain.Cut(&g_AsciiManagerOnDrawMenusChain);
}

// ===========================================================================
// AsciiManager::AddString  (FUN_00401f40)  __thiscall(position, text)
// ===========================================================================
void AsciiManager::AddString(D3DXVECTOR3 *position, char *text)
{
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
    if (CFG_IS_SOFTWARE_TEXTURING)
    {
        curString->isSelected = 0;
    }
    else
    {
        curString->isSelected = this->isSelected;
    }
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
// AsciiManager::DrawStrings  (FUN_004020b0)
// ===========================================================================
#pragma var_order(charWidth, i, string, text, guiString, padding_1, padding_2, padding_3)
void AsciiManager::DrawStrings()
{
    i32 padding_1;
    i32 padding_2;
    i32 padding_3;
    i32 i;
    BOOL guiString;
    f32 charWidth;
    AsciiManagerString *string;
    u8 *text;

    guiString = TRUE;
    string = this->strings;
    // vm0.flags.isVisible |= 1; vm0.flags.anchor = TopLeft (OR 0xc00)
    VM_FIELD(&this->vm0, 0x1c0, u32) |= 1;
    VM_FIELD(&this->vm0, 0x1c0, u32) |= 0xc00;
    for (i = 0; i < this->numStrings; i++, string++)
    {
        // vm0.pos = string->position (vm0.pos is at vm0 + 0x1c8)
        VM_FIELD(&this->vm0, 0x1c8, D3DXVECTOR3) = string->position;
        text = (u8 *)string->text;
        VM_FIELD(&this->vm0, 0x18, f32) = string->scale.x;  // vm0.scaleX
        VM_FIELD(&this->vm0, 0x1c, f32) = string->scale.y;  // vm0.scaleY
        charWidth = (f32)this->charWidth * string->scale.x;
        if (guiString != string->isGui)
        {
            guiString = string->isGui;
            if (guiString)
            {
                // Switch to the arcade-region viewport. In the binary this
                // is four consecutive calls to a GameManager coordinate
                // getter (FUN_0048b8a0) followed by IDirect3DDevice8::
                // SetViewport on the Supervisor's viewport struct at
                // 0x00575a18 via the device pointer at 0x00575958.
                extern i32 __fastcall GetArcadeRegionCoordinate(void);
                extern void __fastcall Supervisor_SetViewport(i32 x, i32 y, i32 w, i32 h);
                Supervisor_SetViewport(GetArcadeRegionCoordinate(),
                                       GetArcadeRegionCoordinate(),
                                       GetArcadeRegionCoordinate(),
                                       GetArcadeRegionCoordinate());
            }
            else
            {
                extern void __fastcall Supervisor_SetViewport(i32 x, i32 y, i32 w, i32 h);
                Supervisor_SetViewport(0, 0, 640, 480);
            }
        }
        while (*text != 0)
        {
            if (*text == '\n')
            {
                VM_FIELD(&this->vm0, 0x1cc, f32) =
                    16.0f * string->scale.y + VM_FIELD(&this->vm0, 0x1cc, f32);
                VM_FIELD(&this->vm0, 0x1c8, f32) = string->position.x;
            }
            else if (*text == ' ')
            {
                VM_FIELD(&this->vm0, 0x1c8, f32) += charWidth;
            }
            else
            {
                if (string->isSelected == FALSE)
                {
                    VM_FIELD(&this->vm0, 0x1e4, void *) =
                        (u8 *)g_AnmManagerPtr + 0x60 + (*text - 1) * 0x40;
                    VM_FIELD(&this->vm0, 0x1b8, D3DCOLOR) = string->color;
                }
                else
                {
                    VM_FIELD(&this->vm0, 0x1e4, void *) =
                        (u8 *)g_AnmManagerPtr + 0x60 + (*text + 0x7c) * 0x40;
                    VM_FIELD(&this->vm0, 0x1b8, D3DCOLOR) = 0xFFFFFFFF;
                }
                AnmManager_DrawNoRotation(g_AnmManagerPtr, &this->vm0);
                VM_FIELD(&this->vm0, 0x1c8, f32) += charWidth;
            }
            text++;
        }
    }
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
    popup->position.x += g_PopupOffsetX;
    popup->position.y += g_PopupOffsetY;

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
    popup->position.x += g_PopupOffsetX;
    popup->position.y += g_PopupOffsetY;

    this->nextPopupIndex2++;
}

// ===========================================================================
// AsciiManager::DrawPopups  (FUN_00404690)
//
// This is a substantial function: besides drawing the damage/score popups
// (the th06 remit) it now also renders the in-game score / point-item /
// graze counters by driving scoreLabelVm / scoreDigitVm / grazeLabelVm /
// pointLabelVm directly. A full faithful port requires the GuiManager and
// GameManager field offsets; until those land this stub preserves the
// popup-iteration shape so the surrounding state stays consistent.
// ===========================================================================
void AsciiManager::DrawPopups()
{
    // TODO(th07): full port of FUN_00404690. See AsciiManager.hpp for the
    // breakdown of what this function does in the binary.
    AsciiManagerPopup *curPopup = this->popups;
    for (i32 i = 0; i < ASCII_POPUPS_COUNT; i++, curPopup++)
    {
        if (!curPopup->inUse)
        {
            continue;
        }
    }
}
}; // namespace th07
