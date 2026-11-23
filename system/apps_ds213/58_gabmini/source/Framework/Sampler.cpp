#include <library.h>
#include "Sampler.h"
#include <Settings.h>

namespace Sampler
{
	// DS203: in the RAM above the OS (app_ds203.lds, .bss_hi), 12 KB less in
	// the region of the application
#if defined(DS203)
	__attribute__((section(".bss_hi")))
#endif
	static uint8_t memory[Samples * BytesPerSample];
	static int nValid = 0;
	static uint32_t nSequence = 0;
	static int nLength = Samples;
	static int nCapRes = -1;        // time base of the samples (-1: not known)
	// V/div and position of CH1 / CH2 when the samples were taken
	static int arrCapVRes[2] = { -1, -1 }, arrCapPos[2] = { 0, 0 };

	// the sample memory as scratch before the first capture (translation)
	uint8_t* Scratch(int& size)
	{
		size = sizeof(memory);
		return memory;
	}

	static inline void _Captured()
	{
		nCapRes = (int)Settings.Time.Resolution;
		arrCapVRes[0] = (int)Settings.CH1.Resolution; arrCapPos[0] = Settings.CH1.u16Position;
		arrCapVRes[1] = (int)Settings.CH2.Resolution; arrCapPos[1] = Settings.CH2.u16Position;
	}

	// ---------------------------------------------------------------- V/div
	// A new V/div or position of CH1 / CH2 while stopped: the stored ADC
	// values are converted to the values the ADC would give for the same
	// voltage at the current V/div and position (table of 256 values per
	// channel, made when the settings change)
	static uint8_t arrLut[2][256];
	static int arrLutKey[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };

	static bool _VScaled( int ch )
	{
		const CSettings::AnalogChannel& c = ch ? Settings.CH2 : Settings.CH1;
		return arrCapVRes[ch] >= 0 && ( arrCapVRes[ch] != (int)c.Resolution || arrCapPos[ch] != c.u16Position );
	}

	static const uint8_t* _Lut( int ch )
	{
		CSettings::AnalogChannel& c = ch ? Settings.CH2 : Settings.CH1;
		CSettings::Calibrator& cal = ch ? Settings.CH2Calib : Settings.CH1Calib;
		int key[4] = { arrCapVRes[ch], arrCapPos[ch], (int)c.Resolution, c.u16Position };
		if ( memcmp( key, arrLutKey[ch], sizeof(key) ) != 0 )
		{
			memcpy( arrLutKey[ch], key, sizeof(key) );
			CSettings::Calibrator::FastCalc fOld, fNew;
			CSettings::Calibrator::Prepare( arrCapVRes[ch], arrCapPos[ch], cal.CalData[arrCapVRes[ch]], fOld );
			CSettings::Calibrator::Prepare( (int)c.Resolution, c.u16Position, cal.CalData[c.Resolution], fNew );
			for ( int v = 0; v < 256; v++ )
			{
				// volts = (Q + adc * K) * multiplier / 65536
				float fVolt = ( fOld.Q + v * (float)fOld.K ) * fOld.fMultiplier;
				float fAdc = ( fVolt / fNew.fMultiplier - fNew.Q ) / (float)fNew.K;
				int n = (int)( fAdc + ( fAdc < 0 ? -0.5f : 0.5f ) );
				arrLut[ch][v] = (uint8_t)( n < 0 ? 0 : n > 255 ? 255 : n );
			}
		}
		return arrLut[ch];
	}

	bool VScaled()
	{
		return _VScaled( 0 ) || _VScaled( 1 );
	}

	static inline uint32_t _Volts( uint32_t v )
	{
		if ( v == BIOS::ADC::NoData )
			return v;
		if ( _VScaled( 0 ) )
			v = ( v & ~0xffu ) | _Lut( 0 )[v & 0xff];
		if ( _VScaled( 1 ) )
			v = ( v & ~0xff00u ) | ( (uint32_t)_Lut( 1 )[(v >> 8) & 0xff] << 8 );
		return v;
	}

	// sample period of a time base in 1/72 us (DS203: the divider of the
	// sampling clock, as BIOS::ADC::ConfigureTimebase); other devices: in ns
	static int64_t _Period(int nRes)
	{
#if defined(DS203)
		static const uint32_t arrDiv[] = {1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
			12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000};
		if ( nRes < 0 ) nRes = 0;
		if ( nRes >= (int)(sizeof(arrDiv)/sizeof(arrDiv[0])) ) nRes = sizeof(arrDiv)/sizeof(arrDiv[0]) - 1;
		return arrDiv[nRes];
#else
		return (int64_t)( CSettings::TimeBase::pfValueResolution[nRes] * 1e9f / 30.0f + 0.5f );
#endif
	}

	bool Scaled()
	{
		return nCapRes >= 0 && nCapRes != (int)Settings.Time.Resolution &&
			_Period( nCapRes ) != _Period( Settings.Time.Resolution );
	}

	int CaptureRes()
	{
		return nCapRes >= 0 ? nCapRes : (int)Settings.Time.Resolution;
	}

	static inline int64_t _FloorDiv(int64_t a, int64_t b)
	{
		return a >= 0 ? a / b : -((-a + b - 1) / b);
	}

	// the trigger position stays where it is
	enum { Anchor = CSettings::Trigger::Position };

	int VirtToPhys(int v)
	{
		if ( !Scaled() )
			return v;
		return Anchor + (int)_FloorDiv( (int64_t)(v - Anchor) * _Period( Settings.Time.Resolution ), _Period( nCapRes ) );
	}

	int PhysToVirt(int p)
	{
		if ( !Scaled() )
			return p;
		return Anchor + (int)_FloorDiv( (int64_t)(p - Anchor) * _Period( nCapRes ), _Period( Settings.Time.Resolution ) );
	}

	static inline void _Store(uint8_t* p, uint32_t value)
	{
		p[0] = value & 0xff;
		p[1] = (value >> 8) & 0xff;
		p[2] = (value >> 16) & 0x03;
	}

	void SetLength(int n)
	{
		if ( n < 16 ) n = 16;
		if ( n > Samples ) n = Samples;
		nLength = n;
		if ( nValid > nLength )
			nValid = nLength;
	}

	int Length()
	{
		return nLength;
	}

	uint8_t* HistoryArea(int& nBytes)
	{
		nBytes = (Samples - nLength) * BytesPerSample;
		return memory + nLength * BytesPerSample;
	}

	void RollStart(int nFirst)
	{
		_Captured();
		if ( nFirst > nLength ) nFirst = nLength;
		nValid = nFirst;
		memset( memory, 0x80, nFirst * BytesPerSample );
	}

	void RollAppend(const uint32_t* pSamples, int nCount, int nFirst, int nWidth)
	{
		_Captured();
		int nEnd = nFirst + nWidth;
		if ( nEnd > nLength || nCount <= 0 )
			return;
		if ( nValid < nFirst )
			RollStart( nFirst );
		if ( nCount > nWidth )
		{
			// only the newest fit to the window
			pSamples += nCount - nWidth;
			nCount = nWidth;
		}
		int nOver = nValid + nCount - nEnd;
		if ( nOver > 0 )
		{
			memmove( memory + nFirst * BytesPerSample,
				memory + (nFirst + nOver) * BytesPerSample,
				(nValid - nFirst - nOver) * BytesPerSample );
			nValid -= nOver;
		}
		for ( int i = 0; i < nCount; i++ )
			_Store( memory + (nValid++) * BytesPerSample, pSamples[i] );
	}

	void Copy(bool bNew)
	{
		_Captured();
		uint8_t* p = memory;
		nValid = nLength;
		if ( bNew )
			nSequence++;
		for (int i=0; i<nLength; i++, p += BytesPerSample)
		{
			uint32_t value = BIOS::ADC::Get();
			if ( value == BIOS::ADC::NoData && nValid == nLength )
				nValid = i;
			_Store( p, value );
		}
	}

	uint8_t* GetBuffer()
	{
		return memory;
	}

	int GetValidRaw()
	{
		return nValid;
	}

	int GetValid()
	{
		if ( !Scaled() )
			return nValid;
		// the virtual samples up to the end of the data, at most the buffer
		int v = PhysToVirt( nValid );
		while ( v > 0 && VirtToPhys( v - 1 ) >= nValid )
			v--;
		if ( v > nLength ) v = nLength;
		if ( v < 0 ) v = 0;
		return v;
	}

	uint32_t GetSequence()
	{
		return nSequence;
	}

	static inline BIOS::ADC::TSample::SampleType _Raw(int i)
	{
		i = (unsigned)i % Samples;
		if ( i >= nValid )
			return BIOS::ADC::NoData;
		const uint8_t* p = memory + i * BytesPerSample;
		return p[0] | (p[1] << 8) | ((p[2] & 0x03) << 16);
	}

	static BIOS::ADC::TSample::SampleType _GetAtTime(int i);

	BIOS::ADC::TSample::SampleType GetAt(int i)
	{
		return _Volts( _GetAtTime( i ) );
	}

	static BIOS::ADC::TSample::SampleType _GetAtTime(int i)
	{
		if ( !Scaled() )
			return _Raw( i );
		if ( i < 0 || i >= nLength )
			return BIOS::ADC::NoData;
		// position in the buffer: p0 + frac / den
		int64_t den = _Period( nCapRes );
		int64_t num = (int64_t)(i - Anchor) * _Period( Settings.Time.Resolution );
		int64_t q = _FloorDiv( num, den );
		int p0 = Anchor + (int)q;
		int64_t frac = num - q * den;
		if ( p0 < 0 || p0 >= nValid )
			return BIOS::ADC::NoData;
		uint32_t a = _Raw( p0 );
		if ( frac == 0 || p0 + 1 >= nValid )
			return a;
		// stretched: CH1 / CH2 linear between two samples, CH3 / CH4 of the first
		uint32_t b = _Raw( p0 + 1 );
		int a1 = a & 0xff, b1 = b & 0xff, a2 = (a >> 8) & 0xff, b2 = (b >> 8) & 0xff;
		int c1 = a1 + (int)((b1 - a1) * frac / den), c2 = a2 + (int)((b2 - a2) * frac / den);
		return (uint32_t)c1 | ((uint32_t)c2 << 8) | (a & 0x30000);
	}
}
