#pragma once

#include <windows.h>

#include "ZunResult.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include "pbg4/Pbg4Archive.hpp"

namespace th07
{
// Forward declaration of the global game error context. Defined in
// GameErrorContext.cpp; placed at 0x00624210 in the th07 binary.
class GameErrorContext;

// ArchiveEntryTable is the global handle FileSystem uses to look up files
// inside th07.dat. In the original binary it is a small struct at 0x00626258
// whose +0x00/+0x04/+0x08/+0x0c slots point at the entry list / file data /
// IStream reader. The full codec lives in the Pbg4 module (FUN_0045fb50 etc.)
// and the objdiff-matching typed lift of that struct lives there.
//
// For the normal build we keep the same public surface -- GetEntrySize /
// ReadEntry -- but back them with our working Pbg4Archive reader. The global
// g_ArchiveEntries below is zero-initialised; Supervisor (or anyone else
// loading a packed resource) must call g_ArchiveEntries.Open("th07.dat")
// before the first resource read.
struct ArchiveEntryTable
{
    // Returns the decompressed size of the named entry, or 0 if missing or
    // if the archive has not been opened. (orig FUN_0045fab0.)
    u32 GetEntrySize(char *entryName);
    // Decompresses the named entry into outBuffer (must be sized via
    // GetEntrySize). (orig FUN_0045f960.) Returns 1 on success, 0 on failure.
    void ReadEntry(char *entryName, void *outBuffer);

    Pbg4Archive archive; // underlying reader (normal build)
};
DIFFABLE_EXTERN(ArchiveEntryTable, g_ArchiveEntries)

namespace FileSystem
{
// Loads a file, either from a packed archive (when isExternalResource == 0) or
// directly from disk. When reading from the archive, the file name is stripped
// to its base component (after the last '\\' or '/') and looked up in the
// global archive entry table (g_ArchiveEntries at 0x00626258). The loaded byte
// count is stored into g_LastFileSize. Returns a malloc'd buffer the caller
// must free, or NULL on failure.
//
// th07 differs from th06 here: instead of iterating over g_Pbg3Archives[16]
// and using CRT fopen/fread, it queries a single global archive entry table
// and reads disk files through CreateFileA/GetFileSize/ReadFile. The new
// "is not found in arcfile" error message replaces th06's silent NULL return.
u8 *__fastcall OpenPath(char *filepath, i32 isExternalResource);
} // namespace FileSystem

// Raw file writer used by GameErrorContext::Flush and by the score/replay
// savers. Wraps CreateFileA + WriteFile and returns 0 on success, -2 on a
// short write, -1 on open failure. This is the th07 successor to th06's
// FileSystem::WriteDataToFile; it was lifted out of the FileSystem namespace
// into a free function and switched from fopen/fwrite to the Win32 API.
//
// Binary location: 0x00431540. Declared here (and re-declared in
// GameErrorContext.hpp for that module's convenience) so both the FileSystem
// and GameErrorContext translation units can link against the single
// definition in FileSystem.cpp.
i32 __fastcall RawWriteFile(LPCSTR fileName, LPCVOID buffer, DWORD size);

// Size of the most recently loaded file. Written by FileSystem::OpenPath and
// read all over the binary (e.g. Supervisor's thbgm.fmt validation). Lives at
// 0x004b9e64 in the th07 binary.
DIFFABLE_EXTERN(u32, g_LastFileSize)
}; // namespace th07
