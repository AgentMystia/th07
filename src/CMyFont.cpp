// ----------------------------------------------------------------------------
//
// CMyFont.cpp - Font rendering
//
// See CMyFont.hpp for the critical divergence note: th07.exe does NOT contain
// a byte-level equivalent of th06's CMyFont (which used D3DXCreateFont +
// ID3DXFont::DrawText). th07 imports neither D3DXCreateFont nor
// D3DXCreateFontIndirect and renders all text through a GDI-only DIB-section
// pipeline (CreateFontA + TextOutA + CreateDIBSection). The corresponding th07
// functions live in a different class with different signatures.
//
// This translation unit therefore preserves the th06 source verbatim (it is the
// documented reference behaviour and may be reused by the SDL2 port) but is
// deliberately NOT registered in mapping.csv — there are no th07 symbols to
// match against, and emitting these would only produce zero-match objs.
//
// Real th07 text-render entry points (for the eventual real module):
//   0x00431a0f  text-target reset (writes 0xffffffff / 0 into struct fields)
//   0x00431a5c  text-target Clean (SelectObject + DeleteDC + DeleteObject)
//   0x00431b2d  text-target Init  (CreateDIBSection + CreateCompatibleDC)
//   0x00431ace  text-target Init wrapper (pixel-format fallback)
//   0x004322a3  text-target Print (CreateFontA + 5x TextOutA drop-shadow)
//
// ----------------------------------------------------------------------------

#include "CMyFont.hpp"
#include "i18n.hpp" // TH_FONT_NAME
#include <d3d8.h>

// GameWindow.hpp does not exist yet in this codebase. th06's values are used
// directly here (the th07 CMyFont class is documented as non-matching; see
// header). When the real GameWindow module lands, swap these for the macros.
namespace
{
constexpr int GAME_WINDOW_WIDTH = 640;
constexpr int GAME_WINDOW_HEIGHT = 480;
}

namespace th06
{

void CMyFont::Init(LPDIRECT3DDEVICE8 lpD3DDEV, int w, int h)
{
    HDC hTextDC = NULL;
    HFONT hFont = NULL, hOldFont = NULL;

    hTextDC = CreateCompatibleDC(NULL);
    hFont = CreateFont(h, w, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, TH_FONT_NAME);
    if (!hFont)
        return;
    hOldFont = (HFONT)SelectObject(hTextDC, hFont);

    if (FAILED(D3DXCreateFont(lpD3DDEV, hFont, &m_lpFont)))
    {
        MessageBox(0, "D3DXCreateFontIndirect FALSE", "ok", MB_OK);
        return;
    }
    SelectObject(hTextDC, hOldFont);
    DeleteObject(hFont);
}

void CMyFont::Print(char *str, int x, int y, D3DCOLOR color)
{
    RECT rect;
    rect.left = x;
    rect.right = GAME_WINDOW_WIDTH;
    rect.top = y;
    rect.bottom = GAME_WINDOW_HEIGHT;

    m_lpFont->DrawText(str, -1, &rect, DT_LEFT | DT_EXPANDTABS, color);
}

void CMyFont::Clean()
{
    RELEASE(m_lpFont);
}

} // namespace th06

namespace th07
{

// th07 stubs. These intentionally do nothing useful — see header. They exist
// only so the class is instantiable if any porting glue references it before
// the real th07 text-render class is checked in.

void CMyFont::Init(LPDIRECT3DDEVICE8 /*lpD3DDEV*/, int /*w*/, int /*h*/)
{
    m_lpFont = NULL;
}

void CMyFont::Print(char * /*str*/, int /*x*/, int /*y*/, D3DCOLOR /*color*/)
{
}

void CMyFont::Clean()
{
    if (m_lpFont)
    {
        m_lpFont->Release();
        m_lpFont = NULL;
    }
}

} // namespace th07
