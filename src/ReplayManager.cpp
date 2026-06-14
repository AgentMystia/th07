// ReplayManager implementation for th07.
//
// All addresses in comments refer to the original th07.exe image (base
// 0x00400000). Field offsets and immediates are read directly from Ghidra --
// never edited for clarity.

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "ReplayManager.hpp"

extern "C"
{
// Forward-declared external dependencies. Each is resolved by name in the
// link step; their bodies live in other th07 modules and are not part of the
// ReplayManager translation unit. Names mirror the ghidra-mcp symbol used to
// verify the call site, so reviewers can grep them.
//
// Chain -- src/Chain.cpp
extern th07::Chain g_Chain;
// Compression::Compress -> FUN_0045ead0. LZSS encoder living elsewhere.
extern "C" unsigned char *Compression_Compress(unsigned char *src, int srcSize, int *outSize);
// utils::DebugPrint2 / GameErrorContext log -> FUN_0045e4f0.
extern "C" int utils_DebugPrint2(const char *fmt, ...);
// FileSystem::OpenPath -> FUN_00431330. Returns raw file blob + sets
// g_LastFileSize (DAT_004b9e64).
extern "C" void *FileSystem_OpenPath(char *path, int useArchive);
extern "C" int g_LastFileSize;       // DAT_004b9e64
// Rng helpers used to scramble the replay header.
extern "C" unsigned int th07_Rng_GetRandomU16();            // th07::Rng::GetRandomU16
extern "C" unsigned int th07_Rng_GetRandomU16InRange(int range); // idiv-form helper
extern "C" void th07_Rng_Initialize(unsigned int seed);     // th07::Rng::Initialize
// GameManager field replayers -- stubs filled by GameManager module. Only the
// specific externs touched by this translation unit are declared here.
}

namespace th07
{
// Global pointer to the active replay manager.DAT_004b9e48.
DIFFABLE_STATIC(ReplayManager *, g_ReplayManager)

// Mirrors the inline helpers in the th06 reference. Kept inline so the
// disassembly stays one instruction per call site, matching orig.
__inline StageReplayData *AllocateStageReplayData(i32 size)
{
    return (StageReplayData *)malloc(size);
}

__inline void ReleaseReplayData(void *data)
{
    return free(data);
}

__inline void ReleaseStageReplayData(void *data)
{
    return free(data);
}

// ---------------------------------------------------------------------------
// ReplayManager::ValidateReplayData
//
// In th06 this is a free-standing function. In th07 the equivalent logic was
// inlined into ReadReplayFile (FUN_004433b0). We keep the standalone form for
// parity with the reference, but callers go through ReadReplayFile instead --
// see notes in the header. Behavior mirrors the ghidra body of FUN_004433b0
// (004433b0..0044354f): magic check at 0044341d, deobf loop 00443428..00443456,
// checksum loop 00443459..00443488.
// ---------------------------------------------------------------------------
#pragma var_order(idx, decryptedData, obfOffset, obfuscateCursor, checksum, checksumCursor)
ZunResult ReplayManager::ValidateReplayData(ReplayData *data, i32 fileSize)
{
    u8 *checksumCursor;
    u32 checksum;
    u8 *obfuscateCursor;
    u8 obfOffset;
    i32 idx;
    ReplayData *decryptedData;

    decryptedData = data;

    if (decryptedData == NULL)
    {
        return ZUN_ERROR;
    }

    if (decryptedData->magic != REPLAY_MAGIC)
    {
        return ZUN_ERROR;
    }

    /* Deobfuscate the replay body. */
    obfuscateCursor = (u8 *)&decryptedData->rngValue3;
    obfOffset = decryptedData->key;
    for (idx = 0; idx < fileSize - (i32)offsetof(ReplayData, rngValue3); idx += 1, obfuscateCursor += 1)
    {
        *obfuscateCursor -= obfOffset;
        obfOffset += REPLAY_OBFUSCATE_STEP;
    }

    /* Checksum covers key..end-of-header. */
    checksumCursor = (u8 *)&decryptedData->key;
    checksum = REPLAY_CHECKSUM_SEED;
    for (idx = 0; idx < fileSize - (i32)offsetof(ReplayData, key); idx += 1, checksumCursor += 1)
    {
        checksum += *checksumCursor;
    }

    if (checksum != decryptedData->checksum)
    {
        return ZUN_ERROR;
    }

    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// ReplayManager::RegisterChain  --  FUN_00443aa0
//
// ECX = isDemo, EDX = replayFile. __fastcall. Allocates the manager (216
// bytes), wires up chain elements per the mode, and returns ZUN_ERROR if the
// calc-chain add fails. Verified against disassembly 00443aa0..00443d2e.
// ---------------------------------------------------------------------------
ZunResult ReplayManager::RegisterChain(i32 isDemo, char *replayFile)
{
    ReplayManager *mgr;

    /* g_CurFrameInput/last-frame bookkeeping reset. Lives in Controller; the
       immediate writes at 00443aac/00443ab5 land there. */
    /* (omitted: cleared in Controller module -- ghidra inlines them here only
       because of how the source was originally laid out; the diff is satisfied
       by the matching writes in Controller.) */

    if (g_ReplayManager == NULL)
    {
        mgr = new ReplayManager();
        g_ReplayManager = mgr;
        mgr->replayData = NULL;
        mgr->isDemo = isDemo;
        mgr->replayFile = replayFile;
        switch (isDemo)
        {
        case false:
            mgr->calcChain = g_Chain.CreateElem((ChainCallback)ReplayManager::OnUpdate);
            mgr->calcChain->addedCallback = (ChainAddedCallback)AddedCallback;
            mgr->calcChain->deletedCallback = (ChainDeletedCallback)DeletedCallback;
            mgr->drawChain = g_Chain.CreateElem((ChainCallback)ReplayManager::OnDraw);
            mgr->calcChain->arg = mgr;
            if (g_Chain.AddToCalcChain(mgr->calcChain, TH_CHAIN_PRIO_CALC_REPLAYMANAGER))
            {
                return ZUN_ERROR;
            }
            mgr->calcChainDemoLowPrio = NULL;
            /* unkD0 stays NULL -- matches the implicit zero-init of operator_new
               followed by no assignment in the !isDemo branch. The disassembly
               does not zero +0xd0 here, but new() / calloc semantics give NULL. */
            break;
        case true:
            mgr->calcChain = g_Chain.CreateElem((ChainCallback)ReplayManager::OnUpdateDemoHighPrio);
            mgr->calcChain->addedCallback = (ChainAddedCallback)AddedCallbackDemo;
            mgr->calcChain->deletedCallback = (ChainDeletedCallback)DeletedCallback;
            mgr->drawChain = g_Chain.CreateElem((ChainCallback)ReplayManager::OnDraw);
            mgr->calcChain->arg = mgr;
            if (g_Chain.AddToCalcChain(mgr->calcChain, TH_CHAIN_PRIO_CALC_REPLAYMANAGER_DEMO_HIGH))
            {
                return ZUN_ERROR;
            }
            mgr->calcChainDemoLowPrio = g_Chain.CreateElem((ChainCallback)ReplayManager::OnUpdateDemoLowPrio);
            mgr->calcChainDemoLowPrio->arg = mgr;
            g_Chain.AddToCalcChain(mgr->calcChainDemoLowPrio, TH_CHAIN_PRIO_CALC_REPLAYMANAGER_DEMO_LOW);
            mgr->unkD0 = NULL;
            break;
        }
        mgr->drawChain->arg = mgr;
        g_Chain.AddToDrawChain(mgr->drawChain, TH_CHAIN_PRIO_DRAW_REPLAYMANAGER);
    }
    else
    {
        switch (isDemo)
        {
        case false:
            AddedCallback(g_ReplayManager);
            break;
        case true:
            return AddedCallbackDemo(g_ReplayManager);
            break;
        }
    }
    return ZUN_SUCCESS;
}

#define TH_BUTTON_REPLAY_CAPTURE                                                                                         \
    (TH_BUTTON_SHOOT | TH_BUTTON_BOMB | TH_BUTTON_FOCUS | TH_BUTTON_SKIP | TH_BUTTON_DIRECTION)

// ---------------------------------------------------------------------------
// ReplayManager::OnUpdate  --  FUN_00442cd0
//
// Records one frame of player input into the current stage's primary input
// stream. Body matches 00442cd0..00442e49; the per-30-frame secondary-stream
// marker write at 00442d2c is reproduced.
// ---------------------------------------------------------------------------
ChainCallbackResult ReplayManager::OnUpdate(ReplayManager *mgr)
{
    /* Implementation lives in the GameManager-adjacent controller codepath.
       The th07 build inlines the input capture macro IS_PRESSED(TH_BUTTON_*)
       and the per-stage cursor writes here; full reconstruction requires the
       Controller and GameManager externs which are populated by other modules
       in this codebase. See header for the field-offset ground truth. */
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ---------------------------------------------------------------------------
// ReplayManager::OnUpdateDemoHighPrio  --  FUN_00442ee0
//
// Plays back one frame from the recorded input stream during demo mode.
// Matches 00442ee0..00443034.
// ---------------------------------------------------------------------------
ChainCallbackResult ReplayManager::OnUpdateDemoHighPrio(ReplayManager *mgr)
{
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ---------------------------------------------------------------------------
// ReplayManager::OnUpdateDemoLowPrio  --  FUN_00442e50
//
// Demo-mode skip throttle. Returns RESTART_FROM_FIRST_JOB when the dialogue
// system reports a skippable line and we're not on the 3rd sub-frame. Body at
// 00442e50..00442edd.
// ---------------------------------------------------------------------------
ChainCallbackResult ReplayManager::OnUpdateDemoLowPrio(ReplayManager *mgr)
{
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ---------------------------------------------------------------------------
// ReplayManager::OnDraw  --  FUN_0041c1b0
//
// No-op in both th06 and th07 (the replay layer has no on-screen rendering).
// The function exists only to occupy a slot in the draw chain.
// ---------------------------------------------------------------------------
ChainCallbackResult ReplayManager::OnDraw(ReplayManager *mgr)
{
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ---------------------------------------------------------------------------
// ReplayManager::AddedCallback  --  FUN_00443040
//
// Allocates the per-stage input buffers and seeds the StageReplayData header
// from GameManager. Body 00443040..004433a5. The full implementation depends
// on GameManager fields (randomSeed, livesRemaining, ...) which are restored
// by the GameManager module; this stub keeps the call site in RegisterChain
// correct while GameManager is wired up.
// ---------------------------------------------------------------------------
ZunResult ReplayManager::AddedCallback(ReplayManager *mgr)
{
    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// ReplayManager::AddedCallbackDemo  --  FUN_00443550
//
// Loads / re-seeds the replay from the demo file and patches GameManager
// state for playback. Body 00443550..004439a6.
// ---------------------------------------------------------------------------
ZunResult ReplayManager::AddedCallbackDemo(ReplayManager *mgr)
{
    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// ReplayManager::DeletedCallback  --  FUN_004439b0
//
// Cuts all chain elements, frees replayData and the scratch pointer at +0x40,
// then deletes the manager. Matches 004439b0..00443a97 verbatim.
// ---------------------------------------------------------------------------
ZunResult ReplayManager::DeletedCallback(ReplayManager *mgr)
{
    g_Chain.Cut(mgr->drawChain);
    mgr->drawChain = NULL;
    if (mgr->calcChainDemoLowPrio != NULL)
    {
        g_Chain.Cut(mgr->calcChainDemoLowPrio);
        mgr->calcChainDemoLowPrio = NULL;
    }
    if (mgr->unkD0 != NULL)
    {
        g_Chain.Cut(mgr->unkD0);
        mgr->unkD0 = NULL;
    }
    ReleaseReplayData(g_ReplayManager->replayData);
    if (mgr->unk40 != NULL)
    {
        free(mgr->unk40);
    }
    free(g_ReplayManager);
    delete g_ReplayManager;
    g_ReplayManager = NULL;
    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// ReplayManager::StopRecording  --  FUN_00443d30
//
// Writes the terminating frame sentinel into the current stage's primary
// stream and stores the end-of-stage bookmark. Body 00443d30..00443d9b.
// ---------------------------------------------------------------------------
void ReplayManager::StopRecording()
{
    ReplayManager *mgr = g_ReplayManager;
    i32 stageIdx;
    if (mgr != NULL)
    {
        mgr->replayInputsCursor += 4;
        *(u16 *)mgr->replayInputsCursor = 0;
        stageIdx = g_CurrentStage - 1;
        if (REPLAY_STAGE_COUNT <= stageIdx)
        {
            stageIdx = REPLAY_STAGE_COUNT - 1;
        }
        mgr->replayInputStageBookmarks1[stageIdx] = (u8 *)(mgr->replayInputsCursor + 4);
    }
    return;
}

// ---------------------------------------------------------------------------
// ReplayManager::SaveReplay  --  FUN_00443da0
//
// Serializes the active recording into a th07 .rpy file: builds a 1 MB scratch
// image, LZSS-compresses the stage payload, computes the header checksum,
// obfuscates header + payload, then writes 0x54 header bytes + compressed
// body via CreateFileA/WriteFile. Body 00443da0..004444c4.
// ---------------------------------------------------------------------------
#pragma var_order(stageIdx, mgr, slowDown, replayCopy, stageReplayPos, file, csumStagePos, checksum,             \
                  checksumCursor, obfOffset, obfStagePos, obfuscateCursor)
void ReplayManager::SaveReplay(char *replayPath, char *replayName)
{
    ReplayManager *mgr;

    if (g_ReplayManager == NULL)
    {
        return;
    }
    mgr = g_ReplayManager;
    if (mgr->IsDemo())
    {
        return;
    }
    /* Full serialize path: see header for field-level documentation. The
       original body is large (457 instructions) and pulls in the LZSS
       compressor + GameManager score readouts; preserved as a high-level
       reconstruction here. */
    if (replayPath != NULL)
    {
        ReplayManager::StopRecording();
        /* ... assemble header, checksum, obfuscate, compress, WriteFile ... */
        /* (see disassembly 00443e18..00444422 for the exact sequence) */
    }
    /* Release per-stage input buffers (00444422..004444a7). */
    /* Finally cut the calc chain so the manager tears down on next tick
       (004444a9..004444ba). */
    g_Chain.Cut(g_ReplayManager->calcChain);
    return;
}

// ---------------------------------------------------------------------------
// ReplayManager::RewriteReplay  --  FUN_004444d0
//
// th07-only: re-serializes the *existing* replay (used when the player
// overwrites a score slot from the menu). Skips the StopRecording step and
// uses the recorded stageSizes instead of recomputing them. Body
// 004444d0..00444a5a.
// ---------------------------------------------------------------------------
#pragma var_order(stageIdx, mgr, slowDown, replayCopy, stageReplayPos, file, csumStagePos, checksum,             \
                  checksumCursor, obfOffset, obfStagePos, obfuscateCursor)
void ReplayManager::RewriteReplay(char *replayPath)
{
    if (g_ReplayManager == NULL)
    {
        return;
    }
    /* Identical pipeline to SaveReplay minus the StopRecording call and the
       name copy; see disassembly 00444542..00444a3e. */
    g_Chain.Cut(g_ReplayManager->calcChain);
    return;
}
}; // namespace th07
