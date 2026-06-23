#include <windows.h>
#include <string.h>

#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "utils.hpp"

namespace th07
{
DIFFABLE_STATIC(u32, g_LastFileSize)

namespace FileSystem
{
#pragma var_order(entryname, fsize, data, file)
u8 *__fastcall OpenPath(char *filepath, i32 isExternalResource)
{
    u8 *data;
    HANDLE file;
    DWORD fsize;
    char *entryname;

    if (isExternalResource == 0)
    {
        // Strip the path down to the base file name. th07 chains strrchr for
        // '\\' and then '/', exactly like th06.
        entryname = strrchr(filepath, '\\');
        if (entryname == NULL)
        {
            entryname = filepath;
        }
        else
        {
            entryname = entryname + 1;
        }
        entryname = strrchr(entryname, '/');
        if (entryname == NULL)
        {
            entryname = filepath;
        }
        else
        {
            entryname = entryname + 1;
        }

        // th07 looks the name up against a single global archive entry table
        // instead of th06's g_Pbg3Archives[16] loop. GetEntrySize returns 0
        // when the entry is missing.
        g_LastFileSize = g_ArchiveEntries.GetEntrySize(entryname);
        fsize = g_LastFileSize;
        if (fsize == 0)
        {
            g_GameErrorContext.Log("error : %s is not found in arcfile.\r\n", entryname);
            return NULL;
        }
        // The original th07 binary carries the entryIdx sentinel through the
        // control flow, producing this redundant non-zero re-check. Kept here
        // to match objdiff block-for-block.
        if (fsize != 0)
        {
            utils::DebugPrint("%s Decode ... \r\n", entryname);
            data = (u8 *)malloc(fsize);
            if (data == NULL)
            {
                return NULL;
            }
            g_ArchiveEntries.ReadEntry(entryname, data);
            return data;
        }
    }

    utils::DebugPrint("%s Load ... \r\n", filepath);
    // th07 switched from th06's fopen/fread to the Win32 file API. The flags
    // below decode to GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, and
    // FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS.
    file = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        utils::DebugPrint("error : %s is not found.\r\n", filepath);
        data = NULL;
    }
    else
    {
        fsize = GetFileSize(file, NULL);
        data = (u8 *)malloc(fsize);
        if (data == NULL)
        {
            CloseHandle(file);
            data = NULL;
        }
        else
        {
            ReadFile(file, data, fsize, &fsize, NULL);
            g_LastFileSize = fsize;
            CloseHandle(file);
        }
    }
    return data;
}
} // namespace FileSystem

// th07's replacement for th06's FileSystem::WriteDataToFile. Lifted into a
// free function and rewritten on top of the Win32 API (CreateFileA/WriteFile).
// Callers: GameErrorContext::Flush (writes ./log.txt), and the score.dat /
// replay savers. Returns 0 on success, -2 on a short write, -1 if the file
// could not be opened.
i32 __fastcall RawWriteFile(LPCSTR fileName, LPCVOID buffer, DWORD size)
{
    HANDLE hFile;
    DWORD bytesWritten;

    hFile = CreateFileA(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        utils::DebugPrint("error : %s write error\r\n", fileName);
        return -1;
    }
    WriteFile(hFile, buffer, size, &bytesWritten, NULL);
    if (size != bytesWritten)
    {
        CloseHandle(hFile);
        utils::DebugPrint("error : %s write error\r\n", fileName);
        return -2;
    }
    CloseHandle(hFile);
    utils::DebugPrint("%s write ...\r\n", fileName);
    return 0;
}

// =============================================================================
// ArchiveEntryTable::GetEntrySize / ReadEntry -- thin wrappers around the
// underlying Pbg4Archive reader (normal build). The objdiff build does not
// link this translation unit's bodies; orig FUN_0045fab0 / FUN_0045f960 live
// in the not-yet-lifted Pbg4 codec module and are resolved via mapping.csv.
// =============================================================================
u32 ArchiveEntryTable::GetEntrySize(char *entryName)
{
    return archive.GetEntrySize(entryName);
}
void ArchiveEntryTable::ReadEntry(char *entryName, void *outBuffer)
{
    archive.ReadEntry(entryName, outBuffer);
}

// =============================================================================
// P0 link-pass: g_ArchiveEntries definition. The real initialiser lives in
// Pbg4Archive; until that module lands we zero-init the singleton here.
// =============================================================================
DIFFABLE_STATIC(ArchiveEntryTable, g_ArchiveEntries);
}; // namespace th07
