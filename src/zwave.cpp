// th07 DirectSound streaming-wave helper classes.
//
// See zwave.hpp for the high-level differences from the th06 reference and the
// recovered struct layouts.  Every field offset below was read directly from
// th07.exe via Ghidra.
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "utils.hpp"
#include "zwave.hpp"

#include <dsound.h>
#include <stdlib.h>
#include <windows.h>

namespace th07
{

// Global base offset added to all wave-data seeks.  Recovered as the dword at
// 0x004bdaa0 in th07.exe; in the shipping build it is zero.
DIFFABLE_STATIC(DWORD, g_WaveBaseOffset);


// ----------------------------------------------------------------------------
// CSoundManager
// ----------------------------------------------------------------------------

// Initialises the DirectSound8 object, sets the cooperative level, then sets
// the primary buffer format.  Corresponds to th07.exe FUN_0045c740.
HRESULT CSoundManager::Initialize(HWND hWnd, DWORD dwCoopLevel, DWORD dwPrimaryChannels, DWORD dwPrimaryFreq,
                                  DWORD dwPrimaryBitRate)
{
    HRESULT hr;

    SAFE_RELEASE(m_pDS);

    if (FAILED(hr = DirectSoundCreate8(NULL, &m_pDS, NULL)))
        return hr;

    if (FAILED(hr = m_pDS->SetCooperativeLevel(hWnd, dwCoopLevel)))
        return hr;

    SetPrimaryBufferFormat(dwPrimaryChannels, dwPrimaryFreq, dwPrimaryBitRate);

    return S_OK;
}

// Creates the primary buffer and applies the requested PCM format.
// Corresponds to th07.exe FUN_0045c7d0.
HRESULT CSoundManager::SetPrimaryBufferFormat(DWORD dwPrimaryChannels, DWORD dwPrimaryFreq,
                                              DWORD dwPrimaryBitRate)
{
    HRESULT hr;
    LPDIRECTSOUNDBUFFER pDSBPrimary = NULL;

    if (m_pDS == NULL)
        return CO_E_NOTINITIALIZED;

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
    dsbd.dwBufferBytes = 0;
    dsbd.lpwfxFormat = NULL;

    if (FAILED(hr = m_pDS->CreateSoundBuffer(&dsbd, &pDSBPrimary, NULL)))
        return hr;

    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)dwPrimaryChannels;
    wfx.nSamplesPerSec = dwPrimaryFreq;
    wfx.wBitsPerSample = (WORD)dwPrimaryBitRate;
    wfx.nBlockAlign = wfx.wBitsPerSample / 8 * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    hr = pDSBPrimary->SetFormat(&wfx);

    SAFE_RELEASE(pDSBPrimary);

    return hr;
}

// Opens strWaveFileName through CWaveFile, then creates the streaming buffer.
// Corresponds to th07.exe FUN_0045c8e0.
HRESULT CSoundManager::CreateStreaming(CStreamingSound **ppStreamingSound, LPSTR strWaveFileName,
                                       DWORD dwCreationFlags, GUID guid3DAlgorithm, DWORD dwNotifyCount,
                                       DWORD dwNotifySize, DWORD dwUnknown, HANDLE hNotifyEvent, WaveFormat *pwfx)
{
    HRESULT hr;
    LPDIRECTSOUNDBUFFER pDSBuffer = NULL;
    LPDIRECTSOUNDNOTIFY pDSNotify = NULL;
    DSBPOSITIONNOTIFY *aPosNotify = NULL;
    CWaveFile *pWaveFile = NULL;
    DWORD dwDSBufferSize = dwNotifySize * dwNotifyCount;

    if (m_pDS == NULL)
        return CO_E_NOTINITIALIZED;

    pWaveFile = new CWaveFile();
    if (FAILED(hr = pWaveFile->Open(strWaveFileName, pwfx, WAVEFILE_READ)))
    {
        SAFE_DELETE(pWaveFile);
        return E_FAIL;
    }

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = dwCreationFlags | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS |
                   DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
    dsbd.dwBufferBytes = dwDSBufferSize;
    dsbd.guid3DAlgorithm = guid3DAlgorithm;
    dsbd.lpwfxFormat = (WAVEFORMATEX *)((BYTE *)pWaveFile->m_pwfx + 0x20);

    if (FAILED(hr = m_pDS->CreateSoundBuffer(&dsbd, &pDSBuffer, NULL)))
        return E_FAIL;

    if (FAILED(hr = pDSBuffer->QueryInterface(IID_IDirectSoundNotify, (VOID **)&pDSNotify)))
        return E_FAIL;

    aPosNotify = new DSBPOSITIONNOTIFY[dwNotifyCount];
    if (aPosNotify == NULL)
        return E_OUTOFMEMORY;

    for (DWORD i = 0; i < dwNotifyCount; i++)
    {
        aPosNotify[i].dwOffset = dwNotifySize * i + dwNotifySize - 1;
        aPosNotify[i].hEventNotify = hNotifyEvent;
    }

    if (FAILED(hr = pDSNotify->SetNotificationPositions(dwNotifyCount, aPosNotify)))
    {
        SAFE_RELEASE(pDSNotify);
        SAFE_DELETE_ARRAY(aPosNotify);
        return E_FAIL;
    }

    SAFE_RELEASE(pDSNotify);
    SAFE_DELETE_ARRAY(aPosNotify);

    *ppStreamingSound = new CStreamingSound(pDSBuffer, dwDSBufferSize, pWaveFile, dwNotifySize);
    memcpy((*ppStreamingSound)->m_reserved34, &dsbd, sizeof(DSBUFFERDESC));
    (*ppStreamingSound)->m_pSoundManager = this;
    (*ppStreamingSound)->m_hNotifyEvent = hNotifyEvent;
    (*ppStreamingSound)->m_pad74 = 0;

    return S_OK;
}

// Same as CreateStreaming but reads from a memory buffer instead of a file.
// Corresponds to th07.exe FUN_0045cc30.
HRESULT CSoundManager::CreateStreamingFromMemory(CStreamingSound **ppStreamingSound, BYTE *pbData, ULONG ulDataSize,
                                                 DWORD dwCreationFlags, GUID guid3DAlgorithm, DWORD dwNotifyCount,
                                                 DWORD dwNotifySize, DWORD dwUnknown, HANDLE hNotifyEvent,
                                                 WaveFormat *pwfx)
{
    utils::DebugPrint2("StreamingSound Create \r\n");

    HRESULT hr;
    LPDIRECTSOUNDBUFFER pDSBuffer = NULL;
    LPDIRECTSOUNDNOTIFY pDSNotify = NULL;
    DSBPOSITIONNOTIFY *aPosNotify = NULL;
    CWaveFile *pWaveFile = NULL;
    DWORD dwDSBufferSize = dwNotifySize * dwNotifyCount;

    if (m_pDS == NULL)
        return CO_E_NOTINITIALIZED;

    pWaveFile = new CWaveFile();
    pWaveFile->OpenFromMemory(pbData, ulDataSize, pwfx, WAVEFILE_READ, 0);

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = dwCreationFlags | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS |
                   DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
    dsbd.dwBufferBytes = dwDSBufferSize;
    dsbd.guid3DAlgorithm = guid3DAlgorithm;
    dsbd.lpwfxFormat = (WAVEFORMATEX *)((BYTE *)pWaveFile->m_pwfx + 0x20);

    if (FAILED(hr = m_pDS->CreateSoundBuffer(&dsbd, &pDSBuffer, NULL)))
        return E_FAIL;

    if (FAILED(hr = pDSBuffer->QueryInterface(IID_IDirectSoundNotify, (VOID **)&pDSNotify)))
        return E_FAIL;

    aPosNotify = new DSBPOSITIONNOTIFY[dwNotifyCount];
    if (aPosNotify == NULL)
        return E_OUTOFMEMORY;

    for (DWORD i = 0; i < dwNotifyCount; i++)
    {
        aPosNotify[i].dwOffset = dwNotifySize * i + dwNotifySize - 1;
        aPosNotify[i].hEventNotify = hNotifyEvent;
    }

    if (FAILED(hr = pDSNotify->SetNotificationPositions(dwNotifyCount, aPosNotify)))
    {
        SAFE_RELEASE(pDSNotify);
        SAFE_DELETE_ARRAY(aPosNotify);
        return E_FAIL;
    }

    SAFE_RELEASE(pDSNotify);
    SAFE_DELETE_ARRAY(aPosNotify);

    *ppStreamingSound = new CStreamingSound(pDSBuffer, dwDSBufferSize, pWaveFile, dwNotifySize);
    memcpy((*ppStreamingSound)->m_reserved34, &dsbd, sizeof(DSBUFFERDESC));
    (*ppStreamingSound)->m_pSoundManager = this;
    (*ppStreamingSound)->m_hNotifyEvent = hNotifyEvent;
    (*ppStreamingSound)->m_pad74 = 0;

    utils::DebugPrint2("Success \r\n");
    return S_OK;
}

// ----------------------------------------------------------------------------
// CSound
// ----------------------------------------------------------------------------

// Constructs the class.  Corresponds to th07.exe FUN_0045cf50.
CSound::CSound(LPDIRECTSOUNDBUFFER *apDSBuffer, DWORD dwDSBufferSize, DWORD dwNumBuffers, CWaveFile *pWaveFile)
{
    DWORD i;

    m_apDSBuffer = new LPDIRECTSOUNDBUFFER[dwNumBuffers];
    for (i = 0; i < dwNumBuffers; i++)
        m_apDSBuffer[i] = apDSBuffer[i];

    m_dwDSBufferSize = dwDSBufferSize;
    m_pWaveFile = pWaveFile;
    m_dwNumBuffers = dwNumBuffers;

    FillBufferWithSound(m_apDSBuffer[0], FALSE);

    for (i = 0; i < dwNumBuffers; i++)
        m_apDSBuffer[i]->SetCurrentPosition(0);

    m_dwIsPlaying = 0;
}

// Scalar-deleting destructor wrapper.  Corresponds to th07.exe FUN_0045d030.
CSound::~CSound()
{
    for (DWORD i = 0; i < m_dwNumBuffers; i++)
        SAFE_RELEASE(m_apDSBuffer[i]);

    SAFE_DELETE_ARRAY(m_apDSBuffer);
    SAFE_DELETE(m_pWaveFile);
}

// Fills a DirectSound buffer with a sound file.  Corresponds to th07.exe
// FUN_0045d3c0.
HRESULT CSound::FillBufferWithSound(LPDIRECTSOUNDBUFFER pDSB, BOOL bRepeatWavIfBufferLarger)
{
    HRESULT hr;
    VOID *pDSLockedBuffer = NULL;
    DWORD dwDSLockedBufferSize = 0;
    DWORD dwWavDataRead = 0;

    if (pDSB == NULL)
        return CO_E_NOTINITIALIZED;

    if (FAILED(hr = RestoreBuffer(pDSB, NULL)))
        return hr;

    if (FAILED(hr = pDSB->Lock(0, m_dwDSBufferSize, &pDSLockedBuffer, &dwDSLockedBufferSize, NULL, NULL, 0L)))
        return hr;

    m_pWaveFile->ResetFile(FALSE);

    if (FAILED(hr = m_pWaveFile->Read((BYTE *)pDSLockedBuffer, dwDSLockedBufferSize, &dwWavDataRead)))
        return hr;

    if (dwWavDataRead == 0)
    {
        // Wav is blank, fill with silence.
        BYTE bSilence = (m_pWaveFile->m_pwfx->m_wBitsPerSample == 8) ? 0x80 : 0x00;
        FillMemory(pDSLockedBuffer, dwDSLockedBufferSize, bSilence);
    }
    else if (dwWavDataRead < dwDSLockedBufferSize)
    {
        if (bRepeatWavIfBufferLarger)
        {
            DWORD dwReadSoFar = dwWavDataRead;
            while (dwReadSoFar < dwDSLockedBufferSize)
            {
                if (FAILED(hr = m_pWaveFile->ResetFile(FALSE)))
                    return hr;

                if (FAILED(hr = m_pWaveFile->Read((BYTE *)pDSLockedBuffer + dwReadSoFar,
                                                  dwDSLockedBufferSize - dwReadSoFar, &dwWavDataRead)))
                    return hr;

                dwReadSoFar += dwWavDataRead;
            }
        }
        else
        {
            BYTE bSilence = (m_pWaveFile->m_pwfx->m_wBitsPerSample == 8) ? 0x80 : 0x00;
            FillMemory((BYTE *)pDSLockedBuffer + dwWavDataRead, dwDSLockedBufferSize - dwWavDataRead, bSilence);
        }
    }

    pDSB->Unlock(pDSLockedBuffer, dwDSLockedBufferSize, NULL, 0);

    return S_OK;
}

// Restores a lost buffer.  Corresponds to th07.exe FUN_0045d5b0.
HRESULT CSound::RestoreBuffer(LPDIRECTSOUNDBUFFER pDSB, BOOL *pbWasRestored)
{
    HRESULT hr;

    if (pDSB == NULL)
        return CO_E_NOTINITIALIZED;
    if (pbWasRestored)
        *pbWasRestored = FALSE;

    DWORD dwStatus;
    if (FAILED(hr = pDSB->GetStatus(&dwStatus)))
        return hr;

    if (dwStatus & DSBSTATUS_BUFFERLOST)
    {
        do
        {
            hr = pDSB->Restore();
            if (hr == DSERR_BUFFERLOST)
                Sleep(10);
        } while (hr = pDSB->Restore());

        if (pbWasRestored != NULL)
            *pbWasRestored = TRUE;

        return S_OK;
    }
    else
    {
        return S_FALSE;
    }
}

// Returns the first buffer that is not currently playing.  Corresponds to
// th07.exe FUN_0045d660.
LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer()
{
    if (m_apDSBuffer == NULL)
        return NULL;

    DWORD i;
    for (i = 0; i < m_dwNumBuffers; i++)
    {
        if (m_apDSBuffer[i])
        {
            DWORD dwStatus = 0;
            m_apDSBuffer[i]->GetStatus(&dwStatus);
            if ((dwStatus & DSBSTATUS_PLAYING) == 0)
                break;
        }
    }

    if (i != m_dwNumBuffers)
        return m_apDSBuffer[i];
    else
        return m_apDSBuffer[rand() % m_dwNumBuffers];
}

// Indexes into the buffer array.  Corresponds to th07.exe FUN_0045d720.
LPDIRECTSOUNDBUFFER CSound::GetBuffer(DWORD dwIndex)
{
    if (m_apDSBuffer == NULL)
        return NULL;
    if (dwIndex >= m_dwNumBuffers)
        return NULL;

    return m_apDSBuffer[dwIndex];
}

// Plays the sound from a free buffer.  Corresponds to th07.exe FUN_0045d760.
HRESULT CSound::Play(DWORD dwPriority, DWORD dwFlags)
{
    HRESULT hr;
    BOOL bRestored;

    if (m_apDSBuffer == NULL)
        return CO_E_NOTINITIALIZED;

    LPDIRECTSOUNDBUFFER pDSB = GetFreeBuffer();
    if (pDSB == NULL)
        return E_FAIL;

    if (FAILED(hr = RestoreBuffer(pDSB, &bRestored)))
        return hr;

    if (bRestored)
    {
        if (FAILED(hr = FillBufferWithSound(pDSB, FALSE)))
            return hr;
        Reset();
    }

    m_dwIsFadingOut = 0;
    m_dwCurFadeoutProgress = 0;
    m_dwTotalFadeout = 0;

    m_dwIsPlaying = 1;
    m_dwLastPriority = dwPriority;
    m_dwLastFlags = dwFlags;
    m_pad2c = 0;

    return pDSB->Play(0, dwPriority, dwFlags);
}

// Stops every buffer.  Corresponds to th07.exe FUN_0045d860.
HRESULT CSound::Stop()
{
    if (m_apDSBuffer == NULL)
        return CO_E_NOTINITIALIZED;

    HRESULT hr = 0;
    m_dwIsPlaying = 0;

    for (DWORD i = 0; i < m_dwNumBuffers; i++)
    {
        hr |= m_apDSBuffer[i]->Stop();
        hr |= m_apDSBuffer[i]->SetCurrentPosition(0);
    }

    m_dwIsFadingOut = 0;
    return hr;
}

// Resets every buffer's cursor to zero.  Corresponds to th07.exe FUN_0045d9b0.
HRESULT CSound::Reset()
{
    if (m_apDSBuffer == NULL)
        return CO_E_NOTINITIALIZED;

    HRESULT hr = 0;

    for (DWORD i = 0; i < m_dwNumBuffers; i++)
        hr |= m_apDSBuffer[i]->SetCurrentPosition(0);

    return hr;
}

// ----------------------------------------------------------------------------
// CStreamingSound
// ----------------------------------------------------------------------------

// Constructs the class.  Corresponds to th07.exe FUN_0045da20.
CStreamingSound::CStreamingSound(LPDIRECTSOUNDBUFFER pDSBuffer, DWORD dwDSBufferSize, CWaveFile *pWaveFile,
                                 DWORD dwNotifySize)
    : CSound(&pDSBuffer, dwDSBufferSize, 1, pWaveFile)
{
    m_dwLastPlayPos = 0;
    m_dwPlayProgress = 0;
    m_pad54 = 0;
    m_pSoundManager = NULL;
    m_dwLastPlayPos2 = 0;
    m_dwPlayProgress2 = 0;
    m_dwNextWriteOffset = 0;
    m_bFillNextNotificationWithSilence = FALSE;
    m_dwNotifySize = dwNotifySize;
    m_hNotifyEvent = NULL;
    m_pad74 = 0;
}

// Empty destructor; the base class does the cleanup.  Corresponds to th07.exe
// FUN_0045dab0.
CStreamingSound::~CStreamingSound()
{
}

// Steps the fade-out; stops the buffer when finished.  Corresponds to th07.exe
// FUN_0045dad0.
HRESULT CStreamingSound::UpdateFadeOut()
{
    if (m_dwIsFadingOut != 0)
    {
        m_dwCurFadeoutProgress = m_dwCurFadeoutProgress - 1;
        if (m_dwCurFadeoutProgress < 1)
        {
            m_dwIsFadingOut = 0;
            m_apDSBuffer[0]->Stop();
            return 1;
        }
        LONG vol = (m_dwCurFadeoutProgress * 5000) / m_dwTotalFadeout - 5000;
        m_apDSBuffer[0]->SetVolume(vol);
    }
    return 0;
}

// Refills the streaming buffer as notifications fire.  Corresponds to th07.exe
// FUN_0045db60.
#pragma var_order(pDSLockedBuffer2, dwDSLockedBufferSize2, bRestored, pDSLockedBuffer, dwDSLockedBufferSize,        \
                  dwBytesWrittenToBuffer, hr, dwCurrentPlayPos, dwPlayDelta)
HRESULT CStreamingSound::HandleWaveStreamNotification(BOOL bLoopedPlay)
{
    HRESULT hr;
    DWORD dwCurrentPlayPos;
    DWORD dwPlayDelta;
    DWORD dwBytesWrittenToBuffer;
    VOID *pDSLockedBuffer;
    VOID *pDSLockedBuffer2;
    DWORD dwDSLockedBufferSize;
    DWORD dwDSLockedBufferSize2;

    if (m_apDSBuffer == NULL || m_pWaveFile == NULL)
        return CO_E_NOTINITIALIZED;

    m_apDSBuffer[0]->GetCurrentPosition(&dwCurrentPlayPos, NULL);

    // Only do work when the play cursor is comfortably ahead of the next
    // write offset; otherwise the notification fired too close to the write
    // cursor and we skip the refill this round.
    if (m_dwNextWriteOffset < dwCurrentPlayPos - m_dwNotifySize ||
        dwCurrentPlayPos <= m_dwNextWriteOffset)
    {
        BOOL bRestored;
        if (FAILED(hr = RestoreBuffer(m_apDSBuffer[0], &bRestored)))
        {
            utils::DebugPrint2("error : RestoreBuffer in HandleWaveStreamNotification\r\n");
            return hr;
        }

        if (bRestored)
        {
            if (FAILED(hr = FillBufferWithSound(m_apDSBuffer[0], FALSE)))
            {
                utils::DebugPrint2("error : FillBufferWithSound in HandleWaveStreamNotification\r\n");
                return hr;
            }
            return S_OK;
        }

        pDSLockedBuffer = NULL;
        pDSLockedBuffer2 = NULL;
        if (FAILED(hr = m_apDSBuffer[0]->Lock(m_dwNextWriteOffset, m_dwNotifySize, &pDSLockedBuffer,
                                              &dwDSLockedBufferSize, &pDSLockedBuffer2, &dwDSLockedBufferSize2, 0L)))
        {
            utils::DebugPrint2("error : Buffer->Lock in HandleWaveStreamNotification\r\n");
            return hr;
        }

        if (pDSLockedBuffer2 != NULL)
            return E_UNEXPECTED;

        if (!m_bFillNextNotificationWithSilence)
        {
            if (FAILED(hr = m_pWaveFile->Read((BYTE *)pDSLockedBuffer, dwDSLockedBufferSize,
                                              &dwBytesWrittenToBuffer)))
            {
                utils::DebugPrint2("error : m_pWaveFile->Read in HandleWaveStreamNotification\r\n");
                return hr;
            }
        }
        else
        {
            BYTE bSilence = (m_pWaveFile->m_pwfx->m_wBitsPerSample == 8) ? 0x80 : 0x00;
            FillMemory(pDSLockedBuffer, dwDSLockedBufferSize, bSilence);
            dwBytesWrittenToBuffer = dwDSLockedBufferSize;
        }

        if (dwBytesWrittenToBuffer < dwDSLockedBufferSize)
        {
            if (!bLoopedPlay)
            {
                BYTE bSilence = (m_pWaveFile->m_pwfx->m_wBitsPerSample == 8) ? 0x80 : 0x00;
                FillMemory((BYTE *)pDSLockedBuffer + dwBytesWrittenToBuffer,
                           dwDSLockedBufferSize - dwBytesWrittenToBuffer, bSilence);
                m_bFillNextNotificationWithSilence = TRUE;
            }
            else
            {
                DWORD dwReadSoFar = dwBytesWrittenToBuffer;
                while (dwReadSoFar < dwDSLockedBufferSize)
                {
                    if (FAILED(hr = m_pWaveFile->ResetFile(TRUE)))
                    {
                        utils::DebugPrint2("error : m_pWaveFile->ResetFile in HandleWaveStreamNotification\r\n");
                        return hr;
                    }

                    if (FAILED(hr = m_pWaveFile->Read((BYTE *)pDSLockedBuffer + dwReadSoFar,
                                                      dwDSLockedBufferSize - dwReadSoFar,
                                                      &dwBytesWrittenToBuffer)))
                    {
                        utils::DebugPrint2("error : m_pWaveFile->Read(+) in HandleWaveStreamNotification\r\n");
                        return hr;
                    }

                    dwReadSoFar += dwBytesWrittenToBuffer;
                }
            }
        }

        m_apDSBuffer[0]->Unlock(pDSLockedBuffer, dwDSLockedBufferSize, NULL, 0);

        if (FAILED(hr = m_apDSBuffer[0]->GetCurrentPosition(&dwCurrentPlayPos, NULL)))
        {
            utils::DebugPrint2("error : m_apDSBuffer[0]->GetCurrentPosition in HandleWaveStreamNotification\r\n");
            return hr;
        }

        if (dwCurrentPlayPos < m_dwLastPlayPos2)
            dwPlayDelta = m_dwDSBufferSize - m_dwLastPlayPos2 + dwCurrentPlayPos;
        else
            dwPlayDelta = dwCurrentPlayPos - m_dwLastPlayPos2;

        m_dwPlayProgress2 += dwPlayDelta;
        m_dwLastPlayPos2 = dwCurrentPlayPos;

        if (m_bFillNextNotificationWithSilence)
        {
            if (m_dwPlayProgress2 >= m_pWaveFile->GetSize())
                m_apDSBuffer[0]->Stop();
        }

        m_dwNextWriteOffset += dwDSLockedBufferSize;
        m_dwNextWriteOffset %= m_dwDSBufferSize;

        return S_OK;
    }
    else
    {
        utils::DebugPrint2("Stream Skip\n");
        return CO_E_NOTINITIALIZED;
    }
}

// Resets the streaming state so playback restarts from the beginning.
// Corresponds to th07.exe FUN_0045df50.
HRESULT CStreamingSound::Reset()
{
    HRESULT hr;

    if (m_apDSBuffer[0] == NULL || m_pWaveFile == NULL)
        return CO_E_NOTINITIALIZED;

    m_dwLastPlayPos = 0;
    m_dwPlayProgress = 0;
    m_dwNextWriteOffset = 0;
    m_bFillNextNotificationWithSilence = FALSE;

    BOOL bRestored;
    if (FAILED(hr = RestoreBuffer(m_apDSBuffer[0], &bRestored)))
        return hr;

    if (bRestored)
    {
        if (FAILED(hr = FillBufferWithSound(m_apDSBuffer[0], FALSE)))
            return hr;
    }

    m_pWaveFile->ResetFile(FALSE);

    return m_apDSBuffer[0]->SetCurrentPosition(0);
}

// ----------------------------------------------------------------------------
// CWaveFile
// ----------------------------------------------------------------------------

// Constructs the class.  Corresponds to th07.exe FUN_0045e020.
CWaveFile::CWaveFile()
{
    m_pwfx = NULL;
    m_pad04 = 0;
    m_dwSize = 0;
    m_bIsReadingFromMemory = 0;
}

// Closes the file if open.  Corresponds to th07.exe FUN_0045e4b0.
HRESULT CWaveFile::Close()
{
    if (m_dwFlags == WAVEFILE_READ)
    {
        CloseHandle(m_hFile);
        m_hFile = (HANDLE)-1;
    }
    return S_OK;
}

// Destructor.  Corresponds to th07.exe FUN_0045e060.
CWaveFile::~CWaveFile()
{
    Close();
}

// Opens a wave file for reading.  Corresponds to th07.exe FUN_0045e080.
HRESULT CWaveFile::Open(LPCSTR strFileName, WaveFormat *pwfx, DWORD dwFlags)
{
    m_dwFlags = dwFlags;
    m_bIsReadingFromMemory = 0;

    if (m_dwFlags == WAVEFILE_READ)
    {
        if (strFileName == NULL)
            return E_INVALIDARG;

        utils::DebugPrint2("Streaming File Open %s\r\n", strFileName);
        m_hFile = CreateFileA(strFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (m_hFile == (HANDLE)-1)
            return E_FAIL;

        m_pwfx = pwfx;
        ResetFile(FALSE);
        m_dwSize = m_dwRemainingInChunk;
    }
    return S_OK;
}

// Reads from a memory buffer instead of a file.  Corresponds to th07.exe
// FUN_0045e190.
HRESULT CWaveFile::OpenFromMemory(BYTE *pbData, ULONG ulDataSize, WaveFormat *pwfx, DWORD dwFlags, DWORD unused)
{
    m_pwfx = pwfx;
    m_ulDataSize = ulDataSize;
    m_pbData = pbData;
    m_pbDataCur = pbData;
    m_bIsReadingFromMemory = TRUE;

    if (dwFlags != WAVEFILE_READ)
        return E_NOTIMPL;

    return S_OK;
}

// Seeks the file back to the start of the wave data, optionally skipping the
// loop start offset.  Corresponds to th07.exe FUN_0045e210.
HRESULT CWaveFile::ResetFile(BOOL bLoop)
{
    if (m_bIsReadingFromMemory)
    {
        m_pbDataCur = m_pbData;
        if (m_pwfx->m_dataSize > 0)
            m_ulDataSize = m_pwfx->m_dataSize;
        if (bLoop && m_pwfx->m_loopStart > 0)
            m_pbDataCur += m_pwfx->m_loopStart;
    }
    else
    {
        if (m_hFile == (HANDLE)-1)
            return CO_E_NOTINITIALIZED;

        if (!bLoop || m_pwfx->m_loopStart < 1)
        {
            SetFilePointer(m_hFile, g_WaveBaseOffset + m_pwfx->m_dataOffset, NULL, FILE_BEGIN);
            m_dwRemainingInChunk = m_pwfx->m_dataSize;
        }
        else
        {
            SetFilePointer(m_hFile, g_WaveBaseOffset + m_pwfx->m_dataOffset + m_pwfx->m_loopStart, NULL,
                           FILE_BEGIN);
            m_dwRemainingInChunk = m_pwfx->m_dataSize - m_pwfx->m_loopStart;
        }
    }
    return S_OK;
}

// Reads up to dwSizeToRead bytes into pBuffer.  Corresponds to th07.exe
// FUN_0045e360.
HRESULT CWaveFile::Read(BYTE *pBuffer, DWORD dwSizeToRead, DWORD *pdwSizeRead)
{
    if (m_bIsReadingFromMemory)
    {
        if (m_pbDataCur == NULL)
            return CO_E_NOTINITIALIZED;
        if (pdwSizeRead != NULL)
            *pdwSizeRead = 0;

        if ((BYTE *)(m_pbDataCur + dwSizeToRead) > (BYTE *)(m_pbData + m_ulDataSize))
            dwSizeToRead = m_ulDataSize - (DWORD)(m_pbDataCur - m_pbData);

        CopyMemory(pBuffer, m_pbDataCur, dwSizeToRead);
        m_pbDataCur += dwSizeToRead;

        if (pdwSizeRead != NULL)
            *pdwSizeRead = dwSizeToRead;

        return S_OK;
    }
    else
    {
        if (m_hFile == (HANDLE)-1)
            return CO_E_NOTINITIALIZED;
        if (pBuffer == NULL || pdwSizeRead == NULL)
            return E_INVALIDARG;

        *pdwSizeRead = 0;

        DWORD cbDataIn = dwSizeToRead;
        if (cbDataIn > m_dwRemainingInChunk)
            cbDataIn = m_dwRemainingInChunk;

        m_dwRemainingInChunk -= cbDataIn;

        DWORD dwBytesRead = 0;
        if (!ReadFile(m_hFile, pBuffer, cbDataIn, &dwBytesRead, NULL))
            return E_FAIL;

        *pdwSizeRead = dwBytesRead;
        return S_OK;
    }
}

// Returns the cached wave size.  Corresponds to th07.exe FUN_0045e1f0.
DWORD CWaveFile::GetSize()
{
    return m_dwSize;
}
}; // namespace th07
