#pragma once

// MidiOutput / MidiDevice / MidiTimer
//
// Reverse-engineered from th07.exe. The three classes implement sequenced
// MIDI playback through winmm (midiOutXxx + a periodic multimedia timer).
//
// Layout differs from th06 in a few places:
//   * The unused `unk124` field between `tempo` and `volume` was removed, so
//     every field after `tempo` is shifted down by 4 bytes (volume @0x128,
//     unk130 @0x130, tracks @0x138, midiOutDev @0x13c).
//   * A new `currentFileIdx` field at @0x10 records which midiFileData slot
//     is currently parsed/playing; StopPlayback resets it to -1.
//   * `unk2f0` / `unk2f8` grew from ULONGLONG (8 bytes) to plain u32 storage
//     split across two dwords each to match the 64-bit save/restore of the
//     (tempo, volume, unk130) triplet in ProcessMsg's breath/foot controller
//     handling.
//
// All member offsets below were verified against the th07 binary.

#include <windows.h>
#include <mmsystem.h>

#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "ZunBool.hpp"
#include "ZunResult.hpp"

namespace th07
{
struct MidiTimer
{
    MidiTimer();
    ~MidiTimer();

    // Pure virtual in the original; MidiOutput overrides it. The MidiTimer
    // vtable itself points at an empty stub (FUN_0044d620), so instantiating
    // a bare MidiTimer is a no-op for timer ticks.
    virtual void OnTimerElapsed() = 0;

    i32 StopTimer();
    u32 StartTimer(u32 delay, LPTIMECALLBACK cb, DWORD_PTR data);

    static void CALLBACK DefaultTimerCallback(u32 uTimerID, u32 uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);

    u32 timerId;
    TIMECAPS timeCaps;
};
ZUN_ASSERT_SIZE(MidiTimer, 0x10);

enum MidiOpcode
{
    MIDI_OPCODE_CHANNEL_1 = 0x01,
    MIDI_OPCODE_CHANNEL_2 = 0x02,
    MIDI_OPCODE_CHANNEL_3 = 0x03,
    MIDI_OPCODE_CHANNEL_4 = 0x04,
    MIDI_OPCODE_CHANNEL_5 = 0x05,
    MIDI_OPCODE_CHANNEL_6 = 0x06,
    MIDI_OPCODE_CHANNEL_7 = 0x07,
    MIDI_OPCODE_CHANNEL_8 = 0x08,
    MIDI_OPCODE_CHANNEL_9 = 0x09,
    MIDI_OPCODE_CHANNEL_A = 0x0A,
    MIDI_OPCODE_CHANNEL_B = 0x0B,
    MIDI_OPCODE_CHANNEL_C = 0x0C,
    MIDI_OPCODE_CHANNEL_D = 0x0D,
    MIDI_OPCODE_CHANNEL_E = 0x0E,
    MIDI_OPCODE_CHANNEL_F = 0x0F,
    MIDI_OPCODE_NOTE_OFF = 0x80,
    MIDI_OPCODE_NOTE_ON = 0x90,
    MIDI_OPCODE_POLYPHONIC_AFTERTOUCH = 0xA0,
    MIDI_OPCODE_MODE_CHANGE = 0xB0,
    MIDI_OPCODE_PROGRAM_CHANGE = 0xC0,
    MIDI_OPCODE_CHANNEL_AFTERTOUCH = 0xD0,
    MIDI_OPCODE_PITCH_BEND_CHANGE = 0xE0,
    MIDI_OPCODE_SYSTEM_EXCLUSIVE = 0xF0,
    MIDI_OPCODE_MIDI_TIME_CODE_QTR_FRAME = 0xF1,
    MIDI_OPCODE_SONG_POSITION_POINTER = 0xF2,
    MIDI_OPCODE_SONG_SELECT = 0xF3,
    MIDI_OPCODE_RESERVED_F4 = 0xF4,
    MIDI_OPCODE_RESERVED_F5 = 0xF5,
    MIDI_OPCODE_TUNE_REQUEST = 0xF6,
    MIDI_OPCODE_END_OF_SYSEX = 0xF7,
    MIDI_OPCODE_TIMING_CLOCK = 0xF8,
    MIDI_OPCODE_RESERVED_F9 = 0xF9,
    MIDI_OPCODE_START = 0xFA,
    MIDI_OPCODE_CONTINUE = 0xFB,
    MIDI_OPCODE_STOP = 0xFC,
    MIDI_OPCODE_RESERVED_FD = 0xFD,
    MIDI_OPCODE_ACTIVE_SENSING = 0xFE,
    MIDI_OPCODE_SYSTEM_RESET = 0xFF,
};

struct MidiTrack
{
    u32 trackPlaying;
    i32 trackLengthOther;
    u32 trackLength;
    u8 opcode;
    u8 *trackData;
    u8 *curTrackDataCursor;
    u8 *startTrackDataMaybe;
    u32 unk1c;
};
ZUN_ASSERT_SIZE(MidiTrack, 0x20);

struct MidiDevice
{
    MidiDevice();
    ~MidiDevice();

    ZunResult Close();
    ZunBool OpenDevice(u32 uDeviceId);
    ZunBool SendShortMsg(u8 midiStatus, u8 firstByte, u8 secondByte);
    ZunBool SendLongMsg(LPMIDIHDR pmh);

    HMIDIOUT handle;
    u32 deviceId;
};
ZUN_ASSERT_SIZE(MidiDevice, 0x8);

struct MidiChannel
{
    u8 keyPressedFlags[16];
    u8 instrument;
    u8 instrumentBank;
    u8 pan;
    u8 effectOneDepth;
    u8 effectThreeDepth;
    u8 channelVolume;
    u8 modifiedVolume;
};
ZUN_ASSERT_SIZE(MidiChannel, 0x17);

struct MidiOutput : MidiTimer
{
    MidiOutput();
    ~MidiOutput();

    void OnTimerElapsed();

    ZunResult UnprepareHeader(LPMIDIHDR pmh);

    ZunResult StopPlayback();
    void LoadTracks();
    void ClearTracks();
    ZunResult ReadFileData(u32 idx, char *path);
    void ReleaseFileData(u32 idx);
    ZunResult ParseFile(i32 fileIdx);
    void ProcessMsg(MidiTrack *track);

    ZunResult LoadFile(char *midiPath);
    ZunResult Play();

    u32 SetFadeOut(u32 ms);
    void FadeOutSetVolume(i32 volume);

    static u16 Ntohs(u16 val);
    static u32 SkipVariableLength(u8 **curTrackDataCursor);

    // Byte-swap a 32-bit big-endian value read from a MIDI chunk to host
    // order. Inlined at every call site (ParseFile), so there is no
    // standalone function in the binary.
    static u32 Ntohl(u32 val)
    {
        u8 tmp[4];

        tmp[0] = ((u8 *)&val)[3];
        tmp[1] = ((u8 *)&val)[2];
        tmp[2] = ((u8 *)&val)[1];
        tmp[3] = ((u8 *)&val)[0];

        return *(const u32 *)tmp;
    }

    // Slot currently parsed / playing. Set to the fileIdx passed to ParseFile
    // and reset to -1 by StopPlayback / the constructor.
    i32 currentFileIdx; // 0x010

    MIDIHDR *midiHeaders[32]; // 0x014
    i32 midiHeadersCursor;    // 0x094
    u8 *midiFileData[32];     // 0x098
    i32 numTracks;            // 0x118
    u32 format;               // 0x11c
    i32 divisions;            // 0x120
    i32 tempo;                // 0x124
    u64 volume;               // 0x128
    u64 unk130;               // 0x130
    MidiTrack *tracks;        // 0x138
    MidiDevice midiOutDev;    // 0x13c
    u8 unk144[16];            // 0x144
    MidiChannel channels[16]; // 0x154
    i8 unk2c4;                // 0x2c4
    f32 fadeOutVolumeMultiplier; // 0x2c8
    u32 fadeOutLastSetVolume;    // 0x2cc
    u32 unk2d0;               // 0x2d0
    u32 unk2d4;               // 0x2d4
    u32 unk2d8;               // 0x2d8
    u32 unk2dc;               // 0x2dc
    u32 fadeOutFlag;          // 0x2e0
    i32 fadeOutInterval;      // 0x2e4
    i32 fadeOutElapsedMS;     // 0x2e8
    u32 unk2ec;               // 0x2ec
    u32 unk2f0;               // 0x2f0  (low dword of saved volume)
    u32 unk2f4;               // 0x2f4  (high dword of saved volume)
    u32 unk2f8;               // 0x2f8  (low dword of saved unk130)
    u32 unk2fc;               // 0x2fc  (high dword of saved unk130)
};
ZUN_ASSERT_SIZE(MidiOutput, 0x300);
}; // namespace th07
