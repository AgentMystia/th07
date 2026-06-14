#include <windows.h>

#include "GameErrorContext.hpp"
#include <stdio.h>

namespace th07
{
DIFFABLE_STATIC(GameErrorContext, g_GameErrorContext)

const char *GameErrorContext::Log(const char *fmt, ...)
{
    // th07 enlarged the formatting scratch buffer to 8192 bytes (th06 used 512).
    char tmpBuffer[8192];
    size_t tmpBufferSize;
    va_list args;

    va_start(args, fmt);
    vsprintf(tmpBuffer, fmt, args);

    tmpBufferSize = strlen(tmpBuffer);

    if (this->m_BufferEnd + tmpBufferSize < &this->m_Buffer[sizeof(this->m_Buffer) - 1])
    {
        strcpy(this->m_BufferEnd, tmpBuffer);

        this->m_BufferEnd += tmpBufferSize;
        *this->m_BufferEnd = '\0';
    }

    va_end(args);

    return fmt;
}

const char *GameErrorContext::Fatal(const char *fmt, ...)
{
    char tmpBuffer[512];
    size_t tmpBufferSize;
    va_list args;

    va_start(args, fmt);
    vsprintf(tmpBuffer, fmt, args);

    tmpBufferSize = strlen(tmpBuffer);

    if (this->m_BufferEnd + tmpBufferSize < &this->m_Buffer[sizeof(this->m_Buffer) - 1])
    {
        strcpy(this->m_BufferEnd, tmpBuffer);

        this->m_BufferEnd += tmpBufferSize;
        *this->m_BufferEnd = '\0';
    }

    va_end(args);

    this->m_ShowMessageBox = true;

    return fmt;
}

void GameErrorContext::Flush()
{
    if (m_BufferEnd != m_Buffer)
    {
        Log(TH_ERR_LOGGER_END);

        if (m_ShowMessageBox)
        {
            MessageBoxA(NULL, m_Buffer, "log", MB_ICONERROR);
        }

        // th07 writes the log through a raw Win32 helper (CreateFileA + WriteFile)
        // instead of th06's CRT fopen/fprintf pair. The helper lives in the
        // Supervisor/FileSystem module and requires the byte length, so we
        // strlen the accumulated buffer here.
        RawWriteFile("./log.txt", m_Buffer, strlen(m_Buffer));
    }
}
}; // namespace th07
