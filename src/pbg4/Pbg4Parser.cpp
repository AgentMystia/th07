// Pbg4Parser.cpp - th07 Pbg4 archive LZSS dictionary helpers.
//
// Source of truth: th07.exe via ghidra. These three methods manage the static
// LZSS dictionary (DAT_004b7e40) and the 0x2001-entry node heap
// (DAT_0049fe30). The full Pbg4 archive codec (decompress FUN_0045ef00,
// open archive FUN_0045fb50, build entry table FUN_0045fde0) lives in the
// not-yet-reversed Pbg4 archive TU and is provided as no-op stubs by the
// normal-build link_stubs.cpp.
//
//   th07::Pbg4Parser::SetIndex     FUN_0045f270   __fastcall (idx in ECX)
//   th07::Pbg4Parser::Reset        FUN_0045f2c0   __fastcall (unused this in ECX)
//   th07::Pbg4Parser::AdvanceNode  FUN_0045f460   __fastcall (idx in ECX)
//
// All three orig functions are compiled with /Od: arguments arrive in ECX and
// are immediately spilled to a stack slot at [EBP-0x4] / [EBP-0x8], then
// re-loaded from that slot for every use. Each node-table access is its own
// `IMUL reg, reg, 0xc` (the compiler does not CSE the stride across reads).
// We mirror that exact stack discipline and per-access IMUL discipline so
// the reimpl reproduces the orig byte stream.

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// ---------------------------------------------------------------------------
// LZSS static state. DAT_004b7e40 is the 0x2000-byte circular dictionary;
// DAT_0049fe30 is the node heap (0x2001 nodes, 0xc bytes each). The curIndex
// global DAT_004b7e38 holds the dictionary write cursor.
//
// These are orig .data/.bss blobs, not objdiff reloc-hack "DAT_ constant
// slots". They are defined (zero-initialized) by link_globals.cpp for the
// normal build; the objdiff build leaves them as extern because the orig
// delinker resolves the DAT_ symbols directly.
// ---------------------------------------------------------------------------
extern "C" u8 g_Pbg4Dict[0x2000];        // DAT_004b7e40
extern "C" u8 g_Pbg4Nodes[0x2001 * 0xc]; // DAT_0049fe30
extern "C" i32 g_Pbg4CurIndex;           // DAT_004b7e38

// ---------------------------------------------------------------------------
// Pbg4Parser is a "static-method bag": orig th07 never instantiates it.
// SetIndex / AdvanceNode are static __fastcall methods whose only register
// argument is `idx` (ECX); Reset takes a `Pbg4Parser*` in ECX that the body
// ignores (orig FUN_0045f2c0 never reads the spill slot). We model the three
// methods as static so MSVC does NOT inject an implicit `this` parameter
// (which would push `idx` into EDX and save EDX in the prologue - orig does
// neither). Reset keeps a single `Pbg4Parser *` formal so the ECX slot the
// orig prologue reserves still exists (and is unused, matching orig).
// ---------------------------------------------------------------------------
struct Pbg4Parser
{
    static void __fastcall SetIndex(i32 idx);
    static void __fastcall Reset(Pbg4Parser * /*unused this*/);
    static void __fastcall AdvanceNode(i32 idx);
};

// ---------------------------------------------------------------------------
// Helpers used by AdvanceNode. These live in the not-yet-reversed Pbg4 archive
// TU at FUN_0045f4f0 / FUN_0045f580 / FUN_0045f640. They are __fastcall with
// (idx in ECX, param in EDX). The orig AdvanceNode sets both ECX and EDX
// before each CALL; we declare them with both parameters so the call site
// emits `MOV ECX, idx` + `MOV EDX, field` + `CALL`.
// ---------------------------------------------------------------------------
extern "C" void __fastcall Pbg4_NodeShrink(i32 idx, i32 param);
extern "C" void __fastcall Pbg4_NodePush(i32 idx, i32 param);
extern "C" i32  __fastcall Pbg4_NodePick(i32 idx);

// ---------------------------------------------------------------------------
// Pbg4Parser::SetIndex  (FUN_0045f270)
//
// Stack layout (orig):
//   [EBP-0x4]  idx        <- single spill of incoming ECX
//
// The orig function is /Od with a 4-byte frame (the prologue is the single
// instruction `PUSH ECX` which MSVC emits in lieu of `SUB ESP, 4` for a
// one-dword frame). The incoming ECX is spilled straight into [EBP-0x4] and
// re-loaded for every subsequent use (curIndex store + each of three node
// field stores). Each node-table store recomputes `IMUL reg, idx, 0xc` from
// scratch. The arg `idx` is `__fastcall` so MSVC already spills it; the
// `#pragma var_order(idx)` pins the slot so MSVC reuses it for every read
// (rather than keeping idx live in a second register).
// ---------------------------------------------------------------------------
#pragma var_order(idx)
void __fastcall Pbg4Parser::SetIndex(i32 idx)
{
    g_Pbg4CurIndex = idx;
    *(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x0) = 0x2000;
    *(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x8) = 0;
    *(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x4) = 0;
}

// ---------------------------------------------------------------------------
// Pbg4Parser::Reset  (FUN_0045f2c0)
//
// Stack layout (orig):
//   [EBP-0x4]  loop counter i   (reused across both loops; same slot that
//                                received the ECX spill, since the orig
//                                function ignores `this` entirely)
//
// Two loops share the single counter slot. Loop 1 zeros the 0x2000-byte
// dictionary one byte at a time (CMP i, 0x2000). Loop 2 zeros all three
// dwords of each node entry for i in [0, 0x2001), recomputing `i*0xc` three
// times per iteration. The prologue's `PUSH ECX` reserves the [EBP-0x4] slot
// (4-byte frame); MSVC merges the ECX spill and the local `i` because the
// formal `this` is unused. The orig control flow is a for-loop shape
// (init -> jmp cond -> body -> inc -> cond), not a while-loop; we mirror it
// with explicit `for` so MSVC emits the same jmp/label sequence.
// ---------------------------------------------------------------------------
#pragma var_order(i)
void __fastcall Pbg4Parser::Reset(Pbg4Parser * /*unused this*/)
{
    i32 i;

    for (i = 0; i < 0x2000; i = i + 1)
    {
        g_Pbg4Dict[i] = 0;
    }
    for (i = 0; i < 0x2001; i = i + 1)
    {
        *(i32 *)(g_Pbg4Nodes + i * 0xc + 0x0) = 0;
        *(i32 *)(g_Pbg4Nodes + i * 0xc + 0x4) = 0;
        *(i32 *)(g_Pbg4Nodes + i * 0xc + 0x8) = 0;
    }
}

// ---------------------------------------------------------------------------
// Pbg4Parser::AdvanceNode  (FUN_0045f460)
//
// Stack layout (orig):
//   [EBP-0x8]  idx         <- spill of incoming ECX
//   [EBP-0x4]  pickedNode  <- result of NodePick()
//
// Orig spills idx to [EBP-0x8] and re-reads it for every node-table access
// and every helper call. The control flow is a 3-way branch:
//   1. nodes[idx].field0 == 0      -> do nothing, return.
//   2. nodes[idx].field8 == 0      -> NodeShrink(idx, nodes[idx].field4); return.
//   3. nodes[idx].field4 == 0      -> NodeShrink(idx, nodes[idx].field8); return.
//   4. otherwise                   -> pickedNode = NodePick(idx);
//                                     AdvanceNode(pickedNode);
//                                     NodePush(idx, pickedNode);
//
// Note orig's EDX setup at the call sites: for case 2, EDX = nodes[idx].field4
// (read with IMUL idx*0xc, then read field at +0x4). For case 3, EDX =
// nodes[idx].field8. The `IMUL reg, reg, 0xc` is re-emitted for every access
// (no CSE) because we are at /Od.
// ---------------------------------------------------------------------------
#pragma var_order(idxSlot, pickedNodeSlot)
void __fastcall Pbg4Parser::AdvanceNode(i32 idx)
{
    i32 idxSlot;
    i32 pickedNodeSlot;

    idxSlot = idx;
    if (*(i32 *)(g_Pbg4Nodes + idxSlot * 0xc + 0x0) == 0)
    {
        return;
    }
    if (*(i32 *)(g_Pbg4Nodes + idxSlot * 0xc + 0x8) == 0)
    {
        Pbg4_NodeShrink(idxSlot, *(i32 *)(g_Pbg4Nodes + idxSlot * 0xc + 0x4));
        return;
    }
    if (*(i32 *)(g_Pbg4Nodes + idxSlot * 0xc + 0x4) == 0)
    {
        Pbg4_NodeShrink(idxSlot, *(i32 *)(g_Pbg4Nodes + idxSlot * 0xc + 0x8));
        return;
    }
    pickedNodeSlot = Pbg4_NodePick(idxSlot);
    AdvanceNode(pickedNodeSlot);
    Pbg4_NodePush(idxSlot, pickedNodeSlot);
}
}; // namespace th07
