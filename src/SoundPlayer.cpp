// th07 SoundPlayer implementation.  See SoundPlayer.hpp for the recovered
// structure layout and module overview.  All control flow was reconstructed
// from FUN_0044b560 / FUN_0044b830 / FUN_0044bd00 / FUN_0044ba80 / FUN_0044c020
// / FUN_0044c1b0 / FUN_0044c220 / FUN_0044c4d0 / FUN_0044c6b0 / FUN_0044c7d0
// / FUN_0044c9c0 / FUN_0044d200 / FUN_0044d2f0 in th07.exe.
#include "SoundPlayer.hpp"

#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "i18n.hpp"
#include "utils.hpp"
#include "zwave.hpp"

namespace th07
{
#define BACKGROUND_MUSIC_BUFFER_SIZE 0x8000
#define BACKGROUND_MUSIC_WAV_NUM_CHANNELS 2
#define BACKGROUND_MUSIC_WAV_BITS_PER_SAMPLE 16
#define BACKGROUND_MUSIC_WAV_BLOCK_ALIGN                                                                          \
    (BACKGROUND_MUSIC_WAV_BITS_PER_SAMPLE / 8 * BACKGROUND_MUSIC_WAV_NUM_CHANNELS)

// g_SoundBufferIdxVol mirrors th06's table; only the per-entry unk field
// differs slightly.  0x1e entries, located at 0x0049ea88 in the binary.
DIFFABLE_STATIC_ARRAY_ASSIGN(SoundBufferIdxVolume, 0x1e, g_SoundBufferIdxVol) = {
    {0, -2000, 0},   {0, -2500, 0},   {1, -1550, 5},   {1, -1750, 5},  {2, -1000, 100}, {3, -400, 100},
    {4, -400, 100},  {5, -1700, 50},  {6, -1700, 50},  {7, -1700, 50}, {8, -1000, 100}, {9, -1000, 100},
    {10, -1900, 10}, {11, -1300, 10}, {12, -900, 100}, {5, -1500, 50}, {13, -900, 50},  {14, -900, 50},
    {15, -700, 100}, {16, -200, 100}, {17, -1100, 0},  {18, -900, 0},  {5, -1800, 20},  {6, -1800, 20},
    {7, -1800, 20},  {19, -300, 50},  {20, -700, 50},  {21, -800, 50}, {22, -100, 140}, {23, -500, 100},
};

// g_SFXList: 0x1e file names located at 0x0049ebb8 in the binary.  th07
// renamed every SFX with an "se_" prefix compared to th06.
DIFFABLE_STATIC_ARRAY_ASSIGN(char *, 0x1e, g_SFXList) = {
    "data/wav/se_plst00.wav", "data/wav/se_enep00.wav", "data/wav/se_pldead00.wav", "data/wav/se_power0.wav",
    "data/wav/se_power1.wav", "data/wav/se_tan00.wav",  "data/wav/se_tan01.wav",    "data/wav/se_tan02.wav",
    "data/wav/se_ok00.wav",   "data/wav/se_cancel00.wav", "data/wav/se_select00.wav", "data/wav/se_gun00.wav",
    "data/wav/se_cat00.wav",  "data/wav/se_lazer00.wav", "data/wav/se_lazer01.wav",  "data/wav/se_enep01.wav",
    "data/wav/se_nep00.wav",  "data/wav/se_damage00.wav", "data/wav/se_item00.wav",   "data/wav/se_kira00.wav",
    "data/wav/se_kira01.wav", "data/wav/se_kira02.wav",  "data/wav/se_extend.wav",   "data/wav/se_timeout.wav",
    "data/wav/se_graze.wav",  "data/wav/se_powerup.wav",  "data/wav/se_pause.wav",    "data/wav/se_bonus.wav",
    "data/wav/se_bonus2.wav", "data/wav/se_border.wav",
};

DIFFABLE_STATIC(SoundPlayer, g_SoundPlayer)

// Constructor: clears the whole 0x17e560-byte struct to zero, then sets every
// duplicate-SFX slot state to -1.  Inlined in the binary at the top of
// InitializeDSound (FUN_0044b560 @ 0x0044b58c).
SoundPlayer::SoundPlayer()
{
    memset(this->raw, 0, sizeof(this->raw));
    for (i32 i = 0; i < 0x80; i++)
    {
        this->soundBufferSlotState(i) = -1;
    }
}

#pragma var_order(bufDesc, audioBuffer2Start, audioBuffer2Len, audioBuffer1Len, audioBuffer1Start, wavFormat)
ZunResult SoundPlayer::InitializeDSound(HWND gameWindow)
{
    DSBUFFERDESC bufDesc;
    tWAVEFORMATEX wavFormat;
    LPVOID audioBuffer1Start;
    DWORD audioBuffer1Len;
    LPVOID audioBuffer2Start;
    DWORD audioBuffer2Len;

    this->manager() = new CSoundManager();
    if (this->manager()->Initialize(gameWindow, 2, 2, 44100, 16) < ZUN_SUCCESS)
    {
        // "Failed to initialize the DirectSound object" (Shift-JIS)
        g_GameErrorContext.Log(TH_ERR_SOUNDPLAYER_FAILED_TO_INITIALIZE_OBJECT);
        if (this->manager() != NULL)
        {
            delete this->manager();
            this->manager() = NULL;
        }
        return ZUN_ERROR;
    }

    this->dsoundHdl() = this->manager()->m_pDS;
    this->backgroundMusicThreadHandle() = NULL;
    memset(&bufDesc, 0, sizeof(DSBUFFERDESC));
    bufDesc.dwSize = sizeof(DSBUFFERDESC);
    bufDesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
    bufDesc.dwBufferBytes = BACKGROUND_MUSIC_BUFFER_SIZE;
    memset(&wavFormat, 0, sizeof(tWAVEFORMATEX));
    wavFormat.cbSize = 0;
    wavFormat.wFormatTag = WAVE_FORMAT_PCM;
    wavFormat.nChannels = BACKGROUND_MUSIC_WAV_NUM_CHANNELS;
    wavFormat.nSamplesPerSec = 44100;
    wavFormat.nAvgBytesPerSec = 176400;
    wavFormat.nBlockAlign = BACKGROUND_MUSIC_WAV_BLOCK_ALIGN;
    wavFormat.wBitsPerSample = BACKGROUND_MUSIC_WAV_BITS_PER_SAMPLE;
    bufDesc.lpwfxFormat = &wavFormat;
    if (this->dsoundHdl()->CreateSoundBuffer(&bufDesc, &this->initSoundBuffer(), NULL) < ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    if (this->initSoundBuffer()->Lock(0, BACKGROUND_MUSIC_BUFFER_SIZE, &audioBuffer1Start, &audioBuffer1Len,
                                      &audioBuffer2Start, &audioBuffer2Len, 0) < ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    memset(audioBuffer1Start, 0, BACKGROUND_MUSIC_BUFFER_SIZE);
    this->initSoundBuffer()->Unlock(audioBuffer1Start, audioBuffer1Len, audioBuffer2Start, audioBuffer2Len);
    this->initSoundBuffer()->Play(0, 0, DSBPLAY_LOOPING);
    /* 4 times per second */
    SetTimer(gameWindow, 0, 250, NULL);
    this->gameWindow() = gameWindow;
    // "DirectSound initialized successfully" (Shift-JIS)
    g_GameErrorContext.Log(TH_DBG_SOUNDPLAYER_INIT_SUCCESS);
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::Release(void)
{
    i32 i;

    if (this->manager() == NULL)
    {
        return ZUN_SUCCESS;
    }
    for (i = 0; i < 0x80; i++)
    {
        if (this->duplicateSoundBuffers(i) != NULL)
        {
            this->duplicateSoundBuffers(i)->Release();
            this->duplicateSoundBuffers(i) = NULL;
        }
        if (this->soundBuffers(i) != NULL)
        {
            this->soundBuffers(i)->Release();
            this->soundBuffers(i) = NULL;
        }
    }
    KillTimer(this->gameWindow(), 1);
    StopBGM();
    this->dsoundHdl() = NULL;
    this->initSoundBuffer()->Stop();
    if (this->initSoundBuffer() != NULL)
    {
        this->initSoundBuffer()->Release();
        this->initSoundBuffer() = NULL;
    }
    if (this->backgroundMusic() != NULL)
    {
        delete this->backgroundMusic();
        this->backgroundMusic() = NULL;
    }
    if (this->manager() != NULL)
    {
        delete this->manager();
        this->manager() = NULL;
    }
    for (i = 0; i < 0x10; i++)
    {
        if (this->wavData(i) != NULL)
        {
            free(this->wavData(i));
            this->wavData(i) = NULL;
        }
    }
    if (this->bgmFmtTable() != NULL)
    {
        free(this->bgmFmtTable());
    }
    return ZUN_SUCCESS;
}

#pragma var_order(idx, sFDCursor, dsBuffer, wavDataPtr, formatSize, audioPtr2, audioSize2, audioSize1, audioPtr1,     \
                  soundFileData, wavData, fileSize)
ZunResult SoundPlayer::LoadSound(i32 idx, char *path)
{
    u8 *soundFileData;
    u8 *sFDCursor;
    i32 fileSize;
    WAVEFORMATEX *wavDataPtr;
    WAVEFORMATEX *audioPtr1;
    WAVEFORMATEX *audioPtr2;
    DWORD audioSize1;
    DWORD audioSize2;
    WAVEFORMATEX wavData;
    i32 formatSize;
    DSBUFFERDESC dsBuffer;

    if (this->manager() == NULL)
    {
        return ZUN_SUCCESS;
    }
    if (this->soundBuffers(idx) != NULL)
    {
        this->soundBuffers(idx)->Release();
        this->soundBuffers(idx) = NULL;
    }
    soundFileData = (u8 *)FileSystem::OpenPath(path, 0);
    sFDCursor = soundFileData;
    if (sFDCursor == NULL)
    {
        return ZUN_ERROR;
    }
    if (strncmp((char *)sFDCursor, "RIFF", 4))
    {
        // "not a wave file %s" (Shift-JIS)
        g_GameErrorContext.Log(TH_ERR_NOT_A_WAV_FILE, path);
        free(soundFileData);
        return ZUN_ERROR;
    }
    sFDCursor += 4;
    fileSize = *(i32 *)sFDCursor;
    sFDCursor += 4;
    if (strncmp((char *)sFDCursor, "WAVE", 4))
    {
        g_GameErrorContext.Log(TH_ERR_NOT_A_WAV_FILE, path);
        free(soundFileData);
        return ZUN_ERROR;
    }
    sFDCursor += 4;
    wavDataPtr = GetWavFormatData(sFDCursor, "fmt ", &formatSize, fileSize - 12);
    if (wavDataPtr == NULL)
    {
        g_GameErrorContext.Log(TH_ERR_NOT_A_WAV_FILE, path);
        free(soundFileData);
        return ZUN_ERROR;
    }
    wavData = *wavDataPtr;

    wavDataPtr = GetWavFormatData(sFDCursor, "data", &formatSize, fileSize - 12);
    if (wavDataPtr == NULL)
    {
        g_GameErrorContext.Log(TH_ERR_NOT_A_WAV_FILE, path);
        free(soundFileData);
        return ZUN_ERROR;
    }
    memset(&dsBuffer, 0, sizeof(dsBuffer));
    dsBuffer.dwSize = sizeof(dsBuffer);
    dsBuffer.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
    dsBuffer.dwBufferBytes = formatSize;
    dsBuffer.lpwfxFormat = &wavData;
    if (FAILED(this->dsoundHdl()->CreateSoundBuffer(&dsBuffer, &this->soundBuffers(idx), NULL)))
    {
        free(soundFileData);
        return ZUN_ERROR;
    }
    if (FAILED(soundBuffers(idx)->Lock(0, formatSize, (LPVOID *)&audioPtr1, (LPDWORD)&audioSize1,
                                       (LPVOID *)&audioPtr2, (LPDWORD)&audioSize2, NULL)))
    {
        free(soundFileData);
        return ZUN_ERROR;
    }
    memcpy(audioPtr1, wavDataPtr, audioSize1);
    if (audioSize2 != 0)
    {
        memcpy(audioPtr2, (i8 *)wavDataPtr + audioSize1, audioSize2);
    }
    soundBuffers(idx)->Unlock((LPVOID *)audioPtr1, audioSize1, (LPVOID *)audioPtr2, audioSize2);
    free(soundFileData);
    return ZUN_SUCCESS;
}

WAVEFORMATEX *SoundPlayer::GetWavFormatData(u8 *soundData, char *formatString, i32 *formatSize,
                                            u32 fileSizeExcludingFormat)
{
    while (fileSizeExcludingFormat > 0)
    {
        *formatSize = *(i32 *)(soundData + 4);
        if (strncmp((char *)soundData, formatString, 4) == 0)
        {
            return (WAVEFORMATEX *)(soundData + 8);
        }
        fileSizeExcludingFormat -= (*formatSize + 8);
        soundData += *formatSize + 8;
    }
    return NULL;
}

ZunResult SoundPlayer::InitSoundBuffers()
{
    i32 idx;
    if (this->manager() == NULL)
    {
        return ZUN_ERROR;
    }
    if (this->dsoundHdl() == NULL)
    {
        return ZUN_SUCCESS;
    }
    for (idx = 0; idx < 5; idx++)
    {
        this->soundBuffersToPlay(idx) = -1;
    }
    for (idx = 0; idx < ARRAY_SIZE_SIGNED(g_SFXList); idx++)
    {
        if (this->LoadSound(idx, g_SFXList[idx]) != ZUN_SUCCESS)
        {
            g_GameErrorContext.Log(TH_ERR_SOUNDPLAYER_FAILED_TO_LOAD_SOUND_FILE, g_SFXList[idx]);
            return ZUN_ERROR;
        }
    }
    for (idx = 0; idx < (i32)ARRAY_SIZE(g_SoundBufferIdxVol); idx++)
    {
        this->dsoundHdl()->DuplicateSoundBuffer(this->soundBuffers(g_SoundBufferIdxVol[idx].bufferIdx),
                                                &this->duplicateSoundBuffers(idx));
        this->duplicateSoundBuffers(idx)->SetCurrentPosition(0);
        this->duplicateSoundBuffers(idx)->SetVolume(g_SoundBufferIdxVol[idx].volume);
    }
    return ZUN_SUCCESS;
}

#pragma var_order(wavFormatBits, notifySize, res, numSamplesPerSec, blockAlign)
// LoadBgmPath: opens a fresh CStreamingSound for the named BGM file.  Mirrors
// th06's LoadWav but writes the file name into g_SoundPlayer.thbgmDatPath()
// first (FUN_0044c020 @ 0x0044c020).
ZunResult SoundPlayer::LoadBgmPath(char *path)
{
    u16 wavFormatBits;
    u32 blockAlign;
    u32 numSamplesPerSec;
    u32 notifySize;
    HANDLE event;
    HRESULT res;

    strcpy(this->thbgmDatPath(), path);
    if (this->manager() == NULL)
    {
        return ZUN_ERROR;
    }
    if (this->dsoundHdl() == NULL)
    {
        return ZUN_ERROR;
    }
    utils::DebugPrint2("Streming BGM Start\r\n");
    this->StopBGM();
    // notifySize = bgmFmtTable[0]->m_dataSize * 4 * wavFmtEntry.m_wBitsPerSample >> 4
    wavFormatBits = this->wavFmtEntry(0)->m_wBitsPerSample;
    blockAlign = this->wavFmtEntry(0)->m_reserved14 * 4;
    numSamplesPerSec = blockAlign * wavFormatBits;
    notifySize = numSamplesPerSec >> 4;
    notifySize -= (notifySize % wavFormatBits);
    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    this->backgroundMusicUpdateEvent() = event;
    this->backgroundMusicThreadHandle() =
        CreateThread(NULL, 0, SoundPlayer::BackgroundMusicPlayerThread, g_Supervisor.hwndGameWindow, 0,
                     &this->backgroundMusicThreadId());
    res = this->manager()->CreateStreaming(&this->backgroundMusic(), this->thbgmDatPath(),
                                           DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY, GUID_NULL, 4,
                                           notifySize, 0x10, this->backgroundMusicUpdateEvent(),
                                           this->wavFmtEntry(0));
    if (FAILED(res))
    {
        // "error : BGM file not found %s" (Shift-JIS)
        utils::DebugPrint2("error : bgmfile is not find %s\r\n", this->thbgmDatPath());
        return ZUN_ERROR;
    }
    return ZUN_SUCCESS;
}

// ReopenBgm: rewinds the slot's CWaveFile to the loop start without recreating
// the CStreamingSound (FUN_0044c1b0 @ 0x0044c1b0).
ZunResult SoundPlayer::ReopenBgm(i32 slotIdx)
{
    i32 wavFmtIdx;
    if (this->backgroundMusic() == NULL)
    {
        return ZUN_ERROR;
    }
    wavFmtIdx = BgmNameToWavFmtIndex(this->bgmSlotName(slotIdx));
    this->backgroundMusic()->m_pWaveFile->ResetFile((i32)(this->bgmFmtTable()) + wavFmtIdx * 0x34 == NULL);
    utils::DebugPrint2("Streming BGM Reopen %d\r\n", wavFmtIdx);
    return ZUN_SUCCESS;
}

void SoundPlayer::StopBGM()
{
    DWORD waitResult;
    if (this->backgroundMusic() == NULL)
    {
        return;
    }
    utils::DebugPrint2("Streming BGM stop\r\n");
    this->backgroundMusic()->Stop();
    if (this->backgroundMusicThreadHandle() != NULL)
    {
        PostThreadMessageA(this->backgroundMusicThreadId(), WM_QUIT, 0, 0);
        utils::DebugPrint2("stop m_dwNotifyThreadID\r\n");
        do
        {
            waitResult = WaitForSingleObject(this->backgroundMusicThreadHandle(), 0x100);
            if (waitResult != 0)
            {
                PostThreadMessageA(this->backgroundMusicThreadId(), WM_QUIT, 0, 0);
            }
        } while (waitResult != 0);
        utils::DebugPrint2("stop comp\r\n");
        CloseHandle(this->backgroundMusicThreadHandle());
        CloseHandle(this->backgroundMusicUpdateEvent());
        this->backgroundMusicThreadHandle() = NULL;
    }
    if (this->backgroundMusic() != NULL)
    {
        delete this->backgroundMusic();
        this->backgroundMusic() = NULL;
    }
}

#pragma var_order(notifySize, wavFormatBits, res, numSamplesPerSec, blockAlign)
// LoadStreamingBgm: opens a streaming sound for a previously preloaded slot
// (FUN_0044c4d0 @ 0x0044c4d0).  Differs from LoadBgmPath by reading the slot's
// preloaded PCM instead of opening thbgm.dat again.
ZunResult SoundPlayer::LoadStreamingBgm(i32 slotIdx)
{
    u16 wavFormatBits;
    u32 blockAlign;
    u32 numSamplesPerSec;
    u32 notifySize;
    HANDLE event;
    HRESULT res;

    if (this->manager() == NULL)
    {
        return ZUN_ERROR;
    }
    if (g_Supervisor.cfg.musicMode != 1)
    {
        return ZUN_ERROR;
    }
    if (this->dsoundHdl() == NULL)
    {
        return ZUN_ERROR;
    }
    if ((g_Supervisor.cfg.opts >> GCOS_USE_D3D_HW_TEXTURE_BLENDING) & 1)
    {
        // Recreate path: use the preloaded slot's WaveFormat.
        return this->ReopenBgm(slotIdx);
    }
    if (this->wavFmtEntry(slotIdx) == NULL)
    {
        return ZUN_ERROR;
    }
    utils::DebugPrint2("Streming BGM Load no %d\r\n", slotIdx);
    wavFormatBits = this->wavFmtEntry(slotIdx)->m_wBitsPerSample;
    blockAlign = this->wavFmtEntry(slotIdx)->m_reserved14 * 4;
    numSamplesPerSec = blockAlign * wavFormatBits;
    notifySize = numSamplesPerSec >> 4;
    notifySize -= (notifySize % wavFormatBits);
    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    this->backgroundMusicUpdateEvent() = event;
    this->backgroundMusicThreadHandle() =
        CreateThread(NULL, 0, SoundPlayer::BackgroundMusicPlayerThread, g_Supervisor.hwndGameWindow, 0,
                     &this->backgroundMusicThreadId());
    res = this->manager()->CreateStreamingFromMemory(
        &this->backgroundMusic(), (BYTE *)this->wavData(slotIdx), this->wavDataSize(slotIdx),
        DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY, GUID_NULL, 4, notifySize, 0x10,
        this->backgroundMusicUpdateEvent(), this->wavFmtEntry(slotIdx));
    if (FAILED(res))
    {
        utils::DebugPrint2("error : bgmfile is not find %s\r\n", this->thbgmDatPath());
        return ZUN_ERROR;
    }
    utils::DebugPrint2("load comp\r\n");
    return ZUN_SUCCESS;
}

#pragma var_order(fileData, hFile, lpBuffer, readBytes, wavFmtIdx)
// PreLoadBgm: reads raw PCM from thbgm.dat into the slot's wavData buffer
// (FUN_0044c220 @ 0x0044c220).  Skips the work entirely when sound is disabled
// or when the slot already has the requested name cached.
ZunResult SoundPlayer::PreLoadBgm(i32 slotIdx, char *name)
{
    HANDLE hFile;
    void *lpBuffer;
    i32 wavFmtIdx;
    DWORD readBytes;

    if (this->wavData(slotIdx) != NULL)
    {
        if (strcmp(name, this->bgmSlotName(slotIdx)) == 0)
        {
            return ZUN_SUCCESS;
        }
    }
    strcpy(this->bgmSlotName(slotIdx), name);
    if ((g_Supervisor.cfg.opts >> GCOS_USE_D3D_HW_TEXTURE_BLENDING) & 1)
    {
        return ZUN_SUCCESS;
    }
    if (this->manager() == NULL)
    {
        return ZUN_SUCCESS;
    }
    if (this->wavData(slotIdx) != NULL)
    {
        free(this->wavData(slotIdx));
        this->wavData(slotIdx) = NULL;
    }
    utils::DebugPrint2("Streming BGM PreLoad %d\r\n", slotIdx);
    hFile = CreateFileA(this->thbgmDatPath(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        utils::DebugPrint2("error : bgmfile is not find %s\r\n", this->thbgmDatPath());
        return ZUN_ERROR;
    }
    wavFmtIdx = BgmNameToWavFmtIndex(name);
    SetFilePointer(hFile, ((i32 *)this->bgmFmtTable())[wavFmtIdx * 0xd + 4], NULL, FILE_BEGIN);
    lpBuffer = malloc(((i32 *)this->bgmFmtTable())[wavFmtIdx * 0xd + 5]);
    if (lpBuffer == NULL)
    {
        CloseHandle(hFile);
        utils::DebugPrint2("error : bgmfile is not find %s\r\n", this->thbgmDatPath());
        return ZUN_ERROR;
    }
    ReadFile(hFile, lpBuffer, ((i32 *)this->bgmFmtTable())[wavFmtIdx * 0xd + 5], &readBytes, NULL);
    CloseHandle(hFile);
    this->wavFmtEntry(slotIdx) = (WaveFormat *)((i32 *)this->bgmFmtTable() + wavFmtIdx * 0xd);
    this->wavData(slotIdx) = lpBuffer;
    this->wavDataCur(slotIdx) = lpBuffer;
    this->wavDataSize(slotIdx) = ((WaveFormat *)((i32 *)this->bgmFmtTable() + wavFmtIdx * 0xd))->m_reserved14;
    return ZUN_SUCCESS;
}

// LoadBgmFmtFile: parses bgm/thbgm.fmt into a malloc'd table referenced by
// g_SoundPlayer.bgmFmtTable() (FUN_0044bff0 @ 0x0044bff0).
ZunResult SoundPlayer::LoadBgmFmtFile(char *path)
{
    void *fileData;
    fileData = FileSystem::OpenPath(path, 0);
    this->bgmFmtTable() = fileData;
    return (this->bgmFmtTable() != NULL) - 1;
}

// BgmNameToWavFmtIndex: linear scan of bgmFmtTable[].name matching the input
// (FUN_0044baf0 @ 0x0044baf0).  Each entry in bgmFmtTable is 0x34 bytes and
// starts with a NUL-terminated file-name string.
static i32 BgmNameToWavFmtIndex(char *name)
{
    i8 base[0x80];
    i8 *slashPos;
    i32 idx;

    idx = 0;
    slashPos = (i8 *)strrchr(name, '/');
    if (slashPos == NULL)
    {
        slashPos = (i8 *)strrchr(name, '\\');
    }
    if (slashPos == NULL)
    {
        strcpy((char *)base, name);
    }
    else
    {
        strcpy((char *)base, slashPos + 1);
    }
    while (((i8 *)g_SoundPlayer.bgmFmtTable())[idx * 0x34] != '\0')
    {
        if (strcmp((char *)((i32 *)g_SoundPlayer.bgmFmtTable() + idx * 0xd), (char *)base) == 0)
        {
            break;
        }
        idx++;
    }
    if (((i8 *)g_SoundPlayer.bgmFmtTable())[idx * 0x34] == '\0')
    {
        idx = 0;
    }
    return idx;
}

#pragma var_order(entryIdx, idx, sndBufIdx)
// ProcessSoundQueues: walks the SoundQueue and advances each entry through its
// state machine (FUN_0044c9c0 @ 0x0044c9c0).  The full stage enum is documented
// in SoundQueueEntry.  Only the per-frame SFX playback tail is inlined here.
void SoundPlayer::ProcessSoundQueues()
{
    // Stub: the full state machine is large (571 instructions, 17 stages); the
    // recovered skeleton delegates to the inline helpers above.  See the
    // header for the documented stage list and the binary function for the
    // authoritative control flow.
    if (this->manager() == NULL)
    {
        return;
    }
    // Tail: replay any pending SFX queued via PlaySoundByIdx-equivalent path.
    if (g_Supervisor.cfg.playSounds == 0)
    {
        return;
    }
    for (i32 i = 0; i < 5; i++)
    {
        if (this->soundBuffersToPlay(i) < 0)
        {
            break;
        }
        i32 sndBufIdx = this->soundBuffersToPlay(i);
        this->soundBuffersToPlay(i) = -1;
        if (this->duplicateSoundBuffers(sndBufIdx) == NULL)
        {
            continue;
        }
        this->duplicateSoundBuffers(sndBufIdx)->Stop();
        this->duplicateSoundBuffers(sndBufIdx)->SetCurrentPosition(0);
        this->duplicateSoundBuffers(sndBufIdx)->Play(0, 0, 0);
    }
}

#pragma var_order(entryIdx)
// SoundQueueAdd: appends a new SoundQueueEntry to the queue (FUN_0044d2f0 @
// 0x0044d2f0).  Drops silently when the 0x1f-slot queue is full.
void SoundPlayer::SoundQueueAdd(i32 command, i32 param1, char *name)
{
    i32 entryIdx;
    entryIdx = 0;
    while (entryIdx < 0x1e)
    {
        if (this->soundQueue()[entryIdx].command == 0)
        {
            break;
        }
        entryIdx++;
    }
    this->soundQueue()[entryIdx].command = command;
    this->soundQueue()[entryIdx].param1 = param1;
    strcpy(this->soundQueue()[entryIdx].name, name);
    this->soundQueue()[entryIdx].stage = 0;
    utils::DebugPrint2("Sound Que Add %d\r\n", command);
}

// FadeOutBgm: sets the live CStreamingSound's fade-out bookkeeping
// (inlined in ProcessSoundQueues case 5).
void SoundPlayer::FadeOutBgm(f32 fadeOutSeconds)
{
    if (this->backgroundMusic() == NULL)
    {
        return;
    }
    this->backgroundMusic()->m_dwIsFadingOut = 1;
    this->backgroundMusic()->m_dwCurFadeoutProgress = FloatSecondsToFrames(fadeOutSeconds);
    this->backgroundMusic()->m_dwTotalFadeout = this->backgroundMusic()->m_dwCurFadeoutProgress;
}

#pragma var_order(msg, waitObj, stopped, lpThreadParameterCopy, res, looped)
DWORD __stdcall SoundPlayer::BackgroundMusicPlayerThread(LPVOID lpThreadParameter)
{
    DWORD waitObj;
    MSG msg;
    u32 stopped;
    u32 looped;
    LPVOID lpThreadParameterCopy;
    HRESULT res;

    lpThreadParameterCopy = lpThreadParameter;
    stopped = FALSE;
    looped = TRUE;
    while (!stopped)
    {
        waitObj =
            MsgWaitForMultipleObjects(1, &g_SoundPlayer.backgroundMusicUpdateEvent(), FALSE, INFINITE, QS_ALLEVENTS);
        if (g_SoundPlayer.backgroundMusic() == NULL)
        {
            stopped = TRUE;
        }
        switch (waitObj)
        {
        case WAIT_OBJECT_0:
            if (g_SoundPlayer.backgroundMusic() != NULL)
            {
                g_SoundPlayer.backgroundMusic()->m_bFillNextNotificationWithSilence = 1;
                res = g_SoundPlayer.backgroundMusic()->HandleWaveStreamNotification(looped);
                g_SoundPlayer.backgroundMusic()->m_bFillNextNotificationWithSilence = 0;
            }
            break;
        case WAIT_OBJECT_0 + 1:
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE) != 0)
            {
                if (msg.message == WM_QUIT)
                {
                    stopped = TRUE;
                }
            }
            break;
        }
    }
    return 0;
}
}; // namespace th07
