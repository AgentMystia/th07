// Auto-generated link stubs (Phase C, P0 link pass). Resolves cross-
// module `@Name@N` fastcall and `_Name` cdecl references that no source
// module defines yet. Each stub is a no-op returning 0; the stack-
// cleanup byte count from the @ suffix is preserved by declaring the
// equivalent number of dummy args so MSVC emits the matching `RET N`.
// th07 uses __fastcall for callbacks (AGENTS.md §6) so the stubs are
// declared __fastcall too -- this yields the `@Name@N` mangling the
// callers expect (no leading underscore, unlike __stdcall).
//
// Normal-build only; link_stubs.obj is excluded from objdiff (not in
// objdiff.json / config/ghidra_ns_to_obj.csv).

#include "inttypes.hpp"

extern "C" {
    void __fastcall AnmManager_Callback_D630(i32 a0) { }
    void __fastcall AnmManager_FlushSprites(i32 a0) { }
    void __fastcall AnmManager_LoadAnm(i32 a0, i32 a1, i32 a2, i32 a3) { }
    void __fastcall AnmMgr_ExecuteAnmIdx(i32 a0, i32 a1) { }
    void __fastcall AnmVm_Die(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall AnmVm_ExecuteScript(i32 a0) { }
    void __fastcall BulletMgr_RemoveAllBullets(i32 a0) { }
    void __fastcall CMyFont_GetPixelFormat(i32 a0) { }
    void __fastcall CStreamingSound_UpdateFadeOut(void) { }
    void __fastcall Callback_01e30(void) { }
    void __fastcall Callback_3225b(void) { }
    void __fastcall Callback_378d0(i32 a0) { }
    void __fastcall Callback_44c20(i32 a0) { }
    void __fastcall Callback_4547f(i32 a0, i32 a1, i32 a2) { }
    void __fastcall Callback_4547f_2arg(i32 a0, i32 a1) { }
    void __fastcall Callback_454fc(i32 a0) { }
    void __fastcall Controller_GetInput(void) { }
    void __fastcall EffectManager_SpawnEffectObj(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall EffectManager_SpawnEffectObj2(i32 a0, i32 a1, i32 a2, i32 a3) { }
    void __fastcall Effect_EnemyDamage(i32 a0, i32 a1, i32 a2) { }
    void __fastcall EnemyManager_IsGameActive(i32 a0) { }
    void __fastcall EnemyManager_ResetBorderState(i32 a0) { }
    void __fastcall EnemyManager_ResetStage(i32 a0) { }
    void __fastcall FloatToI16(i32 a0) { }
    void __fastcall GameManager_BorderSetup(i32 a0) { }
    void __fastcall GameManager_CheckState(i32 a0) { }
    void __fastcall GameManager_GameStateFlag(i32 a0) { }
    void __fastcall GameManager_SetStageState(i32 a0, i32 a1) { }
    void __fastcall GameManager_TintSprites(i32 a0) { }
    void __fastcall Gui_EndPlayerSpellcard2(void) { }
    void __fastcall Gui_EndSpellcard(void) { }
    void __fastcall Gui_HasMessage(void) { }
    void __fastcall Gui_Reset(void) { }
    void __fastcall Gui_ShowBombName(i32 a0, i32 a1) { }
    void __fastcall Gui_ShowBombPortrait2(void) { }
    void __fastcall Item_FullPowerSetup(i32 a0, i32 a1) { }
    void __fastcall Item_SpawnItem(i32 a0, i32 a1, i32 a2) { }
    void __fastcall ListNode_Ctor(i32 a0) { }
    void __fastcall MidiOutput_Ctor(i32 a0) { }
    void __fastcall MidiOutput_Play(i32 a0, i32 a1, i32 a2) { }
    void __fastcall MidiOutput_StopPlayback(void) { }
    void __fastcall Pbg4_NodePick(i32 a0) { }
    void __fastcall Pbg4_NodePush(i32 a0) { }
    void __fastcall Pbg4_NodeShrink(i32 a0) { }
    void __fastcall Player_ResetOptionSpriteScale(i32 a0, i32 a1, i32 a2, i32 a3, i32 a4) { }
    void __fastcall Rng_GetRandomU32(i32 a0) { }
    void __fastcall SoundPlayer_Callback_C020(i32 a0, i32 a1) { }
    void __fastcall SoundPlayer_Callback_C7d0(i32 a0) { }
    void __fastcall SoundPlayer_LoadFmt(i32 a0, i32 a1) { }
    void __fastcall Sound_PlayEffect(i32 a0, i32 a1) { }
    void __fastcall Supervisor_AutosaveScore(i32 a0, i32 a1, i32 a2) { }
    void __fastcall Supervisor_BombPreDraw(void) { }
    void __fastcall Supervisor_Bootstrap(void) { }
    void __fastcall Supervisor_Callback6(void) { }
    void __fastcall Supervisor_Callback7(void) { }
    void __fastcall Supervisor_Callback_Fun383d8(i32 a0) { }
    void __fastcall Supervisor_Cleanup3(void) { }
    void __fastcall Supervisor_ClearAnmScriptChain(void) { }
    void __fastcall Supervisor_CreateWindow(void) { }
    void __fastcall Supervisor_D3DDiscard(i32 a0) { }
    void __fastcall Supervisor_HeapFreeAll(void) { }
    void __fastcall Supervisor_InitD3D(void) { }
    void __fastcall Supervisor_MidiClearTracks(void) { }
    void __fastcall Supervisor_ReadConfigBuffer(i32 a0, i32 a1) { }
    void __fastcall Supervisor_ReleaseAnm0(void) { }
    void __fastcall Supervisor_RunSession(void) { }
    void __fastcall Supervisor_SetAnmFlag(i32 a0, i32 a1) { }
    void __fastcall Supervisor_SetPlayerPosFlag(i32 a0) { }
    void __fastcall Supervisor_SomeCleanup1(void) { }
    void __fastcall Supervisor_SomeCleanup4(void) { }
    void __fastcall Supervisor_SomeCleanup5(void) { }
    void __fastcall Supervisor_Teardown(void) { }
    void __fastcall Supervisor_TickTimer(i32 a0, i32 a1) { }
    void __fastcall Supervisor_TickTimer2(i32 a0, i32 a1) { }
    void __fastcall Supervisor_ValidateSize(i32 a0) { }
    void __fastcall ZunAngleNormalize(i32 a0, i32 a1) { }
    void __fastcall ZunCos(i32 a0) { }
    void __fastcall ZunSin(i32 a0) { }
    void __fastcall operator_new_th07(i32 a0) { }
    void __fastcall utils_AddNormalizeAngle(i32 a0, i32 a1) { }
    void __fastcall utils_GetArcadeRegionMaxX(void) { }
    void __fastcall utils_IsInBounds(i32 a0, i32 a1, i32 a2, i32 a3) { }
    void __fastcall utils_RandF32(void) { }
    void __fastcall utils_RandF32_2(void) { }
} // extern "C"

// _Supervisor_LogStr1: extern "C" data slot (zero-init)
extern "C" int Supervisor_LogStr1 = 0;
// _delete_0047d43c: extern "C" data slot (zero-init)
extern "C" int delete_0047d43c = 0;
// _new_0047d441: extern "C" data slot (zero-init)
extern "C" int new_0047d441 = 0;
// _sprintf_th07: extern "C" data slot (zero-init)
extern "C" int sprintf_th07 = 0;
// _strchr_th07: extern "C" data slot (zero-init)
extern "C" int strchr_th07 = 0;
