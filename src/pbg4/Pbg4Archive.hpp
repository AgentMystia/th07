// Pbg4Archive.hpp - th07 PBG4 archive reader (working Win32 implementation).
//
// This is the *runtime* implementation used by the normal build to actually
// load assets out of th07.dat. The objdiff-tracked typed-C++ lift of the
// original codec still lives in Pbg4Parser.cpp (LZSS dictionary helpers);
// this header + the working .cpp below give the normal build a fully
// functioning archive reader.
//
// On-disk format (reverse-engineered from th07.dat and verified byte-exact):
//
//   offset 0x00  char[4]   magic = "PBG4"
//   offset 0x04  u32       entryCount
//   offset 0x08  u32       entryTableOffset  (file offset of the table block)
//   offset 0x0c  u32       entryTableSize    (uncompressed size of the table)
//
// The entry table block (entryTableSize bytes, LZSS-compressed in-file; the
// compressed bytes run from entryTableOffset to EOF) decompresses to a flat
// blob of records, one per entry. Each record is:
//     char[]    name   (NUL-terminated, variable length)
//     u32       offset (absolute file offset of this entry's compressed data)
//     u32       size   (uncompressed size of this entry)
//     u32       unk    (always 0 in every shipping th07.dat)
// so record stride = strlen(name) + 1 + 12.
//
// Compressed data blocks are laid out back-to-back, sorted by offset. An
// entry's compressed byte count is therefore derived from the next entry's
// offset (or EOF for the last entry). Each block is LZSS-encoded with a
// 13-bit offset / 4-bit length / 0x2000-byte dictionary, identical to th06
// Pbg3's LZSS; the dictionary is reset to all-zero before each entry.
//
// The reader is backed by a Win32 HANDLE so it can re-seek on every
// ReadEntry without caching the whole file (th07.exe also re-reads per call).
#pragma once

#include <windows.h>

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
struct Pbg4Entry
{
    char *name;        // malloc'd copy of the entry name (NUL-terminated)
    u32 offset;        // absolute file offset of this entry's compressed data
    u32 size;          // uncompressed size of this entry
    u32 compressedSize; // derived: next entry's offset - this offset (EOF for last)
};

class Pbg4Archive
{
  public:
    Pbg4Archive();
    ~Pbg4Archive();

    // Opens the archive at the given path, parses the header, decompresses
    // the entry table block, and builds the in-memory entry list (sorted by
    // offset so compressedSize can be derived). Returns 1 on success, 0 on
    // any failure.
    i32 Open(char *path);

    // Releases the file handle and frees the entry list.
    void Close();

    // Returns the decompressed size of the named entry, or 0 if not found
    // (or if the archive was never opened).
    u32 GetEntrySize(char *name);

    // Decompresses the named entry into outBuffer (caller-allocated, must be
    // sized via GetEntrySize). Returns 1 on success, 0 on failure.
    i32 ReadEntry(char *name, void *outBuffer);

    u32 entryCount;          // number of entries (public for boot diagnostics)

  private:
    // Reads exactly `count` bytes from the open HANDLE into `buf`, starting at
    // `fileOffset`. Returns 1 on success.
    i32 ReadAt(u32 fileOffset, void *buf, u32 count);

    // LZSS decompression. `in` is `inSize` bytes of compressed data; the
    // decompressor writes exactly `outSize` bytes into `out` and returns 1 on
    // success. The 0x2000-byte dictionary is reset to zero before each call.
    static i32 Decompress(u8 *in, u32 inSize, u8 *out, u32 outSize);

    HANDLE fileHandle;       // Win32 handle onto th07.dat
    Pbg4Entry *entries;      // malloc'd array of entryCount entries, sorted by offset
};
} // namespace th07