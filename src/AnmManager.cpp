// AnmManager module for th07 (Perfect Cherry Blossom).
//
// Sprite / animation / texture manager. th07 reshaped AnmLoadedSprite (0x40
// bytes, gained spriteId + uvScale{X,Y}) and moved arrays inside AnmManager.
//
// Function addresses (all recovered directly from th07.exe via Ghidra):
//   AnmManager::LoadAnm                FUN_0044df90   __thiscall
//   AnmManager::LoadAnmEntry           FUN_0044e070   __thiscall
//   AnmManager::LoadTexture            FUN_0044d8f0   __thiscall
//   AnmManager::LoadTextureAlphaChan   FUN_0044dbe0   __thiscall
//   AnmManager::LoadTextureFromMemory  FUN_0044d9e0   __thiscall
//   AnmManager::CreateEmptyTexture     FUN_0044df40   __thiscall
//   AnmManager::ReleaseTexture         FUN_0044e6f0   __thiscall
//   AnmManager::ReleaseAnm             FUN_0044e4e0   __thiscall
//   AnmManager::LoadSprite             FUN_0044e780   __thiscall
//   AnmManager::SetActiveSprite        FUN_0044e8e0   __thiscall
//   AnmManager::SetAndExecuteScript    FUN_0044ea20   __thiscall
//   AnmManager::ExecuteScript          FUN_00450d60   __thiscall (huge body)
//
// The Draw family (Draw/Draw2/Draw3/DrawInner/DrawNoRotation/DrawFacingCamera)
// and surface helpers (LoadSurface/ReleaseSurface/CopySurfaceToBackBuffer/
// DrawEndingRect/TakeScreenshot{,IfRequested}) are not yet anchored in this
// file; they will be added once their ghidra addresses are matched.

#include "AnmManager.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#include "ZunColor.hpp"
#include "ZunTimer.hpp"
#include "utils.hpp"

#include <stdio.h>

namespace th07
{
DIFFABLE_STATIC(VertexTex1Xyzrwh, g_PrimitivesToDrawVertexBuf[4]);
DIFFABLE_STATIC(VertexTex1DiffuseXyzrwh, g_PrimitivesToDrawNoVertexBuf[4]);
DIFFABLE_STATIC(VertexTex1DiffuseXyz, g_PrimitivesToDrawUnknown[4]);
DIFFABLE_STATIC(AnmManager *, g_AnmManager)

#ifndef DIFFBUILD
D3DFORMAT g_TextureFormatD3D8Mapping[6] = {
    D3DFMT_UNKNOWN, D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5, D3DFMT_R5G6B5, D3DFMT_R8G8B8, D3DFMT_A4R4G4B4,
};
#endif

// ============================================================================
// Constructor / SetupVertexBuffer
//
// TODO(future): constructor and SetupVertexBuffer body need to be lifted from
// the corresponding FUN_ in th07.exe; they initialize the g_PrimitivesToDraw*
// arrays and (optionally) allocate this->vertexBuffer. The th06 bodies are a
// close starting point but the AnmManager struct layout differs, so they must
// be re-verified against the binary before this file is objdiff-clean.
// ============================================================================

// ============================================================================
// ReleaseTexture  (FUN_0044e6f0)
//
// Verified body. Releases the D3D8 texture at textures[textureIdx] and frees
// the raw image bytes at imageDataArray[textureIdx]. texIdx limit 0x108.
// ============================================================================
void AnmManager::ReleaseTexture(i32 textureIdx)
{
    if (textureIdx >= 0 && textureIdx < 0x108)
    {
        if (this->textures[textureIdx] != NULL)
        {
            this->textures[textureIdx]->Release();
            this->textures[textureIdx] = NULL;
        }
        free(this->imageDataArray[textureIdx]);
        this->imageDataArray[textureIdx] = NULL;
    }
}

// ============================================================================
// CreateEmptyTexture  (FUN_0044df40)
//
// Verified body. Thin wrapper around D3DXCreateTexture with the format taken
// from g_TextureFormatD3D8Mapping.
// ============================================================================
ZunResult AnmManager::CreateEmptyTexture(i32 textureIdx, u32 width, u32 height, i32 textureFormat)
{
    D3DXCreateTexture((LPDIRECT3DDEVICE8)g_Supervisor.d3dDevice, width, height, 1, 0,
                      g_TextureFormatD3D8Mapping[textureFormat], D3DPOOL_MANAGED,
                      this->textures + textureIdx);
    return ZUN_SUCCESS;
}

// ============================================================================
// LoadTexture  (FUN_0044d8f0)
//
// Verified body. Mirrors th06 but with g_Supervisor.cfg.opts bit-tests.
// ============================================================================
#pragma var_order(_Memory, uVar1, iVar2)
ZunResult AnmManager::LoadTexture(i32 textureIdx, char *textureName, i32 textureFormat, D3DCOLOR colorKey)
{
    void *_Memory;
    ZunResult uVar1;
    i32 iVar2;

    this->ReleaseTexture(textureIdx);
    if ((g_Supervisor.cfg.opts >> GCOS_FORCE_16BIT_COLOR_MODE) & 1)
    {
        if (g_TextureFormatD3D8Mapping[textureFormat] == D3DFMT_A8R8G8B8 ||
            g_TextureFormatD3D8Mapping[textureFormat] == D3DFMT_UNKNOWN)
        {
            textureFormat = TEX_FMT_A4R4G4B4;
        }
        else if (g_TextureFormatD3D8Mapping[textureFormat] == D3DFMT_R8G8B8)
        {
            textureFormat = TEX_FMT_R5G6B5;
        }
    }

    _Memory = FileSystem::OpenPath(textureName, 0);
    if (_Memory == NULL)
    {
        return ZUN_ERROR;
    }

    iVar2 = D3DXCreateTextureFromFileInMemoryEx(
        (LPDIRECT3DDEVICE8)g_Supervisor.d3dDevice, _Memory, g_LastFileSize, 0, 0, 0, 0,
        g_TextureFormatD3D8Mapping[textureFormat], D3DPOOL_MANAGED, D3DX_FILTER_NONE | D3DX_FILTER_POINT,
        D3DX_DEFAULT, colorKey, NULL, NULL, this->textures + textureIdx);
    if (iVar2 == D3D_OK)
    {
        this->imageDataArray[textureIdx] = _Memory;
        uVar1 = ZUN_SUCCESS;
    }
    else
    {
        free(_Memory);
        uVar1 = ZUN_ERROR;
    }
    return uVar1;
}

// ============================================================================
// LoadSprite  (FUN_0044e780)
//
// Verified body. Copies the sprite descriptor into this->sprites[spriteIdx],
// assigns a monotonic spriteId from maybeLoadedSpriteCount, and precomputes
// the UV bounds (start/end) and pixel dimensions / uv scale.
// ============================================================================
#pragma var_order(iVar1, puVar2, puVar3)
void AnmManager::LoadSprite(u32 spriteIdx, AnmLoadedSprite *sprite)
{
    i32 iVar1;
    u32 *puVar2;
    u32 *puVar3;

    puVar2 = (u32 *)sprite;
    puVar3 = (u32 *)(this->sprites + spriteIdx);
    for (iVar1 = 0x10; iVar1 != 0; iVar1--)
    {
        *puVar3 = *puVar2;
        puVar2++;
        puVar3++;
    }

    this->sprites[spriteIdx].spriteId = this->maybeLoadedSpriteCount;
    this->maybeLoadedSpriteCount++;

    this->sprites[spriteIdx].uvStart.x =
        this->sprites[spriteIdx].startPixelInclusive.x / this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvEnd.x =
        this->sprites[spriteIdx].endPixelInclusive.x / this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvStart.y =
        this->sprites[spriteIdx].startPixelInclusive.y / this->sprites[spriteIdx].textureHeight;
    this->sprites[spriteIdx].uvEnd.y =
        this->sprites[spriteIdx].endPixelInclusive.y / this->sprites[spriteIdx].textureHeight;

    this->sprites[spriteIdx].widthPx =
        this->sprites[spriteIdx].endPixelInclusive.x - this->sprites[spriteIdx].startPixelInclusive.x;
    this->sprites[spriteIdx].heightPx =
        this->sprites[spriteIdx].endPixelInclusive.y - this->sprites[spriteIdx].startPixelInclusive.y;

    this->sprites[spriteIdx].uvScaleX =
        this->sprites[spriteIdx].widthPx / this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvScaleY =
        this->sprites[spriteIdx].heightPx / this->sprites[spriteIdx].textureHeight;
}

// ============================================================================
// SetActiveSprite  (FUN_0044e8e0)
//
// Verified body. Sets vm->sprite / vm->activeSpriteIndex, builds the sprite
// matrix (widthPx/textureWidth on m[0][0], heightPx/textureHeight on m[1][1]),
// computes uvScale fields at +0x178/+0x18c, and caches the matrix into the
// +0x138 scratch region.
// ============================================================================
ZunResult AnmManager::SetActiveSprite(AnmVm *vm, u32 spriteIdx)
{
    if (this->sprites[spriteIdx].sourceFileIndex < 0)
    {
        return ZUN_ERROR;
    }

    vm->activeSpriteIndex = (i16)spriteIdx;
    vm->sprite = this->sprites + spriteIdx;

    D3DXMatrixIdentity(&vm->matrix);
    D3DXMatrixIdentity((D3DXMATRIX *)((u8 *)vm + 0x138));
    vm->matrix.m[0][0] = vm->sprite->widthPx / vm->sprite->textureWidth;
    vm->matrix.m[1][1] = vm->sprite->heightPx / vm->sprite->textureHeight;

    *(f32 *)((u8 *)vm + 0x178) =
        (vm->sprite->widthPx / vm->sprite->textureHeight) * vm->sprite->uvScaleX;
    *(f32 *)((u8 *)vm + 0x18c) =
        (vm->sprite->heightPx / vm->sprite->textureWidth) * vm->sprite->uvScaleY;

    // Cache the 4x4 matrix into the +0x138 scratch region (16 dwords).
    {
        u32 *src = (u32 *)((u8 *)vm + 0xf8);
        u32 *dst = (u32 *)((u8 *)vm + 0x138);
        i32 i;
        for (i = 0x10; i != 0; i--)
        {
            *dst = *src;
            src++;
            dst++;
        }
    }

    return ZUN_SUCCESS;
}

// ============================================================================
// SetAndExecuteScript  (FUN_0044ea20)
//
// Verified body. Resets the VM, stores the script entry point, kicks off the
// first ExecuteScript pass. The (param_3 == 0) branch memsets the whole VM
// (0x24c bytes == 0x93 dwords) so callers can use it to fully reset a VM.
// ============================================================================
void AnmManager::SetAndExecuteScript(AnmVm *vm, AnmRawInstr *beginingOfScript)
{
    i32 iVar1;

    if (beginingOfScript == NULL)
    {
        u32 *p = (u32 *)vm;
        for (iVar1 = 0x93; iVar1 != 0; iVar1--)
        {
            *p = 0;
            p++;
        }
    }
    else
    {
        // Clear flipMode bit (bit 7 of flags).
        vm->flags &= ~AnmVmFlags_FlipMode;
        vm->Initialize();
        vm->beginningOfScript = beginingOfScript;
        vm->currentInstruction = vm->beginningOfScript;
        vm->currentTimeInScript.previous = -999;
        vm->currentTimeInScript.subFrame = 0.0f;
        vm->currentTimeInScript.current = 0;
        // Clear visible bit; ExecuteScript may re-enable it.
        vm->flags &= ~AnmVmFlags_Visible;
        this->ExecuteScript(vm);
    }
}

// ============================================================================
// ExecuteScript  (FUN_00450d60)
//
// MASSIVE interpreter (~2 KB of switch cases). th07 adds: variable / register
// opcodes (0x26-0x52), conditional branches (0x44-0x4f), RNG helpers, and 5
// independent easing channels with 7 easing modes each. The full body is
// deferred until the rest of the module is wired up; for now we keep the
// prototype so callers link against a stable symbol.
// ============================================================================
i32 AnmManager::ExecuteScript(AnmVm *vm)
{
    // TODO: lift full switch body from FUN_00450d60. This is the single
    // largest function in the module and must be transcribed opcode-by-
    // opcode from the ghidra listing. Each case reads vm fields at fixed
    // offsets (all already mapped in AnmVm.hpp) and mutates them.
    (void)vm;
    return 0;
}

// ============================================================================
// TranslateRotation
// ============================================================================
void AnmManager::TranslateRotation(VertexTex1Xyzrwh *out, f32 x, f32 y, f32 sine, f32 cosine,
                                   f32 xOffset, f32 yOffset)
{
    out->position.x = x * cosine + y * sine + xOffset;
    out->position.y = -x * sine + y * cosine + yOffset;
}

// ============================================================================
// Draw / Draw2 / Draw3 / DrawInner / DrawNoRotation / DrawFacingCamera
//
// TODO: anchored addresses TBD in th07.exe. th06 bodies are a structural
// template; the math is unchanged but the AnmVm field offsets differ (matrix
// at +0xf8, color at +0x1b8, flags u16 at +0x1c0, sprite at +0x1e4).
// ============================================================================

// ============================================================================
// Text helpers (DrawTextToSprite / DrawVmTextFmt / DrawStringFormat[2])
//
// TODO: anchored addresses TBD. th06 bodies rely on TextHelper::RenderTextToTexture.
// ============================================================================

// ============================================================================
// Surface helpers (LoadSurface / ReleaseSurface / ReleaseSurfaces /
// CopySurfaceToBackBuffer / DrawEndingRect)
//
// TODO: anchored addresses TBD.
// ============================================================================

// ============================================================================
// TakeScreenshot{,IfRequested}
//
// TODO: anchored addresses TBD.
// ============================================================================

// ============================================================================
// LoadAnm / ReleaseAnm
//
// LoadAnm (FUN_0044df90) and ReleaseAnm (FUN_0044e4e0) bodies are recovered
// in the ghidra listing above. They will be transcribed once the supporting
// cast (LoadAnmEntry at FUN_0044e070, FileSystem, GameErrorContext message
// string offsets) is finalized.
// ============================================================================
}; // namespace th07
