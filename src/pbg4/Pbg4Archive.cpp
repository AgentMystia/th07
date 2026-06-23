// Pbg4Archive.cpp - th07 PBG4 archive reader (working Win32 implementation).
//
// Runtime reader for th07.dat. See Pbg4Archive.hpp for the on-disk format.
// The decompressor mirrors th06's Pbg3 LZSS (13-bit offset / 4-bit length,
// 0x2000-byte dictionary) which FUN_0045ef00 confirmed is byte-identical in
// th07.
//
// This file is part of the normal build only. objdiff does not consume it.
#include "Pbg4Archive.hpp"

#include <stdlib.h>
#include <string.h>

// Debug-log helpers (defined in link_stubs.cpp). Declared at file scope so the
// `extern "C"` linkage spec is valid (C2598 forbids it inside function bodies).
extern "C" void *__cdecl th07_fopen_w(const char *path, const char *mode);
extern "C" void __cdecl th07_fprintf(void *fp, const char *fmt, ...);
extern "C" void __cdecl th07_fclose(void *fp);

namespace th07
{
// LZSS parameters shared with the original codec (FUN_0045ef00).
#define PBG4_LZSS_DICTSIZE 0x2000
#define PBG4_LZSS_DICTMASK 0x1fff

// PBG4 on-disk header.
#define PBG4_MAGIC 0x34474250u // 'P','B','G','4' little-endian

// Bit-grabbing macros (mirrors th06 Pbg3Archive::ReadDecompressEntry macros).
// We keep them as macros so the bit-twiddling stays inline and matches the
// orig codec's flag/op decode exactly.
//
// inCursor / inAtEnd are captured by reference from the enclosing Decompress
// scope. Once the input buffer is exhausted we stop advancing inCursor and
// treat every subsequent bit read as 0; this guarantees we eventually decode
// the 13-bit offset==0 EOS marker (all zero bits) instead of looping forever
// on the trailing byte.
#define DEC_BITMASK_NEXT()                                                                                                 \
    inBitMask >>= 1;                                                                                                       \
    if (inBitMask == 0)                                                                                                    \
    {                                                                                                                      \
        inBitMask = 0x80;                                                                                                  \
    }

#define DEC_FETCH_NEW_BYTE()                                                                                               \
    if (inBitMask == 0x80)                                                                                                 \
    {                                                                                                                      \
        if ((u32)(inCursor - in) < inSize)                                                                                 \
        {                                                                                                                  \
            currByte = *inCursor;                                                                                          \
            inCursor++;                                                                                                    \
        }                                                                                                                  \
        else                                                                                                               \
        {                                                                                                                  \
            currByte = 0;                                                                                                  \
        }                                                                                                                  \
    }                                                                                                                      \
    else if ((u32)(inCursor - in) >= inSize)                                                                               \
    {                                                                                                                      \
        /* Past EOF and not on a fetch boundary: clear the stale last byte so                                              \
         every subsequent bit reads as 0, guaranteeing we decode the offset==0                                             \
         EOS marker. */                                                                                                    \
        currByte = 0;                                                                                                      \
    }

#define DEC_READ_FLAG_BIT()                                                                                                \
    DEC_FETCH_NEW_BYTE();                                                                                                  \
    opcode = currByte & inBitMask;                                                                                         \
    DEC_BITMASK_NEXT();

#define DEC_READ_BITS(bitCount)                                                                                            \
    outBitMask = 1u << (bitCount - 1);                                                                                     \
    inBits = 0;                                                                                                            \
    while (outBitMask != 0)                                                                                                \
    {                                                                                                                      \
        DEC_FETCH_NEW_BYTE();                                                                                              \
        if ((currByte & inBitMask) != 0)                                                                                   \
        {                                                                                                                  \
            inBits |= outBitMask;                                                                                          \
        }                                                                                                                  \
        outBitMask >>= 1;                                                                                                  \
        DEC_BITMASK_NEXT();                                                                                                \
    }

Pbg4Archive::Pbg4Archive()
{
    fileHandle = INVALID_HANDLE_VALUE;
    entryCount = 0;
    entries = 0;
}

Pbg4Archive::~Pbg4Archive()
{
    Close();
}

void Pbg4Archive::Close()
{
    if (entries != 0)
    {
        for (u32 i = 0; i < entryCount; i++)
        {
            if (entries[i].name != 0)
            {
                free(entries[i].name);
                entries[i].name = 0;
            }
        }
        free(entries);
        entries = 0;
    }
    entryCount = 0;
    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(fileHandle);
        fileHandle = INVALID_HANDLE_VALUE;
    }
}

i32 Pbg4Archive::ReadAt(u32 fileOffset, void *buf, u32 count)
{
    DWORD outRead = 0;
    if (SetFilePointer(fileHandle, (LONG)fileOffset, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        return 0;
    }
    if (!ReadFile(fileHandle, buf, count, &outRead, 0))
    {
        return 0;
    }
    return outRead == count;
}

i32 Pbg4Archive::Decompress(u8 *in, u32 inSize, u8 *out, u32 outSize)
{
    static u8 dict[PBG4_LZSS_DICTSIZE];
    void *dbg = th07_fopen_w("boot_debug.log", "a");

    // Zero the dictionary (memset is fine -- this is the runtime path, not the
    // objdiff-matched orig idiom).
    memset(dict, 0, sizeof(dict));

    u8 *inCursor = in;
    u8 *outCursor = out;
    u8 *outEnd = out + outSize;
    u8 inBitMask = 0x80;
    u32 currByte = 0;
    u32 dictHead = 1;
    u32 opcode;
    u32 inBits;
    u32 outBitMask;
    u32 matchOffset;
    for (;;)
    {
        // Safety: if we've consumed all input AND produced all output, stop.
        // The orig codec relies on the offset==0 EOS marker but some trailing
        // padding can leave a stale byte in the bit register; bail rather
        // than spin.
        if ((u32)(inCursor - in) >= inSize && (u32)(outCursor - out) >= outSize)
        {
            break;
        }
        DEC_READ_FLAG_BIT();

        // Literal byte: read 8 bits, write to output + dictionary.
        if (opcode != 0)
        {
            DEC_READ_BITS(8);
            // Stop writing once the output buffer is full but keep consuming
            // the input stream until the offset==0 EOS marker is seen. The
            // orig codec does the same: it writes into a fixed-size buffer
            // and the table blob is sized to exactly entryTableSize bytes.
            if (outCursor < outEnd)
            {
                *outCursor++ = (u8)inBits;
                dict[dictHead] = (u8)inBits;
                dictHead = (dictHead + 1) & PBG4_LZSS_DICTMASK;
            }
            else
            {
                dict[dictHead] = (u8)inBits;
                dictHead = (dictHead + 1) & PBG4_LZSS_DICTMASK;
            }
        }
        // Dictionary reference: 13-bit offset, 4-bit length.
        else
        {
            DEC_READ_BITS(13);
            matchOffset = inBits;
            // offset == 0 marks end of stream.
            if (matchOffset == 0)
            {
                break;
            }
            DEC_READ_BITS(4);
            for (u32 i = 0; i <= inBits + 2; i++)
            {
                u8 c = dict[(matchOffset + i) & PBG4_LZSS_DICTMASK];
                if (outCursor < outEnd)
                {
                    *outCursor++ = c;
                }
                dict[dictHead] = c;
                dictHead = (dictHead + 1) & PBG4_LZSS_DICTMASK;
            }
        }
    }
    if (dbg) { th07_fprintf(dbg, "[dec] OK produced %ld/%u inCursor=%ld/%u\n", (long)(outCursor-out), outSize, (long)(inCursor-in), inSize); th07_fclose(dbg); }
    return 1;
}

i32 Pbg4Archive::Open(char *path)
{
    void *dbg = th07_fopen_w("boot_debug.log", "a");

    Close();

    fileHandle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] CreateFileA('%s') FAILED gle=0x%lx\n", path, GetLastError()); th07_fclose(dbg); }
        return 0;
    }
    if (dbg) th07_fprintf(dbg, "[arc] CreateFileA('%s') ok\n", path);

    u8 header[16];
    if (!ReadAt(0, header, 16))
    {
        if (dbg) { th07_fprintf(dbg, "[arc] ReadAt(header) FAILED\n"); th07_fclose(dbg); }
        Close();
        return 0;
    }
    if (*(u32 *)header != PBG4_MAGIC)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] BAD MAGIC 0x%08lx\n", *(u32 *)header); th07_fclose(dbg); }
        Close();
        return 0;
    }
    entryCount = *(u32 *)(header + 4);
    u32 entryTableOffset = *(u32 *)(header + 8);
    u32 entryTableSize = *(u32 *)(header + 12);
    if (dbg) th07_fprintf(dbg, "[arc] hdr: entries=%u tableOff=0x%x tableSize=0x%x\n",
                          entryCount, entryTableOffset, entryTableSize);

    // Compute the compressed table block size: it runs from entryTableOffset
    // to EOF (the table is the trailing block of the file).
    DWORD fileSizeLo = GetFileSize(fileHandle, 0);
    if (fileSizeLo == INVALID_FILE_SIZE || fileSizeLo <= entryTableOffset)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] bad fileSize=%lu\n", fileSizeLo); th07_fclose(dbg); }
        Close();
        return 0;
    }
    u32 compSize = fileSizeLo - entryTableOffset;

    // Read the compressed table block.
    u8 *compTable = (u8 *)malloc(compSize);
    if (compTable == 0)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] malloc(compTable=%u) FAILED\n", compSize); th07_fclose(dbg); }
        Close();
        return 0;
    }
    if (!ReadAt(entryTableOffset, compTable, compSize))
    {
        if (dbg) { th07_fprintf(dbg, "[arc] ReadAt(table off=0x%x comp=%u) FAILED\n", entryTableOffset, compSize); th07_fclose(dbg); }
        free(compTable);
        Close();
        return 0;
    }
    if (dbg) th07_fprintf(dbg, "[arc] read table block compSize=%u\n", compSize);

    // Decompress into a scratch buffer of entryTableSize bytes.
    u8 *tableBlob = (u8 *)malloc(entryTableSize + 16);
    if (tableBlob == 0)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] malloc(tableBlob=%u) FAILED\n", entryTableSize); th07_fclose(dbg); }
        free(compTable);
        Close();
        return 0;
    }
    if (!Decompress(compTable, compSize, tableBlob, entryTableSize))
    {
        if (dbg) { th07_fprintf(dbg, "[arc] Decompress FAILED (comp=%u dec=%u)\n", compSize, entryTableSize); th07_fclose(dbg); }
        free(compTable);
        free(tableBlob);
        Close();
        return 0;
    }
    free(compTable);
    if (dbg) th07_fprintf(dbg, "[arc] decompressed table OK (%u bytes)\n", entryTableSize);

    // Allocate the in-memory entry array.
    entries = (Pbg4Entry *)malloc(sizeof(Pbg4Entry) * entryCount);
    if (entries == 0)
    {
        if (dbg) { th07_fprintf(dbg, "[arc] malloc(entries=%u) FAILED\n", entryCount); th07_fclose(dbg); }
        free(tableBlob);
        Close();
        return 0;
    }
    memset(entries, 0, sizeof(Pbg4Entry) * entryCount);

    // Walk the decompressed blob. Each record is:
    //   char[] name (NUL-terminated)
    //   u32    offset
    //   u32    size
    //   u32    unk (always 0)
    // Record stride = strlen(name) + 1 + 12.
    u8 *cursor = tableBlob;
    u8 *blobEnd = tableBlob + entryTableSize;
    for (u32 i = 0; i < entryCount; i++)
    {
        if (cursor >= blobEnd)
        {
            if (dbg) { th07_fprintf(dbg, "[arc] walk[%u] cursor>=blobEnd\n", i); th07_fclose(dbg); }
            free(tableBlob);
            Close();
            return 0;
        }
        u8 *nameStart = cursor;
        while (cursor < blobEnd && *cursor != 0)
        {
            cursor++;
        }
        if (cursor >= blobEnd)
        {
            if (dbg) { th07_fprintf(dbg, "[arc] walk[%u] name runs off end\n", i); th07_fclose(dbg); }
            free(tableBlob);
            Close();
            return 0;
        }
        u32 nameLen = (u32)(cursor - nameStart);
        cursor++; // skip NUL

        if (cursor + 12 > blobEnd)
        {
            if (dbg) { th07_fprintf(dbg, "[arc] walk[%u] need 12 bytes, only %ld left\n", i, (long)(blobEnd - cursor)); th07_fclose(dbg); }
            free(tableBlob);
            Close();
            return 0;
        }
        u32 offset = *(u32 *)(cursor + 0);
        u32 size = *(u32 *)(cursor + 4);
        // u32 unk = *(u32 *)(cursor + 8); // always 0; unused
        cursor += 12;

        entries[i].name = (char *)malloc(nameLen + 1);
        if (entries[i].name == 0)
        {
            free(tableBlob);
            Close();
            return 0;
        }
        memcpy(entries[i].name, nameStart, nameLen);
        entries[i].name[nameLen] = 0;
        entries[i].offset = offset;
        entries[i].size = size;
        entries[i].compressedSize = 0; // derived below
    }

    free(tableBlob);

    // Derive compressedSize by sorting entries by offset. Compressed data is
    // laid out back-to-back, so entry[i].compressedSize = entry[i+1].offset -
    // entry[i].offset. The last entry's compressed data runs to EOF.
    // Simple insertion sort (entryCount is small -- 197 in shipping th07.dat).
    for (u32 i = 1; i < entryCount; i++)
    {
        Pbg4Entry cur = entries[i];
        i32 j = (i32)i - 1;
        while (j >= 0 && entries[j].offset > cur.offset)
        {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = cur;
    }
    for (u32 i = 0; i + 1 < entryCount; i++)
    {
        entries[i].compressedSize = entries[i + 1].offset - entries[i].offset;
    }
    if (entryCount > 0)
    {
        entries[entryCount - 1].compressedSize = fileSizeLo - entries[entryCount - 1].offset;
    }

    if (dbg)
    {
        th07_fprintf(dbg, "[arc] parsed %u entries, sorted; first='%s' off=0x%x sz=%u comp=%u\n",
                     entryCount,
                     entryCount > 0 ? entries[0].name : (char *)"(none)",
                     entryCount > 0 ? entries[0].offset : 0,
                     entryCount > 0 ? entries[0].size : 0,
                     entryCount > 0 ? entries[0].compressedSize : 0);
        th07_fclose(dbg);
    }
    return 1;
}

u32 Pbg4Archive::GetEntrySize(char *name)
{
    for (u32 i = 0; i < entryCount; i++)
    {
        if (entries[i].name != 0 && _stricmp(entries[i].name, name) == 0)
        {
            return entries[i].size;
        }
    }
    return 0;
}

i32 Pbg4Archive::ReadEntry(char *name, void *outBuffer)
{
    Pbg4Entry *entry = 0;
    for (u32 i = 0; i < entryCount; i++)
    {
        if (entries[i].name != 0 && _stricmp(entries[i].name, name) == 0)
        {
            entry = &entries[i];
            break;
        }
    }
    if (entry == 0)
    {
        return 0;
    }

    u8 *comp = (u8 *)malloc(entry->compressedSize + 16);
    if (comp == 0)
    {
        return 0;
    }
    if (!ReadAt(entry->offset, comp, entry->compressedSize))
    {
        free(comp);
        return 0;
    }

    i32 ok = Decompress(comp, entry->compressedSize, (u8 *)outBuffer, entry->size);
    free(comp);
    return ok;
}
} // namespace th07
