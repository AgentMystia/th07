// ----------------------------------------------------------------------------
//
// CMyFont.hpp - GDI / D3DX font wrapper
//
// IMPORTANT (verified against th07.exe, 2026-06-14):
//   th06's CMyFont used D3DXCreateFont + ID3DXFont::DrawText. th07.exe does
//   NOT import D3DXCreateFont / D3DXCreateFontIndirect / ID3DXFont at all (no
//   matching strings, no IAT entries). Instead th07 renders all in-game text
//   through a GDI-only path: CreateFontA + TextOutA onto a 32bpp DIB section
//   (CreateDIBSection + CreateCompatibleDC), then blits the DIB as a texture.
//
//   Consequence: there is NO byte-level CMyFont::Init / Print / Clean in th07.
//   The functions that perform the analogous role live in a different class
//   (a "text render target" / TextHelper equivalent) and have completely
//   different signatures and bodies. This file therefore preserves the th06
//   class shape (so the rest of the codebase can keep referring to CMyFont)
//   while documenting the divergence. It is intentionally NOT registered in
//   mapping.csv — th07 has no matching symbols to export.
//
//   The real th07 text-render entry points (different class, kept here for
//   reference):
//     0x00431a0f  text target reset/destructor stub
//     0x00431a5c  text target Clean  (SelectObject/DeleteDC/DeleteObject)
//     0x00431b2d  text target Init   (CreateDIBSection + CreateCompatibleDC)
//     0x00431ace  text target Init wrapper (format fallback)
//     0x004322a3  text target Print  (CreateFontA + 5x shadowed TextOutA)
//
// ----------------------------------------------------------------------------

#include <windows.h>
#include <d3d8.h>
#include <d3dx8.h>

#include "diffbuild.hpp"
#include "i18n.hpp"

namespace th06
{
// Kept here only as the historical reference shape. The th07 build does not
// emit any of these symbols.
#define RELEASE(o)                                                                                            \
    if (o)                                                                                                    \
    {                                                                                                         \
        o->Release();                                                                                         \
        o = NULL;                                                                                             \
    }
}

namespace th07
{

// th07 keeps the same font face name as th06 (see i18n.tpl:292). The actual
// CreateFontA call site is at 0x004322a3 and uses TH_FONT_NAME directly.
//   CreateFontA(h*2-2, 0, 0, 0, FW_BOLD (700), 0, 0, 0,
//               SHIFTJIS_CHARSET (0x80), 0, 0, DEFAULT_QUALITY (4),
//               FF_DONTCARE|DEFAULT_PITCH (0x11), TH_FONT_NAME);

class CMyFont
{
  private:
    // th06 layout. NOT present in th07 binary — kept for source compatibility.
    LPD3DXFONT m_lpFont;

  public:
    CMyFont()
    {
        m_lpFont = NULL;
    }

    // th06 signatures. th07 has no equivalent; bodies are stubs so the class
    // still links if anything references it during incremental porting.
    virtual void Init(LPDIRECT3DDEVICE8 lpD3DDEV, int w, int h);
    virtual void Print(char *str, int x, int y, D3DCOLOR color = 0xffffffff);
    virtual void Clean();
};

} // namespace th07
