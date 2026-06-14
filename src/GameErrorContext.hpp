#pragma once

#include <windows.h>

#include "diffbuild.hpp"
#include "i18n.hpp"
#include "inttypes.hpp"

namespace th07
{
class GameErrorContext;

// External raw file-write helper used by Flush. Defined elsewhere (Supervisor /
// FileSystem module at 0x00431540). Signature matches the th07 binary:
//   int __fastcall RawWriteFile(LPCSTR fileName, LPCVOID buffer, DWORD size);
// Returns 0 on success, -2 on short write, -1 on open failure.
i32 __fastcall RawWriteFile(LPCSTR fileName, LPCVOID buffer, DWORD size);

class GameErrorContext
{
  public:
    // th07 grew the buffer from th06's 0x800 to a full 0x2000 (8192) bytes.
    char m_Buffer[0x2000];
    char *m_BufferEnd;
    i8 m_ShowMessageBox;

    GameErrorContext()
    {
        m_BufferEnd = m_Buffer;
        m_Buffer[0] = '\0';
        // Required to get some mov eax, [m_Buffer_ptr]
        m_ShowMessageBox = false;
        Log(TH_ERR_LOGGER_START);
    }

    ~GameErrorContext()
    {
    }

    void ResetContext()
    {
        m_BufferEnd = m_Buffer;
        m_BufferEnd[0] = '\0';
        // TODO: check if it should be m_Buffer[0] above.
    }

    void Flush();

    const char *Fatal(const char *fmt, ...);
    const char *Log(const char *fmt, ...);
};

ZUN_ASSERT_SIZE(GameErrorContext, 0x2008);

DIFFABLE_EXTERN(GameErrorContext, g_GameErrorContext)
}; // namespace th07
