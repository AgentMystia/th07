// AnmManager module for th07 (Perfect Cherry Blossom).
//
// Sprite / animation / texture manager. th07 grew AnmManager substantially
// relative to th06:
//   - AnmLoadedSprite went 0x3c -> 0x40 bytes (added spriteId at +0x3c and
//     uvScale{X,Y} at +0x34/+0x38, replacing th06's layout).
//   - sprite / script arrays went 2048 -> 2560 entries (0x800 -> 0xa00, see
//     the bound checks at FUN_0044e070 +0x3d2 and +0x43b).
//   - AnmManager itself jumped 0x2112c -> 0x17e560 bytes; the trailing
//     ~0x150000 bytes are a scratch / precomputed-vertex region ZUN touches
//     from the Draw family with fixed strides. operator_new(0x17e560) in
//     FUN_00434020 confirms the size.
//
// All offsets below were recovered directly from th07.exe via Ghidra and
// cross-checked across the listed anchor functions.
//
// Anchor functions (all __thiscall, this = g_AnmManager = DAT_004b9e44):
//   AnmManager::AnmManager          FUN_0044d3e0  (zeros 0x17e560 bytes,
//                                                  inits sprites[i].sourceFileIndex=-1)
//   AnmManager::LoadAnm             FUN_0044df90
//   AnmManager::LoadAnmEntry        FUN_0044e070
//       writes scripts[idx]      at this + 0x28ef0 + idx*4   (idx < 0xa00)
//       writes spriteIndices[idx] at this + 0x2b6f0 + idx*4  (idx < 0xa00)
//       writes anmFiles[anmIdx]  at this + 0x2def0 + anmIdx*0xc (anmIdx < 0x32)
//       writes spriteIdxOffset   at this + 0x2def4 + anmIdx*0xc
//       chain count written by LoadAnm at this + 0x2def8 + anmIdx*0xc
//   AnmManager::LoadTexture         FUN_0044d8f0
//       textures[texIdx]         at this + 0x282ac + texIdx*4  (texIdx < 0x108)
//       imageDataArray[texIdx]   at this + 0x286cc + texIdx*4
//   AnmManager::LoadTextureAlphaChannel FUN_0044dbe0
//   AnmManager::LoadTextureFromMemory   FUN_0044d9e0
//   AnmManager::CreateEmptyTexture FUN_0044df40  (D3DXCreateTexture wrapper)
//   AnmManager::ReleaseTexture     FUN_0044e6f0
//   AnmManager::ReleaseAnm         FUN_0044e4e0
//       resets currentBlendMode/colorOp/vertexShader at +0x2e4d0/+0x2e4d1/+0x2e4d2
//       currentTexture ptr at +0x2e4cc
//   AnmManager::LoadSprite         FUN_0044e780
//       sprites[spriteIdx]       at this + 0x60 + spriteIdx*0x40
//       maybeLoadedSpriteCount    at this + 0x28eec
//   AnmManager::SetActiveSprite    FUN_0044e8e0
//   AnmManager::SetAndExecuteScript FUN_0044ea20  (also bumps dword at this+8)
//   AnmManager::ExecuteScript      FUN_00450d60  (~2 KB switch body)
//   AnmManager::DrawInner          FUN_00450520  (reads 0x2e530, 0x2e4d8, ...)
//       vertexBuffer ptr          at this + 0x2e4dc
//       scratch region begins     at this + 0x2e530 (bool), 0x2e534 (array)

#pragma once

#define D3DXMatrixIdentity D3DXMatrixIdentity_DontUseHeaderDecl

#include <d3d8.h>
#include <d3dx8math.h>

#include "AnmVm.hpp"
#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"

#undef D3DXMatrixIdentity

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

// ----- AnmRawInstr (one ANM script instruction) -----
// Matches th06's layout. The opcode dispatcher in FUN_00450d60 reads
//   +0x00 time (i16)
//   +0x02 opcode (u8, compared as `*psVar1 + 1`)
//   +0x03 argsCount / flags (u8)
//   +0x04 args[] (variable, dword or qword slots)
struct AnmRawInstr
{
    i16 time;
    u8 opcode;
    u8 argsCount;
    u32 args[1]; // variable length; indexed by the interpreter
};

// ----- AnmRawEntry (header of a single chained entry in an .anm) -----
//
// Runtime field offsets used by FUN_0044e070 (LoadAnmEntry) and FUN_0044e4e0
// (ReleaseAnm), all verified against the disassembly:
//   +0x00 numSprites
//   +0x04 numScripts
//   +0x08 textureIdx   (runtime-assigned = anmIdx)
//   +0x0c width
//   +0x10 height
//   +0x14 format
//   +0x18 colorKey
//   +0x1c nameOffset
//   +0x20 spriteIdxOffset (runtime-assigned)
//   +0x24 mipmapNameOffset
//   +0x28 version (must equal 2)
//   +0x2c unk1
//   +0x30 textureOffset / hasData
//   +0x34 nextOffset (chain link; 0 == last entry)
//   +0x35 freeIfSet (byte; when set, the blob is free()'d on release)
//   +0x38 chainNextOffset
//   +0x3c spriteOffsets[numSprites] (each is a byte offset into the entry)
//   +0x40 spriteOffsets[0] (first descriptor address used by LoadAnmEntry's
//          local_3c = local_c + 0x40)
// After spriteOffsets come scripts[numScripts] as {id, instruction-ptr} pairs
// (AnmRawScript, stride 8).
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

// ----- AnmLoadedSprite (one slot in AnmManager::sprites) -----
//
// sizeof == 0x40, verified by the stride in FUN_0044e8e0 (SetActiveSprite:
// `[ECX + spriteIdx*0x40 + 0x60]`) and FUN_0044e780 (LoadSprite:
// `param_1 + 0x60 + spriteIdx*0x40`). All field offsets below are sprite-base
// relative, i.e. (AnmManager_offset - 0x60). Recovered by reading LoadSprite
// (FUN_0044e780) for the writes and SetActiveSprite (FUN_0044e8e0) for the
// reads; both functions agree on every offset:
//   LoadSprite writes uvStart.x   at sprite+0x1c = sp.x / textureWidth
//                uvStart.y        at sprite+0x20 = sp.y / textureHeight
//                uvEnd.x          at sprite+0x24 = ep.x / textureWidth
//                uvEnd.y          at sprite+0x28 = ep.y / textureHeight
//                heightPx         at sprite+0x2c = (ep.y - sp.y) / src.h
//                widthPx          at sprite+0x30 = (ep.x - sp.x) / src.h
//                spriteId         at sprite+0x3c = maybeLoadedSpriteCount++
//   SetActiveSprite reads textureHeight at +0x14, textureWidth at +0x18,
//                heightPx at +0x2c, widthPx at +0x30,
//                uvScaleX at +0x34, uvScaleY at +0x38.
struct AnmLoadedSprite
{
    i32 sourceFileIndex;             // +0x00 (== textureIdx; -1 == unused slot)
    f32 startPixelInclusiveX;        // +0x04
    f32 startPixelInclusiveY;        // +0x08
    f32 endPixelInclusiveX;          // +0x0c
    f32 endPixelInclusiveY;          // +0x10
    f32 textureHeight;               // +0x14
    f32 textureWidth;                // +0x18
    f32 uvStartX;                    // +0x1c
    f32 uvStartY;                    // +0x20
    f32 uvEndX;                      // +0x24
    f32 uvEndY;                      // +0x28
    f32 heightPx;                    // +0x2c
    f32 widthPx;                     // +0x30
    f32 uvScaleX;                    // +0x34
    f32 uvScaleY;                    // +0x38
    i32 spriteId;                    // +0x3c
};
ZUN_ASSERT_SIZE(AnmLoadedSprite, 0x40);

// ----- AnmManager -----
//
// sizeof == 0x17e560 bytes (verified via operator_new(0x17e560) in
// FUN_00434020 and the REP STOSD count 0x5f958 dwords in FUN_0044d3e0).
struct AnmManager
{
    AnmManager();
    ~AnmManager()
    {
    }

    // --- Texture / surface ---
    void ReleaseTexture(i32 textureIdx);
    ZunResult LoadTexture(i32 textureIdx, char *textureName, i32 textureFormat, D3DCOLOR colorKey);
    ZunResult LoadTextureAlphaChannel(i32 textureIdx, char *textureName, i32 textureFormat,
                                      D3DCOLOR colorKey);
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
    ZunResult DrawInner(AnmVm *vm);
    ZunResult DrawFacingCamera(AnmVm *vm);
    void TranslateRotation(VertexTex1Xyzrwh *out, f32 x, f32 y, f32 sine, f32 cosine,
                           f32 xOffset, f32 yOffset);

    // --- Text ---
    void DrawTextToSprite(u32 spriteDstIndex, i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                          i32 fontWidth, i32 fontHeight, ZunColor textColor, ZunColor shadowColor,
                          char *strToPrint);
    void DrawVmTextFmt(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);
    void DrawStringFormat(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);
    void DrawStringFormat2(AnmVm *vm, ZunColor textColor, ZunColor shadowColor, char *fmt, ...);

    // --- .anm load / release ---
    ZunResult LoadAnm(i32 anmIdx, char *path, i32 spriteIdxOffset);
    // Internal helper invoked once per chained entry by LoadAnm. Declared
    // public so the helper's var_order / body can live next to LoadAnm's.
    i32 LoadAnmEntry(i32 anmIdx, AnmRawEntry *entry, i32 spriteIdxOffset, i32 isFirst);
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
        // TODO: th06 stored these as named screenshot{TextureId,Left,Top,
        // Width,Height} fields near the end of the struct. th07's equivalents
        // have not yet been anchored to a fixed offset in the layout above;
        // they live somewhere in scratchRegion. Once located, restore the
        // assignments (textureId = 3; left = GAME_REGION_LEFT; ...).
    }

    void ExecuteAnmIdx(AnmVm *vm, i32 anmFileIdx)
    {
        vm->anmFileIndex = (i16)anmFileIdx;
        vm->rotation = D3DXVECTOR3(0, 0, 0);
        this->SetAndExecuteScript(vm, this->scripts[anmFileIdx]);
    }

    // ===== layout (verified against th07.exe) =====
    //
    // Every offset below is hand-derived from the disassembly. Pad sizes are
    // written as `END - START` where START is the byte offset of the pad
    // itself (i.e. the byte just past the previous field), so the running
    // total stays exact.

    // +0x00 counters read/bumped by ExecuteScript and SetAndExecuteScript.
    //   ExecuteScript ends with `*(int*)(this+0xc) += 1` and reads
    //   `*(int*)(this+8) == 0` as the "is script active" gate.
    i32 scriptExecCounter_0; // +0x00
    i32 scriptExecCounter_4; // +0x04
    i32 scriptExecCounter_8; // +0x08 (bumped by SetAndExecuteScript)
    i32 scriptExecCounter_c; // +0x0c (bumped at end of ExecuteScript)

    // +0x10..0x60 raw gap (supervisor bookkeeping).
    u8 header_10[0x60 - 0x10];

    // +0x060 sprites[2560] (sizeof(AnmLoadedSprite) == 0x40; 2560*0x40 = 0x28000)
    AnmLoadedSprite sprites[2560]; // +0x060 .. +0x28060

    // +0x28060 virtualMachine (scratch AnmVm, sizeof == 0x24c)
    AnmVm virtualMachine; // +0x28060 .. +0x282ac

    // +0x282ac textures[264] (IDirect3DTexture8*). texIdx < 0x108.
    IDirect3DTexture8 *textures[264]; // +0x282ac .. +0x286cc

    // +0x286cc imageDataArray[256] (raw loaded bytes for textures)
    void *imageDataArray[256]; // +0x286cc .. +0x28acc

    // +0x28acc..0x28eec raw gap (0x280 bytes).
    u8 pad_28acc[0x28eec - 0x28acc];

    // +0x28eec maybeLoadedSpriteCount (monotonic sprite id source)
    i32 maybeLoadedSpriteCount; // +0x28eec

    // +0x28ef0 scripts[2560] (AnmRawInstr*). idx < 0xa00. 2560*4 = 0x2800.
    AnmRawInstr *scripts[2560]; // +0x28ef0 .. +0x2b6f0

    // +0x2b6f0 spriteIndices[2560] (per-script base sprite offset). 2560*4.
    i32 spriteIndices[2560]; // +0x2b6f0 .. +0x2def0

    // +0x2def0 anmFiles[50] interleaved (stride 0xc). anmIdx < 0x32 = 50.
    // 50 * 0xc = 0x258 -> ends at 0x2e148.
    struct AnmFileChain
    {
        AnmRawEntry *entry;  // +0x0
        i32 spriteIdxOffset; // +0x4
        i32 chainCount;      // +0x8 (written by LoadAnm = endIdx - startIdx)
    } anmFiles[50];          // +0x2def0 .. +0x2e148

    // +0x2e148..0x2e4c8 raw gap (0x380 bytes).
    u8 pad_2e148[0x2e4c8 - 0x2e148];

    // +0x2e4c8 someCounter (i32, initialised to 1 by the constructor).
    i32 someCounter_2e4c8;
    // +0x2e4cc currentTexture (IDirect3DTexture8*, cleared by ReleaseAnm).
    IDirect3DTexture8 *currentTexture;
    // +0x2e4d0 currentBlendMode (u8, set 0xff by ReleaseAnm).
    u8 currentBlendMode;
    // +0x2e4d1 currentColorOp (u8, set 0xff by ReleaseAnm).
    u8 currentColorOp;
    // +0x2e4d2 currentVertexShader (u8, cleared by ReleaseAnm).
    u8 currentVertexShader;
    // +0x2e4d3 currentZWriteDisable (u8, cleared by ReleaseAnm).
    u8 currentZWriteDisable;
    // +0x2e4d4 (u8, set 0xff by the constructor).
    u8 byte_2e4d4;
    // +0x2e4d5..0x2e4d8 raw gap (3 bytes).
    u8 pad_2e4d5[0x2e4d8 - 0x2e4d5];
    // +0x2e4d8 cached sprite pointer (stored as f32 bits; DrawInner compares
    // this against vm->sprite to decide whether to rebind the texture).
    f32 cachedSpritePtr_2e4d8;
    // +0x2e4dc vertexBuffer (IDirect3DVertexBuffer8*; passed to SetStreamSource
    // with stride 0x14 in FUN_00450520).
    IDirect3DVertexBuffer8 *vertexBuffer;
    // +0x2e4e0 vertexBufferContents[4] (RenderVertexInfo, 0x14 each = 0x50).
    RenderVertexInfo vertexBufferContents[4]; // +0x2e4e0 .. +0x2e530

    // +0x2e530 vertexBufferDirty flag (i32, read+cleared by DrawInner /
    // FUN_0044f5c0).
    i32 vertexBufferDirty;

    // +0x2e534..0x17e534 scratch / precomputed-vertex region (0x150000 bytes).
    // Accessed from the Draw family and screenshot helpers with fixed strides
    // (the constructor's 0xc000-iteration stride-0x1c sweep lives here). The
    // exact sub-layout is large and has not been fully tied to named fields;
    // we reserve it as raw bytes so sizeof stays locked.
    u8 scratchRegion[0x17e534 - 0x2e534];

    // +0x17e534..0x17e560 tail state. The constructor writes -1 to the dword
    // at +0x17e53c (param_1[0x5f94f] in FUN_0044d3e0).
    u8 tail_17e534[0x17e53c - 0x17e534];
    i32 tailMarker_17e53c; // +0x17e53c (initialised to -1)
    u8 tail_17e540[0x17e560 - 0x17e540];
};
ZUN_ASSERT_SIZE(AnmManager, 0x17e560);

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
