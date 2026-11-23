#pragma once
// Texts of the user interface: the translation of LANG/<code>/OSC.TXT
// (common/lang.h) is applied to every string drawn through UI::Print /
// UI::Printf (to the format of Printf); Width() is the width of the
// translated text.
#include <library.h>
#include <stdarg.h>
#include "../../../common/lang.h"

namespace UI
{
  inline int WidthOf(const char* s) { return s ? Lang::Width(Lang::T(s)) : 0; }
}
// the controls of the OS framework (gui/Controls.h) draw their labels
// translated, centred by the number of characters
#define GUI_TEXT(s) Lang::T(s)
#define GUI_TEXT_WIDTH(s) UI::WidthOf(s)

namespace UI
{
  inline const char* T(const char* s) { return Lang::T(s); }
  inline int Width(const char* s) { return Lang::Width(Lang::T(s)); }
  inline int Print(int x, int y, unsigned short c, unsigned short b, const char* s) { return BIOS::LCD::Print(x, y, c, b, Lang::T(s)); }
  inline int Print(int x, int y, unsigned short c, unsigned short b, char* s) { return Print(x, y, c, b, (const char*)s); }
  inline int Print(int x, int y, unsigned short c, unsigned short b, char ch) { return BIOS::LCD::Print(x, y, c, b, ch); }
  inline int Printf(int x, int y, unsigned short c, unsigned short b, const char* format, ...)
  {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsprintf(buf, Lang::T(format), args);
    va_end(args);
    return BIOS::LCD::Print(x, y, c, b, buf);
  }
}
