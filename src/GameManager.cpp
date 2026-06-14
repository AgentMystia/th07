// th07::GameManager — in-game state manager (largest module).
// See GameManager.hpp for the full address map and struct notes.
//
// Implemented here (first pass): RegisterChain, CutChain, OnDraw.
// TODO (later passes): OnUpdate, AddedCallback, DeletedCallback, OnItemUpdate,
// CalculateChecksum, IsGameActive — these need more of the struct reversed.

#include "GameManager.hpp"

namespace th07
{
DIFFABLE_STATIC(GameManager, g_GameManager)

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

} // namespace th07
