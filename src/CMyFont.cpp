// CMyFont.cpp - th07 text render target (GDI-based, NOT th06's D3DXFont)
#include "CMyFont.hpp"
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include <windows.h>
#include <string.h>

namespace th07
{
// th07 text-target struct: 9 dwords (0x24 bytes). Defined here locally for
// objdiff; CMyFont.hpp keeps the th06 reference shape.
// CMyFont struct is declared in CMyFont.hpp (9 dwords = 0x24 bytes).

extern "C" void *__fastcall CMyFont_GetPixelFormat(i32 format); // FUN_00431cec
extern "C" CMyFont *g_TextTarget;

#pragma optimize("s", on)

CMyFont *__fastcall CMyFont::Reset()
{
    this->format = -1;
    this->width = 0;
    this->height = 0;
    this->stride = 0;
    this->prevObj = 0;
    this->bits = 0;
    this->hbitmap = 0;
    return this;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

i32 __fastcall CMyFont::Clean()
{
    i32 hdc = (i32)this->hdc;
    if (hdc != 0)
    {
        SelectObject((HDC)hdc, (HGDIOBJ)this->prevObj);
        DeleteDC((HDC)hdc);
        DeleteObject((HGDIOBJ)this->hbitmap);
        this->format = -1;
        this->width = 0;
        this->height = 0;
        this->stride = 0;
        this->hbitmap = 0;
        this->prevObj = 0;
        this->bits = 0;
    }
    return hdc != 0;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

i32 __fastcall CMyFont::Init(i32 w, i32 h, i32 format)
{
    this->Clean();
    BITMAPINFO bi;
    memset(&bi, 0, 0x6c);
    void *fmtDesc = CMyFont_GetPixelFormat(format);
    if (fmtDesc == 0)
    {
        return 0;
    }
    i32 bpp = *(i32 *)((u8 *)fmtDesc + 4);
    i32 stride = (((w * bpp) / 8 + 3) / 4) * 4;
    bi.bmiHeader.biSize = 0x6c;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -(h + 1);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = (WORD)bpp;
    bi.bmiHeader.biSizeImage = h * stride;
    if (format != 0x18 && format != 0x16)
    {
        bi.bmiHeader.biCompression = 3;
        bi.bmiColors[0] = *(RGBQUAD *)((u8 *)fmtDesc + 0xc);
    }
    void *bits;
    HBITMAP hbmp = CreateDIBSection((HDC)0, &bi, 0, &bits, (HANDLE)0, 0);
    if (hbmp == (HBITMAP)0)
    {
        return 0;
    }
    memset(bits, 0, bi.bmiHeader.biSizeImage);
    HDC hdc = CreateCompatibleDC((HDC)0);
    HGDIOBJ prev = SelectObject(hdc, hbmp);
    this->hdc = hdc;
    this->hbitmap = hbmp;
    this->bits = bits;
    this->imageSize = bi.bmiHeader.biSizeImage;
    this->prevObj = prev;
    this->width = w;
    this->height = h;
    this->format = format;
    this->stride = stride;
    return 1;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

i32 __fastcall CMyFont::InitWrapper(i32 w, i32 h, i32 format)
{
    if (g_TextTarget->Init(w, h, format) != 0)
    {
        return 1;
    }
    if (format == 0x19 || format == 0x1a)
    {
        return g_TextTarget->Init(w, h, 0x15);
    }
    if (format == 0x17)
    {
        return g_TextTarget->Init(w, h, 0x16);
    }
    return 0;
}

#pragma optimize("s", off)
#pragma optimize("s", on)

void __fastcall CMyFont::Print(i32 a, i32 b, i32 c, i32 d, i32 e, char *s)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)s;
}

#pragma optimize("s", off)

} // namespace th07
