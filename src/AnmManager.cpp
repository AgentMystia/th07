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

// ---- SetRenderStateForVm cross-module references (FUN_0044eae0).
// These globals/helpers live outside the AnmManager module; declared here
// with C linkage so the same call shape compiles in normal and objdiff
// builds. Their SYMBOL_MAP entries (scripts/generate_objdiff_objs.py) map
// each typed name back to its orig DAT_/FUN_ address for byte-true diffing.
//   - 8 colour-slot dwords stamped on the software (no-vertex-buffer) path.
//   - g_SupervisorViewport_575a18: viewport struct (also referenced by GameManager).
//   - g_SupervisorCfgOpts_575a9c: cfg.opts bitfield (orig reads it as an absolute
//     0x575a9c load, not a struct-relative access, so we mirror that here).
//   - g_SupervisorCameraStub_1347b00: ignored `this` passed to the camera
//     helpers (they read the device via the absolute 0x575958 slot).
//   - AnmManager_FlushVertexBuffer (FUN_0044f5c0): in-module helper
//     that drains this->vertexBufferDirty via DrawPrimitiveUP.
//   - Supervisor_Setup3DCamera / _2DCamera_4082b0 (FUN_00408180 /
//     FUN_004082b0): re-install the 3D / 2D view+projection matrices.
extern "C" u32 g_AnmMgrColorSlot_4b9fb8;
extern "C" u32 g_AnmMgrColorSlot_4b9fd4;
extern "C" u32 g_AnmMgrColorSlot_4b9ff0;
extern "C" u32 g_AnmMgrColorSlot_4ba00c;
extern "C" u32 g_AnmMgrColorSlot_4ba084;
extern "C" u32 g_AnmMgrColorSlot_4ba09c;
extern "C" u32 g_AnmMgrColorSlot_4ba0b4;
extern "C" u32 g_AnmMgrColorSlot_4ba0cc;
extern "C" u32 g_SupervisorViewport_575a18;
extern "C" u32 g_SupervisorCfgOpts_575a9c;
// D3D device pointer slot (orig 0x575958 = g_Supervisor + 8). Orig reads
// it as an absolute [DAT_00575958] load; mirroring that avoids the
// struct-relative [DAT_00575950+8] reloc that objdiff treats as distinct.
extern "C" void *g_SupervisorD3dDevice_575958;
extern "C" u8 g_SupervisorCameraStub_1347b00;
extern "C" void __fastcall AnmManager_FlushVertexBuffer(void *anmMgr);
extern "C" void __fastcall Supervisor_Setup3DCamera(void *cameraStub);
extern "C" void __fastcall Supervisor_Setup2DCamera(void *cameraStub);
// DrawInner (FUN_00450520) matrix helpers (D3DX math): build a rotation
// matrix from a single angle (Z/X/Y variants) and multiply two matrices.
// Each takes (D3DXMATRIX *out, float angle) or (out, a, b). Orig FUN_anchors.
extern "C" void __fastcall D3DXMatrixRotationZ_461b85(D3DXMATRIX *out, f32 angle);
extern "C" void __fastcall D3DXMatrixRotationX_461bff(D3DXMATRIX *out, f32 angle);
extern "C" void __fastcall D3DXMatrixRotationY_461c7a(D3DXMATRIX *out, f32 angle);
extern "C" void __fastcall D3DXMatrixMultiply_461aa2(D3DXMATRIX *out, D3DXMATRIX *a, D3DXMATRIX *b);
// DrawPrimitiveUP vertex buffer (orig 0x4ba078). 4 * VertexTex1Xyzrwh = 0x60
// bytes; DrawPrimitiveUP reads it with stride 0x18.
extern "C" u8 g_DrawPrimUpVerts_4ba078[0x60];
// LoadTextureFromMemory cross-module references:
//   g_TextureFormatBytesPerPixel_495144: per-format bytes-per-pixel table
//     (orig 0x495144, 6 entries indexed by the same format index as
//     g_TextureFormatD3D8Mapping).
//   D3DXCreateTextureFromSurface_46298a (FUN_0046298a): wraps
//     IDirect3DDevice8::CreateTexture + D3DX surface bookkeeping.
//   D3DXLoadSurfaceFromMemory_462aa6 (FUN_00462aa6): wraps
//     IDirect3DSurface8::LockRect + D3DXLoadSurfaceFromMemory + UnlockRect.
extern "C" u32 g_TextureFormatBytesPerPixel_495144[6];
extern "C" i32 __fastcall D3DXCreateTextureFromSurface_46298a(void *device, i32 width, i32 height,
                                                              i32 mipLevels, i32 usage,
                                                              i32 format, i32 pool,
                                                              void **ppTexture);
extern "C" i32 __fastcall D3DXLoadSurfaceFromMemory_462aa6(void *dstSurface, void *palette,
                                                           void *dstRect, void *srcMemory,
                                                           void *srcPalette, void *srcRect,
                                                           i32 filter, u32 colorKey);
// LoadTextureAlphaChannel (FUN_0044dbe0): D3DXCreateTextureFromFileInMemoryEx
// wrapper (FUN_00462cf4). __cdecl; mirrors D3DX's 15-arg form but with the
// pool/filter args pre-baked by the wrapper. Orig signature:
//   int cdecl(device, fileData, fileSize, width, height, mipLevels, usage,
//             format, pool, filter, mipFilter, colorKey, srcInfo, palette,
//             &texture)
extern "C" i32 __cdecl D3DXCreateTextureFromFileInMemoryEx_Wrapper_462cf4(
    void *device, void *fileData, u32 fileSize, u32 width, u32 height, u32 mipLevels, u32 usage,
    u32 format, u32 pool, u32 filter, u32 mipFilter, u32 colorKey, void *srcInfo, void *palette,
    IDirect3DTexture8 **ppTexture);

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
// Verified body, lifted from the disassembly. Loads a separate alpha-channel
// mask image and ORs it into the alpha bits of textures[textureIdx]. Only
// proceeds for three destination formats:
//   D3DFMT_A8R8G8B8 (0x15): copy the mask's red byte into each dest pixel's
//                            alpha byte (dest+3 = src red).
//   D3DFMT_A1R5G5B5 (0x19): dest bit 15 = (src blue & 0x1f) >> 4 & 1.
//   D3DFMT_A4R4G4B4 (0x1a): dest bits 12..15 = src low nibble.
// The mask is loaded via D3DXCreateTextureFromFileInMemoryEx wrapper
// (FUN_00462cf4); both surfaces are locked row-by-row for the bit-blit.
// ============================================================================
#pragma var_order(this_save, maskImageData, alphaTexture, dstLockedRect, srcLockedRect, \
                  dstSurfaceDesc, dstRowBytes, dstBitsBase, srcRowBytes, srcBitsBase, \
                  rowIdx, colIdx, dstRowPtr, srcRowPtr)
ZunResult AnmManager::LoadTextureAlphaChannel(i32 textureIdx, char *textureName, i32 textureFormat,
                                              D3DCOLOR colorKey)
{
    AnmManager *this_save;
    void *maskImageData;
    IDirect3DTexture8 *alphaTexture;
    D3DLOCKED_RECT dstLockedRect;
    D3DLOCKED_RECT srcLockedRect;
    D3DSURFACE_DESC dstSurfaceDesc;
    u32 dstRowBytes;
    void *dstBitsBase;
    u32 srcRowBytes;
    void *srcBitsBase;
    u32 rowIdx;
    u32 colIdx;
    u8 *dstRowPtr;
    u8 *srcRowPtr;

    this_save = this;
    alphaTexture = NULL;

    maskImageData = FileSystem::OpenPath(textureName, 0);
    if (maskImageData == NULL)
    {
        return ZUN_ERROR;
    }

    // Query the destination texture's level-0 format.
    this_save->textures[textureIdx]->GetLevelDesc(0, &dstSurfaceDesc);
    if (dstSurfaceDesc.Format != D3DFMT_A8R8G8B8 &&
        dstSurfaceDesc.Format != D3DFMT_A4R4G4B4 &&
        dstSurfaceDesc.Format != D3DFMT_A1R5G5B5)
    {
        g_GameErrorContext.Log("error : \203C\203\201\201[\203W\202\252\203\277\202\360\216"
                               "\235\202\301\202\304\202\242\202\334\202\271\202\361\015\012");
        goto cleanup_and_fail;
    }

    // Load the alpha mask as a scratch texture.
    if (D3DXCreateTextureFromFileInMemoryEx_Wrapper_462cf4(
            (IDirect3DDevice8 *)g_SupervisorD3dDevice_575958, maskImageData, g_LastFileSize, 0, 0, 0,
            0, dstSurfaceDesc.Format, 2, 3, (u32)-1, colorKey, 0, 0, &alphaTexture) != 0)
    {
        goto cleanup_and_fail;
    }
    // Lock both textures' level-0 surfaces (rect=NULL, flags=0 / 0x8000 readonly).
    if (this_save->textures[textureIdx]->LockRect(0, &dstLockedRect, NULL, 0) != 0)
    {
        goto cleanup_and_fail;
    }
    if (alphaTexture->LockRect(0, &srcLockedRect, NULL, D3DLOCK_READONLY) != 0)
    {
        goto cleanup_and_fail;
    }

    if (dstSurfaceDesc.Format == D3DFMT_A8R8G8B8)
    {
        // 32-bit ARGB: copy the mask's red byte (src byte 0) into the dest
        // alpha byte (dest byte 3) for each pixel.
        for (rowIdx = 0; rowIdx < dstSurfaceDesc.Height; rowIdx++)
        {
            dstRowPtr = (u8 *)dstLockedRect.pBits + rowIdx * dstLockedRect.Pitch;
            srcRowPtr = (u8 *)srcLockedRect.pBits + rowIdx * srcLockedRect.Pitch;
            for (colIdx = 0; colIdx < dstSurfaceDesc.Width; colIdx++)
            {
                dstRowPtr[3] = *srcRowPtr;
                srcRowPtr += 4;
                dstRowPtr += 4;
            }
        }
    }
    else if (dstSurfaceDesc.Format == D3DFMT_A1R5G5B5)
    {
        // 16-bit 1-5-5-5: dest bit 15 = (src 5-5-5 blue & 0x1f) >> 4 & 1.
        for (rowIdx = 0; rowIdx < dstSurfaceDesc.Height; rowIdx++)
        {
            u16 *dstRow16 = (u16 *)((u8 *)dstLockedRect.pBits + rowIdx * dstLockedRect.Pitch);
            u16 *srcRow16 = (u16 *)((u8 *)srcLockedRect.pBits + rowIdx * srcLockedRect.Pitch);
            for (colIdx = 0; colIdx < dstSurfaceDesc.Width; colIdx++)
            {
                *dstRow16 = (*dstRow16 & 0x7fff) |
                            (u16)((((i32)(*srcRow16 & 0x1f) >> 4) & 1) << 0xf);
                srcRow16++;
                dstRow16++;
            }
        }
    }
    else /* D3DFMT_A4R4G4B4 */
    {
        // 16-bit 4-4-4-4: dest bits 12..15 = src low nibble.
        for (rowIdx = 0; rowIdx < dstSurfaceDesc.Height; rowIdx++)
        {
            u16 *dstRow16 = (u16 *)((u8 *)dstLockedRect.pBits + rowIdx * dstLockedRect.Pitch);
            u16 *srcRow16 = (u16 *)((u8 *)srcLockedRect.pBits + rowIdx * srcLockedRect.Pitch);
            for (colIdx = 0; colIdx < dstSurfaceDesc.Width; colIdx++)
            {
                *dstRow16 = (*dstRow16 & 0x0fff) | (u16)((*srcRow16 & 0xf) << 0xc);
                srcRow16++;
                dstRow16++;
            }
        }
    }

    alphaTexture->UnlockRect(0);
    this_save->textures[textureIdx]->UnlockRect(0);
    if (alphaTexture != NULL)
    {
        alphaTexture->Release();
        alphaTexture = NULL;
    }
    free(maskImageData);
    return ZUN_SUCCESS;

cleanup_and_fail:
    if (alphaTexture != NULL)
    {
        alphaTexture->Release();
        alphaTexture = NULL;
    }
    free(maskImageData);
    return ZUN_ERROR;
}

// ============================================================================
// LoadTextureFromMemory  (FUN_0044d9e0)
//
// Verified body, lifted from the disassembly. Builds a D3D texture from an
// already-loaded in-memory descriptor:
//   header+0x06 (i16) textureFormat index (into g_TextureFormatD3D8Mapping)
//   header+0x08 (i16) width
//   header+0x0a (i16) height
//   header+0x10       pixel data
//
// Flow:
//   1. ReleaseTexture(textureIdx).
//   2. If cfg.opts bit 2 (FORCE_16BIT_COLOR_MODE): demote A8R8G8B8/UNKNOWN
//      to A4R4G4B4 (idx 5), R8G8B8 to R5G6B5 (idx 3).
//   3. device->CreateImageSurface(width, height, format[header->fmt], &surf).
//   4. surf->LockRect(&lockedRect, NULL, 0).
//   5. For each row: memcpy width*bpp[header->fmt] bytes from
//      header+0x10 + row*width*bpp into lockedRect.pBits + row*lockedRect.Pitch.
//   6. surf->UnlockRect().
//   7. D3DXCreateTextureFromSurface(device, width, height, 1, 0, format[fmt],
//      1, &textures[textureIdx]) via FUN_0046298a.
//   8. textures[textureIdx]->GetSurfaceLevel(0, &texSurface).
//   9. D3DXLoadSurfaceFromSurface(texSurface, NULL, NULL, surf, NULL, NULL,
//      D3DX_FILTER_NONE, 0) via FUN_00462aa6.
//  10. Release the temp image surface + the texture-level surface.
// ============================================================================
#pragma var_order(header, imageSurface, lockedRect, rowIdx, dstRow, srcRow, rowBytes, texSurface)
ZunResult AnmManager::LoadTextureFromMemory(i32 textureIdx, void *headerIn, i32 textureFormat)
{
    void *header;
    IDirect3DSurface8 *imageSurface;
    D3DLOCKED_RECT lockedRect;
    i32 rowIdx;
    u8 *dstRow;
    u8 *srcRow;
    u32 rowBytes;
    IDirect3DSurface8 *texSurface;

    header = headerIn;

    this->ReleaseTexture(textureIdx);

    // 16-bit-colour downgrade (mirror LoadTexture's branch).
    if ((g_SupervisorCfgOpts_575a9c >> GCOS_FORCE_16BIT_COLOR_MODE & 1) != 0)
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

    // Create a scratch image surface and lock it for writing.
    ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
        ->CreateImageSurface((UINT) * (i16 *)((u8 *)header + 0x8),
                             (UINT) * (i16 *)((u8 *)header + 0xa),
                             g_TextureFormatD3D8Mapping[*(i16 *)((u8 *)header + 0x6)],
                             &imageSurface);
    imageSurface->LockRect(&lockedRect, NULL, 0);

    // Copy each row, accounting for the surface's pitch (which may exceed
    // width*bpp due to alignment) vs the source's packed layout.
    for (rowIdx = 0; rowIdx < *(i16 *)((u8 *)header + 0xa); rowIdx++)
    {
        dstRow = (u8 *)lockedRect.pBits + rowIdx * lockedRect.Pitch;
        rowBytes = (u32)(*(i16 *)((u8 *)header + 0x8)) *
                   g_TextureFormatBytesPerPixel_495144[*(i16 *)((u8 *)header + 0x6)];
        srcRow = (u8 *)header + 0x10 +
                 rowIdx * (*(i16 *)((u8 *)header + 0x8)) *
                              g_TextureFormatBytesPerPixel_495144[*(i16 *)((u8 *)header + 0x6)];
        memcpy(dstRow, srcRow, rowBytes);
    }

    imageSurface->UnlockRect();

    // Create the actual texture from the scratch surface.
    if (D3DXCreateTextureFromSurface_46298a(
            (IDirect3DDevice8 *)g_SupervisorD3dDevice_575958,
            (i32) * (i16 *)((u8 *)header + 0x8), (i32) * (i16 *)((u8 *)header + 0xa), 1, 0,
            (i32)g_TextureFormatD3D8Mapping[textureFormat], 1,
            (void **)(this->textures + textureIdx)) != 0)
    {
        return ZUN_ERROR;
    }

    // Copy the scratch surface into the texture's level-0 surface.
    this->textures[textureIdx]->GetSurfaceLevel(0, &texSurface);
    if (D3DXLoadSurfaceFromMemory_462aa6(texSurface, 0, 0, imageSurface, 0, 0, 3, 0) != 0)
    {
        return ZUN_ERROR;
    }

    if (imageSurface != NULL)
    {
        imageSurface->Release();
        imageSurface = NULL;
    }
    if (texSurface != NULL)
    {
        texSurface->Release();
    }

    return ZUN_SUCCESS;
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
    D3DXMatrixIdentity(&vm->scratchMatrix);
    vm->matrix.m[0][0] = vm->sprite->widthPx / vm->sprite->textureWidth;
    vm->matrix.m[1][1] = vm->sprite->heightPx / vm->sprite->textureHeight;

    vm->textureMatrix.m[0][0] =
        (vm->sprite->widthPx / vm->sprite->textureHeight) * vm->sprite->uvScaleX;
    vm->textureMatrix.m[1][1] =
        (vm->sprite->heightPx / vm->sprite->textureWidth) * vm->sprite->uvScaleY;

    // Cache the 4x4 world matrix (vm->matrix) into the scratch copy
    // (vm->scratchMatrix).
    {
        u32 *src = (u32 *)&vm->matrix;
        u32 *dst = (u32 *)&vm->scratchMatrix;
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
// Verified body, lifted instruction-by-instruction from the disassembly.
// For a single AnmRawEntry: validates version==2 and anmIdx<50, releases any
// prior entry, loads the texture (via LoadTexture, CreateEmptyTexture,
// LoadTextureFromMemory, or LoadTextureAlphaChannel as appropriate), records
// the blob pointer in imageDataArray, applies the texture's priority / preload
// and queries its level-0 surface desc (to learn the real texture pixel
// dimensions), then walks spriteOffsets[] registering each sprite via
// LoadSprite (precomputing the per-sprite texture-space rect from the texture
// desc's Width/Height and the entry's declared width/height), and walks
// scripts[] registering each script pointer into scripts[idx] /
// spriteIndices[idx]. Returns (maxRegisteredId + 1); -1 on any failure.
// ============================================================================
#pragma var_order(iVar_maxId, surfaceDesc, textureName, spriteDesc, \
                  loopIdx, descTableCursor, localSprite, invWidth, invHeight)
i32 AnmManager::LoadAnmEntry(i32 anmIdx, AnmRawEntry *entry, i32 spriteIdxOffset, i32 isFirst)
{
    i32 iVar_maxId;
    D3DSURFACE_DESC surfaceDesc;
    char *textureName;
    AnmRawSprite *spriteDesc;
    i32 loopIdx;
    u32 *descTableCursor;
    AnmLoadedSprite localSprite;
    f32 invWidth;
    f32 invHeight;

    iVar_maxId = 0;

    if (entry == NULL)
    {
        g_GameErrorContext.Log("\203A\203j\203\201\202\252\223\307\202\335\215\236\202\337\202\334\202\271"
                               "\202\361\201B\203f\201[\203^\202\252\216\270\202\355\202\352\202\304\202\351"
                               "\202\251\211\363\202\352\202\304\202\242\202\334\202\267\015\012");
        return -1;
    }

    if (anmIdx >= 0x32)
    {
        g_GameErrorContext.Log("\203e\203N\203X\203`\203\203\212i\224[\220\346\202\252\221\253"
                               "\202\350\202\334\202\271\202\361\015\012");
        return -1;
    }

    this->ReleaseAnm(anmIdx);

    if (entry->version != 2)
    {
        g_GameErrorContext.Log("\203A\203j\203\201\202\314\203o\201[\203W\203\207\203\223\202\252"
                               "\210\341\202\242\202\334\202\267\015\012");
        return -1;
    }

    entry->textureIdx = (u32)anmIdx;
    entry->freeIfSet = (u8)isFirst;

    if (entry->hasData == 0)
    {
        textureName = (char *)((u8 *)entry + entry->nameOffset);
        if (textureName[0] == '@')
        {
            this->CreateEmptyTexture((i32)entry->textureIdx, (u32)entry->width, (u32)entry->height,
                                     (i32)entry->format);
        }
        else
        {
            if (this->LoadTexture((i32)entry->textureIdx, textureName, (i32)entry->format,
                                  entry->colorKey) != 0)
            {
                g_GameErrorContext.Log("\203e\203N\203X\203`\203\203 %s \202\252\223\307\202\335\215"
                                       "\236\202\337\202\334\202\271\202\361\201B\203f\201[\203^\202\252"
                                       "\216\270\202\355\202\352\202\304\202\351\202\251\211\363\202\352"
                                       "\202\304\202\242\202\334\202\267\015\012", textureName);
                return -1;
            }
        }

        if (entry->mipmapNameOffset != 0)
        {
            textureName = (char *)((u8 *)entry + entry->mipmapNameOffset);
            if (this->LoadTextureAlphaChannel((i32)entry->textureIdx, textureName, (i32)entry->format,
                                              entry->colorKey) != 0)
            {
                g_GameErrorContext.Log("\203e\203N\203X\203`\203\203 %s \202\252\223\307\202\335\215"
                                       "\236\202\337\202\334\202\271\202\361\201B\203f\201[\203^\202\256"
                                       "\216\270\202\355\202\352\202\304\202\351\202\251\211\363\202\352"
                                       "\202\304\202\242\202\334\202\267\015\012", textureName);
                return -1;
            }
        }
    }
    else
    {
        if (this->LoadTextureFromMemory((i32)entry->textureIdx,
                                        (void *)((u8 *)entry + entry->textureOffset),
                                        (i32)entry->format) != 0)
        {
            g_GameErrorContext.Log("\203e\203N\203X\203`\203\203\202\252\223\307\202\335\215\236\202"
                                   "\337\202\334\202\271\202\361\201B\203f\201[\203^\202\252\216\270\202"
                                   "\355\202\352\202\304\202\351\202\251");
            return -1;
        }
    }

    // Record the source blob (entry + nameOffset) so ReleaseAnm can free it.
    this->imageDataArray[anmIdx] = (void *)((u8 *)entry + entry->nameOffset);

    // Apply texture priority (entry->unk1), preload to GPU, and query the
    // level-0 surface desc to learn the real texture pixel dimensions.
    this->textures[anmIdx]->SetPriority(entry->unk1);
    this->textures[anmIdx]->PreLoad();
    this->textures[anmIdx]->GetLevelDesc(0, &surfaceDesc);

    entry->spriteIdxOffset = (u32)spriteIdxOffset;

    // First descriptor table lives at entry + 0x40; each entry is a u32 byte
    // offset into the blob pointing at an AnmRawSprite.
    descTableCursor = (u32 *)((u8 *)entry + 0x40);
    for (loopIdx = 0; loopIdx < entry->numSprites; loopIdx++)
    {
        spriteDesc = (AnmRawSprite *)((u8 *)entry + *descTableCursor);

        // Precompute the texture-space rect into a local AnmLoadedSprite.
        localSprite.sourceFileIndex = (i32)entry->textureIdx;
        invWidth = (f32)surfaceDesc.Width / (f32)entry->width;
        invHeight = (f32)surfaceDesc.Height / (f32)entry->height;
        localSprite.startPixelInclusiveX = invWidth * spriteDesc->offset.x;
        localSprite.startPixelInclusiveY = invHeight * spriteDesc->offset.y;
        localSprite.endPixelInclusiveX = (spriteDesc->offset.x + spriteDesc->size.x) * invWidth;
        localSprite.endPixelInclusiveY = (spriteDesc->offset.y + spriteDesc->size.y) * invHeight;
        localSprite.textureWidth = (f32)surfaceDesc.Width;
        localSprite.textureHeight = (f32)surfaceDesc.Height;

        if (iVar_maxId < (i32)spriteDesc->id)
        {
            iVar_maxId = (i32)spriteDesc->id;
        }

        if ((i32)spriteDesc->id + spriteIdxOffset >= 0xa00)
        {
            g_GameErrorContext.Log("\203X\203v\203\211\203C\203g\202\252\212i\224[\202\305\202\253"
                                   "\202\334\202\271\202\361\201B\203e\201[\203u\203\213\202\252\225s"
                                   "\221\253\202\265\202\304\202\242\202\334\202\267");
            return -1;
        }

        this->LoadSprite(spriteDesc->id + (u32)spriteIdxOffset, &localSprite);
        descTableCursor++;
    }

    // Script descriptors follow the sprite descriptors in the same table; each
    // is an AnmRawScript { u32 id; AnmRawInstr *firstInstruction; } (stride 8).
    for (loopIdx = 0; loopIdx < entry->numScripts; loopIdx++)
    {
        if ((i32)*descTableCursor + spriteIdxOffset >= 0xa00)
        {
            g_GameErrorContext.Log("\203A\203j\203\201\202\252\212i\224[\202\305\202\253\202\334\202"
                                   "\271\202\361\201B\203e\201[\203u\203\213\202\252\225s\221\253\202"
                                   "\265\202\304\202\242\202\334\202\267");
            return -1;
        }

        if (iVar_maxId < (i32)*descTableCursor)
        {
            iVar_maxId = (i32)*descTableCursor;
        }

        this->scripts[*descTableCursor + (u32)spriteIdxOffset] =
            (AnmRawInstr *)((u8 *)entry + descTableCursor[1]);
        this->spriteIndices[*descTableCursor + (u32)spriteIdxOffset] = spriteIdxOffset;
        descTableCursor += 2;
    }

    this->anmFiles[anmIdx].entry = entry;
    this->anmFiles[anmIdx].spriteIdxOffset = spriteIdxOffset;

    return iVar_maxId + 1;
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

#pragma var_order(worldMatrix, rotMatrix, texMatrix, positionX, positionY, \
                  rotAbsX, rotAbsY)
ZunResult AnmManager::DrawInner(AnmVm *vm)
{
    D3DXMATRIX worldMatrix;
    D3DXMATRIX rotMatrix;
    D3DXMATRIX texMatrix;
    f32 positionX;
    f32 positionY;
    f32 rotAbsX;
    f32 rotAbsY;

    // ---- early bail: VM must be visible, in-scope, and have a colour ------
    if (((*(u32 *)((u8 *)vm + 0x1c0)) & 1) == 0)
    {
        return (ZunResult)-1;
    }
    if (((*(u32 *)((u8 *)vm + 0x1c0)) >> 1 & 1) == 0)
    {
        return (ZunResult)-1;
    }
    if (*((u8 *)&vm->color + 3) == 0)
    {
        return (ZunResult)-1;
    }

    // Flush any pending vertex-buffer draw before touching device state.
    if (this->vertexBufferDirty != 0)
    {
        AnmManager_FlushVertexBuffer(this);
    }

    // ---- optional rotation update (flag 15 clear, flag 3 or 2 set) -------
    if (((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xf & 1) == 0 &&
        (((*(u32 *)((u8 *)vm + 0x1c0)) >> 3 & 1) != 0 || ((*(u32 *)((u8 *)vm + 0x1c0)) >> 2 & 1) != 0))
    {
        // Copy the world matrix (vm->matrix) into the scratch copy
        // (vm->scratchMatrix) and bake the per-axis scale into the diagonal.
        memcpy(&vm->scratchMatrix, &vm->matrix, sizeof(D3DXMATRIX));
        vm->scratchMatrix.m[0][0] = vm->scratchMatrix.m[0][0] * vm->scaleX;
        vm->scratchMatrix.m[1][1] = vm->scratchMatrix.m[1][1] * vm->scaleY;
        vm->flags = (u16)(vm->flags & ~0x8);

        // For each non-zero rotation axis, rotate the matrix and re-multiply.
        if (vm->rotation.x != 0.0f)
        {
            D3DXMatrixRotationZ_461b85(&rotMatrix, vm->rotation.x);
            D3DXMatrixMultiply_461aa2(&vm->scratchMatrix, &vm->scratchMatrix, &rotMatrix);
        }
        if (vm->rotation.y != 0.0f)
        {
            D3DXMatrixRotationX_461bff(&rotMatrix, vm->rotation.y);
            D3DXMatrixMultiply_461aa2(&vm->scratchMatrix, &vm->scratchMatrix, &rotMatrix);
        }
        if (vm->rotation.z != 0.0f)
        {
            D3DXMatrixRotationY_461c7a(&rotMatrix, vm->rotation.z);
            D3DXMatrixMultiply_461aa2(&vm->scratchMatrix, &vm->scratchMatrix, &rotMatrix);
        }
        vm->flags = (u16)(vm->flags & ~0x4);
    }

    // Snapshot the per-VM scratch world matrix for SetTransform.
    memcpy(&worldMatrix, &vm->scratchMatrix, sizeof(D3DXMATRIX));

    // ---- compute the on-screen position -----------------------------------
    if (((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xa & 1) != 0)
    {
        rotAbsX = (vm->sprite->widthPx * vm->scaleX) / 2.0f;
        if (rotAbsX < 0) rotAbsX = -rotAbsX;
        positionX = rotAbsX + vm->positionOffsetX;
    }
    else
    {
        positionX = vm->positionOffsetX;
    }
    if (((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xa & 2) != 0)
    {
        rotAbsY = (vm->sprite->heightPx * vm->scaleY) / 2.0f;
        if (rotAbsY < 0) rotAbsY = -rotAbsY;
        positionY = rotAbsY + vm->positionOffsetY;
    }
    else
    {
        positionY = vm->positionOffsetY;
    }
    // Add the per-AnmManager frame offset.
    positionX = positionX + this->frameOffsetX;
    positionY = positionY + this->frameOffsetY;

    // Apply blend / colour / Z render state for this VM.
    this->SetRenderStateForVm(vm);

    // ---- bind the world transform -----------------------------------------
    ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
        ->SetTransform(D3DTS_WORLD, &worldMatrix);

    // ---- rebind texture + uv-scroll transform when the sprite changes ----
    // Orig compares the cached sprite pointer (stored as f32 bits in
    // this->cachedSpritePtr_2e4d8) against vm->sprite. We mirror the bit
    // reinterpret so the objdiff byte-match holds.
    if (this->cachedSpritePtr_2e4d8 != *(f32 *)&vm->sprite)
    {
        this->cachedSpritePtr_2e4d8 = *(f32 *)&vm->sprite;

        // Snapshot the per-VM texture matrix and bake in the current uv
        // scroll position.
        memcpy(&texMatrix, &vm->textureMatrix, sizeof(D3DXMATRIX));
        texMatrix.m[2][0] = vm->sprite->uvStartX + vm->uvScrollPos.x;
        texMatrix.m[2][1] = vm->sprite->uvStartY + vm->uvScrollPos.y;
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->SetTransform(D3DTS_TEXTURE0, &texMatrix);

        // Rebind the actual texture surface if it changed.
        if (this->currentTexture !=
            this->textures[vm->sprite->sourceFileIndex])
        {
            this->currentTexture = this->textures[vm->sprite->sourceFileIndex];
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetTexture(0, this->currentTexture);
        }
    }

    // ---- (re)install the vertex shader on transition ----------------------
    if (this->currentVertexShader != 2)
    {
        if ((g_SupervisorCfgOpts_575a9c >> 1 & 1) == 0)
        {
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetVertexShader(D3DFVF_TEX1 | D3DFVF_XYZ);
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetStreamSource(0, this->vertexBuffer, sizeof(RenderVertexInfo));
        }
        else
        {
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetVertexShader(D3DFVF_TEX1 | D3DFVF_DIFFUSE | D3DFVF_XYZ);
        }
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTTFF_COUNT3);
        this->currentVertexShader = 2;
    }

    // ---- issue the draw call ----------------------------------------------
    if ((g_SupervisorCfgOpts_575a9c >> 1 & 1) == 0)
    {
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    }
    else
    {
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, g_DrawPrimUpVerts_4ba078,
                              sizeof(VertexTex1DiffuseXyz));
    }

    return ZUN_SUCCESS;
}

ZunResult AnmManager::DrawFacingCamera(AnmVm *vm)
{
    (void)vm;
    return ZUN_ERROR;
}

// ============================================================================
// SetRenderStateForVm  (FUN_0044eae0)
//
// Verified body, lifted instruction-by-instruction from the disassembly.
// Synchronises the D3D device's render state with the VM's flags. Three
// independent state transitions are guarded by per-AnmManager "current*"
// bytes so each is applied at most once per (this byte, vm flag bit) pair:
//
//   1. DESTBLEND (state 20): driven by vm flag bit 4. value 6 (INVSRCALPHA)
//      when bit clear, 2 (ONE) when set.
//   2. TEXTUREFACTOR (state 60) OR the per-VM colour-slot table (8 dwords at
//      0x4b9fb8..0x4ba0cc): driven by vm->color / vm->unk1bc (selected by
//      dword-flag bit 16) modulated by this+0..3 (the per-frame 0x80808080
//      colour multiplier) when GCOS_DONT_USE_VERTEX_BUF is clear. Vertex-buf
//      path also conditionally multiplies when this+0x4 != 0.
//   3. ZWRITEENABLE (state 14): driven by vm flag bit 12. value 1 when set,
//      0 when clear.
//
// The bit-14 (dword) transition also flushes the pending vertex buffer
// (FUN_0044f5c0) and re-installs either the 3D or 2D camera matrices
// (FUN_00408180 / FUN_004082b0), then re-applies the viewport.
//
// All globals (camera helpers, colour slots, camera stub) are declared at the
// top of this TU; their SYMBOL_MAP entries map the typed names back to the
// orig DAT_/FUN_ addresses for objdiff.
// ============================================================================
#pragma var_order(localColorDword, colorTemp_vbuf_g, colorTemp_vbuf_r, \
                  colorTemp_vbuf_a, colorTemp_vbuf_b, colorTemp_soft_g, \
                  colorTemp_soft_r, colorTemp_soft_a, colorTemp_soft_b, \
                  this_save)
void AnmManager::SetRenderStateForVm(AnmVm *vm)
{
    AnmManager *this_save;
    u32 localColorDword;
    u32 colorTemp_vbuf_g, colorTemp_vbuf_r, colorTemp_vbuf_a, colorTemp_vbuf_b;
    u32 colorTemp_soft_g, colorTemp_soft_r, colorTemp_soft_a, colorTemp_soft_b;

    this_save = this;

    // ---- (1) DESTBLEND: vm flag bit 4 -> D3DRS_DESTBLEND ------------------
    if (this_save->currentBlendMode != (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0x4) & 1))
    {
        this_save->currentBlendMode = (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0x4) & 1);
        if ((((*(u32 *)((u8 *)vm + 0x1c0)) >> 0x4) & 1) == 0)
        {
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        else
        {
            ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                ->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        }
    }

    // ---- (2) TEXTUREFACTOR / colour-slot table ----------------------------
    // ---- (2) TEXTUREFACTOR / colour-slot table ----------------------------
    // Pick the source colour: when the byte at vm+0x1c2 (the high half of
    // the dword read at +0x1c0; bit 16) is non-zero, prefer vm->altColor
    // (+0x1bc) over vm->color (+0x1b8).
    if ((*(u32 *)((u8 *)vm + 0x1c0) >> 0x10) & 1)
    {
        localColorDword = vm->altColor;
    }
    else
    {
        localColorDword = vm->color;
    }

    {
        if ((g_SupervisorCfgOpts_575a9c >> 1 & 1) == 0)
        {
            // Vertex-buffer path: modulate by this+0..3 only when this+0x4
            // (scriptExecCounter_4) != 0, then apply via D3DRS_TEXTUREFACTOR
            // if the cached colour changed.
            if (this_save->scriptExecCounter_4 != 0)
            {
                colorTemp_vbuf_g = ((u32)*((u8 *)&localColorDword + 2)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 2)) >> 7;
                if (colorTemp_vbuf_g >= 0x100) colorTemp_vbuf_g = 0xff;
                *((u8 *)&localColorDword + 2) = (u8)colorTemp_vbuf_g;
                colorTemp_vbuf_r = ((u32)*((u8 *)&localColorDword + 1)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 1)) >> 7;
                if (colorTemp_vbuf_r >= 0x100) colorTemp_vbuf_r = 0xff;
                *((u8 *)&localColorDword + 1) = (u8)colorTemp_vbuf_r;
                colorTemp_vbuf_a = ((u32)*((u8 *)&localColorDword + 0)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 0)) >> 7;
                if (colorTemp_vbuf_a >= 0x100) colorTemp_vbuf_a = 0xff;
                *((u8 *)&localColorDword + 0) = (u8)colorTemp_vbuf_a;
                colorTemp_vbuf_b = ((u32)*((u8 *)&localColorDword + 3)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 3)) >> 7;
                if (colorTemp_vbuf_b >= 0x100) colorTemp_vbuf_b = 0xff;
                *((u8 *)&localColorDword + 3) = (u8)colorTemp_vbuf_b;
            }
            if (this_save->someCounter_2e4c8 != (i32)localColorDword)
            {
                this_save->someCounter_2e4c8 = (i32)localColorDword;
                ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                    ->SetRenderState(D3DRS_TEXTUREFACTOR, this_save->someCounter_2e4c8);
            }
        }
        else
        {
            // Software path: always modulate, then stamp the 8 colour-slot
            // dwords.
            if (this_save->scriptExecCounter_4 != 0)
            {
                colorTemp_soft_g = ((u32)*((u8 *)&localColorDword + 2)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 2)) >> 7;
                if (colorTemp_soft_g >= 0x100) colorTemp_soft_g = 0xff;
                *((u8 *)&localColorDword + 2) = (u8)colorTemp_soft_g;
                colorTemp_soft_r = ((u32)*((u8 *)&localColorDword + 1)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 1)) >> 7;
                if (colorTemp_soft_r >= 0x100) colorTemp_soft_r = 0xff;
                *((u8 *)&localColorDword + 1) = (u8)colorTemp_soft_r;
                colorTemp_soft_a = ((u32)*((u8 *)&localColorDword + 0)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 0)) >> 7;
                if (colorTemp_soft_a >= 0x100) colorTemp_soft_a = 0xff;
                *((u8 *)&localColorDword + 0) = (u8)colorTemp_soft_a;
                colorTemp_soft_b = ((u32)*((u8 *)&localColorDword + 3)) *
                                   ((u32)*((u8 *)&this_save->scriptExecCounter_0 + 3)) >> 7;
                if (colorTemp_soft_b >= 0x100) colorTemp_soft_b = 0xff;
                *((u8 *)&localColorDword + 3) = (u8)colorTemp_soft_b;
            }
            g_AnmMgrColorSlot_4b9fb8 = localColorDword;
            g_AnmMgrColorSlot_4b9fd4 = localColorDword;
            g_AnmMgrColorSlot_4b9ff0 = localColorDword;
            g_AnmMgrColorSlot_4ba00c = localColorDword;
            g_AnmMgrColorSlot_4ba084 = localColorDword;
            g_AnmMgrColorSlot_4ba09c = localColorDword;
            g_AnmMgrColorSlot_4ba0b4 = localColorDword;
            g_AnmMgrColorSlot_4ba0cc = localColorDword;
        }
    }

    // ---- (3) ZWRITEENABLE: vm flag bit 12 -> D3DRS_ZWRITEENABLE -----------
    // Skipped entirely when cfg.opts bit 6 (TURN_OFF_DEPTH_TEST) is set.
    if ((g_SupervisorCfgOpts_575a9c >> 6 & 1) == 0)
    {
        if (this_save->currentZWriteDisable != (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xc) & 1))
        {
            this_save->currentZWriteDisable = (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xc) & 1);
            if ((((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xc) & 1) == 0)
            {
                ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                    ->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            }
            else
            {
                ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
                    ->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            }
        }
    }

    // ---- (4) byte_2e4d4: vm flag bit 14 -> camera + viewport --------------
    if (this_save->byte_2e4d4 != (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xe) & 1))
    {
        AnmManager_FlushVertexBuffer(g_AnmManager);
        this_save->byte_2e4d4 = (u8)(((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xe) & 1);
        if ((((*(u32 *)((u8 *)vm + 0x1c0)) >> 0xe) & 1) == 0)
        {
            Supervisor_Setup3DCamera(&g_SupervisorCameraStub_1347b00);
        }
        else
        {
            Supervisor_Setup2DCamera(&g_SupervisorCameraStub_1347b00);
        }
        ((IDirect3DDevice8 *)g_SupervisorD3dDevice_575958)
            ->SetViewport((D3DVIEWPORT8 *)&g_SupervisorViewport_575a18);
    }

    // Per-call counter bump (header_10 byte at +0x10..+0x14, used by the
    // orig as a per-SetRenderStateForVm-call draw counter).
    *(i32 *)((u8 *)this_save + 0x10) = *(i32 *)((u8 *)this_save + 0x10) + 1;
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
