#include "DecodeBand.h"
#include "Font5x7.h"

using namespace Decoder;

namespace DecodeBand
{
    // same bit layout as RGB565RGB() of the LA104 framework (blue in high bits)
    static constexpr uint16_t Rgb(uint32_t rgb)
    {
        return (uint16_t)((((rgb >> 16) & 0xff) >> 3) | ((((rgb >> 8) & 0xff) >> 2) << 5) | (((rgb & 0xff) >> 3) << 11));
    }

    struct TRow
    {
        int p;          // first annotation that may cover current column
        int cur;        // annotation the label was prepared for
        TAnnotation a;  // copy of annotation 'p'
        int x0;         // sample index of the first label pixel
        int len;        // characters shown
        char text[20];
    };

    static TRow s_rows[2];
    static int s_nRows;
    static int s_nViewBegin, s_nViewEnd;
    static int s_nSelected;
    static float s_fRate;
    static const TConfig* s_pCfg;

    static inline int RowOf(const TAnnotation& a) { return (a.flags & FRow1) ? 1 : 0; }
    static inline int EffBegin(const TAnnotation& a) { return (a.flags & FPoint) ? a.begin - 6 : a.begin; }
    static inline int EffEnd(const TAnnotation& a) { return (a.flags & FPoint) ? a.end + 6 : a.end; }

    bool Begin(const TConfig& cfg, float fSampleRate, int nViewBegin, int nViewWidth, int nSelected)
    {
        if ( cfg.protocol == Off || Decoder::Count == 0 )
            return false;
        s_pCfg = &cfg;
        s_fRate = fSampleRate;
        s_nViewBegin = nViewBegin;
        s_nViewEnd = nViewBegin + nViewWidth - 1;
        s_nSelected = nSelected;
        s_nRows = 1;
        for ( int i = 0; i < Decoder::Count; i++ )
            if ( Decoder::Get(i).flags & FRow1 )
            {
                s_nRows = 2;
                break;
            }
        for ( int r = 0; r < 2; r++ )
        {
            s_rows[r].p = -1;
            s_rows[r].cur = -1;
        }
        return true;
    }

    static void Colors(const TAnnotation& a, bool bSelected, uint16_t& fill, uint16_t& border, uint16_t& text)
    {
        text = Rgb(0xffffff);
        if ( a.flags & FError )
        {
            fill = Rgb(0x601818);
            border = Rgb(0xff4040);
        } else
        switch ( a.type )
        {
        case TAddrWrite: case TAddrRead: case TCanId: case TPid: case TNecAddr:
            fill = Rgb(0x583810);
            border = Rgb(0xffa030);
            break;
        case TStart: case TRepStart: case TStop: case TBreak: case TSync: case TReset:
        case TPresence: case TLeader: case TRepeat: case TCanAck: case TCanDlc:
            fill = Rgb(0x184818);
            border = Rgb(0x60e060);
            break;
        case TChecksum: case TCanCrc:
            fill = Rgb(0x484818);
            border = Rgb(0xe0e060);
            break;
        case TPulse:
            fill = Rgb(0x382058);
            border = Rgb(0xc080ff);
            break;
        case TColor:
            fill = Rgb(a.value);
            border = Rgb(0xc0c0c0);
            if ( ((a.value >> 16) & 0xff)*3 + ((a.value >> 8) & 0xff)*6 + (a.value & 0xff) > 1400 )
                text = 0;
            break;
        default:
            fill = Rgb(0x183868);
            border = Rgb(0x60a0ff);
        }
        if ( a.flags & FNak )
            border = Rgb(0xff8040);
        if ( bSelected )
            border = Rgb(0xffffff);
    }

    void Column(uint16_t* column, int nHeight, int nIndex)
    {
        bool bTop = s_pCfg->band == Top;
        for ( int r = 0; r < s_nRows; r++ )
        {
            TRow& row = s_rows[r];
            // advance to the first annotation of this row that ends here or later
            if ( row.p < 0 )
            {
                row.p = 0;
                if ( row.p < Decoder::Count )
                    row.a = Decoder::Get(row.p);
            }
            while ( row.p < Decoder::Count && (RowOf(row.a) != r || EffEnd(row.a) < nIndex) )
            {
                row.p++;
                if ( row.p < Decoder::Count )
                    row.a = Decoder::Get(row.p);
            }
            if ( row.p >= Decoder::Count )
                continue;
            const TAnnotation& a = row.a;
            int nBegin = EffBegin(a), nEnd = EffEnd(a);
            if ( nBegin > nIndex )
                continue;

            if ( row.cur != row.p )
            {
                // entering new annotation, prepare its label centered in the visible part
                row.cur = row.p;
                int len = Decoder::Format( a, row.text, sizeof(row.text), false, *s_pCfg, s_fRate );
                int nVisBegin = nBegin > s_nViewBegin ? nBegin : s_nViewBegin;
                int nVisEnd = nEnd < s_nViewEnd ? nEnd : s_nViewEnd;
                int nVisWidth = nVisEnd - nVisBegin + 1;
                int nMaxChars = (nVisWidth - 3) / 6;
                if ( nMaxChars < 0 )
                    nMaxChars = 0;
                if ( len > nMaxChars )
                    len = nMaxChars;
                row.len = len;
                row.x0 = nVisBegin + (nVisWidth - (len*6 - 1)) / 2;
            }

            int yTop = bTop ? nHeight - 2 - r*12 : 12 + (s_nRows - 1 - r)*12;
            int yBottom = yTop - 10;
            uint16_t fill, border, text;
            Colors( a, row.p == s_nSelected, fill, border, text );

            if ( nIndex == nBegin || nIndex == nEnd )
            {
                for ( int y = yBottom; y <= yTop; y++ )
                    column[y] = border;
                continue;
            }
            for ( int y = yBottom+1; y < yTop; y++ )
                column[y] = fill;
            column[yTop] = border;
            column[yBottom] = border;

            int cx = nIndex - row.x0;
            if ( cx >= 0 && cx < row.len*6 )
            {
                int gx = cx % 6;
                if ( gx < 5 )
                {
                    int c = (unsigned char)row.text[cx / 6];
                    if ( c < 32 || c > 126 )
                        c = '?';
                    uint8_t bits = g_font5x7[(c - 32)*5 + gx];
                    for ( int gy = 0; gy < 7; gy++ )
                        if ( bits & (1<<gy) )
                            column[yTop - 2 - gy] = text;
                }
            }
        }
    }
}
