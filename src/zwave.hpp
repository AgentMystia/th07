// th07 DirectSound streaming-wave helper classes.
//
// This module is the th07 descendant of the DX8 SDK "DSUtil" sample that th06
// also derived from.  Compared to th06 the implementation was substantially
// rewritten:
//
//   * CWaveFile no longer uses mmio* at all.  It opens the file with
//     CreateFileA and reads raw bytes via ReadFile, tracking the remaining
//     "chunk" size itself.
//   * The WAVEFORMATEX pointer stored on CWaveFile is supplied externally by
//     CSoundManager::CreateStreaming / CreateStreamingFromMemory instead of
//     being parsed from the file by a ReadMMIO() helper.  The pointed-to
//     structure is a Zun-custom header (not the standard WAVEFORMATEX) whose
//     layout carries the loop points and data offset used by ResetFile().
//   * CSound gains explicit fields for the last dwPriority / dwFlags passed to
//     Play() and a "playing" flag consumed by CStreamingSound.
//
// All field offsets below were read directly from th07.exe via Ghidra.
#pragma once

#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "dxutil.hpp"

#include <dsound.h>
#include <windows.h>

#define WAVEFILE_READ 1
#define WAVEFILE_WRITE 2

namespace th07
{
class CSoundManager;
class CSound;
class CStreamingSound;
class CWaveFile;

// Zun-custom wave header carried in place of a standard WAVEFORMATEX on
// CWaveFile.  Field offsets were read directly from th07.exe:
//
//   +0x10 m_dataOffset   (added to the wave base DAT_004bdaa0 == 0; read by
//                         CWaveFile::ResetFile @ 0x0045e210)
//   +0x18 m_loopStart    (skipped when ResetFile(true))
//   +0x1c m_dataSize     (size / loopEndPoint)
//   +0x2e m_wBitsPerSample (read by HandleWaveStreamNotification to pick the
//                         silence fill byte: 0x80 for 8-bit, 0x00 otherwise)
//
// The full structure is larger than what th07's zwave touches; only the known
// fields are named and the rest is left as opaque padding so that the layout
// round-trips byte-for-byte against the original allocation sites.
struct WaveFormat
{
    u8 m_prefix[0x10];
    i32 m_dataOffset;     // +0x10
    i32 m_reserved14;     // +0x14
    i32 m_loopStart;      // +0x18
    i32 m_dataSize;       // +0x1c
    u8 m_pad20[0x0e];
    u16 m_wBitsPerSample; // +0x2e
};

// CSoundManager only stores the IDirectSound8 pointer.
class CSoundManager
{
  public:
    LPDIRECTSOUND8 m_pDS;

    CSoundManager()
    {
        m_pDS = NULL;
    }
    ~CSoundManager()
    {
        SAFE_RELEASE(m_pDS);
    }

    HRESULT Initialize(HWND hWnd, DWORD dwCoopLevel, DWORD dwPrimaryChannels, DWORD dwPrimaryFreq,
                       DWORD dwPrimaryBitRate);
    HRESULT SetPrimaryBufferFormat(DWORD dwPrimaryChannels, DWORD dwPrimaryFreq, DWORD dwPrimaryBitRate);
    HRESULT CreateStreaming(CStreamingSound **ppStreamingSound, LPSTR strWaveFileName, DWORD dwCreationFlags,
                            GUID guid3DAlgorithm, DWORD dwNotifyCount, DWORD dwNotifySize, DWORD dwUnknown,
                            HANDLE hNotifyEvent, WaveFormat *pwfx);
    HRESULT CreateStreamingFromMemory(CStreamingSound **ppStreamingSound, BYTE *pbData, ULONG ulDataSize,
                                      DWORD dwCreationFlags, GUID guid3DAlgorithm, DWORD dwNotifyCount,
                                      DWORD dwNotifySize, DWORD dwUnknown, HANDLE hNotifyEvent, WaveFormat *pwfx);
};

// CSound owns one or more DirectSound buffers and (optionally) a CWaveFile.
//
// Recovered layout (size 0x4C):
//   +0x00 vtable
//   +0x04 m_apDSBuffer        (LPDIRECTSOUNDBUFFER*)
//   +0x08 m_dwDSBufferSize
//   +0x0c m_pWaveFile         (CWaveFile*)
//   +0x10 m_dwNumBuffers
//   +0x14 m_dwCurFadeoutProgress
//   +0x18 m_dwTotalFadeout
//   +0x1c m_dwIsFadingOut
//   +0x20 m_dwLastPriority    (last value passed to Play)
//   +0x24 m_dwLastFlags       (last value passed to Play)
//   +0x28 reserved
//   +0x2c reserved            (zeroed by Play)
//   +0x30 m_dwIsPlaying
class CSound
{
  public:
    LPDIRECTSOUNDBUFFER *m_apDSBuffer;
    DWORD m_dwDSBufferSize;

  public:
    CWaveFile *m_pWaveFile;

  public:
    DWORD m_dwNumBuffers;

  public:
    // Fade-out bookkeeping shared with CStreamingSound::UpdateFadeOut.
    i32 m_dwCurFadeoutProgress;
    i32 m_dwTotalFadeout;
    DWORD m_dwIsFadingOut;

    // Cached (dwPriority, dwFlags) of the most recent Play() call; reused by
    // CStreamingSound::Reset() which restarts playback.
    DWORD m_dwLastPriority;
    DWORD m_dwLastFlags;

    DWORD m_pad28;
    DWORD m_pad2c;
    DWORD m_dwIsPlaying;
    // Reserved tail of CSound (0x18 bytes); CStreamingSound::CreateStreaming
    // shadows an embedded copy of the DSBUFFERDESC used to create the buffer
    // into this region (the copy starts at offset +0x34 inside this object).
    DWORD m_reserved34[6];

  public:
    HRESULT RestoreBuffer(LPDIRECTSOUNDBUFFER pDSB, BOOL *pbWasRestored);

  public:
    CSound(LPDIRECTSOUNDBUFFER *apDSBuffer, DWORD dwDSBufferSize, DWORD dwNumBuffers, CWaveFile *pWaveFile);
    virtual ~CSound();

    HRESULT FillBufferWithSound(LPDIRECTSOUNDBUFFER pDSB, BOOL bRepeatWavIfBufferLarger);
    LPDIRECTSOUNDBUFFER GetFreeBuffer();
    LPDIRECTSOUNDBUFFER GetBuffer(DWORD dwIndex);

    HRESULT Play(DWORD dwPriority, DWORD dwFlags);
    HRESULT Stop();
    HRESULT Reset();
};

// CStreamingSound extends CSound with the streaming-notification state.
//
// Recovered layout (size 0x78): first 0x4C bytes overlap CSound, then:
//   +0x4c m_dwLastPlayPos
//   +0x50 m_dwPlayProgress
//   +0x54 reserved
//   +0x58 m_pSoundManager  (CSoundManager*  used to reach m_pDS)
//   +0x5c m_dwLastPlayPos2 (also written by HandleWaveStreamNotification)
//   +0x60 m_dwPlayProgress2
//   +0x64 m_dwNextWriteOffset
//   +0x68 m_bFillNextNotificationWithSilence
//   +0x6c m_dwNotifySize
//   +0x70 m_hNotifyEvent
//   +0x74 reserved
class CStreamingSound : public CSound
{
  public:
    DWORD m_dwLastPlayPos;
    DWORD m_dwPlayProgress;
    DWORD m_pad54;
    CSoundManager *m_pSoundManager;
    DWORD m_dwLastPlayPos2;
    DWORD m_dwPlayProgress2;
    DWORD m_dwNextWriteOffset;
    DWORD m_bFillNextNotificationWithSilence;
    DWORD m_dwNotifySize;
    HANDLE m_hNotifyEvent;
    DWORD m_pad74;

  public:
    CStreamingSound(LPDIRECTSOUNDBUFFER pDSBuffer, DWORD dwDSBufferSize, CWaveFile *pWaveFile, DWORD dwNotifySize);
    ~CStreamingSound();

    HRESULT UpdateFadeOut();
    HRESULT HandleWaveStreamNotification(BOOL bLoopedPlay);
    HRESULT Reset();
};

// CWaveFile (th07 variant) is a thin CreateFileA/ReadFile-based reader.  It
// never calls mmio*; the WAVEFORMATEX-like header is supplied externally.
//
// Recovered layout (size 0x94):
//   +0x00 vtable
//   +0x04 reserved
//   +0x08 m_dwRemainingInChunk  (counts down as Read() consumes bytes)
//   +0x0c..0x2b reserved
//   +0x2c m_dwSize               (returned by GetSize)
//   +0x30..0x77 reserved
//   +0x78 m_dwFlags
//   +0x7c m_bIsReadingFromMemory
//   +0x80 m_pbData               (memory-mode base)
//   +0x84 m_pbDataCur            (memory-mode cursor)
//   +0x88 m_ulDataSize
//   +0x8c m_hFile                (HANDLE from CreateFileA, -1 if none)
//   +0x90 m_pwfx                 (WaveFormat*)
class CWaveFile
{
  public:
    DWORD m_vtable;
    DWORD m_pad04;
    DWORD m_dwRemainingInChunk;
    DWORD m_pad0c[8];
    DWORD m_dwSize;
    DWORD m_pad30[18];
    DWORD m_dwFlags;
    DWORD m_bIsReadingFromMemory;
    BYTE *m_pbData;
    BYTE *m_pbDataCur;
    ULONG m_ulDataSize;
    HANDLE m_hFile;
    WaveFormat *m_pwfx;

    CWaveFile();
    ~CWaveFile();

    HRESULT Open(LPCSTR strFileName, WaveFormat *pwfx, DWORD dwFlags);
    HRESULT OpenFromMemory(BYTE *pbData, ULONG ulDataSize, WaveFormat *pwfx, DWORD dwFlags, DWORD unused);
    HRESULT Close();

    HRESULT Read(BYTE *pBuffer, DWORD dwSizeToRead, DWORD *pdwSizeRead);

    DWORD GetSize();
    HRESULT ResetFile(BOOL bLoop);
};
}; // namespace th07

ZUN_ASSERT_SIZE(th07::CSoundManager, 0x04);
ZUN_ASSERT_SIZE(th07::CSound, 0x4c);
ZUN_ASSERT_SIZE(th07::CStreamingSound, 0x78);
ZUN_ASSERT_SIZE(th07::CWaveFile, 0x94);
