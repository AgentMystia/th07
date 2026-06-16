// th07 BombData implementation.
//
// BombData is a 12-entry table at 0x0049ec50 of (calc, draw) callback pairs
// invoked by Player during spell-card (bomb) use. The 24 callbacks live in the
// th07::BombData struct (per config/ghidra_ns_to_obj.csv) and operate on a
// Player* passed in ECX (__fastcall).
//
// Player struct layout relevant here (verified from decompilation):
//   +0x16a4c  perBombState[0]  (8 bombs, 0x1428-byte / 0x50a-i32 stride)
//     within each entry:
//       [0x000] active flag        (i32)          *pSub != 0
//       [0x010] angle              (i32 as f32)   pSub[4]
//       [0x014] sprite idx         (i32)          pSub[5]
//       [0x018] x                  (i32 as f32)   pSub[6]
//       [0x01c] y                  (i32 as f32)   pSub[7]
//       [0x1c0] currentAngle       (i32)          pSub[0x70]
//       [0x378] flags              (i16)          *(i16*)(pSub+0xde)
//       [0x380] posX               (i32 as f32)   pSub[0xe0]
//       [0x384] posY               (i32 as f32)   pSub[0xe1]
//       [0x388] posZ               (i32 as f32)   pSub[0xe2]
//       [0x1b8] AnmVm              (at pSub+0x6e i32s)  FUN_0044f9a0 draw
//
// Helper functions (extern, addressed by absolute VA so objdiff sees the same
// relocs the orig delinked obj does):
//   FUN_004083f0  Supervisor::RegisterCurrentPosToChainLike (no args)
//   FUN_00431930  utils::AngleNormalize(angle, base)  -> f32 in st0
//   FUN_0044f9a0  AnmVm::Draw3 (ECX = AnmVm ptr)
//   FUN_00450d60  AnmVm::ExecuteScript (ECX = AnmVm ptr)

#include "Supervisor.hpp"
#include "Player.hpp"
#include "BombData.hpp"
#include "diffbuild.hpp"

namespace th07
{

// ---- helper externs (absolute-VA stubs; objdiff matches orig relocs) ----
extern "C" void __fastcall Supervisor_BombPreDraw();             // FUN_004083f0
extern "C" f32 __fastcall ZunAngleNormalize(i32 angle, i32 base); // FUN_00431930 (returns st0)
// ZunMath cos/sin wrappers — orig passes angle in st0 (float arg, no ECX).
// Declared __fastcall with one f32 so MSVC does `fld [arg]; call`.
extern "C" f32 __fastcall ZunCos(f32 a);  // FUN_0048bbf0
extern "C" f32 __fastcall ZunSin(f32 a);  // FUN_0048bb40
// AnmManager::Draw3(this=[0x4b9e44], AnmVm*) @ 0x0044f9a0 (__thiscall).
// We model it as a stub-method-on-global so MSVC emits ECX=[0x4b9e44];
// PUSH AnmVm; CALL.
struct AnmMgrStub
{
    void Draw3(i32 *anmVm); // FUN_0044f9a0
    void Draw2(i32 *anmVm); // FUN_0044f770
};
#define ANM_MGR (*(AnmMgrStub **)0x004b9e44)

// ---- calc-helper externs ----
extern "C" void __fastcall Gui_ShowBombPortrait2();              // FUN_00433a90
extern "C" void __fastcall Gui_ShowBombName(i32 anmIdx, i32 strIdx); // FUN_0042868d (2 args)
extern "C" void __fastcall Supervisor_ClearAnmScriptChain();     // FUN_004084f0
extern "C" void __fastcall Supervisor_SetPlayerPosFlag(f32 a);   // FUN_00408610 (1 float arg)
extern "C" void __fastcall Gui_EndPlayerSpellcard2();            // FUN_004277a0
extern "C" void __fastcall AnmMgr_ExecuteAnmIdx(i32 *anmVm, i32 idx); // FUN_00404f30 (ECX-free, 2 args)
extern "C" void __fastcall Effect_EnemyDamage(i32 a, i32 b, i32 c); // FUN_0044b310
extern "C" void __fastcall AnmVm_ExecuteScript(i32 *anmVm);      // FUN_00450d60 (ECX)
extern "C" void __fastcall Supervisor_TickTimer2(i32 *cur, i32 *sub); // FUN_0043958d
extern "C" void __fastcall Gui_EndSpellcard();                   // FUN_00427b21
extern "C" void __fastcall AnmVm_Die(f32 *pos, f32 x, f32 y, i32 z, i32 a); // FUN_004418b0
extern "C" void __fastcall Sound_PlayEffect(i32 idx, i32 a);     // FUN_0044c930

// perBombState stride in bytes
#define SUB_STRIDE_B 0x1428

// =====================================================================
// MarisaABombDraw2  (FUN_0040d9a0)  -- 224 bytes
// __fastcall, ECX = Player*. Iterates perBombState[0..3]; for active slots
// nudges the AnmVm pos by the focus offset, draws, then restores. Orig
// treats sub+0x380/0x384 as raw f32 (no int roundtrip).
// =====================================================================
void __fastcall BombData::MarisaABombDraw2(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 4; i++)
    {
        if (*(i32 *)sub != 0)
        {
            *(f32 *)(sub + 0x380) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(sub + 0x384) += *reinterpret_cast<f32 *>(0x0062f868);
            *(i32 *)(sub + 0x388) = 0;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(sub + 0x1b8));
            *(f32 *)(sub + 0x380) -= *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(sub + 0x384) -= *reinterpret_cast<f32 *>(0x0062f868);
        }
        sub += SUB_STRIDE_B;
    }
}

// =====================================================================
// ReimuABombDraw2  (FUN_0040c970)  -- 224 bytes
// Like MarisaABombDraw2 but sets angle/flags first (no focus offset). Orig
// caches a tmp = sub+0x1b8 (the AnmVm base) into a local and accesses the
// currentAngle/flags through it; we mirror that to match the codegen.
// =====================================================================
void __fastcall BombData::ReimuABombDraw2(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 0x60; i++)
    {
        if (*(i32 *)sub != 0)
        {
            f32 ang = ZunAngleNormalize(*(i32 *)(sub + 0x10), 0x3fc90fdb);
            i32 *tmp = reinterpret_cast<i32 *>(sub + 0x1b8);
            *(f32 *)(tmp + 2) = ang;        // currentAngle @ tmp+8 (sub+0x1c0)
            *(i32 *)(tmp + 0x70) |= 4;      // flags @ tmp+0x1c0 (sub+0x378) DWORD
            *(i32 *)((u8 *)tmp + 0x200) = *(i32 *)(sub + 0x14);   // posX (tmp+0x200)
            *(i32 *)((u8 *)tmp + 0x204) = *(i32 *)(sub + 0x18);   // posY
            *(i32 *)((u8 *)tmp + 0x208) = *(i32 *)(sub + 0x1c);   // posZ
            *(i32 *)((u8 *)tmp + 0x208) = 0;
            ANM_MGR->Draw3(tmp);
        }
        sub += SUB_STRIDE_B;
    }
}

// =====================================================================
// YoumuBBombDraw  (FUN_0040d3b0)  -- 272 bytes
// Sets angle/flags then nudges by focus offset before draw.
// =====================================================================
void __fastcall BombData::YoumuBBombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 0x60; i++)
    {
        if (*(i32 *)sub != 0)
        {
            f32 ang = ZunAngleNormalize(*(i32 *)(sub + 0x10), 0x3fc90fdb);
            i32 *tmp = reinterpret_cast<i32 *>(sub + 0x1b8);
            *(f32 *)(tmp + 2) = ang;        // currentAngle
            *(i32 *)(tmp + 0x70) |= 4;      // flags DWORD
            *(f32 *)((u8 *)tmp + 0x200) = *(f32 *)(sub + 0x14);
            *(f32 *)((u8 *)tmp + 0x204) = *(f32 *)(sub + 0x18);
            *(f32 *)((u8 *)tmp + 0x200) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)((u8 *)tmp + 0x204) += *reinterpret_cast<f32 *>(0x0062f868);
            *(i32 *)((u8 *)tmp + 0x208) = 0;
            ANM_MGR->Draw3(tmp);
        }
        sub += SUB_STRIDE_B;
    }
}

// =====================================================================
// MarisaABombDraw  (FUN_0040a280)  -- 288 bytes
// Loops 4 bombs; for each computes pos = subEntry.pos + subEntry.delta,
// stores into AnmVm pos, nudges by focus offset, draws.
// Orig uses [ebp-0x4]=AnmVm entry @ player+i*0x1428+0x16c04, with the entry's
// own pos @ +0x230 and source pos @ player+i*0x1428+0x16a60.
// =====================================================================
void __fastcall BombData::MarisaABombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    for (i32 i = 0; i < 4; i++)
    {
        u8 *anm = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16c04;
        f32 *entryDelta = reinterpret_cast<f32 *>(anm + 0x230);
        f32 *src = reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16a60);
        f32 z = src[2] + entryDelta[2];
        f32 y = src[1] + entryDelta[1];
        f32 x = src[0] + entryDelta[0];
        // store into AnmVm pos @ anm+0x1c8 (as a D3DXVECTOR3 copied via ints).
        i32 *pos = reinterpret_cast<i32 *>(anm + 0x1c8);
        pos[0] = *(i32 *)&x;
        pos[1] = *(i32 *)&y;
        pos[2] = *(i32 *)&z;
        *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
        *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
        *(i32 *)(anm + 0x1d0) = 0;
        ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
    }
}

// =====================================================================
// MarisaBBombDraw  (FUN_0040a6b0)  -- 272 bytes
// Loops 3 entries (stride 0x24c); AnmVm @ player+i*0x24c+0x16c04; the single
// source pos is @ player+0xb7e4c (MarisaB laser origin, shared by all 3).
// =====================================================================
void __fastcall BombData::MarisaBBombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    for (i32 i = 0; i < 3; i++)
    {
        u8 *anm = reinterpret_cast<u8 *>(p) + i * 0x24c + 0x16c04;
        f32 *entryDelta = reinterpret_cast<f32 *>(anm + 0x230);
        f32 *src = reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(p) + 0xb7e4c);
        f32 z = src[2] + entryDelta[2];
        f32 y = src[1] + entryDelta[1];
        f32 x = src[0] + entryDelta[0];
        i32 *pos = reinterpret_cast<i32 *>(anm + 0x1c8);
        pos[0] = *(i32 *)&x;
        pos[1] = *(i32 *)&y;
        pos[2] = *(i32 *)&z;
        *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
        *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
        *(i32 *)(anm + 0x1d0) = 0;
        ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
    }
}

// =====================================================================
// YoumuABombDraw  (FUN_0040c160)  -- 384 bytes
// Loops 4 entries (anm ptr advances by 0x24c each iteration). Computes an
// angle from the loop index, copies player pos into the AnmVm, nudges X/Y by
// cos/sin(angle)*sprite-scale, sets angle+flags, nudges by focus offset, draws.
// rdata floats: 0x498ce8 (step), 0x498a8c, 0x498a64, 0x498ce4 (angle base),
// 0x498a70 (scale divisor).
// =====================================================================
void __fastcall BombData::YoumuABombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *anm = reinterpret_cast<u8 *>(p) + 0x16c04;
    for (i32 i = 0; i < 4; i++)
    {
        f32 ang = ((f32)i * *reinterpret_cast<f32 *>(0x498ce8)) / *reinterpret_cast<f32 *>(0x498a8c)
                  - *reinterpret_cast<f32 *>(0x498a64) + *reinterpret_cast<f32 *>(0x498ce4);
        f32 *pos = reinterpret_cast<f32 *>(p) + (0x930 / 4); // player posCenter
        *(f32 *)(anm + 0x1c8) = pos[0];
        *(f32 *)(anm + 0x1cc) = pos[1];
        *(f32 *)(anm + 0x1d0) = pos[2];
        f32 c = ZunCos(ang);
        *(f32 *)(anm + 0x1c8) += c * *reinterpret_cast<f32 *>(*(i32 *)(anm + 0x1e4) + 0x2c) * *reinterpret_cast<f32 *>(anm + 0x1c) / *reinterpret_cast<f32 *>(0x498a70);
        f32 s = ZunSin(ang);
        *(f32 *)(anm + 0x1cc) += s * *reinterpret_cast<f32 *>(*(i32 *)(anm + 0x1e4) + 0x2c) * *reinterpret_cast<f32 *>(anm + 0x1c) / *reinterpret_cast<f32 *>(0x498a70);
        f32 na = ZunAngleNormalize(*(i32 *)&ang, 0x3fc90fdb);
        *(f32 *)(anm + 0x8) = na;
        *(i32 *)(anm + 0x1c0) |= 4;
        *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
        *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
        *(i32 *)(anm + 0x1d0) = 0;
        ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        anm += 0x24c;
    }
}

// --- Remaining callbacks: stubs (not yet ported — large calc bodies) ---
void __fastcall BombData::ReimuCBombCalc(Player *) {}
void __fastcall BombData::ReimuABombCalc(Player *) {}

// =====================================================================
// ReimuABombDraw  (FUN_00409990)  -- 1088 bytes
// Loops 8 entries (stride 0x1428). Each draws 4 sub-sprites (anm advances
// 0x24c). Every sub-sprite: pos = src(player+i*0x1428+0x16a60) + delta(anm+0x230),
// stored to anm+0x1c8, nudged by focus offset, Draw2 (FUN_0044f770). Orig is
// fully unrolled (4 copies); we mirror that.
// =====================================================================
void __fastcall BombData::ReimuABombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    for (i32 i = 0; i < 8; i++)
    {
        if (*(i32 *)(reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16a4c) != 0)
        {
            u8 *anm = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16c04;
            f32 *src = reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16a60);
            // sub-sprite 0
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 1
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 2
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 3
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
        }
    }
}
void __fastcall BombData::MarisaABombCalc(Player *) {}
void __fastcall BombData::MarisaBBombCalc(Player *) {}
void __fastcall BombData::SakuyaABombCalc(Player *) {}

// =====================================================================
// SakuyaABombDraw  (FUN_0040aba0)  -- 880 bytes
// Loops 8 entries (stride 0x1428); each draws 4 sub-sprites. The AnmVm anm
// pointer is @ player+i*0x1428+0x16c04. Source pos @ +0x16a60; velocity/delta
// scratch @ +0x16bec. Block 0 copies pos; blocks 1-3 subtract scaled deltas
// and add small offsets. rdata: 0x498b54 (0.83), 0x498c68 (offset), 0x498b08
// (offset). scale pairs: 3.05, 2.2, 2.2(none), 1.0.
// =====================================================================
void __fastcall BombData::SakuyaABombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    for (i32 i = 0; i < 8; i++)
    {
        u8 *anm = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16c04;
        u8 *src = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16a60;
        // block 0
        *(i32 *)(anm + 0x1c8 + 0) = *(i32 *)(src + 0);
        *(i32 *)(anm + 0x1c8 + 4) = *(i32 *)(src + 4);
        *(i32 *)(anm + 0x1c8 + 8) = *(i32 *)(src + 8);
        *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
        *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
        *(u32 *)(anm + 0x1d0) = 0;
        *(u32 *)(anm + 0x18) = 0x404ccccd;
        *(u32 *)(anm + 0x1c) = 0x404ccccd;
        ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        // block 1: delta = *(0x16bec) * 0.83
        {
            u8 *d = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16bec;
            f32 dz = *(f32 *)(d + 8) * *reinterpret_cast<f32 *>(0x498b54);
            f32 dy = *(f32 *)(d + 4) * *reinterpret_cast<f32 *>(0x498b54);
            f32 dx = *(f32 *)(d + 0) * *reinterpret_cast<f32 *>(0x498b54);
            *(f32 *)(anm + 0x1c8) -= dx;
            *(f32 *)(anm + 0x1cc) -= dy;
            *(f32 *)(anm + 0x1d0) -= dz;
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x498c68);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x498c68);
            *(u32 *)(anm + 0x1d0) = 0;
            *(u32 *)(anm + 0x18) = 0x400ccccd;
            *(u32 *)(anm + 0x1c) = 0x400ccccd;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        }
        // block 2: delta = *(0x16bec) * 2
        {
            u8 *d = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16bec;
            f32 dz = *(f32 *)(d + 8) * 2.0f;
            f32 dy = *(f32 *)(d + 4) * 2.0f;
            f32 dx = *(f32 *)(d + 0) * 2.0f;
            *(f32 *)(anm + 0x1c8) -= dx;
            *(f32 *)(anm + 0x1cc) -= dy;
            *(f32 *)(anm + 0x1d0) -= dz;
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x498b08);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x498b08);
            *(u32 *)(anm + 0x1d0) = 0;
            *(u32 *)(anm + 0x18) = 0x400ccccd;
            *(u32 *)(anm + 0x1c) = 0x400ccccd;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        }
        // block 3: delta = *(0x16bec) * 2
        {
            u8 *d = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16bec;
            f32 dz = *(f32 *)(d + 8) * 2.0f;
            f32 dy = *(f32 *)(d + 4) * 2.0f;
            f32 dx = *(f32 *)(d + 0) * 2.0f;
            *(f32 *)(anm + 0x1c8) -= dx;
            *(f32 *)(anm + 0x1cc) -= dy;
            *(f32 *)(anm + 0x1d0) -= dz;
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x498c68);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x498c68);
            *(u32 *)(anm + 0x1d0) = 0;
            *(u32 *)(anm + 0x18) = 0x3f800000;
            *(u32 *)(anm + 0x1c) = 0x3f800000;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        }
        // orig advances anm by 0x24c (but this draw only uses 1 AnmVm slot;
        // the increment is the loop-end bookkeeping).
        anm += 0x24c;
    }
}
void __fastcall BombData::SakuyaBBombCalc(Player *) {}

// =====================================================================
// SakuyaBBombDraw  (FUN_0040b5d0)  -- 512 bytes
// Loops 0x18 entries (stride 0x1428); each draws 3 sub-sprites from src
// @ sub+0x14 / +0x44 / +0x74 with distinct scale constants. anm = sub+0x1b8
// (single AnmVm, reused per sub-sprite before advancing).
// scale z/x/y pairs: (0.3/3.05), (0.32/2.2), (0.34/1.3).
// =====================================================================
void __fastcall BombData::SakuyaBBombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 0x18; i++)
    {
        if (*(i32 *)sub != 0)
        {
            u8 *anm = sub + 0x1b8;
            // sub-sprite 0 (src sub+0x14)
            *(i32 *)(anm + 0x1c8 + 0) = *(i32 *)(sub + 0x14 + 0);
            *(i32 *)(anm + 0x1c8 + 4) = *(i32 *)(sub + 0x14 + 4);
            *(i32 *)(anm + 0x1c8 + 8) = *(i32 *)(sub + 0x14 + 8);
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
            *(u32 *)(anm + 0x1d0) = 0x3e99999a;
            *(u32 *)(anm + 0x18) = 0x404ccccd;
            *(u32 *)(anm + 0x1c) = 0x404ccccd;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
            // sub-sprite 1 (src sub+0x44)
            *(i32 *)(anm + 0x1c8 + 0) = *(i32 *)(sub + 0x44 + 0);
            *(i32 *)(anm + 0x1c8 + 4) = *(i32 *)(sub + 0x44 + 4);
            *(i32 *)(anm + 0x1c8 + 8) = *(i32 *)(sub + 0x44 + 8);
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
            *(u32 *)(anm + 0x1d0) = 0x3ea3d70a;
            *(u32 *)(anm + 0x18) = 0x400ccccd;
            *(u32 *)(anm + 0x1c) = 0x400ccccd;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
            // sub-sprite 2 (src sub+0x74)
            *(i32 *)(anm + 0x1c8 + 0) = *(i32 *)(sub + 0x74 + 0);
            *(i32 *)(anm + 0x1c8 + 4) = *(i32 *)(sub + 0x74 + 4);
            *(i32 *)(anm + 0x1c8 + 8) = *(i32 *)(sub + 0x74 + 8);
            *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
            *(u32 *)(anm + 0x1d0) = 0x3eae147b;
            *(u32 *)(anm + 0x18) = 0x3fa66666;
            *(u32 *)(anm + 0x1c) = 0x3fa66666;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        }
        sub += SUB_STRIDE_B;
    }
}
void __fastcall BombData::ReimuBBombCalc(Player *) {}

// =====================================================================
// ReimuBBombDraw  (FUN_0040bca0)  -- 384 bytes
// Loops 3 entries (anm advances 0x24c). Per-bomb start angle @ player+i*0x1428
// +0x16a54 (read as i32 -> f32). Copies player pos, nudges X/Y by cos/sin(angle)
// * spriteScale, sets angle+flags, nudges by focus offset, Draw3.
// =====================================================================
void __fastcall BombData::ReimuBBombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    for (i32 i = 0; i < 3; i++)
    {
        u8 *anm = reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16c04;
        f32 ang = *(f32 *)(reinterpret_cast<u8 *>(p) + i * 0x1428 + 0x16a54);
        f32 *pos = reinterpret_cast<f32 *>(p) + (0x930 / 4);
        *(f32 *)(anm + 0x1c8) = pos[0];
        *(f32 *)(anm + 0x1cc) = pos[1];
        *(f32 *)(anm + 0x1d0) = pos[2];
        f32 c = ZunCos(ang);
        *(f32 *)(anm + 0x1c8) += c * *reinterpret_cast<f32 *>(*(i32 *)(anm + 0x1e4) + 0x2c) * *reinterpret_cast<f32 *>(anm + 0x1c) / *reinterpret_cast<f32 *>(0x498a70);
        f32 s = ZunSin(ang);
        *(f32 *)(anm + 0x1cc) += s * *reinterpret_cast<f32 *>(*(i32 *)(anm + 0x1e4) + 0x2c) * *reinterpret_cast<f32 *>(anm + 0x1c) / *reinterpret_cast<f32 *>(0x498a70);
        f32 na = ZunAngleNormalize(*(i32 *)&ang, 0x3fc90fdb);
        *(f32 *)(anm + 0x8) = na;
        *(i32 *)(anm + 0x1c0) |= 4;
        *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
        *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
        *(i32 *)(anm + 0x1d0) = 0;
        ANM_MGR->Draw3(reinterpret_cast<i32 *>(anm));
        anm += 0x24c;
    }
}
void __fastcall BombData::YoumuABombCalc(Player *) {}
void __fastcall BombData::ReimuABombCalc2(Player *) {}
void __fastcall BombData::YoumuBBombCalc(Player *) {}
// =====================================================================
// MarisaABombCalc2  (FUN_0040d4c0)  -- 0x4e0 bytes
// __fastcall, ECX = Player*. Master-Spark-like spell: timer-gated init at
// t==0, screen-shake/effect calls at t==0x3c/0x78, bomb-spawn loop at t==0x1e,
// damage-region updates while t>0x1d & t%4==0, enemy-damage at t==0x28/100,
// final AnmVm::ExecuteScript loop, then TickTimer.
// Globals: time fields @ player+0x16a38/16a34/16a30/16a28/16a20.
// =====================================================================
void __fastcall BombData::MarisaABombCalc2(Player *p)
{
    u8 *P = reinterpret_cast<u8 *>(p);
    i32 curTime = *(i32 *)(P + 0x16a38);
    i32 prevTime = *(i32 *)(P + 0x16a30);
    if (curTime >= *(i32 *)(P + 0x16a28))
    {
        Gui_EndSpellcard();
        *(i32 *)(P + 0x16a20) = 0;
        *(u32 *)(P + 0x23f4) = 0x3f800000;
        *(u32 *)(P + 0x23f0) = 0x3f800000;
        AnmVm_Die(reinterpret_cast<f32 *>(P + 0x930), 0x44480000, 0.0f, 0, 6);
        return;
    }
    f32 lp70, lp74;
    if (curTime != prevTime && curTime == 0)
    {
        Gui_ShowBombPortrait2();
        Gui_ShowBombName(0x4a3, *(i32 *)0x00498768);
        *(i32 *)(P + 0x16a28) = 0xa0;
        *(i32 *)(P + 0x16a08) = 0x104;
        *(i32 *)(P + 0x16a04) = 0;
        *(i32 *)(P + 0x16a00) = (i32)0xfffffc19;
        Supervisor_ClearAnmScriptChain();
        // clear 4 bombs
        i32 *sub = reinterpret_cast<i32 *>(P + 0x16a4c);
        for (u32 i = 0; (i32)i < 4; i++)
        {
            *sub = 0;
            sub += 0x50a;
        }
        Sound_PlayEffect(0x13, 0);
        Supervisor_SetPlayerPosFlag(0x3e851eb8);
        *(u32 *)(P + 0x23f4) = 0x40000000;
        *(u32 *)(P + 0x23f0) = 0x40000000;
        Gui_EndPlayerSpellcard2();
    }
    if (curTime != prevTime && curTime == 0x3c)
    {
        Gui_EndPlayerSpellcard2();
    }
    if (curTime != prevTime && curTime == 0x78)
    {
        Gui_EndPlayerSpellcard2();
    }
    if (curTime != prevTime && curTime == 0x1e)
    {
        i32 *sub = reinterpret_cast<i32 *>(P + 0x16a4c);
        for (u32 i = 0; (i32)i < 4; i++)
        {
            *sub = 1;
            AnmMgr_ExecuteAnmIdx(sub + 0x6e, (i32)i + 0x409);
            if ((i & 1) == 0)
            {
                lp70 = -128.0f;
            }
            else
            {
                lp70 = 128.0f;
            }
            sub[0xe0] = (i32)(*reinterpret_cast<f32 *>(0x00498c74) + lp70);
            if ((i32)i / 2 == 0)
            {
                lp74 = -128.0f;
            }
            else
            {
                lp74 = 128.0f;
            }
            sub[0xe1] = (i32)(*reinterpret_cast<f32 *>(0x00498b2c) + lp74);
            sub[0xe2] = (i32)0x3efae148;
            sub[0x6a] = 0;
            sub += 0x50a;
        }
    }
    if (curTime > 0x1d && curTime != prevTime)
    {
        u32 r = (u32)curTime & 0x80000003;
        if ((i32)r < 0)
        {
            r = (r - 1 | 0xfffffffc) + 1;
        }
        if (r == 0)
        {
            *(u32 *)(P + 0x9dc) = 0x43400000;
            *(u32 *)(P + 0x9e0) = 0x43600000;
            *(u32 *)(P + 0x9e8) = 0x43b00000;
            *(u32 *)(P + 0x9ec) = 0x43d00000;
            *(u32 *)(P + 0x9f4) = 3;
        }
    }
    if (curTime != prevTime && curTime == 0x28)
    {
        Effect_EnemyDamage(1, 7, 0);
    }
    if (curTime != prevTime && curTime == 100)
    {
        Effect_EnemyDamage(0x18, 0, 0);
    }
    {
        i32 *sub = reinterpret_cast<i32 *>(P + 0x16a4c);
        for (u32 i = 0; (i32)i < 4; i++)
        {
            if (*sub != 0)
            {
                AnmVm_ExecuteScript(sub + 0x6e);
            }
            sub += 0x50a;
        }
    }
    *(u8 *)(P + 0x2408) = 3;
    *(i32 *)(P + 0x16a30) = *(i32 *)(P + 0x16a38);
    Supervisor_TickTimer2(reinterpret_cast<i32 *>(P + 0x16a38), reinterpret_cast<i32 *>(P + 0x16a34));
}
void __fastcall BombData::SakuyaABombCalc2(Player *) {}

// =====================================================================
// SakuyaABombDraw2  (FUN_0040e280)  -- 0x195 bytes
// Loops 2 entries; for each active one: draws 1 AnmVm then an inner loop
// j=3..0x1f (step 4) drawing AnmVm-with-rotated-frame. The frame counter byte
// @ sub+0x373 is advanced by `frame += j; frame = frame - (frame*j >> 5)`.
// =====================================================================
void __fastcall BombData::SakuyaABombDraw2(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 2; i++)
    {
        if (*(i32 *)sub != 0)
        {
            i32 frame = *(u8 *)(sub + 0x373);
            // first draw
            *(i32 *)(sub + 0x380 + 0) = *(i32 *)(sub + 0x14 + 0);
            *(i32 *)(sub + 0x380 + 4) = *(i32 *)(sub + 0x14 + 4);
            *(i32 *)(sub + 0x380 + 8) = *(i32 *)(sub + 0x14 + 8);
            *(f32 *)(sub + 0x380) += *reinterpret_cast<f32 *>(0x0062f864);
            *(f32 *)(sub + 0x384) += *reinterpret_cast<f32 *>(0x0062f868);
            *(i32 *)(sub + 0x388) = 0;
            ANM_MGR->Draw3(reinterpret_cast<i32 *>(sub + 0x1b8));
            // inner loop: j = 3, 7, 11, ..., 0x1f (step 4)
            for (i32 j = 3; j < 0x20; j += 4)
            {
                u8 *src = sub + j * 0xc + 0x20;
                *(i32 *)(sub + 0x380 + 0) = *(i32 *)(src + 0);
                *(i32 *)(sub + 0x380 + 4) = *(i32 *)(src + 4);
                *(i32 *)(sub + 0x380 + 8) = *(i32 *)(src + 8);
                // orig: frame += (frame*j) signed-div-32 trick
                i32 prod = frame * j;
                i32 carry = (prod >> 31) & 0x1f;
                frame = frame - ((prod + carry) >> 5);
                *(u8 *)(sub + 0x373) = (u8)frame;
                *(f32 *)(sub + 0x380) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(sub + 0x384) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(sub + 0x388) = 0;
                ANM_MGR->Draw3(reinterpret_cast<i32 *>(sub + 0x1b8));
            }
            *(u8 *)(sub + 0x373) = (u8)frame;
        }
        sub += SUB_STRIDE_B;
    }
}

// =====================================================================
// ReimuCBombDraw  (FUN_00408e10)  -- 0x398 bytes
// Loops 8 per-bomb entries (stride 0x1428). For each active entry it draws 4
// sub-sprites (AnmVm @ sub+0x1b8 advancing by 0x24c). Each sub-sprite copies
// pos = srcPos(sub+0x14) + subSpriteDelta(anm+0x230) into AnmVm pos @ +0x1c8,
// nudges by focus offset, and calls AnmManager::Draw2 (FUN_0044f770). Orig is
// fully unrolled (4 copies of the block); we mirror that.
// =====================================================================
void __fastcall BombData::ReimuCBombDraw(Player *p)
{
    Supervisor_BombPreDraw();
    u8 *sub = reinterpret_cast<u8 *>(p) + 0x16a4c;
    for (i32 i = 0; i < 8; i++)
    {
        if (*(i32 *)sub != 0)
        {
            u8 *anm = sub + 0x1b8;
            // sub-sprite 0
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 *src = reinterpret_cast<f32 *>(sub + 0x14);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 1
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 *src = reinterpret_cast<f32 *>(sub + 0x14);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 2
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 *src = reinterpret_cast<f32 *>(sub + 0x14);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
            anm += 0x24c;
            // sub-sprite 3
            {
                f32 *delta = reinterpret_cast<f32 *>(anm + 0x230);
                f32 *src = reinterpret_cast<f32 *>(sub + 0x14);
                f32 x = src[0] + delta[0];
                f32 y = src[1] + delta[1];
                f32 z = src[2] + delta[2];
                *(f32 *)(anm + 0x1c8) = x;
                *(f32 *)(anm + 0x1cc) = y;
                *(f32 *)(anm + 0x1d0) = z;
                *(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);
                *(f32 *)(anm + 0x1cc) += *reinterpret_cast<f32 *>(0x0062f868);
                *(i32 *)(anm + 0x1d0) = 0;
                ANM_MGR->Draw2(reinterpret_cast<i32 *>(anm));
            }
        }
        sub += SUB_STRIDE_B;
    }
}

} // namespace th07
