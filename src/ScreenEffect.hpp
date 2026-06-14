#pragma once

#include <Windows.h>
#include <d3d8.h>

#include "Chain.hpp"
#include "ZunResult.hpp"
#include "ZunTimer.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// Game window dimensions (640x480, same as th06).
#define GAME_WINDOW_WIDTH 640
#define GAME_WINDOW_HEIGHT 480

// A simple rect using floats. Matches the th06 layout.
struct ZunRect
{
    f32 left;
    f32 top;
    f32 right;
    f32 bottom;
};

// Screen effect identifiers passed to ScreenEffect::RegisterChain. th07 added two
// extra effects over th06 (3 and 4): a "flicker" fade and a full-screen fade-out.
enum ScreenEffects
{
    SCREEN_EFFECT_FADE_IN = 0,
    SCREEN_EFFECT_SHAKE = 1,
    SCREEN_EFFECT_FADE_OUT = 2,
    SCREEN_EFFECT_FLICKER_FADE = 3,
    SCREEN_EFFECT_FULL_FADE_OUT = 4,
};

// Chain priorities. th07 shifts these by one compared to th06 (14/16 -> 15/17).
#define TH_CHAIN_PRIO_CALC_SCREENEFFECT 15
#define TH_CHAIN_PRIO_DRAW_SCREENEFFECT 17

struct ScreenEffect
{
    // Register a new screen effect on the calc/draw chains. Returns NULL on
    // failure. `effect` selects the effect type; `ticks` is its duration;
    // effectParam1/effectParam2 carry the RGB color (fade effects) or the
    // base/shakiness parameters (shake effects); unusedEffectParam is unused.
    static ScreenEffect *__fastcall RegisterChain(i32 effect, u32 ticks, u32 effectParam1,
                                                  u32 effectParam2, u32 unusedEffectParam);

    static ZunResult __fastcall AddedCallback(ScreenEffect *effect);
    static ZunResult __fastcall DeletedCallback(ScreenEffect *effect);

    static ChainCallbackResult __fastcall DrawFadeIn(ScreenEffect *effect);
    static ChainCallbackResult __fastcall CalcFadeIn(ScreenEffect *effect);
    static ChainCallbackResult __fastcall ShakeScreen(ScreenEffect *effect);
    static ChainCallbackResult __fastcall DrawFadeOut(ScreenEffect *effect);
    static ChainCallbackResult __fastcall CalcFadeOut(ScreenEffect *effect);

    // th07 added a "flicker" fade effect with its own calc/draw callbacks.
    static ChainCallbackResult __fastcall CalcFlickerFade(ScreenEffect *effect);
    static ChainCallbackResult __fastcall DrawFlickerFade(ScreenEffect *effect);

    static void __fastcall DrawSquare(ZunRect *rect, D3DCOLOR rectColor);
    static void __fastcall Clear(D3DCOLOR color);
    static void __fastcall SetViewport(D3DCOLOR color);

    enum ScreenEffects usedEffect;
    ChainElem *calcChainElement;
    ChainElem *drawChainElement;
    u32 unused;
    i32 fadeAlpha;
    i32 effectLength;
    i32 genericParam;   // effectParam1
    i32 shakinessParam; // effectParam2
    i32 unusedParam;
    ZunTimer timer;
};
ZUN_ASSERT_SIZE(ScreenEffect, 0x30);
}; // namespace th07
