#pragma once

#include <windows.h>
#include <d3dx8math.h>

#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "ZunBool.hpp"
#include "diffbuild.hpp"
#include "i18n.hpp"
#include "inttypes.hpp"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define ARRAY_SIZE_SIGNED(x) ((i32)sizeof(x) / (i32)sizeof(x[0]))

#define ZUN_BIT(a) (1 << (a))
#define ZUN_MASK(a) (ZUN_BIT(a) - 1)
#define ZUN_RANGE(a, count) (ZUN_MASK((a) + (count)) & ~ZUN_MASK(a))
#define ZUN_CLEAR_BITS(a, keep_mask) (a & ~keep_mask)

namespace th07
{
// Cross-module helpers used by utils::CheckForRunningGameInstance. Their
// definitions live in the Supervisor module; only forward declarations are
// provided here so utils.cpp links against them. Signatures match the th07
// binary (all __fastcall):
//   FileExists       @ 0x004314f0  — tests whether a file can be opened.
//   ResolveShortcut  @ 0x00435fc0  — resolves a .lnk target via the shell.
ZunBool __fastcall FileExists(LPCSTR path);
i32 __fastcall ResolveShortcut(LPCSTR sourcePath, LPSTR resolvedPath, i32 bufSize);

// Global flag set by CheckForRunningGameInstance when the executable was
// launched from a directory different than where the running instance lives
// (e.g. via a .lnk shortcut). Lives at 0x00575c3c and is read by the
// Supervisor module. Declared extern here because utils owns the write.
extern u8 g_StartedFromDifferentAppDir;

namespace utils
{
ZunResult __fastcall CheckForRunningGameInstance(HINSTANCE hInstance);
void DebugPrint(const char *fmt, ...);
void DebugPrint2(const char *fmt, ...);

f32 __fastcall AddNormalizeAngle(f32 a, f32 b);
void __fastcall Rotate(D3DXVECTOR3 *outVector, D3DXVECTOR3 *point, f32 angle);
}; // namespace utils
}; // namespace th07
