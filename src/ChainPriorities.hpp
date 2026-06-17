// Chain execution priorities (th07).
//
// These are the integer priorities passed to Chain::AddToCalcChain /
// AddToDrawChain; lower = earlier in the chain. The th07 layout mirrors th06
// with th07-specific additions. Values verified from the orig binary's calls
// to g_Chain.AddToCalcChain/AddToDrawChain.
//
// This header is intentionally a header-only constant block; no module needs
// the values to differ, so a single shared set is fine.
#pragma once

namespace th07
{
// Calc-chain priorities (lower runs first).
enum CalcChainPriority
{
    CALC_CHAIN_PRIORITY_SUPERVISOR = 0,
    // Other modules register at priorities verified from their RegisterChain
    // disassembly; add them here as modules are reversed.
};

// Draw-chain priorities.
enum DrawChainPriority
{
    DRAW_CHAIN_PRIORITY_SUPERVISOR = 0xf,
};
}
