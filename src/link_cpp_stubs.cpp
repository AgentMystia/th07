// Auto-generated link stubs (Phase C, P0 link pass) -- free-function and
// data symbols referenced from other modules but not yet defined. Each
// function stub returns ZUN_ERROR / 0 / no-op; each data symbol is zero-
// initialised. Class methods (`public: ... A::f(...)`) are NOT handled here --
// those need to be defined in the .cpp that owns the struct declaration,
// because the C++ ABI distinguishes per-translation-unit struct definitions
// (each TU's `struct AnmMgrStub` is a separate type for mangling purposes).
//
// Normal-build only; does NOT contribute to objdiff.

#include "AnmVm.hpp"
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "inttypes.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"

#include <d3dx8math.h>

namespace th07
{
// ---- forward-declared opaque struct types referenced by some signatures ----
struct ArchiveEntryTable;
struct ScoreSub;

// ---- primitive data symbols (zero-init) ----
// Note: g_CurrentStage is referenced as a GLOBAL-namespace symbol
// (`?g_CurrentStage@@3HA`, no th07::) from ReplayManager.cpp. Define it in
// the global namespace, not th07.
int g_EffectAnmBaseIdx = 0;
int g_EffectAnmFileCount = 0;
unsigned char g_StartedFromDifferentAppDir = 0;
float g_Supervisor_effectiveFramerateMultiplier = 1.0f;
void *g_Pbg4Archive = 0;

// ---- struct-typed data symbols ----
// ChainElem globals (AsciiManager uses 3 chains; defined here until
// AsciiManager.cpp lands the real RegisterChain).
ChainElem g_AsciiCalcChain;
ChainElem g_AsciiDrawMenusChain;
ChainElem g_AsciiDrawPopupsChain;
// g_GameErrorContext is defined in GameErrorContext.cpp already.
// Singleton handles declared in Player.cpp (EffectManagerSpawn /
// SoundPlayerPlayback / GameManagerScore / AnmManagerFiles / ScoreSub) are
// defined in Player.cpp itself, since their struct types are local to that TU.
// g_BombDataTable likewise lives in Player.cpp.

// ---- free-function stubs returning ZUN_ERROR / 0 / void ----

ZunResult __fastcall Anm_LoadAnm(int a, char const *b, int c)
{
    (void)a; (void)b; (void)c; return ZUN_ERROR;
}
int __fastcall CalculateChecksum_0042d7be(struct GameManager *) { return 0; }
int __fastcall CheckSomething_004429d0(void *) { return 0; }
int __fastcall FileExists(char const *) { return 0; }
int __fastcall InitBoss_0041d0a0(void) { return 0; }
int __fastcall InitEffect_004276a0(void *) { return 0; }
int __fastcall InitEnemy_00422f40(int, int) { return 0; }
int __fastcall InitItemMgr_004074c0(int) { return 0; }
int __fastcall InitStageGame_0042d136(void) { return 0; }
int __fastcall IsGameActive_0042ad66(void *) { return 0; }
int __fastcall LoadStageData_0040e420(void *, void *) { return 0; }
int __fastcall ProcessSoundQueue_0044c9c0(void *) { return 0; }
int __fastcall RandFloatToInt_0048b8a0(float) { return 0; }
int __fastcall ResolveShortcut(char const *, char *, int) { return 0; }
int __fastcall StageInitCheck_0042e634(void) { return 0; }
int __fastcall Supervisor_SomePulseFlag(void) { return 0; }
unsigned int __fastcall Rng_GetRandomU32_004318d0(void *) { return 0; }
unsigned int __stdcall timeGetTime(void) { return 0; }
void *__cdecl Malloc_0047d39d(unsigned int) { return 0; }
void *__cdecl OperatorNew_0047d441(unsigned int) { return 0; }
void __cdecl AnmMgr_ExecuteScript_450d60(struct AnmMgrStub *, void *) { }
void __cdecl DebugPrint_00437903(char const *, ...) { }
void __cdecl ErrorLog_004315f0(void *, void *) { }
void __cdecl Free2_0047d285(void *) { }
void __cdecl Free_0047d43c(void *) { }
void __fastcall AnmColorSetup_0042d657(struct GameManager *) { }
void __fastcall AnmMgrTeardown_442b10(void) { }
void __fastcall AnmVm_Initialize_4010f0(void *) { }
void __fastcall AnmVm_Initialize_401170(void *) { }
void __fastcall AnmVm_ResetInterpTimers_4011b0(void *) { }
void __fastcall Anm_ReleaseAnm(int) { }
void __fastcall AsciiMgr_ExecuteLabelVms_401400(struct AsciiManager *) { }
void __fastcall BgmFadeBook_443d30(void) { }
void __fastcall DispParamsDtor_40e4f0(void *) { }
void __fastcall DrawFpsCounter_004390a5(void *) { }
void __fastcall FadeOutMusic_0043a0d6(void *, float) { }
void __fastcall FirstInit_0042e1f8(void) { }
void __fastcall FullPowerSetup_00443aa0(int, void *) { }
void __fastcall PlayAudio_00439dd0(void *, int, void *) { }
void __fastcall ReleaseBosses_41d150(void) { }
void __fastcall ReleaseEnemies_423050(void) { }
void __fastcall ScoreSubInit_0042e3da(void) { }
void __fastcall StopAudio_00439ec1(void *, int) { }
void __fastcall SupOnDelete_0043a05f(void *) { }
void __fastcall SupUpdateTimeAccumA_43a27f(void *) { }
void __fastcall SupUpdateTimeAccumB_0043a3f4(void *) { }
// Additional cross-module free functions (referenced from AsciiManager /
// Supervisor / GameManager / SoundPlayer / Stage).
void __fastcall EffectItemTeardown_427760(void) { }
void __fastcall EffectMgrReset_401a00(void *) { }
void __fastcall GrantExtend_0042e81b(void *, int) { }
void __fastcall MidiCloseAll_436b30(void *) { }
void __fastcall MidiPlay_436ad0(void *) { }
void __fastcall PauseSound_0044d2f0(void *, int, int, char const *) { }
void __fastcall ReleaseChainCtrl_42d53d(void) { }
void __fastcall ReleaseItems_004075d0(void) { }
void __fastcall RngAdvance_004012b0(void *) { }
void __fastcall SoundCmd_0044b310(int, int, int, int, int) { }
void __fastcall SoundCmd_0044c930(void *, int, int) { }
void __fastcall StageColor_0042e38c(struct GameManager *) { }
void __fastcall StageMenu_OnDrawGameMenu_403a20(void *) { }
void __fastcall StageMenu_OnDrawRetryMenu_404560(void *) { }
void __fastcall StageMenu_OnUpdateGameMenu_402780(void *) { }
void __fastcall StageMenu_OnUpdateRetryMenu_403b60(void *) { }
void __fastcall Supervisor_TickTimer(int *, float *) { }
void __fastcall Supervisor_TickTimer_43958d(struct Supervisor *, int *, float *) { }

// AnmManager::ExecuteScript / Draw3 / Draw3NoOffset / SetAndExecuteScriptIdx
// are referenced as free __fastcall methods (ECX = this). They are NOT class
// methods here -- the orig invokes them as `AnmManager::Method(vm)` where
// AnmManager* is the implicit `this` and the first pushed arg is the AnmVm*.
// We declare them as AnmManager-member free functions matching that form.
struct AnmManagerStub;
i32  __fastcall AnmManager_ExecuteScript(struct AnmManagerStub *, void *) { return 0; }
void __fastcall AnmManager_Draw3(struct AnmManagerStub *, void *) { }
void __fastcall AnmManager_Draw3NoOffset(struct AnmManagerStub *, void *) { }
void __fastcall AnmManager_SetAndExecuteScriptIdx(struct AnmManagerStub *, void *, int) { }

// MidiDevStub / RngStub: class methods referenced via local stub structs in
// MidiOutput.cpp / Supervisor.cpp. Declared here as AnmManager-style free
// stubs to emit the matching mangled name.
struct MidiDevStub;
struct RngStub;
void __fastcall MidiDevStub_Open_436790(struct MidiDevStub *, unsigned int) { }
void __fastcall RngStub_Consume_00401390(struct RngStub *, int) { }
} // namespace th07

// Global-namespace `struct GameErrorContext g_GameErrorContext` (no th07::
// prefix) -- GameManager.cpp uses an old C-style forward declaration. MSVC
// encodes the struct name in the symbol, so it must be named `GameErrorContext`
// (in the global namespace, distinct from th07::GameErrorContext).
struct GameErrorContext { u8 pad[0x18]; };
GameErrorContext g_GameErrorContext;

// Global-namespace `int __cdecl GetArcadeRegionCoordinate_48b8a0(void)`.
// Referenced from AsciiManager.cpp as a global C++ function (mangled
// `?...@@YAHXZ`), so defined here outside namespace th07.
int __cdecl GetArcadeRegionCoordinate_48b8a0(void) { return 0; }

// Global-namespace `int g_CurrentStage` -- ReplayManager.cpp references it
// via a plain `extern int g_CurrentStage;` (no th07::).
int g_CurrentStage = 0;

// Global-namespace `void * g_BombDataTable[24]` -- Player.cpp references it
// via a plain `extern void *g_BombDataTable[6 * 4];` (no th07::).
void *g_BombDataTable[6 * 4] = {0};

// @Supervisor_TickTimer@12 -- __fastcall (12 bytes / 3 args). Referenced from
// ScreenEffect.cpp via `extern void __fastcall Supervisor_TickTimer(...)` and
// mangled as a global @Name@N symbol (no th07::).
extern "C" void __fastcall Supervisor_TickTimer(i32 *current, f32 *subFrame, i32 *prev)
{
    (void)current; (void)subFrame; (void)prev;
}

// Extern "C" alias for the th07::-namespace free functions that are
// referenced via C linkage from one or more call sites.
extern "C" {
void __fastcall AnmMgr_ExecuteScript_450d60_C(th07::AnmMgrStub *a, void *b)
{
    (void)a; (void)b;
}

// AnmManager::SetRenderStateForVm cross-module callees (orig FUN_0044f5c0 /
// FUN_00408180 / FUN_004082b0). FlushVertexBuffer is now a real AnmManager
// method (Draw2DCore batch flush via DrawPrimitiveUP); the cameras remain
// no-ops. This C-linkage thunk lets DrawInner / SetRenderStateForVm (which
// reference the orig absolute address) reach the real flush.
void __fastcall AnmManager_FlushVertexBuffer(void *anmMgr)
{
    if (anmMgr != 0)
    {
        ((th07::AnmManager *)anmMgr)->FlushVertexBuffer();
    }
}
void __fastcall Supervisor_Setup3DCamera(void *cameraStub)
{
    (void)cameraStub;
}
void __fastcall Supervisor_Setup2DCamera(void *cameraStub)
{
    (void)cameraStub;
}

// D3DX matrix helpers called by AnmManager::DrawInner (FUN_00450520). Orig
// FUN_00461b85 / 00461bff / 00461c7a / 00461aa2. Real bodies live in the
// d3dx8 math library; stubbed as no-ops here (normal-build only).
void __fastcall D3DXMatrixRotationZ_461b85(void *out, float angle)
{
    (void)out; (void)angle;
}
void __fastcall D3DXMatrixRotationX_461bff(void *out, float angle)
{
    (void)out; (void)angle;
}
void __fastcall D3DXMatrixRotationY_461c7a(void *out, float angle)
{
    (void)out; (void)angle;
}
void __fastcall D3DXMatrixMultiply_461aa2(void *out, void *a, void *b)
{
    (void)out; (void)a; (void)b;
}

// LoadTextureFromMemory (FUN_0044d9e0) surface helpers. Orig FUN_0046298a /
// FUN_00462aa6. The orig wraps IDirect3DDevice8::CreateTexture +
// D3DXLoadSurfaceFromMemory; for the normal build we call the D3D8 device
// method directly so embedded-texture .anm entries (e.g. title01.anm) load.
int __fastcall D3DXCreateTextureFromSurface_46298a(void *device, int width, int height,
                                                    int mipLevels, int usage, int format,
                                                    int pool, void **ppTexture)
{
    // Orig uses IDirect3DDevice8::CreateTexture with the caller-supplied
    // width/height/mipLevels/usage/format/pool. The caller passes
    // g_SupervisorD3dDevice_575958 as `device`.
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)device;
    HRESULT hr = dev->CreateTexture((UINT)width, (UINT)height, (UINT)mipLevels,
                                    (DWORD)usage, (D3DFORMAT)format, (D3DPOOL)pool,
                                    (IDirect3DTexture8 **)ppTexture);
    return FAILED(hr) ? -1 : 0;
}
int __fastcall D3DXLoadSurfaceFromMemory_462aa6(void *dstSurface, void *palette, void *dstRect,
                                                void *srcMemory, void *srcPalette, void *srcRect,
                                                int filter, unsigned int colorKey)
{
    // The orig wraps D3DXLoadSurfaceFromMemory. We don't link d3dx8's surface
    // loader in the normal build, so we replicate the load by locking both
    // surfaces and memcpy-ing the rows. dstSurface is the destination
    // IDirect3DSurface8* (the texture's level-0 surface); srcMemory is the
    // source IDirect3DSurface8* (the scratch imageSurface the caller filled).
    // Both share the same dimensions and format (verified by
    // LoadTextureFromMemory's CreateImageSurface call), so a straight
    // row-by-row copy with pitch adjustment is sufficient.
    (void)palette; (void)dstRect; (void)srcPalette; (void)srcRect;
    (void)filter; (void)colorKey;

    IDirect3DSurface8 *dst = (IDirect3DSurface8 *)dstSurface;
    IDirect3DSurface8 *src = (IDirect3DSurface8 *)srcMemory;
    if (dst == 0 || src == 0)
    {
        return 0;
    }

    D3DSURFACE_DESC srcDesc;
    D3DLOCKED_RECT srcLR;
    D3DLOCKED_RECT dstLR;
    if (FAILED(src->GetDesc(&srcDesc)) ||
        FAILED(src->LockRect(&srcLR, 0, D3DLOCK_READONLY)) ||
        FAILED(dst->LockRect(&dstLR, 0, 0)))
    {
        if (srcLR.pBits != 0) src->UnlockRect();
        return 0;
    }

    // Bytes per pixel for the surface format. We support the two formats the
    // .anm decoder uses (A4R4G4B4 == 2 bytes, A8R8G8B8 == 4 bytes) plus the
    // common 16-bit / 32-bit cases by deriving bpp from Format.
    u32 bpp;
    switch ((u32)srcDesc.Format)
    {
    case 20:  // D3DFMT_A8R8G8B8
    case 22:  // D3DFMT_X8R8G8B8
        bpp = 4;
        break;
    default:
        // 21 (R5G6B5), 25 (A1R5G5B5), 26 (A4R4G4B4) and other 16-bit fmts.
        bpp = 2;
        break;
    }
    u32 rowBytes = srcDesc.Width * bpp;

    for (u32 y = 0; y < srcDesc.Height; y++)
    {
        u8 *srcRow = (u8 *)srcLR.pBits + y * srcLR.Pitch;
        u8 *dstRow = (u8 *)dstLR.pBits + y * dstLR.Pitch;
        memcpy(dstRow, srcRow, rowBytes);
    }

    src->UnlockRect();
    dst->UnlockRect();
    return 0;
}

// LoadTextureAlphaChannel (FUN_0044dbe0): D3DXCreateTextureFromFileInMemoryEx
// wrapper (FUN_00462cf4). __cdecl; 15 args all on the stack. Normal-build
// stub returns error.
extern "C"
{
int __cdecl D3DXCreateTextureFromFileInMemoryEx_Wrapper_462cf4(void *device, void *fileData,
                                                                unsigned int fileSize,
                                                                unsigned int width,
                                                                unsigned int height,
                                                                unsigned int mipLevels,
                                                                unsigned int usage, unsigned int format,
                                                                unsigned int pool, unsigned int filter,
                                                                unsigned int mipFilter,
                                                                unsigned int colorKey, void *srcInfo,
                                                                void *palette, void **ppTexture)
{
    (void)device; (void)fileData; (void)fileSize; (void)width; (void)height; (void)mipLevels;
    (void)usage; (void)format; (void)pool; (void)filter; (void)mipFilter; (void)colorKey;
    (void)srcInfo; (void)palette; (void)ppTexture;
    return -1;
}
}
}
