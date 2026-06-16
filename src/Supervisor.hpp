#pragma once


//












//

// DeletedCallback/LoadConfig/SetupDInput/DrawFpsCounter/PlayAudio/FadeOutMusic


#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "ZunResult.hpp"
#include "ZunBool.hpp"
#include "Chain.hpp"


namespace th07
{
struct Supervisor;
struct ChainElem;
struct ChainCallbackResultTag;
} // namespace th07



struct D3DXMATRIX_FAKE
{
    f32 m[16];
};

struct D3DVIEWPORT8_FAKE
{
    u32 X;
    u32 Y;
    u32 Width;
    u32 Height;
    f32 MinZ;
    f32 MaxZ;
};

struct D3DPRESENT_PARAMETERS_FAKE
{
    u32 fields[13];

};

struct DIDEVCAPS_FAKE
{
    u32 dwSize;
    u32 dwFlags;
    u32 dwDevType;
    u32 dwAxes;
    u32 dwButtons;
    u32 dwPOVs;
    u32 dwFirmwareRevision;
    u32 dwHardwareRevision;
    u32 dwFFDriverVersion;
    u32 dwFFSamplePeriod;
    u32 dwFFMinTimeResolution;
};


struct D3DCAPS8_FAKE
{
    u32 raw[0x40];
};

namespace th07
{
#define GAME_VERSION 0x70002

enum GameConfigOptsShifts
{
    GCOS_USE_D3D_HW_TEXTURE_BLENDING = 0x0,
    GCOS_DONT_USE_VERTEX_BUF = 0x1,
    GCOS_FORCE_16BIT_COLOR_MODE = 0x2,
    GCOS_CLEAR_BACKBUFFER_ON_REFRESH = 0x3,
    GCOS_DISPLAY_MINIMUM_GRAPHICS = 0x4,
    GCOS_SUPPRESS_USE_OF_GOROUD_SHADING = 0x5,
    GCOS_TURN_OFF_DEPTH_TEST = 0x6,
    GCOS_FORCE_60FPS = 0x7,
    GCOS_NO_COLOR_COMP = 0x8,
    GCOS_LAUNCH_WINDOWED = 0x9,
    GCOS_NO_FOG = 0xa,
    GCOS_DONT_RENDER_ITEMS = 0xb,
    GCOS_NO_DIRECTINPUT_PAD = 0xc,
    GCOS_FORCE_REFERENCE_RASTERIZER = 0xd,

};

struct ControllerMapping
{
    i16 shootButton;
    i16 bombButton;
    i16 focusButton;
    i16 menuButton;
    i16 upButton;
    i16 downButton;
    i16 leftButton;
    i16 rightButton;
    i16 skipButton;
};

enum MusicMode
{
    MUSIC_OFF = 0,
    MUSIC_WAV = 1,
    MUSIC_MIDI = 2
};






//   0x12 pad
//   0x14 version (i32) -> DAT_00575a7c
//   0x18 padXAxis (i16) -> DAT_00575a80
//   0x1a padYAxis (i16) -> DAT_00575a82
//   0x1c lifeCount (u8) -> DAT_00575a84
//   0x1d bombCount (u8) -> DAT_00575a85
//   0x1e colorMode16bit (u8) -> DAT_00575a86
//   0x1f musicMode (u8) -> DAT_00575a87
//   0x20 playSounds (u8) -> DAT_00575a88
//   0x21 defaultDifficulty (u8) -> DAT_00575a89
//   0x22 windowed (u8) -> DAT_00575a8a
//   0x23 frameskipConfig (u8) -> DAT_00575a8b
//   0x24 playModeA (u8) -> DAT_00575a8c
//   0x25 playModeB (u8) -> DAT_00575a8d
//   0x26 chara (u8) -> DAT_00575a8e
//   0x27 pad
//   0x28..0x33 pad (unk)
//   0x34 opts (u32) -> DAT_00575a9c
struct GameConfiguration
{
    ControllerMapping controllerMapping; // 0x00
    i16 _pad12;                          // 0x12
    i32 version;                         // 0x14
    i16 padXAxis;                        // 0x18
    i16 padYAxis;                        // 0x1a
    u8 lifeCount;                        // 0x1c
    u8 bombCount;                        // 0x1d
    u8 colorMode16bit;                   // 0x1e
    u8 musicMode;                        // 0x1f
    u8 playSounds;                       // 0x20
    u8 defaultDifficulty;                // 0x21
    u8 windowed;                         // 0x22
    u8 frameskipConfig;                  // 0x23
    u8 unk24;                            // 0x24
    u8 unk25;                            // 0x25
    u8 unk26;                            // 0x26
    u8 unk27;                            // 0x27
    u8 unk28[12];                        // 0x28..0x33
    u32 opts;                            // 0x34
};
ZUN_ASSERT_SIZE(GameConfiguration, 0x38);



enum SupervisorState
{
    SUPERVISOR_STATE_INIT = 0,
    SUPERVISOR_STATE_MAINMENU = 1,
    SUPERVISOR_STATE_GAMEMANAGER = 2,
    SUPERVISOR_STATE_GAMEMANAGER_REINIT = 3,
    SUPERVISOR_STATE_EXITSUCCESS = 4,
    SUPERVISOR_STATE_RESULTSCREEN = 5,
    SUPERVISOR_STATE_MUSICROOM = 6,
    SUPERVISOR_STATE_EXITERROR = 7,
    SUPERVISOR_STATE_RESULTSCREEN_FROMGAME = 8,
    SUPERVISOR_STATE_MAINMENU_REPLAY = 9,
    SUPERVISOR_STATE_ENDING = 10,
    SUPERVISOR_STATE_ENDING_B = 11,
    SUPERVISOR_STATE_ENDING_C = 12,
};

struct Supervisor
{

    static ZunResult RegisterChain();
    static ChainCallbackResult __fastcall OnUpdate(Supervisor *s);
    static ChainCallbackResult __fastcall OnDraw(Supervisor *s);
    static ZunResult __fastcall AddedCallback(Supervisor *s);
    static ZunResult __fastcall DeletedCallback(Supervisor *s);
    static void __fastcall DrawFpsCounter(i32 drawArg);

    // Internal helpers (mapped in config/mapping.csv, called by other Supervisor methods).
    static void DebugPrint(char *fmt, ...);
    static i32 D3DDiscard(i32 mode);
    i32 AutosaveScore(char *p1, i32 p2, i32 p3); // __thiscall: ECX=g_Supervisor singleton
    static void SomeCleanup1();
    static void ReleaseAnm0();
    static void HeapFreeAll();
    static void SomeCleanup4();
    static void SomeCleanup5();
    static void MidiClearTracks();
    static void Cleanup3();


    ZunResult ReadMidiFile(u32 midiFileIdx);
    ZunResult PlayMidiFile(char *midiPath);
    ZunResult PlayAudio(i32 channel, char *path);
    ZunResult StopAudio(i32 channel);
    ZunResult SetupMidiPlayback();
    ZunResult FadeOutMusic(f32 fadeOutSeconds);

    static ZunResult __fastcall SetupDInput(Supervisor *s);

    ZunResult LoadConfig(char *path);

    void TickTimer(i32 *frames, f32 *subframes);

    f32 FramerateMultiplier()
    {
        return this->framerateMultiplier;
    }


    void *hInstance;                  // 0x000 DAT_00575950
    void *d3dIface;                   // 0x004 DAT_00575954
    void *d3dDevice;                  // 0x008 DAT_00575958
    void *dinputIface;                // 0x00c
    void *keyboard;                   // 0x010
    void *controller;                 // 0x014
    DIDEVCAPS_FAKE controllerCaps;
    void *hwndGameWindow;             // 0x044
    D3DXMATRIX_FAKE viewMatrix;
    D3DXMATRIX_FAKE projectionMatrix;
    D3DVIEWPORT8_FAKE viewport;
    u32 _viewportPad;
    D3DPRESENT_PARAMETERS_FAKE presentParameters;
    GameConfiguration cfg;

    i32 calcCount;                    // 0x150 DAT_00575aa0
    i32 wantedState;                  // 0x154 DAT_00575aa4
    i32 curState;                     // 0x158 DAT_00575aa8
    i32 wantedState2;                 // 0x15c
    i32 unk15c[2];
    i32 unk198;
    i32 unkIsInEnding;
    i32 vsyncDisabled;
    i32 _pad170;
    u32 hasD3dHardwareVertexProcessing; // 0x174 DAT_00575ac4
    f32 framerateMultiplier;          // 0x178 DAT_00575ac8
    void *midiOutput;                 // 0x17c DAT_00575acc（MidiOutput *）
    f32 unk1b4;
    f32 unk1b8;
    i32 unk188;
    u32 frameBasedStuffFlags;
    u32 startupTimeBeforeMenuMusic;   // 0x190 DAT_00575ae0（timeGetTime）
    u32 lastFrameTime;                // 0x194 DAT_00575ae4
    u8 unk198_to_caps[0x1b0 - 0x198]; // 0x198..0x1af
    D3DCAPS8_FAKE d3dCaps;


    u8 tail[0x2c8 - (0x1b0 + sizeof(D3DCAPS8_FAKE))];
};




DIFFABLE_EXTERN(Supervisor, g_Supervisor)
DIFFABLE_EXTERN(ControllerMapping, g_ControllerMapping)
DIFFABLE_EXTERN(u16, g_LastFrameInput)
DIFFABLE_EXTERN(u16, g_CurFrameInput)
DIFFABLE_EXTERN(u16, g_IsEigthFrameOfHeldInput)
DIFFABLE_EXTERN(u16, g_NumOfFramesInputsWereHeld)


DIFFABLE_EXTERN(void *, g_Pbg4Archive)        // DAT_00575c1c
DIFFABLE_EXTERN(char *, g_Pbg4ArchiveName)

}; // namespace th07
