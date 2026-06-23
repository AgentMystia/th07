// MainMenu module for th07 (Perfect Cherry Blossom).
//
// Lifts the orig cluster (all verified from th07.exe disassembly):
//   FUN_0045c5d0  MainMenu::RegisterChain   (alloc 0xd158 obj, register chain)
//   FUN_0045c4c8  MainMenu::AddedCallback-thunk  -> FUN_0045bf15
//   FUN_0045bf15  MainMenu::AddedCallback   (load title01.anm, play th07_01.mid)
//   FUN_004554d6  MainMenu::OnCalc (per-frame)
//   FUN_0045bd6c  MainMenu::OnDraw (per-frame)
//   FUN_0045c546  MainMenu::DeletedCallback
//
// RegisterChain is lifted faithfully. AddedCallback lifts the boot-critical
// tail (title01.anm load + bgm/th07_01.mid play) faithfully; the giant
// phantasm.jpg boot-fade loop (DAT_0062f894 == 0 first-time path) is left as
// a skip because it needs the full AnmManager draw stack to be useful. The
// calc/draw callbacks are minimal CONTINUE returns pending the full
// menu-state-machine lift (the 0xe menu sprites, input handling, transitions).
#include "MainMenu.hpp"

#include "AnmManager.hpp"
#include "Chain.hpp"
#include "MidiOutput.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <stdio.h>

// Boot-path debug log helpers (defined in link_stubs.cpp).
extern "C" void *__cdecl th07_fopen_w(const char *path, const char *mode);
extern "C" void __cdecl th07_fprintf(void *fp, const char *fmt, ...);
extern "C" void __cdecl th07_fclose(void *fp);
// rdata float used by the AddedCallback sprite priming.
extern "C" const f32 g_AnmMgrScaleDiv_498a70; // DAT_00498A70 (sprite half-extent divisor, 2.0f)

#include <windows.h>
#include <stdio.h>

namespace th07
{
namespace MainMenu
{

// ---------------------------------------------------------------------------
// Orig DAT slots referenced by FUN_0045bf15. Declared as extern "C" globals
// in link_globals.cpp; the boot-path reads them.
//   0x00575aac  g_SupervisorAac       -- some accumulated state (0 on first boot)
//   0x00575a87  g_SupervisorMusicMode -- 1 = WAVE, 2 = MIDI (set by cfg read)
//   0x00575acc  g_SupervisorMidiSlot  -- MidiOutput* allocated lazily by boot
//   0x0062f648  g_MainMenuFlagBits    -- menu-control flag word (boot skips
//                                        BGM when bit 1 is set)
// ---------------------------------------------------------------------------
extern "C" i32  g_SupervisorAac_575aac;
extern "C" u8   g_SupervisorMusicMode_575a87;
extern "C" void *g_SupervisorMidiSlot_575acc;
extern "C" i32  g_MainMenuFlagBits_62f648;

// AnmManager::LoadAnm -- 0x0044df90. Normal-build stub returns 0 (success).
extern "C" i32 __fastcall AnmManager_LoadAnm(void *anm, i32 a, char *path, i32 b);
// operator new thunk -- 0x0047d441. Normal build wraps malloc.
extern "C" void *__fastcall operator_new_th07(u32 size);
// MidiOutput::Play -- 0x00436650. this=MidiOutput*, trackIdx, path.
extern "C" i32 __fastcall MidiOutput_Play(void *p, i32 a, char *path);
// SoundPlayer wave-play -- 0x0044d2f0. mode, trackCtx, path.
extern "C" void __fastcall SoundPlayer_PlayWaveVariant(i32 a, void *b, char *path);

// MainMenu object layout lives in MainMenu.hpp (MainMenuObj, 0xd158 bytes).

// ---------------------------------------------------------------------------
// AddedCallback (orig FUN_0045bf15, simplified). The orig is a 0x5b0-byte
// function that also loops 900 frames loading data/title/phantasm.jpg with a
// boot-fade, sets up the 0xe menu sprites, picks an entry state from
// g_SupervisorAac_575aac, and configures per-sprite blend modes. For the
// normal-build demo we lift only the asset-load + BGM-play tail:
//   - AnmManager::LoadAnm(0x20, "data/title01.anm", 0x900)
//   - if music mode initialised and not in all-clear (5) state, play
//     bgm/th07_01.mid (MIDI) or bgm/th07_01.wav (WAVE) via the BGM dispatcher.
// Returns ZUN_SUCCESS on happy path, -1 if the anm load fails.
// ---------------------------------------------------------------------------
static ZunResult __fastcall AddedCallback(MainMenuObj *mm)
{
    ZunResult iVar;

    (void)mm;
#ifndef DIFFBUILD
    {
        void *d = th07_fopen_w("boot_debug.log", "a");
        if (d) { th07_fprintf(d, "[menu] AddedCallback entered, LoadAnm(title01.anm)\n"); th07_fclose(d); }
    }
#endif

    // 0x20 -> anmIdx slot for the menu. "data/title01.anm", 0x900 sprites.
    iVar = g_AnmManager->LoadAnm(0x20, "data/title01.anm", 0x900);
#ifndef DIFFBUILD
    {
        void *d = th07_fopen_w("boot_debug.log", "a");
        if (d) { th07_fprintf(d, "[menu] LoadAnm returned %d\n", (i32)iVar); th07_fclose(d); }
    }
#endif
    if (iVar != ZUN_SUCCESS)
    {
        return (ZunResult)-1;
    }

    // BGM dispatch: only when bit 1 of the menu flag word is clear.
    if ((g_MainMenuFlagBits_62f648 >> 1 & 1) == 0)
    {
        if (g_SupervisorAac_575aac != 5) // 5 == all-clear state (skip BGM)
        {
            // FUN_00439dd0 (orig): musicMode 2 (MIDI) -> MidiOutput::Play;
            // mode 1 (WAVE) -> rewrite .mid -> .wav + SoundPlayer play.
            if (g_SupervisorMusicMode_575a87 == 2)
            {
                if (g_SupervisorMidiSlot_575acc != 0)
                {
#ifndef DIFFBUILD
                    // Normal-build: call the real MidiOutput methods so the
                    // menu BGM actually plays. Orig MidiOutput_Play (stub)
                    // returns 0 without playing.
                    MidiOutput *midi = (MidiOutput *)g_SupervisorMidiSlot_575acc;
                    midi->LoadFile("bgm/th07_01.mid");
                    midi->Play();
#else
                    MidiOutput_Play(g_SupervisorMidiSlot_575acc, 8, "bgm/th07_01.mid");
#endif
                }
            }
            else if (g_SupervisorMusicMode_575a87 == 1)
            {
#ifndef DIFFBUILD
                // WAV mode: start streaming th07_01.wav. thbgm.dat is a ZWAV
                // container (not a single RIFF file), so we cannot use
                // LoadBgmPath (which opens the file as a CWaveFile). Instead:
                //   1. PreLoadBgm(0, name) reads the wav chunk from thbgm.dat
                //      into wavData[0] and fills wavFmtEntry[0].
                //   2. CreateStreamingFromMemory creates the CStreamingSound
                //      from that buffer (this is what the orig
                //      ProcessSoundQueues stage 2 does internally).
                //   3. backgroundMusic->Play starts playback.
                g_SoundPlayer.PreLoadBgm(0, "bgm/th07_01.wav");
                {
                    void *d = th07_fopen_w("boot_debug.log", "a");
                    if (d) { th07_fprintf(d, "[menu] PreLoadBgm done, wavData[0]=%p wavFmtEntry[0]=%p\n",
                                          g_SoundPlayer.wavData[0], (void *)g_SoundPlayer.wavFmtEntry[0]); th07_fclose(d); }
                }
                if (g_SoundPlayer.wavData[0] != 0 && g_SoundPlayer.wavFmtEntry[0] != 0 &&
                    g_SoundPlayer.manager != 0)
                {
                    // The WAVEFORMATEX lives at WaveFormat + 0x20:
                    //   +0x20 wFormatTag (u16) + nChannels (u16)
                    //   +0x24 nSamplesPerSec (u32)
                    //   +0x28 nAvgBytesPerSec (u32)
                    //   +0x2c nBlockAlign (u16) + wBitsPerSample (u16)
                    // notifySize = (nAvgBytesPerSec / 4) rounded down to a
                    // multiple of nBlockAlign (matching the orig
                    // BackgroundMusicPlayerThread's fill quantum).
                    u8 *wfx = (u8 *)g_SoundPlayer.wavFmtEntry[0] + 0x20;
                    u16 blockAlign = *(u16 *)(wfx + 0x0c);
                    u16 bitsPerSample = *(u16 *)(wfx + 0x0e);
                    u32 avgBytesPerSec = *(u32 *)(wfx + 0x08);
                    u32 notifySize = avgBytesPerSec >> 4;
                    notifySize -= (notifySize % (blockAlign > 0 ? blockAlign : 1));
                    (void)bitsPerSample;
                    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
                    g_SoundPlayer.backgroundMusicUpdateEvent = ev;
                    g_SoundPlayer.backgroundMusicThreadHandle = CreateThread(
                        NULL, 0, SoundPlayer::BackgroundMusicPlayerThread,
                        g_Supervisor.hwndGameWindow, 0, &g_SoundPlayer.backgroundMusicThreadId);
                    HRESULT hr = g_SoundPlayer.manager->CreateStreamingFromMemory(
                        &g_SoundPlayer.backgroundMusic,
                        (BYTE *)g_SoundPlayer.wavData[0],
                        g_SoundPlayer.dataSize[0],
                        DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY,
                        GUID_NULL, 4, notifySize, 0x10, ev,
                        g_SoundPlayer.wavFmtEntry[0]);
                    {
                        void *d = th07_fopen_w("boot_debug.log", "a");
                        if (d) { th07_fprintf(d, "[menu] CreateStreamingFromMemory hr=0x%lx bgMusic=%p notifySize=%u\n",
                                              (unsigned long)hr, (void *)g_SoundPlayer.backgroundMusic, notifySize); th07_fclose(d); }
                    }
                    if (SUCCEEDED(hr) && g_SoundPlayer.backgroundMusic != 0)
                    {
                        // DSBPLAY_LOOPING (0x1) is mandatory for a streaming
                        // buffer: it keeps the play cursor circulating so the
                        // position-notify events keep firing and the BGM thread
                        // keeps refilling. Without it the buffer plays once
                        // (~0.25s) and falls silent.
                        g_SoundPlayer.backgroundMusic->Play(0, DSBPLAY_LOOPING);
                        {
                            void *d = th07_fopen_w("boot_debug.log", "a");
                            if (d) { th07_fprintf(d, "[menu] backgroundMusic->Play(LOOPING) called\n"); th07_fclose(d); }
                        }
                    }
                }
#else
                SoundPlayer_PlayWaveVariant(1, (void *)0, "bgm/th07_01.wav");
#endif
            }
        }
    }

    // Construct the 0xe (14) menu AnmVm sprites. Orig FUN_0045bf15 tail loop
    // (0x45c405..0x45c49a) sets the script-table key 0x706, calls
    // SetAndExecuteScript(scripts[0x706]) + SetActiveSprite. The script's
    // first instruction (opcode 3 = sprite-select, time N) is supposed to
    // re-bind the sprite via ExecuteScript case 4. However the AnmRawInstr
    // field layout in AnmManager.hpp is currently swapped (opcode lives at
    // +0x00, not +0x02 as documented), so every ExecuteScript case
    // mismatches and the sprite never gets bound. Until that pre-existing
    // P1.2 debt is fixed, bind the sprite + sane defaults directly here so
    // the menu at least renders.
    //
    // NOTE: sprites[0x706+i] point at text.anm slots (sourceFileIndex=0,
    // which uses an external texture that is empty until
    // LoadTexture(th07logo.jpg) wires it -- currently broken). The title01.anm
    // chain loads embedded textures at anmIdx 32..41 (textures[32..41]). The
    // first entry (anmIdx=32, loaded at sprite offset 0x900) is a 1-sprite
    // title graphic backed by textures[32] (which now contains real pixel
    // data). We bind all 14 menu Vm slots to sprite 0x900 so they all draw
    // the same title image as a sanity check that the 2D pipeline works
    // end-to-end. Once the text.anm external-texture path is fixed, the per-i
    // binding below can go back to sprites[0x706+i].
    for (i32 i = 0; i < 0xe; i++)
    {
        AnmVm *vm = &mm->sprites[i];
        *(i16 *)((u8 *)vm + 0x1d8) = (i16)0x900; // anmFileIndex / script key
        // Bind sprite 0x900 (title01.anm entry 0, texture 32 with real pixels).
        AnmLoadedSprite *sp = &g_AnmManager->sprites[0x900];
        vm->sprite = sp;
        vm->activeSpriteIndex = (i16)0x900;
        vm->anmFileIndex = (i16)0x900;
        // Visible + in-scope flags (bits 0,1) = 0x7 (matches Initialize).
        *(u16 *)((u8 *)vm + 0x1c0) = 0x7;
        // Identity scale + opaque white colour (Initialize defaults).
        vm->scaleX = 1.0f;
        vm->scaleY = 1.0f;
        vm->color = 0xffffffff;
        // Stack the 14 sprites vertically down the screen so each is visible
        // (orig positions them via the script; with the interpreter disabled
        // we place them at fixed offsets).
        vm->positionOffsetX = 320.0f;
        vm->positionOffsetY = 100.0f + (f32)i * 24.0f;
        vm->positionOffsetZ = 0.0f;
        // uv scroll at origin.
        vm->uvScrollPos.x = 0.0f;
        vm->uvScrollPos.y = 0.0f;
        // Prime the texture matrices so DrawNoRotation's cull/bounds pass has
        // valid data (Initialize zeros them; the orig primes them in
        // SetActiveSprite via the sprite's uvStart/uvEnd).
        vm->textureMatrix.m[0][0] = sp->uvScaleX;
        vm->textureMatrix.m[1][1] = sp->uvScaleY;
        vm->scratchMatrix.m[0][0] = sp->widthPx / g_AnmMgrScaleDiv_498a70;
        vm->scratchMatrix.m[1][1] = sp->heightPx / g_AnmMgrScaleDiv_498a70;
    }
    mm->activeVm = &mm->sprites[0];
    mm->spriteCount = 0xe;

#ifndef DIFFBUILD
    {
        void *d = th07_fopen_w("boot_debug.log", "a");
        if (d)
        {
            AnmLoadedSprite *sp = &g_AnmManager->sprites[0x900];
            th07_fprintf(d,
                         "[spr900] srcFile=%d startPx=(%.0f,%.0f) endPx=(%.0f,%.0f) "
                         "texW=%.0f texH=%.0f widthPx=%.0f heightPx=%.0f "
                         "tex32=%p\n",
                         (i32)sp->sourceFileIndex,
                         sp->startPixelInclusiveX, sp->startPixelInclusiveY,
                         sp->endPixelInclusiveX, sp->endPixelInclusiveY,
                         sp->textureWidth, sp->textureHeight,
                         sp->widthPx, sp->heightPx,
                         (void *)g_AnmManager->textures[32]);
            th07_fclose(d);
        }
    }
#endif

    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// OnCalc (orig FUN_004554d6 dispatcher). Drives the menu state machine. For
// the boot demo we implement only state 0 (FUN_004555dd) minimally: tick all
// active AnmVm sprites via the AnmManager ExecuteScript pass, then run the
// active-vm script. Other menu states (2..0xe) are reached only via input,
// which is stubbed, so they are no-op CONTINUE returns. The trailing
// FUN_004550c0 (tick-all-sprites) + FUN_00450d60(activeVm) calls are done
// unconditionally so sprites keep animating every frame.
// ---------------------------------------------------------------------------
static ChainCallbackResult __fastcall OnCalc(MainMenuObj *mm)
{
    // State 0 (main menu) is the boot default. With the AnmRawInstr layout
    // bug outstanding the ANM interpreter can't advance sprites, so we skip
    // the ExecuteScript pass here and just bump the per-frame counters (the
    // orig FUN_004550c0 tick-all + FUN_00450d60(activeVm) tail is left for the
    // interpreter-fix follow-up).
    mm->frameCounter = mm->frameCounter + 1;
    mm->stateFrame = mm->stateFrame + 1;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ---------------------------------------------------------------------------
// OnDraw (orig FUN_0045bd6c). Iterates the menu sprites and draws each one
// directly via DrawPrimitiveUP in screen space (D3DFVF_XYZRHW | DIFFUSE |
// TEX1). This is a self-contained 2D blit that bypasses the buggy AnmManager
// batch path (FUN_0044efb0 + AnmRawInstr interpreter) so the menu renders
// visibly even before those are polished.
//
// Per sprite:
//   - source texture = textures[sprite->sourceFileIndex]
//   - half extent    = (sprite->widthPx / 2, sprite->heightPx / 2)
//   - center         = (positionOffsetX, positionOffsetY) + frameOffset
//   - 4 corner xy    = center +/- half-extent (no rotation; sprites[0x706+i]
//                       are all axis-aligned text glyphs on boot)
//   - uv corners     = sprite->uvStart / sprite->uvEnd (a rect)
//   - color          = vm->color (modulated by texture alpha)
//   - 6 vertices     = 2 triangles (v0,v1,v2 + v0,v2,v3) = TRIANGLELIST
// ---------------------------------------------------------------------------
// Screen-space vertex used by the self-contained 2D draw. Matches the FVF
// D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 (= 0x144).
struct MenuVertex
{
    f32 x;        // screen-space x (pixels)
    f32 y;        // screen-space y (pixels)
    f32 z;        // 0.0
    f32 rhw;      // 1.0 (no perspective divide)
    D3DCOLOR diffuse;
    f32 u;
    f32 v;
};

static ChainCallbackResult __fastcall OnDraw(MainMenuObj *mm)
{
    IDirect3DDevice8 *dev;
    AnmVm *vm;
    AnmLoadedSprite *sprite;
    IDirect3DTexture8 *tex;
    MenuVertex verts[6];
    f32 halfW, halfH;
    f32 centerX, centerY;
    f32 leftX, rightX, topY, botY;
    f32 u0, u1, v0, v1;
    D3DCOLOR color;
    i32 i;

    dev = (IDirect3DDevice8 *)g_SupervisorD3dDevice_575958;
    if (dev == 0)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

#ifndef DIFFBUILD
    // Single first-frame trace so we can confirm OnDraw is firing.
    if (mm->frameCounter == 1)
    {
        void *d = th07_fopen_w("boot_debug.log", "a");
        if (d)
        {
            th07_fprintf(d, "[draw] OnDraw frame=%d spriteCount=%d dev=%p\n",
                         (i32)mm->frameCounter, (i32)mm->spriteCount, (void *)dev);
            th07_fclose(d);
        }
    }
#endif

    // Reset the cached current-texture pointer so the first sprite rebinds
    // (orig: *(DAT_004b9e44 + 0x2e4cc) = 0).
    g_AnmManager->currentTexture = 0;

    // One-time per-frame render state for the screen-space 2D blit. The orig
    // installs these per-VM via SetRenderStateForVm; here we set a single
    // stable state for all menu sprites (alpha-blended textured quads).
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    // Screen-space FVF (XYZRHW = pre-transformed vertices; no camera needed).
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    // Orig reads spriteArrayPtr (+0xb0c4) and spriteCount (+0xd0f4). On boot
    // spriteArrayPtr is 0 (OnCalc state-0 hasn't allocated the label pool
    // yet), so fall back to the embedded 14-sprite array.
    vm = (AnmVm *)mm->spriteArrayPtr;
    if (vm == 0)
    {
        vm = &mm->sprites[0];
    }

    for (i = 0; i < mm->spriteCount; i++)
    {
        sprite = vm->sprite;
        // Skip disabled / texture-less sprites.
        if (sprite == 0 ||
            sprite->sourceFileIndex < 0 ||
            g_AnmManager->textures[sprite->sourceFileIndex] == 0)
        {
            vm = (AnmVm *)((u8 *)vm + 0x24c);
            continue;
        }

        tex = g_AnmManager->textures[sprite->sourceFileIndex];
        dev->SetTexture(0, tex);

        // Compute the axis-aligned quad in screen space. The sprite's
        // widthPx/heightPx are pre-baked as fractions of the texture size,
        // and scaleX/Y default to 1.0. g_AnmMgrScaleDiv_498a70 = 2.0 (orig
        // half-extent divisor), so half-extent = widthPx * scale / 2.
        halfW = (sprite->widthPx * vm->scaleX) / g_AnmMgrScaleDiv_498a70;
        halfH = (sprite->heightPx * vm->scaleY) / g_AnmMgrScaleDiv_498a70;
        centerX = vm->positionOffsetX + g_AnmManager->frameOffsetX;
        centerY = vm->positionOffsetY + g_AnmManager->frameOffsetY;
        leftX = centerX - halfW;
        rightX = centerX + halfW;
        topY = centerY - halfH;
        botY = centerY + halfH;

        // UV rectangle (already normalised into 0..1 by LoadSprite).
        u0 = sprite->uvStartX + vm->uvScrollPos.x;
        u1 = sprite->uvEndX + vm->uvScrollPos.x;
        v0 = sprite->uvStartY + vm->uvScrollPos.y;
        v1 = sprite->uvEndY + vm->uvScrollPos.y;

        color = (D3DCOLOR)vm->color;

        // 6 vertices = 2 triangles forming the quad:
        //   tri0 = (TL, BL, TR)   tri1 = (TR, BL, BR)
        // (TL=top-left, BR=bottom-right, etc.)
        verts[0].x = leftX;  verts[0].y = topY; verts[0].u = u0; verts[0].v = v0;
        verts[1].x = leftX;  verts[1].y = botY; verts[1].u = u0; verts[1].v = v1;
        verts[2].x = rightX; verts[2].y = topY; verts[2].u = u1; verts[2].v = v0;
        verts[3].x = rightX; verts[3].y = topY; verts[3].u = u1; verts[3].v = v0;
        verts[4].x = leftX;  verts[4].y = botY; verts[4].u = u0; verts[4].v = v1;
        verts[5].x = rightX; verts[5].y = botY; verts[5].u = u1; verts[5].v = v1;
        verts[0].z = 0.5f; verts[0].rhw = 1.0f; verts[0].diffuse = color;
        verts[1].z = 0.5f; verts[1].rhw = 1.0f; verts[1].diffuse = color;
        verts[2].z = 0.5f; verts[2].rhw = 1.0f; verts[2].diffuse = color;
        verts[3].z = 0.5f; verts[3].rhw = 1.0f; verts[3].diffuse = color;
        verts[4].z = 0.5f; verts[4].rhw = 1.0f; verts[4].diffuse = color;
        verts[5].z = 0.5f; verts[5].rhw = 1.0f; verts[5].diffuse = color;

        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, verts, sizeof(MenuVertex));

        vm = (AnmVm *)((u8 *)vm + 0x24c);
    }

    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

static ZunResult __fastcall DeletedCallback(MainMenuObj *mm)
{
    (void)mm;
    return ZUN_SUCCESS;
}

// ---------------------------------------------------------------------------
// RegisterChain (orig FUN_0045c5d0). Allocates the 0xd158-byte MainMenu
// object, zeroes it, and installs a calc chain element (priority 3) whose
// addedCallback fires AddedCallback immediately, plus a draw chain element.
// The calc element's addedCallback is what loads title01.anm + starts BGM.
// ---------------------------------------------------------------------------
ZunResult RegisterChain(i32 unused)
{
    (void)unused;

    MainMenuObj *mm;
    ChainElem *calcElem;
    ChainElem *drawElem;

    mm = (MainMenuObj *)operator_new_th07(0xd158);
    if (mm == 0)
    {
        mm = 0;
    }
    if (mm != 0)
    {
        memset(mm, 0, 0xd158);
    }

    // Calc chain element (priority 3). addedCallback fires on registration.
    calcElem = g_Chain.CreateElem((ChainCallback)OnCalc);
    calcElem->arg = mm;
    calcElem->addedCallback = (ChainAddedCallback)AddedCallback;
    calcElem->deletedCallback = (ChainDeletedCallback)DeletedCallback;
    if (g_Chain.AddToCalcChain(calcElem, 3) != 0)
    {
        return (ZunResult)-1;
    }

    // Draw chain element (priority 0).
    drawElem = g_Chain.CreateElem((ChainCallback)OnDraw);
    drawElem->arg = mm;
    g_Chain.AddToDrawChain(drawElem, 0);

    return ZUN_SUCCESS;
}

} // namespace MainMenu
} // namespace th07
