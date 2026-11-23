#include "Bios.h"
#include "font.h"
#include "fontuni.h"

namespace SPF {
#include <../../../os_host/library/spf/spf.h>
}

#define FLIP(y) (BIOS::LCD::Height-1-(y))

// TODO: global namespace
int _DrawChar(int x, int y, unsigned short clrf, unsigned short clrb, char ch);

void BIOS::LCD::Clear(unsigned short clr)
{
	BIOS::LCD::Bar(0, 0, BIOS::LCD::Width, BIOS::LCD::Height, clr);
	
}

void BIOS::LCD::Bar(int x1, int y1, int x2, int y2, unsigned short clr)
{
  _ASSERT(x2-x1 >= 1 && y2-y1 >= 1);
  int t = FLIP(y1)+1;
  y1 = FLIP(y2)+1;
  y2 = t;

  for(int x=x1; x<x2; x++)
    for(int y=y1; y<y2; y++)
    {
      Set_Posi(x, y);
      Set_Pixel(clr);
    }
}

void BIOS::LCD::Bar(const CRect& rc, unsigned short clr)
{
  BIOS::LCD::Bar(rc.left, rc.top, rc.right, rc.bottom, clr);
}

void BIOS::LCD::PutPixel(int x, int y, unsigned short clr)
{
  Set_Posi(x, FLIP(y));
  Set_Pixel(clr);
}

void BIOS::LCD::PutPixel(const CPoint& cp, unsigned short clr)
{
  Set_Posi(cp.x, FLIP(cp.y));
  Set_Pixel(clr);
}

unsigned short BIOS::LCD::GetPixel(int x, int y)
{
    Set_Posi(x, FLIP(y));
    return Get_Pixel();
}

int _DrawGlyph(int x, int y, unsigned short clrf, unsigned short clrb, const unsigned char *pFont);

// UTF-8 text: a valid sequence is a Unicode code point, any other byte
// (a single byte >= 0x80, the code of the CP437 font as before) is drawn
// as it is
static uint32_t _Utf8Next(const char*& s)
{
	const uint8_t* p = (const uint8_t*)s;
	uint8_t b = p[0];
	if (b >= 0xc2 && b <= 0xdf && (p[1] & 0xc0) == 0x80)
	{
		s += 2;
		return ((b & 0x1f) << 6) | (p[1] & 0x3f);
	}
	if (b >= 0xe0 && b <= 0xef && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80)
	{
		uint32_t c = ((b & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
		if (c >= 0x800)
		{
			s += 3;
			return c;
		}
	}
	if (b >= 0xf0 && b <= 0xf4 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80)
	{
		s += 4;
		return ((b & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
	}
	s++;
	return b < 0x80 ? b : 0x110000 | b;   // a raw byte of the CP437 font
}

// glyph (14 rows) of a code point
static const unsigned char* _GlyphUni(uint32_t c)
{
	if (c < 0x80)
		return GetFont((ui8)c);
	if (c >= 0x110000)
		return GetFont((ui8)c);
	int lo = 0, hi = sizeof(fontUniCp437) / sizeof(fontUniCp437[0]) - 1;
	while (lo <= hi)
	{
		int m = (lo + hi) >> 1;
		if (fontUniCp437[m][0] == c) return GetFont((ui8)fontUniCp437[m][1]);
		if (fontUniCp437[m][0] < c) lo = m + 1; else hi = m - 1;
	}
	lo = 0; hi = sizeof(fontUniExtraCp) / sizeof(fontUniExtraCp[0]) - 1;
	while (lo <= hi)
	{
		int m = (lo + hi) >> 1;
		if (fontUniExtraCp[m] == c) return fontUniExtra + m * 14;
		if (fontUniExtraCp[m] < c) lo = m + 1; else hi = m - 1;
	}
	return GetFont('?');
}

int BIOS::LCD::Print (int x, int y, unsigned short clrf, unsigned short clrb, char c)
{
    return _DrawChar(x, y, clrf, clrb, c);
}

int BIOS::LCD::Print (int x, int y, unsigned short clrf, unsigned short clrb, const char *str)
{
	if (!str || !*str)
		return 0;
	int _x = x;
	while (*str)
	{
		uint32_t c = _Utf8Next(str);
		if (c == '\n')
		{
			x = _x;
			y += 16;
			continue;
		}
		x += _DrawGlyph(x, y, clrf, clrb, _GlyphUni(c));
	}
	return x - _x;
}

int BIOS::LCD::Print (int x, int y, unsigned short clrf, unsigned short clrb, char *str)
{
	return BIOS::LCD::Print (x, y, clrf, clrb, (const char*)str);
}

int BIOS::LCD::Printf(int x, int y, unsigned short clrf, unsigned short clrb, const char * format, ...)
{
    char buf[128];
    char* bbuf = buf;
    
    va_list args;
	    
    va_start( args, format );
    SPF::vsprintf( bbuf, format, args );
    va_end(args);

    return BIOS::LCD::Print(x, y, clrf, clrb, buf);
}


int _DrawChar(int x, int y, unsigned short clrf, unsigned short clrb, char ch)
{
	return _DrawGlyph(x, y, clrf, clrb, GetFont(ch));
}

int _DrawGlyph(int x, int y, unsigned short clrf, unsigned short clrb, const unsigned char *pFont)
{
	if (clrb == RGBTRANS)
	{
		for (ui8 _y=0; _y<14; _y++)
		{
			ui8 col = *pFont++;
	
			for (ui8 _x=0; _x<8; _x++, col <<= 1)
				if ( col & 128 )
					BIOS::LCD::PutPixel(x+_x, y+_y, clrf);
		}
	} else if (clrf == RGBTRANS)
	{
		for (ui8 _y=0; _y<14; _y++)
		{
			ui8 col = ~*pFont++;
	
			for (ui8 _x=0; _x<8; _x++, col <<= 1)
				if ( col & 128 )
					BIOS::LCD::PutPixel(x+_x, y+_y, clrb);
		}
	} else
	{
				CRect rcChar(x, y, x+8, y+14);
				BIOS::LCD::BufferBegin(rcChar);
				uint16_t buf[14];

					for (ui8 _x=0; _x<8; _x++)
		{
				for (ui8 _y=0; _y<14; _y++)
						buf[_y] = (pFont[13-_y] & (128 >> _x)) ? clrf : clrb;
					
					BIOS::LCD::BufferWrite(buf, 14);
				}
	}
	return 8;
}

uint8_t _Round(int x, int y);

void BIOS::LCD::RoundRect(int x1, int y1, int x2, int y2, unsigned short clr)
{
	for (int y=y1; y<y2; y++)
	{
		if (y == y1+3)
		{
			BIOS::LCD::Bar(x1, y, x2, y2-3, clr);
			y = y2-3;
		}
		for (int x=x1; x<x2; x++)
			if ( !_Round(min(x-x1, x2-x-1), min(y-y1, y2-y-1)) )
				PutPixel(x, y, clr);
	}
}

void BIOS::LCD::Rectangle(const CRect& rc, unsigned short clr)
{
    Bar(rc.left, rc.top, rc.right, rc.top+1, clr);
    Bar(rc.left, rc.bottom-1, rc.right, rc.bottom, clr);
    Bar(rc.left, rc.top, rc.left+1, rc.bottom, clr);
    Bar(rc.right-1, rc.top, rc.right, rc.bottom, clr);
}

void BIOS::LCD::RoundRect(const CRect& rc, unsigned short clr)
{
	RoundRect(rc.left, rc.top, rc.right, rc.bottom, clr);
} 

uint8_t _Round(int x, int y)
{
//TODO: optimize
	const static uint8_t r[] = 
	{
		1, 1, 1, 0,
		1, 0, 0, 0,
		1, 0, 0, 0,
		0, 0, 0, 0
	};
	if ( !(x&~3) && !(y&~3) )
		return r[(y<<2)|x];
	return 0;
}

void BIOS::LCD::BufferBegin(const CRect& rc)
{
  Set_Block(rc.left, FLIP(rc.bottom)+1, rc.right, FLIP(rc.top)+1);
}

void BIOS::LCD::BufferWrite(uint16_t clr)
{
  Set_Pixel(clr);
}

void BIOS::LCD::BufferWrite(const uint16_t* buf, int n)
{
    while (n--)
        Set_Pixel(*buf++);
}

void BIOS::LCD::BufferRead(uint16_t* buf, int n)
{
    while (n--)
        *buf++ = BufferRead();
}

uint16_t BIOS::LCD::BufferRead()
{
    return Get_Pixel();
}

void BIOS::LCD::BufferEnd()
{
}

int BIOS::LCD::Draw(int x, int y, unsigned short clrf, unsigned short clrb, const char *p)
{
	int h = *p++;
//	y+=h-1;
	for (int _x=0; _x<80; _x++)
		for (int _y=0; _y<h; _y++)
		{
			unsigned char d = *p++; //[_x*h+_y];
			if (!d)
				return _x;
			if ( d & 4 )
			{
				if (clrf != RGBTRANS)
					PutPixel(x+_x, y+_y, clrf);
			}
			else
			{
				if (clrb != RGBTRANS)
					PutPixel(x+_x, y+_y, clrb);
			}
		}
	return 8;
}

void BIOS::LCD::Pattern(int x1, int y1, int x2, int y2, const ui16 *pat, int l)
{
    int t = FLIP(y1)+1;
    y1 = FLIP(y2)+1;
    y2 = t;
    int i = 0;
    for(int x=x1; x<x2; x++)
        for(int y=y1; y<y2; y++)
        {
            Set_Posi(x, y);
            Set_Pixel(pat[i]);
            if (++i == l)
                i = 0;
        }
}

void BIOS::LCD::Shadow(int x1, int y1, int x2, int y2, unsigned int nColor)
{
  _ASSERT(0);
}



// SDL

int lcdX = 0;
int lcdY = 0;
int lcdX1 = 0;
int lcdY1 = 0;
int lcdX2 = 320-1;
int lcdY2 = 240-1;

void Set_Posi(uint16_t x, uint16_t y)
{
    lcdX = x;
    lcdY = y;
}

void Set_Pixel(uint16_t Color)
{
    gHal->SetPixel(lcdX, lcdY, Color);
	lcdY++;
    if (lcdY >= lcdY2)
    {
        lcdY = lcdY1;
        lcdX++;
        if (lcdX >= lcdX2)
        {
            lcdX = lcdX1;
        }
    }
	/*
    lcdX++;
    if (lcdX >= lcdX2)
    {
        lcdX = lcdX1;
        lcdY++;
        if (lcdY >= lcdY2)
        {
            lcdY = lcdY1;
        }
    }*/
}

int Get_Pixel()
{
    int c = gHal->GetPixel(lcdX, lcdY);
    lcdY++;
    if (lcdY >= lcdY2)
    {
        lcdY = lcdY1;
        lcdX++;
        if (lcdX >= lcdX2)
        {
            lcdX = lcdX1;
        }
    }

    /*
    lcdX++;
    if (lcdX >= lcdX2)
    {
        lcdX = lcdX1;
        lcdY++;
        if (lcdY >= lcdY2)
        {
            lcdY = lcdY1;
        }
    }
     */
    return c;
}

void Set_Block(int x1, int y1, int x2, int y2)
{
    lcdX = x1;
    lcdY = y1;
    lcdX1 = x1;
    lcdY1 = y1;
    lcdX2 = x2;
    lcdY2 = y2;
    assert(lcdX2 > lcdX1);
    assert(lcdY2 > lcdY1);
}
