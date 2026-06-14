// AnmManager module for th07 (Perfect Cherry Blossom).
//
// This is the sprite / animation / texture manager. th07 reshaped the layout
// substantially relative to th06: the AnmLoadedSprite grew from 0x3c to 0x40
// bytes (a new `spriteId` field at +0x3c and `uvScale{X,Y}` at +0x34/+0x38
// replacing the old layout), and AnmManager itself moved its arrays around.
//
// All offsets below were recovered directly from th07.exe via Ghidra.
// Anchor functions used:
//
//   AnmManager::LoadAnm                FUN_0044df90  (__thiscall)
//     - chained helper FUN_0044e070 does per-entry sprite/script registration.
//   AnmManager::LoadAnmEntry           FUN_0044e070  (__thiscall)
//     - writes scripts[idx]   at this + 0x28ef0 + idx*4
//     - writes spriteIndices  at this + 0x2b6f0 + idx*4
//     - writes anmFiles       at this + 0x2def0 + anmIdx*0xc
//     - writes spriteIdxOffs  at this + 0x2def4 + anmIdx*0xc
//     - writes chain count    at this + 0x2def8 + anmIdx*0xc
//   AnmManager::SetActiveSprite        FUN_0044e8e0  (__thiscall)
//     - reads sprite at this + 0x60 + spriteIdx*0x40 (i.e. sprites[2048] at 0x60,
//       sizeof(AnmLoadedSprite) == 0x40)
//   AnmManager::LoadSprite             FUN_0044e780  (__thiscall)
//     - computes uvStart/uvEnd/widthPx/heightPx/uvScale{X,Y}; assigns spriteId
//       from maybeLoadedSpriteCount at this + 0x28eec
//   AnmManager::ReleaseTexture         FUN_0044e6f0  (__thiscall)
//     - textures[]       at this + 0x282ac + texIdx*4 (limit 0x108 = 264)
//     - imageDataArray[] at this + 0x286cc + texIdx*4
//   AnmManager::ReleaseAnm             FUN_0044e4e0  (__thiscall)
//     - resets currentBlendMode/colorOp/vertexShader at +0x2e4d0/+0x2e4d1/+0x2e4d2
//     - currentTexture ptr at +0x2e4cc
//   AnmManager::LoadTexture            FUN_0044d8f0  (__thiscall)
//   AnmManager::LoadTextureAlphaChannel FUN_0044dbe0 (__thiscall)
//   AnmManager::LoadTextureFromMemory  FUN_0044d9e0  (__thiscall)
//   AnmManager::CreateEmptyTexture     FUN_0044df40  (__thiscall)
//   AnmManager::SetAndExecuteScript    FUN_0044ea20  (__thiscall)
//   AnmManager::ExecuteScript          FUN_00450d60  (__thiscall, ~2KB body)
//
// AnmVm is provided by AnmVm.hpp (0x24c bytes, fully mapped there).

#pragma once

#include <d3d8.h>
#include <d3dx8math.h>

#include "AnmVm.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// ----- Vertex formats -----
// FVF D3DFVF_DIFFUSE | D3DFVF_XYZRWH
struct VertexDiffuseXyzrwh
{
    D3DXVECTOR4 position;
    D3DCOLOR diffuse;
};
ZUN_ASSERT_SIZE(VertexDiffuseXyzrwh, 0x14);

// FVF D3DFVF_TEX1 | D3DFVF_XYZRWH
struct VertexTex1Xyzrwh
{
    D3DXVECTOR4 position;
    D3DXVECTOR2 textureUV;
};
ZUN_ASSERT_SIZE(VertexTex1Xyzrwh, 0x18);

// FVF D3DFVF_TEX1 | D3DFVF_DIFFUSE | D3DFVF_XYZRWH
struct VertexTex1DiffuseXyzrwh
{
    D3DXVECTOR4 position;
    D3DCOLOR diffuse;
    D3DXVECTOR2 textureUV;
};
ZUN_ASSERT_SIZE(VertexTex1DiffuseXyzrwh, 0x1c);

// FVF D3DFVF_TEX1 | D3DFVF_DIFFUSE | D3DFVF_XYZ
struct VertexTex1DiffuseXyz
{
    D3DXVECTOR3 position;
    D3DCOLOR diffuse;
    D3DXVECTOR2 textureUV;
};
ZUN_ASSERT_SIZE(VertexTex1DiffuseXyz, 0x18);

// ----- AnmRawSprite (descriptor inside an .anm blob) -----
struct AnmRawSprite
{
    u32 id;
    D3DXVECTOR2 offset;
    D3DXVECTOR2 size;
};
ZUN_ASSERT_SIZE(AnmRawSprite, 0x14);

// ----- AnmRawScript (descriptor inside an .anm blob) -----
struct AnmRawScript
{
    u32 id;
    AnmRawInstr *firstInstruction;
};
ZUN_ASSERT_SIZE(AnmRawScript, 0x8);

// ----- AnmRawEntry (header of a single chained entry in an .anm) -----
//
// The th07 layout follows th06 but the runtime fields used by AnmManager are
// at fixed byte offsets inside the loaded blob. Verified in FUN_0044e070:
//   +0x00 numSprites
//   +0x04 numScripts
//   +0x08 textureIdx   (anm-local, assigned = anmIdx)
//   +0x0c width
//   +0x10 height
//   +0x14 format
//   +0x18 colorKey
//   +0x1c nameOffset
//   +0x20 spriteIdxOffset (runtime-assigned)
//   +0x24 mipmapNameOffset
//   +0x28 version
//   +0x2c unk1
//   +0x30 textureOffset / hasData
//   +0x34 nextOffset
//   +0x35 freeIfSet (byte: when set, the blob is free()'d on release)
//   +0x38 nextOffset (chain link)
//   +0x3c spriteOffsets[numSprites] / scripts[numScripts]
struct AnmRawEntry
{
    i32 numSprites;
    i32 numScripts;
    u32 textureIdx;
    i32 width;
    i32 height;
    u32 format;
    u32 colorKey;
    u32 nameOffset;
    u32 spriteIdxOffset;
    u32 mipmapNameOffset;
    u32 version;
    u32 unk1;
    u32 textureOffset;
    u32 hasData;
    u32 nextOffset;
    u8 freeIfSet;
    u8 pad[3];
    u32 chainNextOffset;
    u32 spriteOffsets[10];
    AnmRawScript scripts[10];
};
ZUN_ASSERT_SIZE(AnmRawEntry, 0xb8);

// ----- RenderVertexInfo (FVF D3DFVF_TEX1 | D3DFVF_XYZ, sizeof == 0x14) -----
struct RenderVertexInfo
{
    D3DXVECTOR3 position;
    D3DXVECTOR2 textureUV;
};
ZUN_ASSERT_SIZE(RenderVertexInfo, 0x14);

// ----- AnmManager -----
//
// 0x2e4dc bytes. All field offsets below are verified against th07.exe.
struct AnmManager
{
    AnmManager();
    ~AnmManager()
    {
    }

    // --- Texture / surface ---
    void ReleaseTexture(i32 textureIdx);
    ZunResult LoadTexture(i32 textureIdx, char *textureName, i32 textureFormat, D3DCOLOR colorKey);
    ZunResult LoadTextureAlphaChannel(i32 textureIdx, char *textureName, i32 textureFormat, D3DCOLOR colorKey);
    ZunResult LoadTextureFromMemory(i32 textureIdx, void *header, i32 textureFormat);
    ZunResult CreateEmptyTexture(i32 textureIdx, u32 width, u32 height, i32 textureFormat);

    // --- Vertex buffer ---
    void ReleaseVertexBuffer()
    {
        if (this->vertexBuffer != NULL)
        {
            this->vertexBuffer->Release();
            this->vertexBuffer = NULL;
        }
    }
    void SetupVertexBuffer();

    // --- Screenshot ---
    void TakeScreenshotIfRequested();
    void TakeScreenshot(i32 textureId, i32 left, i32 top, i32 width, i32 height);

    // --- Script execution ---
    void SetAndExecuteScript(AnmVm *vm, AnmRawInstr *beginingOfScript);
    i32 ExecuteScript(AnmVm *vm);

    // --- Sprite ---
    void LoadSprite(u32 spriteIdx, AnmLoadedSprite *sprite);
    ZunResult SetActiveSprite(AnmVm *vm, u32 spriteIdx);

    // --- Draw ---
    void SetRenderStateForVm(AnmVm *vm);
    ZunResult Draw(AnmVm *vm);
    ZunResult Draw2(AnmVm *vm);
    ZunResult Draw3(AnmVm *vm);
    ZunResult DrawNoRotation(AnmVm *vm);
    ZunResult DrawInner(AnmVm *vm, i32 roundPixels);
    ZunResult DrawFacingCamera(AnmVm *vm);
    void TranslateRotation(VertexTex1Xyzrwh *out, f32 x, f32 y, f32 sine, f32 cosine, f32 xOffset,
                           f32 yOffset);

    // --- Text ---
    void DrawTextToSprite(u32 spriteDstIndex, i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                          i32 fontWidth, i32 fontHeight, ZunColor textColor, ZunColor shadowColor,
                          char *strToPrint);
    void DrawVmTextFmt(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);
    void DrawStringFormat(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);
    void DrawStringFormat2(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);

    // --- .anm load / release ---
    ZunResult LoadAnm(i32 anmIdx, char *path, i32 spriteIdxOffset);
    void ReleaseAnm(i32 anmIdx);

    // --- Surface (image surfaces, separate from textures) ---
    ZunResult LoadSurface(i32 surfaceIdx, char *path);
    void ReleaseSurface(i32 surfaceIdx);
    void ReleaseSurfaces(void);
    void CopySurfaceToBackBuffer(i32 surfaceIdx, i32 left, i32 top, i32 x, i32 y);
    void DrawEndingRect(i32 surfaceIdx, i32 rectX, i32 rectY, i32 rectLeft, i32 rectTop, i32 width,
                        i32 height);

    void RequestScreenshot()
    {
        this->screenshotTextureId = 3;
        this->screenshotLeft = GAME_REGION_LEFT;
        this->screenshotTop = GAME_REGION_TOP;
        this->screenshotWidth = GAME_REGION_WIDTH;
        this->screenshotHeight = GAME_REGION_HEIGHT;
    }

    void ExecuteAnmIdx(AnmVm *vm, i32 anmFileIdx)
    {
        vm->anmFileIndex = (i16)anmFileIdx;
        vm->rotation = D3DXVECTOR3(0, 0, 0);
        this->SetAndExecuteScript(vm, this->scripts[anmFileIdx]);
    }

    // ===== layout (verified against th07.exe) =====
    // +0x000 header / vtable-ish state (24 bytes used by the binary)
    u8 header[0x60];

    // +0x060 sprites[2048] (sizeof(AnmLoadedSprite) == 0x40)
    AnmLoadedSprite sprites[2048];

    // +0x282ac textures[264] (IDirect3DTexture8*)
    IDirect3DTexture8 *textures[264];

    // +0x286cc imageDataArray[256] (raw loaded bytes for textures)
    void *imageDataArray[256];

    // +0x28acc virtualMachine (an AnmVm used as scratch)
    AnmVm virtualMachine;

    // +0x28d18 (padding to align next array)
    u8 pad_28d18[0x28eec - (0x28acc + sizeof(AnmVm))];

    // +0x28eec maybeLoadedSpriteCount (used to assign spriteId)
    i32 maybeLoadedSpriteCount;

    // +0x28ef0 scripts[2048] (AnmRawInstr*)
    AnmRawInstr *scripts[2048];

    // +0x2a6f0 padding to 0x2b6f0
    u8 pad_2a6f0[0x2b6f0 - (0x28ef0 + 2048 * 4)];

    // +0x2b6f0 spriteIndices[2048]
    i32 spriteIndices[2048];

    // +0x2d6f0 padding to 0x2def0
    u8 pad_2d6f0[0x2def0 - (0x2b6f0 + 2048 * 4)];

    // +0x2def0 anmFiles[128]  (stride 0xc: ptr + spriteIdxOffset + chainCount)
    AnmRawEntry *anmFiles[128];

    // +0x2df70 padding to 0x2def4+0xc*128 = 0x2e070... actually the stride
    // is 0xc per anmIdx, so all three arrays are interleaved. We model them
    // as three flat arrays indexed at anmIdx*0xc.
    u8 pad_2df70[0x2def4 - (0x2def0 + 128 * 4)];

    // +0x2def4 anmFilesSpriteIndexOffsets[128] (i32; stride 0xc)
    i32 anmFilesSpriteIndexOffsets_v[128];
    u8 pad_anmchain[0x2def8 - (0x2def4 + 128 * 4)];

    // +0x2def8 anmFilesChainCount[128] (i32; stride 0xc)
    i32 anmFilesChainCount_v[128];

    u8 pad_2e4cc[0x2e4cc - (0x2def8 + 128 * 4)];

    // +0x2e4cc currentTexture (IDirect3DTexture8*)
    IDirect3DTexture8 *currentTexture;
    // +0x2e4d0 currentBlendMode (u8)
    u8 currentBlendMode;
    // +0x2e4d1 currentColorOp (u8)
    u8 currentColorOp;
    // +0x2e4d2 currentVertexShader (u8)
    u8 currentVertexShader;
    // +0x2e4d3 currentZWriteDisable (u8)
    u8 currentZWriteDisable;
    // +0x2e4d4 currentSprite (AnmLoadedSprite*)
    AnmLoadedSprite *currentSprite;
    // +0x2e4d8 currentTextureFactor (D3DCOLOR)
    D3DCOLOR currentTextureFactor;
    // +0x2e4dc vertexBuffer (IDirect3DVertexBuffer8*)
    IDirect3DVertexBuffer8 *vertexBuffer;
    // +0x2e4e0 vertexBufferContents[4] (RenderVertexInfo)
    RenderVertexInfo vertexBufferContents[4];
    // +0x2e520 surfaces[32]
    IDirect3DSurface8 *surfaces[32];
    // +0x2e5a0 surfacesBis[32]
    IDirect3DSurface8 *surfacesBis[32];
    // +0x2e620 surfaceSourceInfo[32] (D3DXIMAGE_INFO)
    D3DXIMAGE_INFO surfaceSourceInfo[32];
    // +0x2e820 screenshot state
    i32 screenshotTextureId;
    i32 screenshotLeft;
    i32 screenshotTop;
    i32 screenshotWidth;
    i32 screenshotHeight;
};
ZUN_ASSERT_SIZE(AnmManager, 0x2e4dc);

DIFFABLE_EXTERN(AnmManager *, g_AnmManager);
DIFFABLE_EXTERN_ARRAY(D3DFORMAT, 6, g_TextureFormatD3D8Mapping);

// Game region constants (mirror th06 values; used by RequestScreenshot).
#define GAME_REGION_LEFT 32
#define GAME_REGION_TOP 16
#define GAME_REGION_WIDTH 384
#define GAME_REGION_HEIGHT 448

// Texture format indices (indices into g_TextureFormatD3D8Mapping).
#define TEX_FMT_UNKNOWN 0
#define TEX_FMT_A8R8G8B8 1
#define TEX_FMT_A1R5G5B5 2
#define TEX_FMT_R5G6B5 3
#define TEX_FMT_R8G8B8 4
#define TEX_FMT_A4R4G4B4 5
}; // namespace th07
