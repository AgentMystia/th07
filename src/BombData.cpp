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
// AnmManager::Draw3(this=[0x4b9e44], AnmVm*) @ 0x0044f9a0 (__thiscall).
// We model it as a stub-method-on-global so MSVC emits ECX=[0x4b9e44];
// PUSH AnmVm; CALL.
struct AnmMgrStub
{
    void Draw3(i32 *anmVm); // FUN_0044f9a0
};
#define ANM_MGR (*(AnmMgrStub **)0x004b9e44)

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

// --- Remaining callbacks: stubs (not yet ported — large calc bodies) ---
void __fastcall BombData::ReimuCBombCalc(Player *) {}
void __fastcall BombData::ReimuCBombDraw(Player *) {}
void __fastcall BombData::ReimuABombCalc(Player *) {}
void __fastcall BombData::ReimuABombDraw(Player *) {}
void __fastcall BombData::MarisaABombCalc(Player *) {}
void __fastcall BombData::MarisaBBombCalc(Player *) {}
void __fastcall BombData::MarisaBBombDraw(Player *) {}
void __fastcall BombData::SakuyaABombCalc(Player *) {}
void __fastcall BombData::SakuyaABombDraw(Player *) {}
void __fastcall BombData::SakuyaBBombCalc(Player *) {}
void __fastcall BombData::SakuyaBBombDraw(Player *) {}
void __fastcall BombData::ReimuBBombCalc(Player *) {}
void __fastcall BombData::ReimuBBombDraw(Player *) {}
void __fastcall BombData::YoumuABombCalc(Player *) {}
void __fastcall BombData::YoumuABombDraw(Player *) {}
void __fastcall BombData::ReimuABombCalc2(Player *) {}
void __fastcall BombData::YoumuBBombCalc(Player *) {}
void __fastcall BombData::MarisaABombCalc2(Player *) {}
void __fastcall BombData::SakuyaABombCalc2(Player *) {}
void __fastcall BombData::SakuyaABombDraw2(Player *) {}

} // namespace th07
