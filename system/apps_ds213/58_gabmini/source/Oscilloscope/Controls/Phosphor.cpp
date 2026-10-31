#include "Phosphor.h"
#include <string.h>

namespace Phosphor
{
    enum EMode { ModeNone, ModeTY, ModeXY };

    static uint8_t* s_pArea = nullptr;
    static int s_nStride = 0;           // bytes per frame
    static int s_nFrames = 0;           // capacity
    static int s_nHead = 0;             // newest frame
    static int s_nCount = 0;            // valid frames
    static uint32_t s_nKey = 0;
    static EMode s_eMode = ModeNone;
    static uint16_t s_arrPoints[MaxFrames];

    void Clear()
    {
        s_nCount = 0;
        s_nHead = 0;
    }

    static bool _Begin(EMode eMode, int nStride, uint8_t* pArea, int nBytes, uint32_t nKey, bool& bNew)
    {
        int nFrames = (nBytes - ScratchBytes) / nStride;
        if ( nFrames > MaxFrames )
            nFrames = MaxFrames;
        if ( nFrames < 2 )
        {
            s_eMode = ModeNone;
            return false;
        }
        if ( eMode != s_eMode || nKey != s_nKey || pArea != s_pArea || nFrames != s_nFrames )
        {
            s_eMode = eMode;
            s_nKey = nKey;
            s_pArea = pArea;
            s_nFrames = nFrames;
            s_nStride = nStride;
            Clear();
        }
        // cleared (settings changed or by the user): store this frame
        if ( s_nCount == 0 )
            bNew = true;
        if ( bNew )
        {
            s_nHead = s_nCount == 0 ? 0 : (s_nHead + 1) % s_nFrames;
            if ( s_nCount < s_nFrames )
                s_nCount++;
            memset( s_pArea + s_nHead * s_nStride, None, s_nStride );
            s_arrPoints[s_nHead] = 0;
        }
        return true;
    }

    // ------------------------------------------------------------------ T-Y

    bool BeginTY(uint8_t* pArea, int nBytes, uint32_t nKey, bool& bNew)
    {
        return _Begin( ModeTY, 2 * Width, pArea, nBytes, nKey, bNew );
    }

    void StoreTY(int x, int nChannel, int y)
    {
        if ( s_eMode != ModeTY || x < 0 || x >= Width || nChannel < 0 || nChannel > 1 )
            return;
        if ( y != None && (y < 0 || y >= Height) )
            y = y < 0 ? 0 : Height - 1;
        s_pArea[s_nHead * s_nStride + nChannel * Width + x] = (uint8_t)y;
    }

    void SumColumn(int x, int nChannel, bool bLines, bool bGraded, uint8_t* pSum)
    {
        memset( pSum, 0, Height );
        if ( s_eMode != ModeTY || x < 0 || x >= Width )
            return;
        for ( int nAge = 0; nAge < s_nCount; nAge++ )
        {
            int f = (s_nHead - nAge + s_nFrames) % s_nFrames;
            const uint8_t* pRow = s_pArea + f * s_nStride + nChannel * Width;
            int y = pRow[x];
            if ( y == None )
                continue;
            int yPrev = x > 0 ? pRow[x-1] : y;
            if ( yPrev == None )
                yPrev = y;
            int nLow = y, nHigh = y;
            if ( bLines )
            {
                nLow = y < yPrev ? y : yPrev;
                nHigh = y < yPrev ? yPrev : y;
            }
            int w = bGraded ? MaxFrames - nAge : 1;
            for ( int i = nLow; i <= nHigh; i++ )
            {
                int s = pSum[i] + w;
                pSum[i] = (uint8_t)(s > 255 ? 255 : s);
            }
        }
    }

    // ------------------------------------------------------------------ X-Y

    bool BeginXY(uint8_t* pArea, int nBytes, uint32_t nKey, bool& bNew)
    {
        return _Begin( ModeXY, 2 * MaxPoints, pArea, nBytes, nKey, bNew );
    }

    void StoreXY(int nPoints, const uint8_t* pX, const uint8_t* pY)
    {
        if ( s_eMode != ModeXY )
            return;
        if ( nPoints > MaxPoints )
            nPoints = MaxPoints;
        uint8_t* p = s_pArea + s_nHead * s_nStride;
        for ( int i = 0; i < nPoints; i++ )
        {
            *p++ = pX[i];
            *p++ = pY[i];
        }
        s_arrPoints[s_nHead] = (uint16_t)nPoints;
    }

    uint8_t* Scratch()
    {
        return s_eMode == ModeNone ? nullptr : s_pArea + s_nFrames * s_nStride;
    }

    int Frames()
    {
        return s_eMode == ModeNone ? 0 : s_nCount;
    }

    const uint8_t* FrameXY(int nAge, int& nPoints)
    {
        nPoints = 0;
        if ( s_eMode != ModeXY || nAge < 0 || nAge >= s_nCount )
            return nullptr;
        int f = (s_nHead - nAge + s_nFrames) % s_nFrames;
        nPoints = s_arrPoints[f];
        return s_pArea + f * s_nStride;
    }

    // ------------------------------------------------------------------ colors

    static uint16_t _Rgb(int r, int g, int b)
    {
        return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    // ceil(136 * (i/31)^2), i = 1..31
    static const uint8_t g_arrThreshold[LutSize - 1] = {1, 1, 2, 3, 4, 6, 7, 10, 12, 15, 18, 21,
        24, 28, 32, 37, 41, 46, 52, 57, 63, 69, 75, 82, 89, 96, 104, 111, 120, 128, 136};

    int LutIndex(int nSum)
    {
        if ( nSum <= 0 )
            return 0;
        // count of thresholds <= nSum, binary search
        int nLow = 0, nHigh = LutSize - 1;
        while ( nLow < nHigh )
        {
            int nMid = (nLow + nHigh) / 2;
            if ( g_arrThreshold[nMid] <= nSum )
                nLow = nMid + 1;
            else
                nHigh = nMid;
        }
        return nLow;
    }

    void MakeLut(uint16_t clr, uint16_t* pLut)
    {
        int r = ((clr >> 11) & 0x1f) << 3, g = ((clr >> 5) & 0x3f) << 2, b = (clr & 0x1f) << 3;
        pLut[0] = 0;
        for ( int i = 1; i <= 20; i++ )
        {
            int k = 46 + (i - 1) * (256 - 46) / 19;     // 18 % .. 100 %
            pLut[i] = _Rgb( r * k >> 8, g * k >> 8, b * k >> 8 );
        }
        for ( int i = 21; i < LutSize; i++ )
        {
            int t = (i - 20) * 180 / (LutSize - 1 - 20);    // up to 70 % white
            pLut[i] = _Rgb( r + ((255 - r) * t >> 8), g + ((255 - g) * t >> 8), b + ((255 - b) * t >> 8) );
        }
    }
}
