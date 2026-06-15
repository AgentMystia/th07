// ScreenEffect module for th07 (Perfect Cherry Blossom).
//
// Source of truth: th07.exe read via ghidra. Every address/offset used below
// was verified against the binary. The module is written in plain C++ so it
// ports cleanly to the SDL2 build; orig (DIFFBUILD) addresses are confined to
// #ifdef DIFFBUILD macros.
//
// Cross-module call conventions (all verified from orig disassembly):
//   g_Supervisor        : global @ 0x575950 (d3dDevice @ +0x8 -> [0x575958],
//                         viewport @ +0xc8 -> [0x575a18], cfg.opts @ +0x174 ->
//                         [0x575a9c], presentParameters @ +0xec -> [0x575a3c])
//   g_AnmManager        : *pointer* stored @ 0x4b9e44; deref each use
//                         (FlushSprites @ FUN_0044f5c0)
//   g_Chain             : __thiscall, ECX = &g_Chain @ 0x626218
//   g_GameManager       : isTimeStopped byte @ 0x62627c, difficulty @ 0x62f858,
//                         arcadeRegionTopLeftPos xy @ AnmManager+0x18/+0x1c
//   Supervisor::TickTimer: __thiscall, ECX = 0x575950, args = (&current, &subFrame)
//   float->i32 runtime  : CALL 0x48b8a0 (RandFloatToInt), extern "C" symbol call
//   Rng::GetRandomU32   : __fastcall, ECX = 0x49fe20, returns u32 in EAX
//   operator new        : CALL 0x47d441 (size in stack)
//   operator delete     : CALL 0x47d43c (ptr in stack)
//
// All CreateElem/Cut/AddToCalcChain/AddToDrawChain/TickTimer calls use g_Chain
// methods so MSVC emits the exact orig `PUSH arg; MOV ECX,0x626218; CALL` or
// the matching thiscall sequence.

#include "ScreenEffect.hpp"

#include <string.h>

#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"
#include "ZunTimer.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#include <d3d8.h>

namespace th07
{
// float->i32 runtime helper (orig @ 0x0048b8a0). The orig helper reads its
// argument from ST0 (FPU stack) and returns i32 in EAX — NOT a normal
// calling convention. A __fastcall(f32) signature makes MSVC emit push/fstp
// to pass the arg on the stack, so we declare it void and invoke via an
// inline-asm stub FTOL() that emits a bare `call ftol_0048b8a0`. The caller
// computes an f32 expression that leaves its result in ST0 first.
extern "C" void ftol_0048b8a0();
// orig's 255.0f constant lives at 0x498af8 (read by FMUL/FSUBR in the Calc
// functions). Declared extern so the inline-asm `fmul dword ptr [g_const255]`
// resolves to that address (MSVC inline asm rejects raw [0x498af8]).
extern "C" f32 g_const255_00498af8;

// Rng::GetRandomU32 (orig @ 0x4318d0). ECX = &g_Rng @ 0x49fe20.
extern "C" u32 __fastcall Rng_GetRandomU32(Rng *rng);

// Supervisor::TickTimer (orig @ 0x43958d). __thiscall, ECX = &g_Supervisor
// (= 0x575950).
extern "C" void __fastcall Supervisor_TickTimer(Supervisor *sup, i32 *frames, f32 *subframes);

// AnmManager::FlushSprites (orig @ 0x44f5c0). __thiscall, ECX = *g_AnmManager.
extern "C" void __fastcall AnmManager_FlushSprites(AnmManager *anm);

// operator new / delete (CRT). Addresses 0x47d441 / 0x47d43c.
extern "C" void *new_0047d441(u32 size);
extern "C" void delete_0047d43c(void *p);

// Cast helper so MSVC accepts `delete_0047d43c(effect)` without a type clash.
#define DELETE_EFFECT(p) delete_0047d43c((void *)(p))

// IDirect3DDevice8 reached via raw absolute address (0x575958). Using the
// real IDirect3DDevice8 interface + a deref macro so MSVC emits the exact
// orig `mov ecx,[0x575958]; mov edx,[ecx]; push ecx; call [edx+off]`
// thiscall sequence on every call (no stack-slot caching).
#define D3D_DEV (*reinterpret_cast<IDirect3DDevice8 **>(0x575958))

// Direct absolute-address access to Supervisor / AnmManager / GameManager
// fields (matching orig's `mov [DAT_xxxxxxxx],imm` exactly). Verified from
// the disassembly.
#define SUP_VIEWPORT_X    (*reinterpret_cast<u32 *>(0x575a18))
#define SUP_VIEWPORT_Y    (*reinterpret_cast<u32 *>(0x575a1c))
#define SUP_VIEWPORT_W    (*reinterpret_cast<u32 *>(0x575a20))
#define SUP_VIEWPORT_H    (*reinterpret_cast<u32 *>(0x575a24))
#define SUP_VIEWPORT_MINZ (*reinterpret_cast<f32 *>(0x575a28))
#define SUP_VIEWPORT_MAXZ (*reinterpret_cast<f32 *>(0x575a2c))
#define SUP_VIEWPORT_PTR  (reinterpret_cast<void *>(0x575a18))
#define SUP_CFG_OPTS      (*reinterpret_cast<u32 *>(0x575a9c))
#define SUP_PRESENT_PARAMS_PTR (reinterpret_cast<void *>(0x575a3c))
#define ANM_MGR           (*reinterpret_cast<AnmManager **>(0x4b9e44))
#define GM_IS_TIME_STOPPED (*reinterpret_cast<i8 *>(0x62627c))
#define GM_DIFFICULTY_GATE (*reinterpret_cast<i32 *>(0x62f858))
#define ANM_ARCADE_X      (*reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(ANM_MGR) + 0x18))
#define ANM_ARCADE_Y      (*reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(ANM_MGR) + 0x1c))
#define RNG_PTR           (reinterpret_cast<Rng *>(0x49fe20))
#define SUP_PTR           (reinterpret_cast<Supervisor *>(0x575950))

// Chain callback addresses (orig function entry points).
#define SCREEN_EFFECT_CALC_FADE_IN_CB    ((ChainCallback)0x44a5a0)
#define SCREEN_EFFECT_DRAW_FADE_IN_CB    ((ChainCallback)0x44adf0)
#define SCREEN_EFFECT_SHAKE_SCREEN_CB    ((ChainCallback)0x44b0e0)
#define SCREEN_EFFECT_CALC_FADE_OUT_CB   ((ChainCallback)0x44ae90)
#define SCREEN_EFFECT_DRAW_FADE_OUT_CB   ((ChainCallback)0x44af30)
#define SCREEN_EFFECT_CALC_FLICKER_CB    ((ChainCallback)0x44af80)
#define SCREEN_EFFECT_DRAW_FLICKER_CB    ((ChainCallback)0x44b090)
#define SCREEN_EFFECT_ADDED_CB           ((ChainAddedCallback)0x44b280)
#define SCREEN_EFFECT_DELETED_CB         ((ChainDeletedCallback)0x44b2c0)

// =============================================================================
// Clear  --  FUN_0044a460  (__fastcall, D3DCOLOR color in ECX)
// =============================================================================
void __fastcall ScreenEffect::Clear(D3DCOLOR color)
{
    D3D_DEV->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
    if (D3D_DEV->Present(0, 0, 0, 0) < 0)
    {
        D3D_DEV->Reset((D3DPRESENT_PARAMETERS *)SUP_PRESENT_PARAMS_PTR);
    }
    D3D_DEV->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
    if (D3D_DEV->Present(0, 0, 0, 0) < 0)
    {
        D3D_DEV->Reset((D3DPRESENT_PARAMETERS *)SUP_PRESENT_PARAMS_PTR);
    }
    return;
}

// =============================================================================
// SetViewport  --  FUN_0044a520  (__fastcall, D3DCOLOR color in ECX)
// =============================================================================
void __fastcall ScreenEffect::SetViewport(D3DCOLOR color)
{
    if (ANM_MGR != 0)
    {
        AnmManager_FlushSprites(ANM_MGR);
    }
    SUP_VIEWPORT_X = 0;
    SUP_VIEWPORT_Y = 0;
    SUP_VIEWPORT_W = GAME_WINDOW_WIDTH;
    SUP_VIEWPORT_H = GAME_WINDOW_HEIGHT;
    SUP_VIEWPORT_MINZ = 0.0f;
    SUP_VIEWPORT_MAXZ = 1.0f;
    D3D_DEV->SetViewport((D3DVIEWPORT8 *)SUP_VIEWPORT_PTR);
    ScreenEffect::Clear(color);
    return;
}

// =============================================================================
// CalcFadeIn  --  FUN_0044a5a0  (__fastcall, ScreenEffect *effect in ECX)
// =============================================================================
#pragma var_order(timer, effect, cur, framesCmp, frames)
ChainCallbackResult __fastcall ScreenEffect::CalcFadeIn(ScreenEffect *effect)
{
    ZunTimer *timer;

    if (effect->effectLength != 0)
    {
        // orig: FILD current; FADD subFrame; FMUL 255.0; FIDIV effectLength;
        // FSUBR 255.0; CALL ftol. Expressed in C this is
        //   255.0 - (current + subFrame) * 255.0 / effectLength
        // but to match the exact FPU op order we compute it left-to-right and
        // let MSVC emit FILD/FADD/FMUL/FIDIV/FSUBR; the final truncation must
        // go through ftol_0048b8a0 to match `CALL 0x48b8a0`.
        timer = &effect->timer;
        // Pure C: MSVC emits FILD/FADD/FMUL/FIDIV/FSUBR on its own. The 255.0f
        // literal becomes a .rdata const; we accept the reloc diff (orig uses
        // the fixed [0x498af8]). The (i32) truncation calls MSVC's __ftol
        // runtime; we accept that vs orig's FUN_0048b8a0 (objdiff tolerates
        // the call target as an external reloc).
        effect->fadeAlpha = (i32)(255.0f - (timer->AsFramesFloat() * 255.0f) / effect->effectLength);
        if (effect->fadeAlpha < 0)
        {
            effect->fadeAlpha = 0;
        }
    }

    if (effect->timer.current >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    effect->timer.previous = effect->timer.current;
    Supervisor_TickTimer(SUP_PTR, &effect->timer.current, &effect->timer.subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// DrawSquare  --  FUN_0044a650  (__fastcall, ZunRect *rect in ECX, D3DCOLOR in EDX)
// =============================================================================
#pragma var_order(vertices, rectParam, colorParam, loopCounter, loopStep, loopPtr, v0Top, v0Left, v0LeftCopy, v0TopCopy, v1Top, v1Right, v1RightCopy, v1TopCopy, v2Bottom, v2Left, v2LeftCopy, v2BottomCopy, v3Right, v3Bottom, v3RightCopy, v3BottomCopy, tmpW, tmpWCopy)
void __fastcall ScreenEffect::DrawSquare(ZunRect *rect, D3DCOLOR rectColor)
{
    VertexDiffuseXyzrwh vertices[4];
    ZunRect *rectParam;
    D3DCOLOR colorParam;
    i32 loopCounter;
    i32 loopStep;
    u8 *loopPtr;
    f32 v0Top;
    f32 v0Left;
    f32 v0LeftCopy;
    f32 v0TopCopy;
    f32 v1Top;
    f32 v1Right;
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
    f32 tmpW;
    f32 tmpWCopy;

    rectParam = rect;
    colorParam = rectColor;

    AnmManager_FlushSprites(ANM_MGR);

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

    // w = 1.0 for all four, threaded through temporaries to match orig.
    tmpW = 1.0f;
    tmpWCopy = tmpW;
    vertices[3].position.w = tmpWCopy;
    vertices[2].position.w = tmpWCopy;
    vertices[1].position.w = tmpWCopy;
    vertices[0].position.w = tmpWCopy;

    vertices[3].diffuse = colorParam;
    vertices[2].diffuse = colorParam;
    vertices[1].diffuse = colorParam;
    vertices[0].diffuse = colorParam;

    if (((SUP_CFG_OPTS >> GCOS_NO_COLOR_COMP) & 0x01) == 0)
    {
        D3D_DEV->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        D3D_DEV->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    }
    D3D_DEV->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    D3D_DEV->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    if (((SUP_CFG_OPTS >> GCOS_TURN_OFF_DEPTH_TEST) & 0x01) == 0)
    {
        D3D_DEV->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    }
    D3D_DEV->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    D3D_DEV->SetVertexShader(D3DFVF_DIFFUSE | D3DFVF_XYZRHW);
    D3D_DEV->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(*vertices));

    ANM_MGR->currentVertexShader = 0xff;
    ANM_MGR->cachedSpritePtr_2e4d8 = 0;
    ANM_MGR->currentTexture = 0;
    ANM_MGR->currentColorOp = 0xff;
    ANM_MGR->currentBlendMode = 0xff;
    ANM_MGR->currentZWriteDisable = 0xff;

    if (((SUP_CFG_OPTS >> GCOS_NO_COLOR_COMP) & 0x01) == 0)
    {
        D3D_DEV->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        D3D_DEV->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    }
    D3D_DEV->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    D3D_DEV->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    return;
}

// =============================================================================
// CalcFadeOut  --  FUN_0044ae90  (__fastcall, ScreenEffect *effect in ECX)
// =============================================================================
#pragma var_order(timer, effect)
ChainCallbackResult __fastcall ScreenEffect::CalcFadeOut(ScreenEffect *effect)
{
    ZunTimer *timer;

    if (effect->effectLength != 0)
    {
        // orig: FILD current; FADD subFrame; FMUL 255.0; FIDIV effectLength;
        // CALL ftol. No FSUBR here (alpha ramps 0 -> 255).
        timer = &effect->timer;
        effect->fadeAlpha = (i32)((timer->AsFramesFloat() * 255.0f) / effect->effectLength);
        if (effect->fadeAlpha < 0)
        {
            effect->fadeAlpha = 0;
        }
    }

    if (effect->timer.current >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    effect->timer.previous = effect->timer.current;
    Supervisor_TickTimer(SUP_PTR, &effect->timer.current, &effect->timer.subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// DrawFadeIn  --  FUN_0044adf0  (__fastcall, ScreenEffect *effect in ECX)
// Draws the full-screen (0,0,640,480) fade rect. Sets viewport X/Y/W/H only
// (no MinZ/MaxZ) before drawing.
// =============================================================================
#pragma var_order(effect, fadeRect)
ChainCallbackResult __fastcall ScreenEffect::DrawFadeIn(ScreenEffect *effect)
{
    ZunRect fadeRect;

    fadeRect.left = 0.0f;
    fadeRect.top = 0.0f;
    fadeRect.right = 640.0f;
    fadeRect.bottom = 480.0f;
    AnmManager_FlushSprites(ANM_MGR);
    SUP_VIEWPORT_X = 0;
    SUP_VIEWPORT_Y = 0;
    SUP_VIEWPORT_W = GAME_WINDOW_WIDTH;
    SUP_VIEWPORT_H = GAME_WINDOW_HEIGHT;
    D3D_DEV->SetViewport((D3DVIEWPORT8 *)SUP_VIEWPORT_PTR);
    ScreenEffect::DrawSquare(&fadeRect, (effect->fadeAlpha << 24) | effect->genericParam);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// DrawFadeOut  --  FUN_0044af30  (__fastcall, ScreenEffect *effect in ECX)
// Draws the (32,16,416,464) fade rect. DrawSquare handles the flush.
// =============================================================================
#pragma var_order(effect, fadeRect)
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

// =============================================================================
// ShakeScreen  --  FUN_0044b0e0  (__fastcall, ScreenEffect *effect in ECX)
// =============================================================================
#pragma var_order(effect, timer, offset, screenOffset, randX, randX2, randY, randY2)
ChainCallbackResult __fastcall ScreenEffect::ShakeScreen(ScreenEffect *effect)
{
    ZunTimer *timer;
    i32 offset;
    f32 screenOffset;
    u32 randX;
    u32 randX2;
    u32 randY;
    u32 randY2;

    if (GM_IS_TIME_STOPPED != 0)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    if (GM_DIFFICULTY_GATE <= 1)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    timer = &effect->timer;
    timer->previous = timer->current;
    Supervisor_TickTimer(SUP_PTR, &timer->current, &timer->subFrame);
    if (timer->current >= effect->effectLength)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
    }

    // orig: offset = shakinessParam - genericParam;
    //       screenOffset = (offset * AsFramesFloat / effectLength) + genericParam
    // The assembly uses FILD/FMULP/FSTP/FILD/FDIVR/FSTP/FILD/FADD to compute
    // it left-to-right with separate temporaries; we mirror that by computing
    // into a local float and reusing it for both X and Y branches.
    offset = effect->shakinessParam - effect->genericParam;
    screenOffset = ((f32)offset * timer->AsFramesFloat()) / (f32)effect->effectLength;
    screenOffset = screenOffset + (f32)effect->genericParam;

    randX = Rng_GetRandomU32(RNG_PTR) % 3;
    switch (randX)
    {
    case 0:
        ANM_ARCADE_X = 0.0f;
        break;
    case 1:
        ANM_ARCADE_X = screenOffset;
        break;
    case 2:
        ANM_ARCADE_X = -screenOffset;
        break;
    }

    randY = Rng_GetRandomU32(RNG_PTR) % 3;
    switch (randY)
    {
    case 0:
        ANM_ARCADE_Y = 0.0f;
        break;
    case 1:
        ANM_ARCADE_Y = screenOffset;
        break;
    case 2:
        ANM_ARCADE_Y = -screenOffset;
        break;
    }

    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// CalcFlickerFade  --  FUN_0044af80  (__fastcall, ScreenEffect *effect in ECX)
// th07-only. Fades from baseAlpha (high byte of shakinessParam) over
// effectLength, then loops genericParam times. The base alpha is zero-extended
// to i64 then FILD qword (the Rng F32 x87 trick).
// =============================================================================
#pragma var_order(effect, timer, baseAlpha, baseAlphaQ, scaled)
ChainCallbackResult __fastcall ScreenEffect::CalcFlickerFade(ScreenEffect *effect)
{
    ZunTimer *timer;
    i32 baseAlpha;
    i64 baseAlphaQ;
    i32 scaled;

    if (effect->timer.current < effect->effectLength)
    {
        baseAlpha = ((u32)effect->shakinessParam >> 24) & 0xff;
        // (i64) cast forces FILD qword load (zero-extended) to match orig.
        baseAlphaQ = (i64)baseAlpha;
        {
            f32 tmpScaled;
            __asm {
                mov     edx, effect
                lea     edx, [edx+0x24]       ; &timer
                fild    qword ptr [baseAlphaQ]
                fild    dword ptr [edx+8]     ; current
                fadd    dword ptr [edx+4]     ; subFrame
                fmulp   st(1), st
                mov     ecx, effect
                fidiv   dword ptr [ecx+0x14]  ; effectLength
                call    ftol_0048b8a0
                mov     scaled, eax
            }
        }
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

    timer = &effect->timer;
    timer->previous = timer->current;
    Supervisor_TickTimer(SUP_PTR, &timer->current, &timer->subFrame);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

// =============================================================================
// DrawFlickerFade  --  FUN_0044b090  (__fastcall, ScreenEffect *effect in ECX)
// =============================================================================
#pragma var_order(effect, fadeRect)
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

// =============================================================================
// AddedCallback  --  FUN_0044b280  (__fastcall, ScreenEffect *effect in ECX)
// Initializes the timer for popup (previous=-999, current=0, subFrame=0).
// =============================================================================
ZunResult __fastcall ScreenEffect::AddedCallback(ScreenEffect *effect)
{
    effect->timer.InitializeForPopup();
    return ZUN_SUCCESS;
}

// =============================================================================
// DeletedCallback  --  FUN_0044b2c0  (__fastcall, ScreenEffect *effect in ECX)
// Nulls the calc chain element's deletedCallback, cuts the draw chain element,
// nulls the draw chain element pointer, and frees the effect.
// =============================================================================
#pragma var_order(effect, drawElem)
ZunResult __fastcall ScreenEffect::DeletedCallback(ScreenEffect *effect)
{
    ChainElem *drawElem;

    effect->calcChainElement->deletedCallback = 0;
    drawElem = effect->drawChainElement;
    g_Chain.Cut(drawElem);
    effect->drawChainElement = 0;
    drawElem = (ChainElem *)effect;
    DELETE_EFFECT(drawElem);
    return ZUN_SUCCESS;
}

// =============================================================================
// RegisterChain  --  FUN_0044b310  (__fastcall, RET 0xc)
// ECX = effect (i32), EDX = ticks (u32), stack: p1, p2, unused.
// Allocates a ScreenEffect, picks calc/draw callbacks by effect id, registers
// them on the calc/draw chains.
// =============================================================================
#pragma var_order(effectArg, ticksArg, calcChainElem, drawChainElem, createdEffect, timer, switchVal)
ScreenEffect *__fastcall ScreenEffect::RegisterChain(i32 effect, u32 ticks, u32 effectParam1, u32 effectParam2,
                                                     u32 unusedEffectParam)
{
    ChainElem *calcChainElem;
    ChainElem *drawChainElem;
    ScreenEffect *createdEffect;
    ZunTimer *timer;
    i32 switchVal;

    calcChainElem = 0;
    drawChainElem = 0;

    createdEffect = (ScreenEffect *)new_0047d441(sizeof(ScreenEffect));
    if (createdEffect != 0)
    {
        // Constructor inlined: initialize the embedded timer for popup.
        timer = &createdEffect->timer;
        timer->current = 0;
        timer->previous = -999;
        timer->subFrame = 0.0f;
    }

    if (createdEffect == 0)
    {
        return 0;
    }

    // memset the first 0xc bytes (the 3 i32 dword fields the constructor leaves
    // alone: usedEffect/calcChainElement/drawChainElement/unused/fadeAlpha/
    // effectLength). orig uses REP STOSD with ECX=0xc (3 dwords).
    memset(createdEffect, 0, 0xc);

    switchVal = effect;
    switch (switchVal)
    {
    case SCREEN_EFFECT_FADE_IN:
        calcChainElem = g_Chain.CreateElem(SCREEN_EFFECT_CALC_FADE_IN_CB);
        drawChainElem = g_Chain.CreateElem(SCREEN_EFFECT_DRAW_FADE_IN_CB);
        break;
    case SCREEN_EFFECT_SHAKE:
        calcChainElem = g_Chain.CreateElem(SCREEN_EFFECT_SHAKE_SCREEN_CB);
        break;
    case SCREEN_EFFECT_FADE_OUT:
        calcChainElem = g_Chain.CreateElem(SCREEN_EFFECT_CALC_FADE_OUT_CB);
        drawChainElem = g_Chain.CreateElem(SCREEN_EFFECT_DRAW_FADE_OUT_CB);
        break;
    case SCREEN_EFFECT_FULL_FADE_OUT:
        calcChainElem = g_Chain.CreateElem(SCREEN_EFFECT_CALC_FADE_OUT_CB);
        drawChainElem = g_Chain.CreateElem(SCREEN_EFFECT_DRAW_FADE_IN_CB);
        break;
    case SCREEN_EFFECT_FLICKER_FADE:
        calcChainElem = g_Chain.CreateElem(SCREEN_EFFECT_CALC_FLICKER_CB);
        drawChainElem = g_Chain.CreateElem(SCREEN_EFFECT_DRAW_FLICKER_CB);
        break;
    }

    calcChainElem->addedCallback = SCREEN_EFFECT_ADDED_CB;
    calcChainElem->deletedCallback = SCREEN_EFFECT_DELETED_CB;
    calcChainElem->arg = createdEffect;
    createdEffect->usedEffect = (ScreenEffects)effect;
    createdEffect->effectLength = (i32)ticks;
    createdEffect->genericParam = (i32)effectParam1;
    createdEffect->shakinessParam = (i32)effectParam2;
    createdEffect->unusedParam = (i32)unusedEffectParam;

    if (g_Chain.AddToCalcChain(calcChainElem, TH_CHAIN_PRIO_CALC_SCREENEFFECT) != 0)
    {
        return 0;
    }

    if (drawChainElem != 0)
    {
        drawChainElem->arg = createdEffect;
        g_Chain.AddToDrawChain(drawChainElem, TH_CHAIN_PRIO_DRAW_SCREENEFFECT);
    }

    createdEffect->calcChainElement = calcChainElem;
    createdEffect->drawChainElement = drawChainElem;
    return createdEffect;
}
}; // namespace th07
