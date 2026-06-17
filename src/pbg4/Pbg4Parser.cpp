// Pbg4Parser.cpp - th07 Pbg4 archive bitstream parser helpers
//
// Source of truth: th07.exe via ghidra. These are the Pbg4 LZSS dictionary
// helpers (reset / index set / node advance). The full Pbg4Parser class lives
// across FUN_0045ef00 (decompress), FUN_0045fb50 (archive open), FUN_0045fde0
// (entry table build) — those are large bitstream codecs not yet ported.
//
// Globals:
//   DAT_004b7e40 = LZSS dictionary (0x2000 bytes)
//   DAT_0049fe30 = node table (0x2001 entries × 0xc bytes)

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
// LZSS static data tables. These are orig .bss/.data blobs (not "constant
// slots" used for objdiff reloc hacking). They are declared extern here
// because the owning TU (the full Pbg4 archive codec) is not yet reversed;
// the linker provides zero-filled storage via the normal-build stub until
// the real definitions land. See AGENTS.md §2 for the distinction.
extern "C" u8 g_Pbg4Dict[0x2000];        // DAT_004b7e40 (LZSS dictionary)
extern "C" u8 g_Pbg4Nodes[0x2001 * 0xc]; // DAT_0049fe30 (LZSS node table)

struct Pbg4Parser
{
    void __fastcall Reset();
    void __fastcall SetIndex(i32 idx);
    void __fastcall AdvanceNode(i32 idx);
};

#pragma optimize("s", on)

// Pbg4Parser::Reset (FUN_0045f2c0): zero the dictionary + node table.
void __fastcall Pbg4Parser_Reset()
{
    for (i32 i = 0; i < 0x2000; i++)
    {
        g_Pbg4Dict[i] = 0;
    }
    for (i32 i = 0; i < 0x2001; i++)
    {
        *(u32 *)(g_Pbg4Nodes + i * 0xc + 0x0) = 0;
        *(u32 *)(g_Pbg4Nodes + i * 0xc + 0x4) = 0;
        *(u32 *)(g_Pbg4Nodes + i * 0xc + 0x8) = 0;
    }
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// extern helpers (FUN_0045f4f0 / FUN_0045f640 / FUN_0045f580)
extern "C" void __fastcall Pbg4_NodeShrink(i32 idx);
extern "C" u32 __fastcall Pbg4_NodePick(i32 idx);
extern "C" void __fastcall Pbg4_NodePush(i32 idx);

// Pbg4Parser::AdvanceNode (FUN_0045f460): traverse the node heap.
void __fastcall Pbg4Parser_AdvanceNode(i32 idx)
{
    if (*(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x0) != 0)
    {
        if (*(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x8) == 0)
        {
            Pbg4_NodeShrink(idx);
        }
        else if (*(i32 *)(g_Pbg4Nodes + idx * 0xc + 0x4) == 0)
        {
            Pbg4_NodeShrink(idx);
        }
        else
        {
            u32 picked = Pbg4_NodePick(idx);
            Pbg4Parser_AdvanceNode(picked);
            Pbg4_NodePush(idx);
        }
    }
}

#pragma optimize("s", off)
#pragma optimize("s", on)

// Pbg4Parser::SetIndex (FUN_0045f270): set current index + init node entry.
extern "C" i32 g_Pbg4CurIndex; // DAT_004b7e38 (LZSS current index; orig .data)
void __fastcall Pbg4Parser_SetIndex(i32 idx)
{
    g_Pbg4CurIndex = idx;
    *(u32 *)(g_Pbg4Nodes + idx * 0xc + 0x0) = 0x2000;
    *(u32 *)(g_Pbg4Nodes + idx * 0xc + 0x8) = 0;
    *(u32 *)(g_Pbg4Nodes + idx * 0xc + 0x4) = 0;
}

#pragma optimize("s", off)

// Namespace-qualified definitions to match mapping names.
void __fastcall Pbg4Parser::Reset() { Pbg4Parser_Reset(); }
void __fastcall Pbg4Parser::SetIndex(i32 idx) { Pbg4Parser_SetIndex(idx); }
void __fastcall Pbg4Parser::AdvanceNode(i32 idx) { Pbg4Parser_AdvanceNode(idx); }

} // namespace th07
