#include "Bios.h"
#include "imports.h"
#include "font.h"
#include "fontuni.h"

#define FLIP(y) (BIOS::LCD::Height-1-(y))
// TODO: global namespace
int _DrawChar(int x, int y, unsigned short clrf, unsigned short clrb, char ch);

void BIOS::LCD::Clear(unsigned short clr)
{
  uint32_t i;
  Set_Posi(0, 0);
  for(i = 0; i < BIOS::LCD::Width * BIOS::LCD::Height; i++) 
    Set_Pixel(clr);
}

void BIOS::LCD::Bar(int x1, int y1, int x2, int y2, unsigned short clr)
{
  Set_Block(x1, FLIP(y2)+1, x2, FLIP(y1)+1);
  int32_t count = (x2-x1)*(y2-y1);
  const uint_fast16_t _clr = clr;

  while (count--)
    Set_Pixel(_clr);
}

void BIOS::LCD::Bar(const CRect& rc, unsigned short clr)
{
  BIOS::LCD::Bar(rc.left, rc.top, rc.right, rc.bottom, clr);
}

void BIOS::LCD::Rectangle(const CRect& rc, unsigned short clr)
{
	BIOS::LCD::Bar(rc.left, rc.top, rc.right, rc.top+1, clr);
	BIOS::LCD::Bar(rc.left, rc.bottom-1, rc.right, rc.bottom, clr);
	BIOS::LCD::Bar(rc.left, rc.top, rc.left+1, rc.bottom, clr);
	BIOS::LCD::Bar(rc.right-1, rc.top, rc.right, rc.bottom, clr);
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

uint16_t BIOS::LCD::GetPixel(int x, int y)
{
  Set_Posi(x, FLIP(y));
  ReadStart();
  uint16_t c = ReadPixel();
  ReadFinish();
  return c;
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

int BIOS::LCD::Print (int x, int y, unsigned short clrf, unsigned short clrb, char c)
{
  return _DrawChar(x, y, clrf, clrb, c);
}

int BIOS::LCD::Print (int x, int y, unsigned short clrf, unsigned short clrb, char *str)
{
	return BIOS::LCD::Print (x, y, clrf, clrb, (const char*)str);
}

int BIOS::LCD::Printf (int x, int y, unsigned short clrf, unsigned short clrb, const char *format, ...)
{
        va_list args;
        
        va_start( args, format );

	char buffer[128];
	char* _buffer = buffer;
        vsprintf( _buffer, format, args );
        va_end(args);

	return Print(x, y, clrf, clrb, buffer);
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

bool reading = false;

void BIOS::LCD::BufferBegin(const CRect& rc)
{
  _ASSERT(!reading);
  Set_Block(rc.left, FLIP(rc.bottom)+1, rc.right, FLIP(rc.top)+1);
  reading = false;
}

void BIOS::LCD::BufferWrite(uint16_t clr)
{
  Set_Pixel(clr);
}

void BIOS::LCD::BufferWrite(const uint16_t* buffer, int length)
{
  while (length--)
    Set_Pixel(*buffer++);
}

uint16_t BIOS::LCD::BufferRead()
{
  if (!reading)
  {
    reading = true;
    ReadStart();
  }

  uint32_t c = ReadPixel();
  return c;
}

void BIOS::LCD::BufferRead(uint16_t* buffer, int length)
{
  if (!reading)
  {
    reading = true;
    ReadStart();
  }

  while (length--)
    *buffer++ = ReadPixel();
}

void BIOS::LCD::BufferEnd()
{
  if (reading)
  {
    ReadFinish();
    reading = false;
  }
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
//  if (x2 > 320)
//    return;
  Set_Block(x1, y1, x2, y2);

  const ui16* patb = pat;
  const ui16* pate = patb + l;

  for (int y=y1; y<y2; y++)
    for (int x=x1; x<x2; x++)
    {
      Set_Pixel(*pat);
      if (++pat == pate) 
        pat = patb;
    }
}

void BIOS::LCD::Shadow(int x1, int y1, int x2, int y2, unsigned int nColor)
{
  _ASSERT(0);
}