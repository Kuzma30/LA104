#include "Fft.h"
#include <MainWnd.h>
#include <Oscilloscope/Meas/MeasCore.h>

namespace Fft
{
	enum { N = 256, M = N / 2, Stages = 7 };   // 256 real samples = 128 complex
	enum { Range = 80 };                        // dB from the top to the bottom

	// sin(2 pi a / 256) in Q15, a = 0..64 (quarter wave)
	static const int16_t s_arrSin[65] = {
		0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278,
		11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868,
		19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832,
		26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571,
		30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
		32728, 32757, 32767 };

	static int Sin( int a )
	{
		a &= 255;
		if ( a < 64 ) return s_arrSin[a];
		if ( a < 128 ) return s_arrSin[128 - a];
		if ( a < 192 ) return -s_arrSin[a - 128];
		return -s_arrSin[256 - a];
	}

	static int Cos( int a )
	{
		return Sin( a + 64 );
	}

	static uint32_t Isqrt( uint32_t v )
	{
		uint32_t r = 0, b = 1u << 30;
		while ( b > v ) b >>= 2;
		while ( b )
		{
			if ( v >= r + b ) { v -= r + b; r = ( r >> 1 ) + b; }
			else r >>= 1;
			b >>= 2;
		}
		return r;
	}

	// 20 log10(v), v > 0, about 0.1 dB
	static int DbX10( uint32_t v )
	{
		if ( v == 0 )
			return -1000;
		int e = 31 - __builtin_clz( v );
		// mantissa 1..2 -> log2 by a quadratic fit
		float m = (float)v / (float)( 1u << e );
		float l2 = e + ( -0.34484843f * m + 2.02466578f ) * m - 0.67487759f;
		return (int)( l2 * 60.206f );
	}

	// in place radix 2 FFT of M complex Q15 values, scaled by 1/M
	static void Transform( int16_t* p )
	{
		// bit reversal
		for ( int i = 1, j = 0; i < M; i++ )
		{
			int bit = M >> 1;
			for ( ; j & bit; bit >>= 1 )
				j ^= bit;
			j |= bit;
			if ( i < j )
			{
				int16_t t;
				t = p[2*i]; p[2*i] = p[2*j]; p[2*j] = t;
				t = p[2*i+1]; p[2*i+1] = p[2*j+1]; p[2*j+1] = t;
			}
		}
		for ( int len = 2; len <= M; len <<= 1 )
		{
			int step = N / len;     // twiddle index step in 1/256 of the circle
			for ( int i = 0; i < M; i += len )
				for ( int k = 0; k < len / 2; k++ )
				{
					int c = Cos( k * step ), s = Sin( k * step );   // W = c - j s
					int16_t* a = p + 2 * ( i + k );
					int16_t* b = p + 2 * ( i + k + len / 2 );
					int32_t tr = ( (int32_t)b[0] * c + (int32_t)b[1] * s ) >> 15;
					int32_t ti = ( (int32_t)b[1] * c - (int32_t)b[0] * s ) >> 15;
					int32_t ar = a[0], ai = a[1];
					a[0] = ( ar + tr ) >> 1; a[1] = ( ai + ti ) >> 1;
					b[0] = ( ar - tr ) >> 1; b[1] = ( ai - ti ) >> 1;
				}
		}
	}

	// magnitudes of the 128 bins of the real signal into p[2k] (uint16)
	static void Magnitudes( int16_t* p )
	{
		uint16_t* pMag = (uint16_t*)p;
		pMag[0] = 0;   // DC is not shown
		for ( int k = 1; k <= M / 2; k++ )
		{
			int mk = M - k;
			int32_t zr = p[2*k], zi = p[2*k+1], wr = p[2*mk], wi = p[2*mk+1];
			int32_t er = ( zr + wr ) >> 1, ei = ( zi - wi ) >> 1;     // even part
			int32_t orr = ( zi + wi ) >> 1, oi = ( wr - zr ) >> 1;    // odd part
			int32_t c = Cos( k ), s = Sin( k );                       // W_256^k = c - j s
			int32_t xr1 = er + ( ( orr * c + oi * s ) >> 15 );
			int32_t xi1 = ei + ( ( oi * c - orr * s ) >> 15 );
			int32_t xr2 = er - ( ( orr * c + oi * s ) >> 15 );
			int32_t xi2 = -ei + ( ( oi * c - orr * s ) >> 15 );
			uint32_t m1 = Isqrt( (uint32_t)( xr1 * xr1 + xi1 * xi1 ) );
			uint32_t m2 = Isqrt( (uint32_t)( xr2 * xr2 + xi2 * xi2 ) );
			pMag[2*k] = m1 > 0xffff ? 0xffff : m1;
			pMag[2*mk] = m2 > 0xffff ? 0xffff : m2;
		}
	}

	void Paint( CWndOscGraph* pGraph, int nSource, uint8_t* pScratch )
	{
		const int W = CWndGraph::DivsX * CWndGraph::BlkX;
		const int H = CWndGraph::DivsY * CWndGraph::BlkY;
		int16_t* p = (int16_t*)pScratch;
		int nShift = nSource == 1 ? 0 : 8;   // 1 CH1, 2 CH2

		// 256 samples from the start of the screen (or as many as valid)
		int nBegin = Settings.Time.Shift;
		if ( nBegin + N > Sampler::GetValid() )
			nBegin = Sampler::GetValid() - N;
		bool bValid = nBegin >= Settings.Time.InvalidFirst;

		int nPeak = 0;
		if ( bValid )
		{
			int nSum = 0;
			for ( int i = 0; i < N; i++ )
				nSum += ( Sampler::GetAt( nBegin + i ) >> nShift ) & 0xff;
			int nMean = nSum / N;
			for ( int i = 0; i < N; i++ )
			{
				int v = ( ( Sampler::GetAt( nBegin + i ) >> nShift ) & 0xff ) - nMean;
				// Hann: 0.5 - 0.5 cos(2 pi i / N)
				int w = ( 32768 - Cos( i ) ) >> 1;
				p[i] = (int16_t)( ( ( v << 6 ) * w ) >> 15 );
			}
			Transform( p );
			Magnitudes( p );
			uint16_t* pMag = (uint16_t*)p;
			for ( int k = 2; k < M; k++ )
				if ( pMag[2*k] > pMag[2*nPeak] )
					nPeak = k;
		}

		// full scale sine (127 counts) gives 127*64/2 = 4064
		const int nRefX10 = DbX10( 4064 );
		ui16 clrBar = nSource == 1 ? Settings.CH1.u16Color : Settings.CH2.u16Color;
		ui16 column[H];
		uint16_t* pMag = (uint16_t*)p;
		const CRect& rc = pGraph->m_rcClient;
		for ( int x = 0; x < W; x++ )
		{
			int k = x * M / W;
			for ( int y = 0; y < H; y++ )
				column[y] = 0x0101;
			// grid: every 10 dB, and the divisions
			for ( int y = 0; y < H; y += H * 10 / Range )
				column[y] = RGB565(404040);
			if ( x % CWndGraph::BlkX == 0 )
				for ( int y = 0; y < H; y += 2 )
					column[y] = RGB565(404040);
			if ( bValid && k > 0 )
			{
				int nDb = ( DbX10( pMag[2*k] ) - nRefX10 ) / 10;   // 0 = full scale, negative below
				int h = ( nDb + Range ) * H / Range;
				if ( h > H ) h = H;
				for ( int y = 0; y < h; y++ )
					column[y] = clrBar;
			}
			BIOS::LCD::BufferBegin( CRect( rc.left + x, rc.top, rc.left + x + 1, rc.top + H ) );
			BIOS::LCD::BufferWrite( column, H );
			BIOS::LCD::BufferEnd();
		}

		// labels
		char str[16];
		int x = rc.left + 4, y = rc.top + 2;
		x += BIOS::LCD::Print( x, y, clrBar, RGBTRANS, nSource == 1 ? "FFT CH1" : "FFT CH2" );
		if ( !bValid )
		{
			BIOS::LCD::Print( x + 8, y, RGB565(ff4040), RGBTRANS, "no data" );
			return;
		}
		float fs = MeasCore::SampleRate();
		x += 8;
		x += BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, "peak " );
		MeasCore::FormatSI( nPeak * fs / N, "Hz", str );
		x += BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, str );
		BIOS::LCD::Printf( x + 8, y, RGB565(ffffff), RGBTRANS, "%ddB",
			( DbX10( pMag[2*nPeak] ) - nRefX10 ) / 10 );
		MeasCore::FormatSI( fs / 2, "Hz", str );
		int w = strlen( str ) * 8;
		BIOS::LCD::Print( rc.right - w - 4, rc.bottom - 16, RGB565(b0b0b0), RGBTRANS, str );
		MeasCore::FormatSI( fs / 2 / CWndGraph::DivsX, "Hz/div", str );
		BIOS::LCD::Print( rc.left + 4, rc.bottom - 16, RGB565(b0b0b0), RGBTRANS, str );
	}
}
