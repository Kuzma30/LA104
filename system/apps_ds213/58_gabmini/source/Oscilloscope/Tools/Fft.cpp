#include "Fft.h"
#include <MainWnd.h>
#include <Oscilloscope/Meas/MeasCore.h>

namespace Fft
{
	// up to 1024 real samples = 512 complex, fewer when the buffer is shorter
	enum { NMax = 1024, NMin = 256, Circle = 1024 };
	enum { Range = 80 };                        // dB from the top to the bottom

	// DS203: 2 KB in the RAM above the OS, next to the sample buffer
#if defined(DS203)
	__attribute__((section(".bss_hi")))
#endif
	static int16_t s_arrData[NMax];

	// sin(2 pi a / 1024) in Q15, a = 0..256 (quarter wave)
	static const int16_t s_arrSin[257] = {
		0, 201, 402, 603, 804, 1005, 1206, 1407, 1608, 1809, 2009, 2210, 2410, 2611, 2811, 3012, 3212, 3412,
		3612, 3811, 4011, 4210, 4410, 4609, 4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195, 6393, 6590,
		6786, 6983, 7179, 7375, 7571, 7767, 7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319, 9512, 9704,
		9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
		12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828, 14010, 14191, 14372, 14553, 14732, 14912,
		15090, 15269, 15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360,
		17530, 17700, 17869, 18037, 18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680,
		19841, 20000, 20159, 20317, 20475, 20631, 20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
		22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027, 23170, 23311, 23452, 23592, 23731, 23870,
		24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201, 25329, 25456, 25582, 25708,
		25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133, 27245, 27356,
		27466, 27575, 27683, 27790, 27896, 28001, 28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
		28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037,
		30117, 30195, 30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050,
		31113, 31176, 31237, 31297, 31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736, 31785, 31833,
		31880, 31926, 31971, 32014, 32057, 32098, 32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382,
		32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589, 32609, 32628, 32646, 32663, 32678, 32692,
		32705, 32717, 32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766, 32767
	};

	static int Sin( int a )
	{
		a &= Circle - 1;
		if ( a < 256 ) return s_arrSin[a];
		if ( a < 512 ) return s_arrSin[512 - a];
		if ( a < 768 ) return -s_arrSin[a - 512];
		return -s_arrSin[1024 - a];
	}

	static int Cos( int a )
	{
		return Sin( a + 256 );
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

	// in place radix 2 FFT of m complex Q15 values, scaled by 1/m
	static void Transform( int16_t* p, int m )
	{
		// bit reversal
		for ( int i = 1, j = 0; i < m; i++ )
		{
			int bit = m >> 1;
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
		for ( int len = 2; len <= m; len <<= 1 )
		{
			int step = Circle / len;     // twiddle index step in 1/1024 of the circle
			for ( int i = 0; i < m; i += len )
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

	// magnitudes of the n/2 bins of the real signal of n samples into p[2k] (uint16)
	static void Magnitudes( int16_t* p, int n )
	{
		int m = n / 2, step = Circle / n;
		uint16_t* pMag = (uint16_t*)p;
		pMag[0] = 0;   // DC is not shown
		for ( int k = 1; k <= m / 2; k++ )
		{
			int mk = m - k;
			int32_t zr = p[2*k], zi = p[2*k+1], wr = p[2*mk], wi = p[2*mk+1];
			int32_t er = ( zr + wr ) >> 1, ei = ( zi - wi ) >> 1;     // even part
			int32_t orr = ( zi + wi ) >> 1, oi = ( wr - zr ) >> 1;    // odd part
			int32_t c = Cos( k * step ), s = Sin( k * step );         // W_n^k = c - j s
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

	// the peak between the bins: parabola through the dB values of k-1, k, k+1
	// (with the Hann window within a few % of a bin and 0.2 dB)
	static void Interpolate( const uint16_t* pMag, int k, int bins, float& fBin, float& fDbX10 )
	{
		fBin = (float)k;
		fDbX10 = (float)DbX10( pMag[2*k] );
		if ( k < 1 || k + 1 >= bins || pMag[2*(k-1)] == 0 || pMag[2*(k+1)] == 0 )
			return;
		float a = (float)DbX10( pMag[2*(k-1)] ), b = fDbX10, c = (float)DbX10( pMag[2*(k+1)] );
		float den = a - 2 * b + c;
		if ( den >= 0 )
			return;
		float d = 0.5f * ( a - c ) / den;
		if ( d > 0.5f ) d = 0.5f;
		if ( d < -0.5f ) d = -0.5f;
		fBin = k + d;
		fDbX10 = b - 0.25f * ( a - c ) * d;
	}

	void Paint( CWndOscGraph* pGraph, int nSource, uint8_t* /*pScratch*/ )
	{
		const int W = CWndGraph::DivsX * CWndGraph::BlkX;
		const int H = CWndGraph::DivsY * CWndGraph::BlkY;
		int16_t* p = s_arrData;
		int nShift = nSource == 1 ? 0 : 8;   // 1 CH1, 2 CH2

		// 1024 (512, 256) samples from the start of the screen, as many as the
		// buffer holds (Buf:Scr: 256)
		int nValid = Sampler::GetValid(), nFirst = Settings.Time.InvalidFirst;
		int N = NMax;
		while ( N > NMin && nValid - nFirst < N )
			N >>= 1;
		int nBegin = Settings.Time.Shift;
		if ( nBegin + N > nValid )
			nBegin = nValid - N;
		bool bValid = nBegin >= nFirst;
		int nBins = N / 2;

		int nPeak = 0;
		if ( bValid )
		{
			int nSum = 0;
			for ( int i = 0; i < N; i++ )
				nSum += ( Sampler::GetAt( nBegin + i ) >> nShift ) & 0xff;
			int nMean = nSum / N;
			int nWinStep = Circle / N;
			for ( int i = 0; i < N; i++ )
			{
				int v = ( ( Sampler::GetAt( nBegin + i ) >> nShift ) & 0xff ) - nMean;
				// Hann: 0.5 - 0.5 cos(2 pi i / N)
				int w = ( 32768 - Cos( i * nWinStep ) ) >> 1;
				p[i] = (int16_t)( ( ( v << 6 ) * w ) >> 15 );
			}
			Transform( p, nBins );
			Magnitudes( p, N );
			uint16_t* pMag = (uint16_t*)p;
			for ( int k = 2; k < nBins; k++ )
				if ( pMag[2*k] > pMag[2*nPeak] )
					nPeak = k;
		}

		// full scale sine (127 counts) gives 127*64/2 = 4064 (scaled by 1/N)
		const int nRefX10 = DbX10( 4064 );
		ui16 clrBar = nSource == 1 ? Settings.CH1.u16Color : Settings.CH2.u16Color;
		ui16 column[H];
		uint16_t* pMag = (uint16_t*)p;
		const CRect& rc = pGraph->m_rcClient;
		for ( int x = 0; x < W; x++ )
		{
			// the highest bin of the column (more bins than columns at N = 1024)
			int k0 = x * nBins / W, k1 = ( x + 1 ) * nBins / W;
			if ( k1 <= k0 )
				k1 = k0 + 1;
			uint16_t nMag = 0;
			for ( int k = k0 > 0 ? k0 : 1; k < k1; k++ )
				if ( pMag[2*k] > nMag )
					nMag = pMag[2*k];
			for ( int y = 0; y < H; y++ )
				column[y] = 0x0101;
			// grid: every 10 dB, and the divisions
			for ( int y = 0; y < H; y += H * 10 / Range )
				column[y] = RGB565(404040);
			if ( x % CWndGraph::BlkX == 0 )
				for ( int y = 0; y < H; y += 2 )
					column[y] = RGB565(404040);
			if ( bValid && nMag )
			{
				int nDb = ( DbX10( nMag ) - nRefX10 ) / 10;   // 0 = full scale, negative below
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
		float fBin, fDbX10;
		Interpolate( pMag, nPeak, nBins, fBin, fDbX10 );
		x += 8;
		MeasCore::FormatSI( fBin * fs / N, "Hz", str );
		x += BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, str );
		int nDb = (int)( fDbX10 - nRefX10 );
		nDb = nDb < 0 ? -( ( -nDb + 5 ) / 10 ) : ( nDb + 5 ) / 10;
		x += 8 + BIOS::LCD::Printf( x + 8, y, RGB565(ffffff), RGBTRANS, "%ddB", nDb );
		// resolution: one bin (left of the STOP label)
		MeasCore::FormatSI( fs / N, "Hz", str );
		x += 8 + BIOS::LCD::Print( x + 8, y, RGB565(808080), RGBTRANS, "bin " );
		BIOS::LCD::Print( x, y, RGB565(808080), RGBTRANS, str );
		MeasCore::FormatSI( fs / 2, "Hz", str );
		int w = strlen( str ) * 8;
		BIOS::LCD::Print( rc.right - w - 4, rc.bottom - 16, RGB565(b0b0b0), RGBTRANS, str );
		MeasCore::FormatSI( fs / 2 / CWndGraph::DivsX, "Hz/div", str );
		BIOS::LCD::Print( rc.left + 4, rc.bottom - 16, RGB565(b0b0b0), RGBTRANS, str );
	}
}
