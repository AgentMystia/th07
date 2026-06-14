#ifdef DEBUG
#include <cstdarg>
#include <stdio.h>
#endif

#include <windows.h>
#include <string.h>

#include "GameErrorContext.hpp"
#include "ZunMath.hpp"
#include "i18n.hpp"
#include "utils.hpp"

namespace th07
{
DIFFABLE_STATIC(HANDLE, g_ExclusiveMutex)

namespace utils
{
ZunResult __fastcall CheckForRunningGameInstance(HINSTANCE hInstance)
{
    STARTUPINFOA startupInfo;
    char consoleTitle[0x105];
    char moduleFileName[0x105];
    char *ext;

    g_ExclusiveMutex = CreateMutexA(NULL, TRUE, TEXT("Touhou YouYouMu App"));

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        g_GameErrorContext.Fatal(TH_ERR_ALREADY_RUNNING);
        return ZUN_ERROR;
    }

    startupInfo.cb = sizeof(startupInfo);
    memset(&startupInfo.lpReserved, 0, sizeof(startupInfo) - sizeof(startupInfo.cb));
    GetModuleFileNameA(NULL, moduleFileName, 0x105);
    GetConsoleTitleA(consoleTitle, 0x105);
    GetStartupInfoA(&startupInfo);
    if (startupInfo.lpTitle != NULL)
    {
        ext = strrchr(startupInfo.lpTitle, '.');
        if (FileExists(startupInfo.lpTitle) != 0 && ext != NULL)
        {
            if (_stricmp(ext, ".lnk") == 0)
            {
                do
                {
                    ResolveShortcut(startupInfo.lpTitle, consoleTitle, 0x104);
                    ext = strrchr(consoleTitle, '.');
                } while (_stricmp(ext, ".lnk") == 0);
            }
            else
            {
                strcpy(consoleTitle, startupInfo.lpTitle);
            }

            if (strcmp(consoleTitle, moduleFileName) != 0)
            {
                g_StartedFromDifferentAppDir = 1;
            }
        }
    }

    if (g_ExclusiveMutex == NULL)
    {
        return ZUN_ERROR;
    }

    return ZUN_SUCCESS;
}

void DebugPrint(const char *fmt, ...)
{
#ifdef DEBUG
    char tmpBuffer[512];
    va_list args;

    va_start(args, fmt);
    vsprintf(tmpBuffer, fmt, args);
    va_end(args);

    printf("DEBUG2: %s\n", tmpBuffer);
#endif
}

f32 __fastcall AddNormalizeAngle(f32 a, f32 b)
{
    i32 i;

    i = 0;
    a += b;
    while (a > ZUN_PI)
    {
        a -= ZUN_2PI;
        if (i++ > 16)
        {
            break;
        }
    }
    while (a < -ZUN_PI)
    {
        a += ZUN_2PI;
        if (i++ > 16)
        {
            break;
        }
    }
    return a;
}

#pragma var_order(sinOut, cosOut)
void __fastcall Rotate(D3DXVECTOR3 *outVector, D3DXVECTOR3 *point, f32 angle)
{
    f32 sinOut;
    f32 cosOut;

    sinOut = sinf(angle);
    cosOut = cosf(angle);
    outVector->x = cosOut * point->x + sinOut * point->y;
    outVector->y = cosOut * point->y - sinOut * point->x;
}

void DebugPrint2(const char *fmt, ...)
{
#ifdef DEBUG
    char tmpBuffer[512];
    va_list args;

    va_start(args, fmt);
    vsprintf(tmpBuffer, fmt, args);
    va_end(args);

    printf("DEBUG2: %s\n", tmpBuffer);
#endif
}
}; // namespace utils
}; // namespace th07
