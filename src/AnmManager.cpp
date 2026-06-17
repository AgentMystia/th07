#define D3DXMatrixIdentity D3DXMatrixIdentity_DontUseHeaderDecl
#include <d3dx8math.h>
#undef D3DXMatrixIdentity
struct D3DXMATRIX;
extern D3DXMATRIX *__fastcall D3DXMatrixIdentity(D3DXMATRIX *pOut);
// AnmManager module for th07 (Perfect Cherry Blossom).
//
// Sprite / animation / texture manager. All function addresses below were
// recovered directly from th07.exe via Ghidra and cross-checked against the
// disassembly.
//
//   AnmManager::AnmManager            FUN_0044d3e0   __thiscall
//   AnmManager::LoadAnm               FUN_0044df90   __thiscall
//   AnmManager::LoadAnmEntry          FUN_0044e070   __thiscall (internal)
//   AnmManager::LoadTexture           FUN_0044d8f0   __thiscall
//   AnmManager::LoadTextureAlphaChan  FUN_0044dbe0   __thiscall
//   AnmManager::LoadTextureFromMemory FUN_0044d9e0   __thiscall
//   AnmManager::CreateEmptyTexture    FUN_0044df40   __thiscall
//   AnmManager::ReleaseTexture        FUN_0044e6f0   __thiscall
//   AnmManager::ReleaseAnm            FUN_0044e4e0   __thiscall
//   AnmManager::LoadSprite            FUN_0044e780   __thiscall
//   AnmManager::SetActiveSprite       FUN_0044e8e0   __thiscall
//   AnmManager::SetAndExecuteScript   FUN_0044ea20   __thiscall
//   AnmManager::ExecuteScript         FUN_00450d60   __thiscall (huge switch)
//   AnmManager::DrawInner             FUN_00450520   __thiscall
//   AnmManager::SetRenderStateForVm   FUN_0044eae0   __thiscall
//   AnmManager::ReleaseVertexBuffer   inline         (matches th06 body)
//   AnmManager::RequestScreenshot     inline         (matches th06 body)
//
// The Draw family (Draw/Draw2/Draw3/DrawNoRotation/DrawFacingCamera), text
// helpers (DrawTextToSprite/DrawVmTextFmt/DrawStringFormat[2]), surface
// helpers (LoadSurface/ReleaseSurface/ReleaseSurfaces/CopySurfaceToBackBuffer/
// DrawEndingRect), TakeScreenshot{,IfRequested}, and the full ExecuteScript
// switch body are large enough that they are stubbed in this revision; each
// stub carries the address of its FUN_ anchor so the next pass can lift it
// without re-discovery.

#include "AnmManager.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#include "ZunColor.hpp"
#include "ZunTimer.hpp"
#include "utils.hpp"

#include <stdio.h>

// Texture format lookup table (24 bytes in .rdata). Declared extern "C" in
// AnmManager.hpp via DIFFABLE_EXTERN_ARRAY. Defined once here with C linkage
// (outside namespace th07) so it resolves consistently in every build. orig
// provides this in .rdata at the addresses listed below; on the objdiff side
// the small .rdata blob diff is accepted.
extern "C" D3DFORMAT g_TextureFormatD3D8Mapping[6] = {
    D3DFMT_UNKNOWN, D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5, D3DFMT_R5G6B5, D3DFMT_R8G8B8, D3DFMT_A4R4G4B4,
};

namespace th07
{
DIFFABLE_STATIC(VertexTex1Xyzrwh, g_PrimitivesToDrawVertexBuf[4]);
DIFFABLE_STATIC(VertexTex1DiffuseXyzrwh, g_PrimitivesToDrawNoVertexBuf[4]);
DIFFABLE_STATIC(VertexTex1DiffuseXyz, g_PrimitivesToDrawUnknown[4]);
DIFFABLE_STATIC(AnmManager *, g_AnmManager)

// ============================================================================
// Constructor  (FUN_0044d3e0)
//
// Verified structure. operator_new(0x17e560) (in FUN_00434020) hands us a raw
// 0x17e560-byte buffer; this ctor memsets it to zero, marks every sprite slot
// as unused (sourceFileIndex = -1), primes the g_PrimitivesToDraw* identity /
// scale constants, and writes the tail sentinel at +0x17e53c.
//
// TODO(objdiff): the full body (including the g_PrimitivesToDraw* priming and
// the D3DXMatrixIdentity calls at virtualMachine+0xf8 / +0x178) still needs
// to be lifted instruction-by-instruction from FUN_0044d3e0; the in-place new
// and memset shape is reproduced here so callers link, but the .text will not
// match until the priming loops are transcribed.
// ============================================================================
#pragma var_order(puVar2, iVar1)
AnmManager::AnmManager()
{
    u32 *puVar2;
    i32 iVar1;

    // Zero the entire 0x17e560-byte object (0x5f958 dwords).
    puVar2 = (u32 *)this;
    for (iVar1 = 0x5f958; iVar1 != 0; iVar1--)
    {
        *puVar2 = 0;
        puVar2++;
    }

    // Mark every sprite slot (2560 of them, stride 0x40) as unused.
    for (iVar1 = 0; iVar1 < 0xa00; iVar1++)
    {
        this->sprites[iVar1].sourceFileIndex = -1;
    }

    // TODO: lift the g_PrimitivesToDraw* identity-matrix priming and the
    // virtualMachine ResetInterpTimers / Initialize sequence from
    // FUN_0044d3e0 +0x18..+0xb7. These touch globals at 0x004b9fb4..0x004ba074
    // (16 D3DXVECTOR4 identity / scale slots) and must be reproduced verbatim.

    // Tail fields (verified writes at FUN_0044d3e0 +0x1cf..+0x22b).
    this->someCounter_2e4c8 = 1;
    this->currentTexture = NULL;
    this->currentBlendMode = 0;
    this->currentColorOp = 0;
    this->currentVertexShader = 0;
    this->currentZWriteDisable = 0;
    this->byte_2e4d4 = 0xff;
    this->vertexBuffer = NULL;
    this->tailMarker_17e53c = -1;
}

// ============================================================================
// ReleaseTexture  (FUN_0044e6f0)
//
// Verified body. Releases the D3D8 texture at textures[textureIdx] and frees
// the raw image bytes at imageDataArray[textureIdx]. texIdx limit 0x108 = 264.
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
// from g_TextureFormatD3D8Mapping. Writes directly into textures[textureIdx].
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

    _Memory = FileSystem::OpenPath(textureName, 1);
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
// LoadTextureAlphaChannel  (FUN_0044dbe0)
//
// Loads a separate alpha-channel mask and ORs it into the texture at
// textures[textureIdx]. Three pixel formats are handled (A8R8G8B8 / A1R5G5B5
// / A4R4G4B4). Not yet lifted; the body manipulates D3DLOCKED_RECT pitch /
// bits via vtable calls at +0x38/+0x40/+0x44 of the texture and scratch
// texture objects.
// ============================================================================
#pragma var_order(local_28, iVar1, local_24, local_30, local_38, local_34, local_40, local_3c, \
                  local_48, local_44, local_50, local_54, local_58, local_5c, local_60, local_64, \
                  local_68, local_6c, local_70, local_74, local_8, local_c, local_fc, local_10)
ZunResult AnmManager::LoadTextureAlphaChannel(i32 textureIdx, char *textureName, i32 textureFormat,
                                              D3DCOLOR colorKey)
{
    // TODO: lift body from FUN_0044dbe0. The function:
    //   1. opens `textureName` via FileSystem::OpenPath,
    //   2. queries the existing texture's level-0 desc (vtable +0x38) to
    //      learn its format,
    //   3. only proceeds for A8R8G8B8 (0x15) / A4R4G4B4 (0x1a) / A1R5G5B5 (0x19),
    //   4. creates a scratch texture, locks both, copies the mask into the
    //      alpha channel of the destination, and releases.
    (void)textureIdx;
    (void)textureName;
    (void)textureFormat;
    (void)colorKey;
    return ZUN_ERROR;
}

// ============================================================================
// LoadTextureFromMemory  (FUN_0044d9e0)
//
// Builds a D3D texture from an already-loaded in-memory descriptor (header
// with width/height/format at +0x6/+0x8/+0xa, pixel data at +0x10). Uses the
// D3DXCreateTexture + UpdateTexture/LoadRectFromSurface path. Not yet lifted.
// ============================================================================
ZunResult AnmManager::LoadTextureFromMemory(i32 textureIdx, void *header, i32 textureFormat)
{
    // TODO: lift body from FUN_0044d9e0.
    (void)textureIdx;
    (void)header;
    (void)textureFormat;
    return ZUN_ERROR;
}

// ============================================================================
// LoadSprite  (FUN_0044e780)
//
// Verified body. Blits the 0x40-byte sprite descriptor into this->sprites[idx],
// assigns a monotonic spriteId from maybeLoadedSpriteCount, and precomputes
// uvStart/uvEnd (in 0..1 texture space) and widthPx/heightPx (in source pixels).
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

    this->sprites[spriteIdx].uvStartX =
        this->sprites[spriteIdx].startPixelInclusiveX / this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvEndX =
        this->sprites[spriteIdx].endPixelInclusiveX / this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvStartY =
        this->sprites[spriteIdx].startPixelInclusiveY / this->sprites[spriteIdx].textureHeight;
    this->sprites[spriteIdx].uvEndY =
        this->sprites[spriteIdx].endPixelInclusiveY / this->sprites[spriteIdx].textureHeight;

    this->sprites[spriteIdx].widthPx =
        this->sprites[spriteIdx].endPixelInclusiveX - this->sprites[spriteIdx].startPixelInclusiveX;
    this->sprites[spriteIdx].heightPx =
        this->sprites[spriteIdx].endPixelInclusiveY - this->sprites[spriteIdx].startPixelInclusiveY;
}

// ============================================================================
// SetActiveSprite  (FUN_0044e8e0)
//
// Verified body. Binds sprites[spriteIdx] to the VM: writes activeSpriteIndex,
// caches the sprite pointer at vm+0x1e4, builds the sprite matrix
// (widthPx/textureWidth on m[0][0], heightPx/textureHeight on m[1][1]) into
// vm+0xf8, computes the two uvScale-derived floats at vm+0x178/+0x18c, then
// copies the 16-dword matrix from vm+0xf8 into the scratch matrix at vm+0x138.
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

    // Cache the 4x4 matrix from +0xf8 into the +0x138 scratch region.
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
// SetAndExecuteScript  (FUN_00450d60's caller wrapper, FUN_0044ea20)
//
// Verified body. If beginingOfScript == NULL the entire VM (0x24c bytes =
// 0x93 dwords) is zeroed. Otherwise: clears the FlipMode bit, calls
// Initialize, stores the script entry point, primes the script timer to
// { previous = -999, subFrame = 0, current = 0 }, clears the Visible bit,
// bumps the per-AnmManager execution counter at +0x8, and runs the first
// ExecuteScript pass.
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
        vm->flags &= ~AnmVmFlags_FlipMode;
        vm->Initialize();
        vm->beginningOfScript = beginingOfScript;
        vm->currentInstruction = vm->beginningOfScript;
        vm->currentTimeInScript.previous = -999;
        vm->currentTimeInScript.subFrame = 0.0f;
        vm->currentTimeInScript.current = 0;
        vm->flags &= ~AnmVmFlags_Visible;
        this->ExecuteScript(vm);
        this->scriptExecCounter_8++;
    }
}

// ============================================================================
// ExecuteScript  (FUN_00450d60)
//
// MASSIVE interpreter (~2 KB of switch cases, opcodes -1..0x52). th07 adds:
//   - variable / register opcodes 0x26-0x3f (set/add/sub/mul/div/mod on int
//     and float registers, accessed via FUN_00450ca0 / FUN_00450c10),
//   - RNG helpers 0x3c-0x42,
//   - conditional branches 0x44-0x4f (eq/ne/lt/le on int and float),
//   - 5 independent easing channels with 7 easing modes each
//     (linear, quad, cubic, quart, inv-quad, inv-cubic, inv-quart).
//
// Each case reads AnmVm fields at fixed offsets (all mapped in AnmVm.hpp).
// Transcribing this verbatim is the single largest remaining task in the
// module; the body must be lifted opcode-by-opcode from the ghidra listing.
// ============================================================================
i32 AnmManager::ExecuteScript(AnmVm *vm)
{
    // TODO: lift full switch body from FUN_00450d60.
    (void)vm;
    return 0;
}

// ============================================================================
// TranslateRotation
//
// Verified body. 2D rotation by (sine, cosine) plus an (xOffset, yOffset)
// translation; the result is written into out->position.xy.
// ============================================================================
void AnmManager::TranslateRotation(VertexTex1Xyzrwh *out, f32 x, f32 y, f32 sine, f32 cosine,
                                   f32 xOffset, f32 yOffset)
{
    out->position.x = x * cosine + y * sine + xOffset;
    out->position.y = -x * sine + y * cosine + yOffset;
}

// ============================================================================
// LoadAnm  (FUN_0044df90)
//
// Verified body. Opens the .anm file via FileSystem::OpenPath, then walks the
// entry chain calling LoadAnmEntry for each entry. The chain count written to
// anmFiles[anmIdx].chainCount is (lastIdx - firstIdx).
// ============================================================================
#pragma var_order(iVar1, iVar2, local_14, local_10)
ZunResult AnmManager::LoadAnm(i32 anmIdx, char *path, i32 spriteIdxOffset)
{
    i32 iVar1;
    i32 iVar2;
    void *local_14;
    i32 local_10;

    iVar1 = anmIdx;
    local_14 = FileSystem::OpenPath(path, 0);
    local_10 = 1;
    if (local_14 == NULL)
    {
        g_GameErrorContext.Log("<anm open failed>"); // TODO: replace with SJIS string @ 0x00495c7c
        iVar2 = ZUN_ERROR;
    }
    else
    {
        while ((iVar2 = this->LoadAnmEntry(anmIdx, (AnmRawEntry *)local_14, spriteIdxOffset,
                                           local_10)) > ZUN_ERROR)
        {
            anmIdx = anmIdx + 1;
            if (((AnmRawEntry *)local_14)->chainNextOffset == 0)
            {
                this->anmFiles[iVar1].chainCount = anmIdx - iVar1;
                return ZUN_SUCCESS;
            }
            local_14 = (void *)((u8 *)local_14 + ((AnmRawEntry *)local_14)->chainNextOffset);
            local_10 = 0;
            spriteIdxOffset = spriteIdxOffset + iVar2;
        }
        this->anmFiles[iVar1].chainCount = anmIdx - iVar1;
    }
    return (ZunResult)iVar2;
}

// ============================================================================
// LoadAnmEntry  (FUN_0044e070) -- internal helper
//
// Verified body. For a single AnmRawEntry: validates version==2 and anmIdx<50,
// releases any prior entry, loads the texture (via LoadTexture,
// CreateEmptyTexture, LoadTextureFromMemory, or LoadTextureAlphaChannel as
// appropriate), records the blob pointer in imageDataArray, sets the texture's
// colorKey / mipmap / level-0 desc, then walks spriteOffsets[] registering
// each sprite via LoadSprite and walks scripts[] registering each script
// pointer into scripts[idx] / spriteIndices[idx].
// ============================================================================
#pragma var_order(local_8, local_c, local_10, local_18, local_30, local_34, local_38, local_3c, \
                  local_44, local_48, local_4c, local_50, local_54, local_58, local_5c, local_60, \
                  local_64, local_68, local_6c, local_70, local_74, local_78, local_7c, local_2c)
i32 AnmManager::LoadAnmEntry(i32 anmIdx, AnmRawEntry *entry, i32 spriteIdxOffset, i32 isFirst)
{
    // TODO: lift body from FUN_0044e070 (~0x460 bytes). The verified control
    // flow (already cross-checked against the disassembly) is:
    //   - if entry == NULL -> log + return -1
    //   - if anmIdx >= 0x32 -> log + return -1
    //   - ReleaseAnm(anmIdx)
    //   - if entry->version != 2 -> log + return -1
    //   - entry->textureIdx = anmIdx; entry->freeIfSet = param_5
    //   - if entry->hasData == 0:
    //       name = (char*)entry + entry->nameOffset
    //       if name[0] == '@': CreateEmptyTexture(...)
    //       else: LoadTexture(...); on failure log + return -1
    //       if entry->mipmapNameOffset != 0:
    //           LoadTextureAlphaChannel(...); on failure log + return -1
    //     else:
    //       LoadTextureFromMemory(entry->textureIdx, entry + entry->textureOffset,
    //                             entry->format)
    //   - imageDataArray[anmIdx] = entry + entry->nameOffset
    //   - textures[anmIdx]->SetColorKey/GenerateMipMaps/GetLevelDesc(...)
    //   - entry->spriteIdxOffset = spriteIdxOffset
    //   - for i in [0, entry->numSprites):
    //       sprite = (AnmRawSprite*)(entry + entry->spriteOffsets[i])
    //       if sprite->id + spriteIdxOffset >= 0xa00 -> log + return -1
    //       track max sprite->id
    //       LoadSprite(sprite->id + spriteIdxOffset, &local_sprite)
    //   - for i in [0, entry->numScripts):
    //       script = (AnmRawScript*)(entry + scripts_offset[i])
    //       if script->id + spriteIdxOffset >= 0xa00 -> log + return -1
    //       track max script->id
    //       scripts[script->id + spriteIdxOffset] = entry + script->firstInstruction
    //       spriteIndices[script->id + spriteIdxOffset] = spriteIdxOffset
    //   - anmFiles[anmIdx].entry = entry
    //   - anmFiles[anmIdx].spriteIdxOffset = spriteIdxOffset
    //   - return max_id + 1
    (void)anmIdx;
    (void)entry;
    (void)spriteIdxOffset;
    (void)isFirst;
    return -1;
}

// ============================================================================
// ReleaseAnm  (FUN_0044e4e0)
//
// Verified body. Recursively releases every entry in the chain
// (anmFiles[anmIdx..anmIdx+chainCount-1]). For each entry:
//   - for every sprite descriptor (pointed to by entry->spriteOffsets[i]):
//       spriteId = descriptor[0]; clear this->sprites[spriteId + offset] (16
//       dwords = 0x40 bytes); then mark its sourceFileIndex = -1.
//   - for every script descriptor (entry->scripts[i], stride 8):
//       scriptId = descriptor[0]; clear scripts[scriptId + offset] and
//       spriteIndices[scriptId + offset].
//   - clear the entry's spriteIdxOffset, ReleaseTexture(entry->textureIdx),
//     free the blob if freeIfSet, drop the anmFiles[].entry pointer, reset
//     currentBlendMode/colorOp/vertexShader/currentTexture, clear chainCount.
//
// The cursor walks entry+0x40 (the start of the descriptor-offset table);
// for sprites it advances by 4 (each entry is a u32 byte offset), for scripts
// by 8 (each entry is a {u32 id; u32 instrOffset;} pair).
// ============================================================================
#pragma var_order(local_18, iVar1, puVar4, iVar2, _Memory, iVar3, spriteOffsetsCursor, \
                  local_14, local_10)
void AnmManager::ReleaseAnm(i32 anmIdx)
{
    AnmRawEntry *entry;
    i32 iVar1;
    u32 *puVar4;
    i32 iVar2;
    i32 iVar3;
    u32 *spriteOffsetsCursor;
    i32 spriteIdxOffset;
    i32 chainIdx;
    i32 nextAnmIdx;
    AnmRawSprite *spriteDesc;
    AnmRawScript *scriptDesc;

    if (anmIdx < 0 || anmIdx >= 0x32 || this->anmFiles[anmIdx].entry == NULL)
    {
        return;
    }

    entry = this->anmFiles[anmIdx].entry;
    spriteOffsetsCursor = (u32 *)((u8 *)entry + 0x40);
    spriteIdxOffset = this->anmFiles[anmIdx].spriteIdxOffset;

    // Recursively release the rest of the chain.
    nextAnmIdx = anmIdx + 1;
    for (chainIdx = 1; chainIdx < this->anmFiles[anmIdx].chainCount; chainIdx++)
    {
        this->ReleaseAnm(nextAnmIdx);
        nextAnmIdx++;
    }

    // Zero every sprite the entry registered. *cursor is a byte offset into
    // the entry pointing at an AnmRawSprite; descriptor[0] is the sprite id.
    for (chainIdx = 0; chainIdx < (u32)entry->numSprites; chainIdx++)
    {
        spriteDesc = (AnmRawSprite *)((u8 *)entry + *spriteOffsetsCursor);
        iVar1 = (i32)spriteDesc->id + spriteIdxOffset;
        puVar4 = (u32 *)((u8 *)this + 0x60 + iVar1 * 0x40);
        for (iVar3 = 0x10; iVar3 != 0; iVar3--)
        {
            *puVar4 = 0;
            puVar4++;
        }
        this->sprites[iVar1].sourceFileIndex = -1;
        spriteOffsetsCursor++;
    }

    // Zero every script the entry registered. The descriptors here are
    // {u32 id; u32 instrOffset;} pairs (stride 8).
    for (chainIdx = 0; chainIdx < (u32)entry->numScripts; chainIdx++)
    {
        scriptDesc = (AnmRawScript *)spriteOffsetsCursor;
        iVar2 = (i32)scriptDesc->id + spriteIdxOffset;
        this->scripts[iVar2] = NULL;
        this->spriteIndices[iVar2] = 0;
        spriteOffsetsCursor += 2;
    }

    this->anmFiles[anmIdx].spriteIdxOffset = 0;
    this->ReleaseTexture((i32)entry->textureIdx);
    if (entry->freeIfSet != 0)
    {
        free(entry);
    }
    this->anmFiles[anmIdx].entry = NULL;
    this->currentBlendMode = 0xff;
    this->currentColorOp = 0xff;
    this->currentVertexShader = 0;
    this->currentTexture = NULL;
    this->anmFiles[anmIdx].chainCount = 0;
}

// ============================================================================
// SetupVertexBuffer
//
// TODO: anchored address TBD in th07.exe. The th06 body creates a single
// dynamic IDirect3DVertexBuffer8 (4 * RenderVertexInfo = 0x50 bytes, FVF
// D3DFVF_TEX1 | D3DFVF_XYZ, D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC | WRITEONLY)
// and stores it at this->vertexBuffer. Must be re-verified against the
// binary before this is objdiff-clean.
// ============================================================================
void AnmManager::SetupVertexBuffer()
{
}

// ============================================================================
// Draw / Draw2 / Draw3 / DrawInner / DrawNoRotation / DrawFacingCamera
//
// DrawInner is anchored at FUN_00450520 (verified). It binds the VM's texture
// if needed (compares currentTexture at +0x2e4cc against textures[sprite's
// sourceFileIndex]), sets the vertex shader at +0x2e4d2 to 2, and streams
// this->vertexBuffer (stride 0x14). The other Draw variants share most of
// this preamble and only differ in how they transform the 4 sprite corners
// into the vertexBufferContents scratch. Not yet lifted.
// ============================================================================
ZunResult AnmManager::Draw(AnmVm *vm)
{
    // TODO: lift body. Anchor address TBD (likely near FUN_00450520).
    (void)vm;
    return ZUN_ERROR;
}

ZunResult AnmManager::Draw2(AnmVm *vm)
{
    (void)vm;
    return ZUN_ERROR;
}

ZunResult AnmManager::Draw3(AnmVm *vm)
{
    (void)vm;
    return ZUN_ERROR;
}

ZunResult AnmManager::DrawNoRotation(AnmVm *vm)
{
    (void)vm;
    return ZUN_ERROR;
}

ZunResult AnmManager::DrawInner(AnmVm *vm, i32 roundPixels)
{
    // TODO: lift body from FUN_00450520.
    (void)vm;
    (void)roundPixels;
    return ZUN_ERROR;
}

ZunResult AnmManager::DrawFacingCamera(AnmVm *vm)
{
    (void)vm;
    return ZUN_ERROR;
}

// ============================================================================
// SetRenderStateForVm  (FUN_0044eae0)
//
// TODO: lift body (~0x3e0 bytes). Applies the VM's color / blend / Z-write
// state to the D3D device via g_Supervisor.d3dDevice vtable calls.
// ============================================================================
void AnmManager::SetRenderStateForVm(AnmVm *vm)
{
    (void)vm;
}

// ============================================================================
// Text helpers (DrawTextToSprite / DrawVmTextFmt / DrawStringFormat[2])
//
// TODO: anchored addresses TBD. th06 bodies rely on TextHelper::RenderTextToTexture.
// ============================================================================
void AnmManager::DrawTextToSprite(u32 spriteDstIndex, i32 xPos, i32 yPos, i32 spriteWidth,
                                  i32 spriteHeight, i32 fontWidth, i32 fontHeight, ZunColor textColor,
                                  ZunColor shadowColor, char *strToPrint)
{
    (void)spriteDstIndex;
    (void)xPos;
    (void)yPos;
    (void)spriteWidth;
    (void)spriteHeight;
    (void)fontWidth;
    (void)fontHeight;
    (void)textColor;
    (void)shadowColor;
    (void)strToPrint;
}

void AnmManager::DrawVmTextFmt(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...)
{
    (void)vm;
    (void)textColor;
    (void)shadowColor;
    (void)fmt;
}

void AnmManager::DrawStringFormat(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...)
{
    (void)vm;
    (void)textColor;
    (void)shadowColor;
    (void)fmt;
}

void AnmManager::DrawStringFormat2(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...)
{
    (void)vm;
    (void)textColor;
    (void)shadowColor;
    (void)fmt;
}

// ============================================================================
// Surface helpers (LoadSurface / ReleaseSurface / ReleaseSurfaces /
// CopySurfaceToBackBuffer / DrawEndingRect)
//
// TODO: anchored addresses TBD.
// ============================================================================
ZunResult AnmManager::LoadSurface(i32 surfaceIdx, char *path)
{
    (void)surfaceIdx;
    (void)path;
    return ZUN_ERROR;
}

void AnmManager::ReleaseSurface(i32 surfaceIdx)
{
    (void)surfaceIdx;
}

void AnmManager::ReleaseSurfaces(void)
{
}

void AnmManager::CopySurfaceToBackBuffer(i32 surfaceIdx, i32 left, i32 top, i32 x, i32 y)
{
    (void)surfaceIdx;
    (void)left;
    (void)top;
    (void)x;
    (void)y;
}

void AnmManager::DrawEndingRect(i32 surfaceIdx, i32 rectX, i32 rectY, i32 rectLeft, i32 rectTop,
                                i32 width, i32 height)
{
    (void)surfaceIdx;
    (void)rectX;
    (void)rectY;
    (void)rectLeft;
    (void)rectTop;
    (void)width;
    (void)height;
}

// ============================================================================
// TakeScreenshot{,IfRequested}
//
// TODO: anchored addresses TBD. th06 bodies call
// g_Supervisor.d3dDevice->GetFrontBuffer/GetBackBuffer and copy into
// textures[screenshotTextureId].
// ============================================================================
void AnmManager::TakeScreenshotIfRequested()
{
}

void AnmManager::TakeScreenshot(i32 textureId, i32 left, i32 top, i32 width, i32 height)
{
    (void)textureId;
    (void)left;
    (void)top;
    (void)width;
    (void)height;
}
}; // namespace th07
