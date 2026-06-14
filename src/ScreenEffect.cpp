#include "ScreenEffect.hpp"

#include <string.h>

#include "AnmManager.hpp"
#include "ChainPriorities.hpp"
#include "GameWindow.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#include "GameManager.hpp"

namespace th07
{
// ============================================================================
// Clear
// ----------------------------------------------------------------------------
// Clears the backbuffer+Z buffer twice, presenting between each clear. If the
// Present fails the device is Reset with the Supervisor present parameters.
// Matches FUN_0044a460.
// ============================================================================
void __fastcall ScreenEffect::Clear(D3DCOLOR color)
{
    g_Supervisor.d3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
    if (g_Supervisor.d3dDevice->Present(NULL, NULL, NULL, NULL) < 0)
    {
        g_Supervisor.d3dDevice->Reset(&g_Supervisor.presentParameters);
    }
    g_Supervisor.d3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
    if (g_Supervisor.d3dDevice->Present(NULL, NULL, NULL, NULL) < 0)
    {
        g_Supervisor.d3dDevice->Reset(&g_Supervisor.presentParameters);
    }
    return;
}

// ============================================================================
// SetViewport
// ----------------------------------------------------------------------------
// Resets the viewport to the full 640x480 window and clears the screen. In
// th07 an AnmManager flush is performed first when the AnmManager is present.
// Matches FUN_0044a520.
// ============================================================================
void __fastcall ScreenEffect::SetViewport(D3DCOLOR color)
{
    if (g_AnmManager != NULL)
    {
        g_AnmManager->FlushSprites();
    }
    g_Supervisor.viewport.X = 0;
    g_Supervisor.viewport.Y = 0;
    g_Supervisor.viewport.Width = GAME_WINDOW_WIDTH;
    g_Supervisor.viewport.Height = GAME_WINDOW_HEIGHT;
    g_Supervisor.viewport.MinZ = 0.0f;
    g_Supervisor.viewport.MaxZ = 1.0f;
    g_Supervisor.d3dDevice->SetViewport(&g_Supervisor.viewport);
    ScreenEffect::Clear(color);
    return;
}

// ============================================================================
// CalcFadeIn
// ----------------------------------------------------------------------------
// Computes the fade alpha for a fade-in effect (255 -> 0 over effectLength).
// Matches FUN_0044a5a0.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::CalcFadeIn(ScreenEffect *effect)
{
    if (effect->effectLength != 0)
    {
        // Note: effectLength is i32, so MSVC emits FIDIV (integer load) rather
        // than FDIV. The 255.0f - ... uses FSUBR (reverse subtract). The
        // resulting float is truncated to i32 via the runtime _ftol helper.
        effect->fadeAlpha = 255.0f - ((effect->timer.AsFramesFloat() * 255.0f) / effect->effectLength);
        if (effect->fadeAlpha < 0)
        {
            effect->fadeAlpha = 0;
        }
    }

    if (effect->timer >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    // Inlined ZunTimer::Tick() — the assembly writes previous=current then
    // calls Supervisor::TickTimer(&current, &subFrame) directly rather than
    // going through ZunTimer::Tick().
    effect->timer.previous = effect->timer.current;
    g_Supervisor.TickTimer(&effect->timer.current, &effect->timer.subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// DrawSquare
// ----------------------------------------------------------------------------
// Draws a coloured quad as a 2-triangle strip with the given rect and colour.
// The assembly reloads the rect fields separately for each vertex and threads
// them through distinct temporaries, so the C++ mirrors that pattern. Matches
// FUN_0044a650.
// ============================================================================
#pragma var_order(vertices, colorParam, rectParam, loopCounter, loopStep, loopPtr, v0Left, v0Top, v0LeftCopy, v0TopCopy, v1Right, v1Top, v1RightCopy, v1TopCopy, v2Bottom, v2Left, v2LeftCopy, v2BottomCopy, v3Right, v3Bottom, v3RightCopy, v3BottomCopy)
void __fastcall ScreenEffect::DrawSquare(ZunRect *rect, D3DCOLOR rectColor)
{
    VertexDiffuseXyzrwh vertices[4];
    D3DCOLOR colorParam;
    ZunRect *rectParam;
    i32 loopCounter;
    i32 loopStep;
    u8 *loopPtr;
    f32 v0Left;
    f32 v0Top;
    f32 v0LeftCopy;
    f32 v0TopCopy;
    f32 v1Right;
    f32 v1Top;
    f32 v1RightCopy;
    f32 v1TopCopy;
    f32 v2Bottom;
    f32 v2Left;
    f32 v2LeftCopy;
    f32 v2BottomCopy;
    f32 v3Right;
    f32 v3Bottom;
    f32 v3RightCopy;
    f32 v3BottomCopy;

    rectParam = rect;
    colorParam = rectColor;

    g_AnmManager->FlushSprites();

    // ZUN-ism: pointless loop that computes the address past the vertex array
    // without using the result. Reproduced verbatim.
    loopCounter = 4;
    loopStep = sizeof(*vertices);
    loopPtr = (u8 *)vertices;
    do
    {
        loopCounter = loopCounter - 1;
        loopPtr = loopPtr + loopStep;
    } while (0 <= loopCounter);

    // Vertex 0: (left, top, 0)
    v0Top = rectParam->top;
    v0Left = rectParam->left;
    v0LeftCopy = v0Left;
    v0TopCopy = v0Top;
    vertices[0].position.x = v0LeftCopy;
    vertices[0].position.y = v0TopCopy;
    vertices[0].position.z = 0.0f;

    // Vertex 1: (right, top, 0)
    v1Top = rectParam->top;
    v1Right = rectParam->right;
    v1RightCopy = v1Right;
    v1TopCopy = v1Top;
    vertices[1].position.x = v1RightCopy;
    vertices[1].position.y = v1TopCopy;
    vertices[1].position.z = 0.0f;

    // Vertex 2: (left, bottom, 0)
    v2Bottom = rectParam->bottom;
    v2Left = rectParam->left;
    v2LeftCopy = v2Left;
    v2BottomCopy = v2Bottom;
    vertices[2].position.x = v2LeftCopy;
    vertices[2].position.y = v2BottomCopy;
    vertices[2].position.z = 0.0f;

    // Vertex 3: (right, bottom, 0)
    v3Right = rectParam->right;
    v3Bottom = rectParam->bottom;
    v3RightCopy = v3Right;
    v3BottomCopy = v3Bottom;
    vertices[3].position.x = v3RightCopy;
    vertices[3].position.y = v3BottomCopy;
    vertices[3].position.z = 0.0f;

    vertices[0].position.w = 1.0f;
    vertices[1].position.w = 1.0f;
    vertices[2].position.w = 1.0f;
    vertices[3].position.w = 1.0f;

    vertices[3].diffuse = colorParam;
    vertices[2].diffuse = colorParam;
    vertices[1].diffuse = colorParam;
    vertices[0].diffuse = colorParam;

    if (((g_Supervisor.cfg.opts >> GCOS_NO_COLOR_COMP) & 0x01) == 0)
    {
        g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    }
    g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (((g_Supervisor.cfg.opts >> GCOS_TURN_OFF_DEPTH_TEST) & 0x01) == 0)
    {
        g_Supervisor.d3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    }
    g_Supervisor.d3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_Supervisor.d3dDevice->SetVertexShader(D3DFVF_DIFFUSE | D3DFVF_XYZRHW);
    g_Supervisor.d3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(*vertices));

    g_AnmManager->currentVertexShader = 0xff;
    g_AnmManager->currentSprite = 0;
    g_AnmManager->currentTexture = 0;
    g_AnmManager->currentColorOp = 0xff;
    g_AnmManager->currentBlendMode = 0xff;
    g_AnmManager->currentZWriteDisable = 0xff;

    if (((g_Supervisor.cfg.opts >> GCOS_NO_COLOR_COMP) & 0x01) == 0)
    {
        g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    }
    g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_Supervisor.d3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    return;
}

// ============================================================================
// CalcFadeOut
// ----------------------------------------------------------------------------
// Computes the fade alpha for a fade-out effect (0 -> 255 over effectLength).
// Matches FUN_0044ae90.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::CalcFadeOut(ScreenEffect *effect)
{
    if (effect->effectLength != 0)
    {
        // Same FIDIV pattern as CalcFadeIn, but without the FSUBR — alpha
        // simply ramps up from 0 to 255.
        effect->fadeAlpha = (effect->timer.AsFramesFloat() * 255.0f) / effect->effectLength;
        if (effect->fadeAlpha < 0)
        {
            effect->fadeAlpha = 0;
        }
    }

    if (effect->timer >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    effect->timer.previous = effect->timer.current;
    g_Supervisor.TickTimer(&effect->timer.current, &effect->timer.subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// RegisterChain
// ----------------------------------------------------------------------------
// Allocates a ScreenEffect and registers it on the calc/draw chains. The
// switch picks the calc/draw callbacks for the requested effect. th07 adds
// two extra effect types (3 and 4) over th06.
// Matches FUN_0044b310.
// ============================================================================
#pragma var_order(calcChainElem, drawChainElem, createdEffect)
ScreenEffect *__fastcall ScreenEffect::RegisterChain(i32 effect, u32 ticks, u32 effectParam1, u32 effectParam2,
                                                      u32 unusedEffectParam)
{
    ChainElem *calcChainElem;
    ScreenEffect *createdEffect;
    ChainElem *drawChainElem;

    calcChainElem = NULL;
    drawChainElem = NULL;

    createdEffect = new ScreenEffect;

    if (createdEffect == NULL)
    {
        return NULL;
    }

    memset(createdEffect, 0, sizeof(*createdEffect));

    switch (effect)
    {
    case SCREEN_EFFECT_FADE_IN:
        calcChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::CalcFadeIn);
        drawChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::DrawFadeIn);
        break;
    case SCREEN_EFFECT_SHAKE:
        calcChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::ShakeScreen);
        break;
    case SCREEN_EFFECT_FADE_OUT:
        calcChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::CalcFadeOut);
        drawChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::DrawFadeOut);
        break;
    case SCREEN_EFFECT_FLICKER_FADE:
        calcChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::CalcFlickerFade);
        drawChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::DrawFlickerFade);
        break;
    case SCREEN_EFFECT_FULL_FADE_OUT:
        calcChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::CalcFadeOut);
        drawChainElem = g_Chain.CreateElem((ChainCallback)ScreenEffect::DrawFadeIn);
        break;
    }

    calcChainElem->addedCallback = (ChainAddedCallback)ScreenEffect::AddedCallback;
    calcChainElem->deletedCallback = (ChainDeletedCallback)ScreenEffect::DeletedCallback;
    calcChainElem->arg = createdEffect;
    createdEffect->usedEffect = (ScreenEffects)effect;
    createdEffect->effectLength = ticks;
    createdEffect->genericParam = effectParam1;
    createdEffect->shakinessParam = effectParam2;
    createdEffect->unusedParam = unusedEffectParam;

    if (g_Chain.AddToCalcChain(calcChainElem, TH_CHAIN_PRIO_CALC_SCREENEFFECT) != 0)
    {
        return NULL;
    }

    if (drawChainElem != NULL)
    {
        drawChainElem->arg = createdEffect;
        g_Chain.AddToDrawChain(drawChainElem, TH_CHAIN_PRIO_DRAW_SCREENEFFECT);
    }

    createdEffect->calcChainElement = calcChainElem;
    createdEffect->drawChainElement = drawChainElem;
    return createdEffect;
}

// ============================================================================
// DrawFadeIn
// ----------------------------------------------------------------------------
// Draws a full-screen fade rect (0,0,640,480) using the fade alpha and the
// generic param (RGB). Sets the viewport X/Y/Width/Height (but, unlike th06,
// not MinZ/MaxZ) before drawing. Matches FUN_0044adf0.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::DrawFadeIn(ScreenEffect *effect)
{
    ZunRect fadeRect;

    fadeRect.left = 0.0f;
    fadeRect.top = 0.0f;
    fadeRect.right = 640.0f;
    fadeRect.bottom = 480.0f;
    g_AnmManager->FlushSprites();
    g_Supervisor.viewport.X = 0;
    g_Supervisor.viewport.Y = 0;
    g_Supervisor.viewport.Width = GAME_WINDOW_WIDTH;
    g_Supervisor.viewport.Height = GAME_WINDOW_HEIGHT;
    g_Supervisor.d3dDevice->SetViewport(&g_Supervisor.viewport);
    ScreenEffect::DrawSquare(&fadeRect, (effect->fadeAlpha << 24) | effect->genericParam);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// DrawFadeOut
// ----------------------------------------------------------------------------
// Draws the fade-out rect (32,16,416,464) using the fade alpha and the
// generic param (RGB). DrawSquare handles the AnmManager flush. Matches
// FUN_0044af30.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::DrawFadeOut(ScreenEffect *effect)
{
    ZunRect fadeRect;

    fadeRect.left = 32.0f;
    fadeRect.top = 16.0f;
    fadeRect.right = 416.0f;
    fadeRect.bottom = 464.0f;
    ScreenEffect::DrawSquare(&fadeRect, (effect->fadeAlpha << 24) | effect->genericParam);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// ShakeScreen
// ----------------------------------------------------------------------------
// Shakes the arcade region using a random offset on each axis. The offset
// grows from genericParam to shakinessParam over effectLength. th07 differs
// from th06: the offset is signed (+/-) written directly to the
// arcadeRegionTopLeftPos rather than a 32.0f/384.0f base plus clamp, and there
// is a "game active" early-out (DAT_0062f858 > 1 must hold). Matches
// FUN_0044b0e0.
// ============================================================================
#pragma var_order(screenOffset, tmpParam, randX, randY)
ChainCallbackResult __fastcall ScreenEffect::ShakeScreen(ScreenEffect *effect)
{
    f32 screenOffset;
    i32 tmpParam;
    u32 randX;
    u32 randY;

    if (g_GameManager.isTimeStopped)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    if (g_GameManager.difficulty <= 1)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    effect->timer.previous = effect->timer.current;
    g_Supervisor.TickTimer(&effect->timer.current, &effect->timer.subFrame);
    if (effect->timer >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    tmpParam = effect->shakinessParam - effect->genericParam;
    screenOffset = ((f32)tmpParam * effect->timer.AsFramesFloat()) / (f32)effect->effectLength;
    screenOffset = screenOffset + (f32)effect->genericParam;

    randX = g_Rng.GetRandomU32InRange(3);
    switch (randX)
    {
    case 0:
        g_GameManager.arcadeRegionTopLeftPos.x = 0.0f;
        break;
    case 1:
        g_GameManager.arcadeRegionTopLeftPos.x = screenOffset;
        break;
    case 2:
        g_GameManager.arcadeRegionTopLeftPos.x = -screenOffset;
        break;
    }

    randY = g_Rng.GetRandomU32InRange(3);
    switch (randY)
    {
    case 0:
        g_GameManager.arcadeRegionTopLeftPos.y = 0.0f;
        break;
    case 1:
        g_GameManager.arcadeRegionTopLeftPos.y = screenOffset;
        break;
    case 2:
        g_GameManager.arcadeRegionTopLeftPos.y = -screenOffset;
        break;
    }

    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// CalcFlickerFade (th07-only, case 3)
// ----------------------------------------------------------------------------
// Fades out from a base alpha held in the high byte of shakinessParam over
// effectLength frames, then loops genericParam times. The base alpha is
// reloaded through an (i64) cast so MSVC emits FILD qword (same x87 trick as
// Rng::GetRandomF32ZeroToOne). Matches FUN_0044af80.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::CalcFlickerFade(ScreenEffect *effect)
{
    i32 baseAlpha;
    i32 scaled;

    if (effect->timer.current < effect->effectLength)
    {
        baseAlpha = ((u32)effect->shakinessParam >> 24) & 0xff;
        // (i64) cast forces FILD qword load to match the assembly.
        scaled = (i32)(((i64)baseAlpha * effect->timer.AsFramesFloat()) / effect->effectLength);
        effect->fadeAlpha = baseAlpha - scaled;
        if (effect->fadeAlpha < 0)
        {
            effect->fadeAlpha = 0;
        }
    }
    else
    {
        effect->fadeAlpha = 0;
        effect->genericParam = effect->genericParam - 1;
        if (effect->genericParam < 1)
        {
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
        effect->timer.InitializeForPopup();
    }

    effect->timer.previous = effect->timer.current;
    g_Supervisor.TickTimer(&effect->timer.current, &effect->timer.subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// DrawFlickerFade (th07-only, case 3)
// ----------------------------------------------------------------------------
// Draws the (32,16,416,464) fade rect with alpha from fadeAlpha and the RGB
// portion of shakinessParam. Matches FUN_0044b090.
// ============================================================================
ChainCallbackResult __fastcall ScreenEffect::DrawFlickerFade(ScreenEffect *effect)
{
    ZunRect fadeRect;

    fadeRect.left = 32.0f;
    fadeRect.top = 16.0f;
    fadeRect.right = 416.0f;
    fadeRect.bottom = 464.0f;
    ScreenEffect::DrawSquare(&fadeRect, (effect->fadeAlpha << 24) | (effect->shakinessParam & 0xffffff));
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// AddedCallback
// ----------------------------------------------------------------------------
// Initializes the effect timer for popup. Matches FUN_0044b280.
// ============================================================================
ZunResult __fastcall ScreenEffect::AddedCallback(ScreenEffect *effect)
{
    effect->timer.InitializeForPopup();
    return ZUN_SUCCESS;
}

// ============================================================================
// DeletedCallback
// ----------------------------------------------------------------------------
// Nulls the calc chain element's deleted callback (so it does not fire
// recursively), cuts the draw chain element, and frees the effect.
// Matches FUN_0044b2c0.
// ============================================================================
ZunResult __fastcall ScreenEffect::DeletedCallback(ScreenEffect *effect)
{
    effect->calcChainElement->deletedCallback = NULL;
    g_Chain.Cut(effect->drawChainElement);
    effect->drawChainElement = NULL;
    delete effect;

    return ZUN_SUCCESS;
}
}; // namespace th07
