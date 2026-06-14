// th07 SoundPlayer: DirectSound streaming-BGM + SFX engine.
//
// Compared to th06 the module was thoroughly rewritten:
//
//   * The SoundPlayer struct ballooned from 0x638 bytes to 0x17e560 bytes.
//     Instead of one streaming "backgroundMusic" CStreamingSound* it owns a
//     pool of 16 BGM slots, a 0x1f-entry SoundQueue, and a 0x80-entry duplicate
//     SFX pool driven by g_SoundBufferIdxVol.
//   * BGM playback is no longer inline in LoadWav/PlayBGM.  A state machine in
//     ProcessSoundQueues walks the queue, advancing each entry through PreLoad
//     / Load / Stop / Reset / Recreate / Fill Buffer / Play / ReOpen / FadeOut
//     / Thread Stop / Handle Close stages, all on the main thread instead of
//     inside the worker thread.
//   * SFX are loaded from data/wav/se_*.wav (renamed from th06's plain names)
//     and driven by g_SoundBufferIdxVol + g_SFXList (kept in the binary at
//     0x0049ea88 / 0x0049ebb8 with 0x1e entries each).
//
// All field offsets below were read directly from th07.exe via Ghidra.  The
// global instance lives at DAT_004b9e44 (allocated with operator
// new(0x17e560) in FUN_00434020 @ 0x0043429b).  The worker thread uses
// DAT_004bda94 (== &g_SoundPlayer.backgroundMusic) and DAT_004bda98 (==
// &g_SoundPlayer.backgroundMusicUpdateEvent) to reach the live stream.
#pragma once

#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "zwave.hpp"

#include <dsound.h>
#include <windows.h>

namespace th07
{
// SFX volume table.  Each entry binds a SoundIdx to a wav index in g_SFXList
// plus a per-entry DirectSound volume (hundredths of a dB) and a play-count
// "unk" slot.  Same layout as th06's SoundBufferIdxVolume.  Size 0x8.
struct SoundBufferIdxVolume
{
    i32 bufferIdx; // 0x00 — indexes g_SFXList
    i16 volume;    // 0x04 — IDirectSoundBuffer::SetVolume argument
    i16 unk;       // 0x06 — play counter reset to zero before each playback
};
ZUN_ASSERT_SIZE(SoundBufferIdxVolume, 0x8);

// Pending BGM / SFX stage record enqueued by SoundQueueAdd
// (FUN_0044d2f0 @ 0x0044d2f0).  Each entry is 0x10c bytes and lives inside
// g_SoundPlayer.soundQueue at offset 0x73c (0x1f slots, ending around 0x2798).
//
//   +0x00 command  (1=PreLoad, 2=Load+play, 3=Stop, 4=ThreadStop, 5=FadeOut,
//                    6=RestoreFromLock, 7=ResetStop)
//   +0x04 param1   (slot index for BGM commands; fade-out frames for 5)
//   +0x08 stage    (state-machine step counter; incremented per visit)
//   +0x0c..0x10c name (NUL-terminated BGM file name; copied by SoundQueueAdd)
struct SoundQueueEntry
{
    i32 command;     // +0x00
    i32 param1;      // +0x04
    i32 stage;       // +0x08
    i8 name[0x100];  // +0x0c
};
ZUN_ASSERT_SIZE(SoundQueueEntry, 0x10c);

// SoundPlayer is a single huge object (0x17e560 bytes, allocated by the
// supervisor in FUN_00434020).  The original layout does not use a
// StreamingBgmSlot sub-struct; instead it keeps the 16 per-slot fields as
// parallel arrays.  Every offset below was read from the binary:
//
//   +0x000 dsoundHdl                  (LPDIRECTSOUND)         DAT_004b9e44
//   +0x004 unk4
//   +0x008 soundBuffers[0x80]         (LPDIRECTSOUNDBUFFER)   read by Release
//   +0x208 duplicateSoundBuffers[0x80](LPDIRECTSOUNDBUFFER)   read by Release
//   +0x408 soundBufferSlotState[0x80] (i32, init -1 in ctor)  read by PlaySounds
//   +0x608 initSoundBuffer            (LPDIRECTSOUNDBUFFER)   primary buffer
//   +0x60c gameWindow                 (HWND)
//   +0x610 manager                    (CSoundManager*)        DAT_004b9e54
//   +0x614 backgroundMusicThreadId    (DWORD)
//   +0x618 backgroundMusicThreadHandle(HANDLE)
//   +0x61c soundBuffersToPlay2[5]     (i32, init -1)
//   +0x620 soundBuffersToPlay[5]      (i32, init -1)
//   +0x634 wavFmtEntry[0x10]          (WaveFormat*[]; per BGM slot)
//   +0x674 wavData[0x10]              (void*[]; malloc'd PCM from thbgm.dat)
//   +0x6b4 wavDataCur[0x10]           (void*[]; read cursor)
//   +0x6f4 dataSize[0x10]             (i32[]; bytes of PCM available)
//   +0x738 bgmFmtTable                (void*; allocated from bgm/thbgm.fmt)
//   +0x73c soundQueue[0x1f]           (SoundQueueEntry[]; 0x1f*0x10c bytes)
//   +0x28bc bgmSlotName[0x10]         (char[0x100][]; per BGM slot)
//   +0x38bc thbgmDatPath              (char[0x...]; "thbgm.dat" copy)
//   +0x39bc backgroundMusic           (CStreamingSound*)      DAT_004bda94
//   +0x39c0 backgroundMusicUpdateEvent(HANDLE)               DAT_004bda98
//
// To preserve every offset byte-for-byte we model the object as one giant
// packed byte buffer with named accessor helpers rather than a C struct.
struct SoundPlayer
{
    SoundPlayer();

    ZunResult InitializeDSound(HWND gameWindow);
    ZunResult Release();
    ZunResult InitSoundBuffers();
    ZunResult LoadSound(i32 idx, char *path);
    static WAVEFORMATEX *GetWavFormatData(u8 *soundData, char *formatString, i32 *formatSize,
                                          u32 fileSizeExcludingFormat);

    ZunResult LoadBgmPath(char *path);
    ZunResult LoadStreamingBgm(i32 slotIdx);
    ZunResult ReopenBgm(i32 slotIdx);
    void StopBGM();
    ZunResult PreLoadBgm(i32 slotIdx, char *name);
    ZunResult LoadBgmFmtFile(char *path);

    void ProcessSoundQueues();
    void SoundQueueAdd(i32 command, i32 param1, char *name);
    void FadeOutBgm(f32 fadeOutSeconds);

    static DWORD __stdcall BackgroundMusicPlayerThread(LPVOID lpThreadParameter);

    // ---- Field accessors (offsets are absolute from `this`) ----
    LPDIRECTSOUND &dsoundHdl()
    {
        return *(LPDIRECTSOUND *)(&raw[0x000]);
    }
    LPDIRECTSOUNDBUFFER *&soundBuffers(i32 idx)
    {
        return ((LPDIRECTSOUNDBUFFER *)&raw[0x008])[idx];
    }
    LPDIRECTSOUNDBUFFER *&duplicateSoundBuffers(i32 idx)
    {
        return ((LPDIRECTSOUNDBUFFER *)&raw[0x208])[idx];
    }
    i32 &soundBufferSlotState(i32 idx)
    {
        return ((i32 *)&raw[0x408])[idx];
    }
    LPDIRECTSOUNDBUFFER &initSoundBuffer()
    {
        return *(LPDIRECTSOUNDBUFFER *)&raw[0x608];
    }
    HWND &gameWindow()
    {
        return *(HWND *)&raw[0x60c];
    }
    CSoundManager *&manager()
    {
        return *(CSoundManager **)&raw[0x610];
    }
    DWORD &backgroundMusicThreadId()
    {
        return *(DWORD *)&raw[0x614];
    }
    HANDLE &backgroundMusicThreadHandle()
    {
        return *(HANDLE *)&raw[0x618];
    }
    i32 &soundBuffersToPlay(i32 idx)
    {
        return ((i32 *)&raw[0x620])[idx];
    }
    WaveFormat *&wavFmtEntry(i32 slotIdx)
    {
        return ((WaveFormat **)&raw[0x634])[slotIdx];
    }
    void *&wavData(i32 slotIdx)
    {
        return ((void **)&raw[0x674])[slotIdx];
    }
    void *&wavDataCur(i32 slotIdx)
    {
        return ((void **)&raw[0x6b4])[slotIdx];
    }
    i32 &wavDataSize(i32 slotIdx)
    {
        return ((i32 *)&raw[0x6f4])[slotIdx];
    }
    void *&bgmFmtTable()
    {
        return *(void **)&raw[0x738];
    }
    SoundQueueEntry *soundQueue()
    {
        return (SoundQueueEntry *)&raw[0x73c];
    }
    i8 *bgmSlotName(i32 slotIdx)
    {
        return (i8 *)&raw[0x28bc + slotIdx * 0x100];
    }
    i8 *thbgmDatPath()
    {
        return (i8 *)&raw[0x38bc];
    }
    CStreamingSound *&backgroundMusic()
    {
        return *(CStreamingSound **)&raw[0x39bc];
    }
    HANDLE &backgroundMusicUpdateEvent()
    {
        return *(HANDLE *)&raw[0x39c0];
    }

    // The full 0x17e560-byte allocation backing every field above.
    u8 raw[0x17e560];
};
ZUN_ASSERT_SIZE(SoundPlayer, 0x17e560);

DIFFABLE_EXTERN(SoundBufferIdxVolume, g_SoundBufferIdxVol[0x1e])
DIFFABLE_EXTERN(char *, g_SFXList[0x1e])
DIFFABLE_EXTERN(SoundPlayer, g_SoundPlayer)
}; // namespace th07
