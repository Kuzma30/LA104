#include "MeasCore.h"
#include "FpgaRegs.h"
#include <Framework.h>
#include <Settings.h>

namespace MeasCore
{
	static const char* const s_arrNames[TypeCount] = {
		"Vpp", "Vmax", "Vmin", "Vavg", "Vrms",
		"Freq", "Period", "Duty", "Wid+", "Wid-",
		"Rise", "Fall", "Delay", "Phase", "FreqHW" };
	static const char* const s_arrSources[SourceCount] = { "CH1", "CH2", "CH3", "CH4" };

	const char* Name( int nType )
	{
		return ( nType >= 0 && nType < TypeCount ) ? s_arrNames[nType] : "?";
	}

	const char* SourceName( int nSource )
	{
		return ( nSource >= 0 && nSource < SourceCount ) ? s_arrSources[nSource] : "?";
	}

	static bool IsAnalog( int nSource )
	{
		return nSource == CH1 || nSource == CH2;
	}

	bool Allowed( int nType, int nSource )
	{
		if ( nType < 0 || nType >= TypeCount || nSource < 0 || nSource >= SourceCount )
			return false;
		if ( IsAnalog( nSource ) )
			return true;
		// digital inputs: only timing
		return nType == Freq || nType == Period || nType == Duty || nType == WidthH ||
			nType == WidthL || nType == FreqHw;
	}

	float SampleRate()
	{
		int n = (int)Settings.Time.Resolution;
#if defined(DS203)
		// FPGA sampling clock 72 MHz / (PSC+1) / (ARR+1), as BIOS::ADC::ConfigureTimebase
		static const uint32_t arrDiv[] = {1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
			12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000};
		if ( n < 0 ) n = 0;
		if ( n >= (int)COUNT(arrDiv) ) n = COUNT(arrDiv) - 1;
		return 72e6f / arrDiv[n];
#else
		return 30.0f / CSettings::TimeBase::pfValueResolution[n];
#endif
	}

	// ------------------------------------------------------------ samples
	// square root without libm (it would link errno and the reentrancy data)
	static float Sqrt( float f )
	{
		if ( f <= 0 )
			return 0;
		union { float f; uint32_t u; } v;
		v.f = f;
		v.u = ( v.u >> 1 ) + 0x1fc00000;      // first guess
		float r = v.f;
		for ( int i = 0; i < 4; i++ )
			r = 0.5f * ( r + f / r );
		return r;
	}

	static int Begin()
	{
		return Settings.Time.InvalidFirst;
	}

	static int End()
	{
		return Sampler::GetValid();
	}

	static int Raw( int i, int nSource )
	{
		uint32_t v = Sampler::GetAt( i );
		switch ( nSource )
		{
			case CH1: return v & 0xff;
			case CH2: return ( v >> 8 ) & 0xff;
			case CH3: return ( v >> 16 ) & 1;
			default:  return ( v >> 17 ) & 1;
		}
	}

	// volts = fA + fB * raw (calibration of the range and position, probe)
	static void Scale( int nSource, float& fA, float& fB )
	{
		CSettings::AnalogChannel* pCh = nSource == CH1 ? &Settings.CH1 : &Settings.CH2;
		CSettings::Calibrator& cal = nSource == CH1 ? Settings.CH1Calib : Settings.CH2Calib;
		CSettings::Calibrator::FastCalc fast;
		cal.Prepare( pCh, fast );
		float fProbe = CSettings::AnalogChannel::pfValueProbe[ pCh->Probe ];
		fA = fast.Q * fast.fMultiplier / 65536.0f * fProbe;
		fB = fast.K * fast.fMultiplier / 65536.0f * fProbe;
	}

	struct TAnalog
	{
		int nMin, nMax, nCount;
		float fMean, fMeanSq;
	};

	static bool ScanAnalog( int nSource, TAnalog& a )
	{
		a.nMin = 255; a.nMax = 0; a.nCount = 0;
		uint32_t nSum = 0;
		uint64_t nSumSq = 0;
		for ( int i = Begin(); i < End(); i++ )
		{
			int v = Raw( i, nSource );
			a.nMin = min( a.nMin, v );
			a.nMax = max( a.nMax, v );
			nSum += v;
			nSumSq += (uint32_t)( v * v );
			a.nCount++;
		}
		if ( a.nCount < 2 )
			return false;
		a.fMean = (float)nSum / a.nCount;
		a.fMeanSq = (float)nSumSq / a.nCount;
		return true;
	}

	// ------------------------------------------------------------ edges
	struct TEdges
	{
		int nRise;          // rising edges
		int nFirst, nLast;  // first and last rising edge
		int nHigh;          // samples at high level between them
	};

	// thresholds for an analog channel: middle +- 1/10 of the swing
	static bool Levels( int nSource, int& nLow, int& nHigh, int& nMid )
	{
		if ( !IsAnalog( nSource ) )
		{
			nLow = 0; nHigh = 1; nMid = 0;
			return true;
		}
		TAnalog a;
		if ( !ScanAnalog( nSource, a ) || a.nMax - a.nMin < 8 )
			return false;
		nMid = ( a.nMin + a.nMax ) / 2;
		int nHys = max( 1, ( a.nMax - a.nMin ) / 10 );
		nLow = nMid - nHys;
		nHigh = nMid + nHys;
		return true;
	}

	static bool ScanEdges( int nSource, TEdges& e )
	{
		int nLow, nHigh, nMid;
		if ( !Levels( nSource, nLow, nHigh, nMid ) )
			return false;
		e.nRise = 0; e.nFirst = e.nLast = -1; e.nHigh = 0;
		int nHighRun = 0;
		bool bHigh = Raw( Begin(), nSource ) > nMid;
		for ( int i = Begin() + 1; i < End(); i++ )
		{
			int v = Raw( i, nSource );
			if ( !bHigh && v >= nHigh )
			{
				bHigh = true;
				if ( e.nFirst < 0 )
					e.nFirst = i;
				else
					e.nHigh += nHighRun;   // high time of the finished period
				e.nLast = i;
				e.nRise++;
				nHighRun = 0;
			} else
			if ( bHigh && v <= nLow )
				bHigh = false;
			if ( bHigh )
				nHighRun++;
		}
		return e.nRise >= 2;
	}

	// mean time (samples) from 10 % to 90 % of the swing (bRising) or back
	static bool Transition( int nSource, bool bRising, float& fSamples )
	{
		TAnalog a;
		if ( !ScanAnalog( nSource, a ) || a.nMax - a.nMin < 8 )
			return false;
		float fLo = a.nMin + 0.1f * ( a.nMax - a.nMin );
		float fHi = a.nMin + 0.9f * ( a.nMax - a.nMin );
		float fFrom = bRising ? fLo : fHi;
		float fTo = bRising ? fHi : fLo;
		float fStart = -1, fSum = 0;
		int nCount = 0;
		int nPrev = Raw( Begin(), nSource );
		for ( int i = Begin() + 1; i < End(); i++ )
		{
			int v = Raw( i, nSource );
			if ( v != nPrev )
			{
				// crossing of a level between i-1 and i, linear interpolation
				float fDen = (float)( v - nPrev );
				if ( bRising ? ( nPrev < fFrom && v >= fFrom ) : ( nPrev > fFrom && v <= fFrom ) )
					fStart = i - 1 + ( fFrom - nPrev ) / fDen;
				if ( fStart >= 0 && ( bRising ? ( nPrev < fTo && v >= fTo ) : ( nPrev > fTo && v <= fTo ) ) )
				{
					fSum += i - 1 + ( fTo - nPrev ) / fDen - fStart;
					nCount++;
					fStart = -1;
				}
				// back over the start level: the transition did not finish
				if ( fStart >= 0 && ( bRising ? v < fFrom : v > fFrom ) )
					fStart = -1;
			}
			nPrev = v;
		}
		if ( nCount == 0 )
			return false;
		fSamples = fSum / nCount;
		return true;
	}

	// mean time (samples) from a rising edge of nSource to the next rising
	// edge of the other analog channel
	static bool EdgeDelay( int nSource, float& fSamples )
	{
		int nOther = nSource == CH1 ? CH2 : CH1;
		int nLowA, nHighA, nMidA, nLowB, nHighB, nMidB;
		if ( !Levels( nSource, nLowA, nHighA, nMidA ) || !Levels( nOther, nLowB, nHighB, nMidB ) )
			return false;
		bool bHighA = Raw( Begin(), nSource ) > nMidA;
		bool bHighB = Raw( Begin(), nOther ) > nMidB;
		int nEdgeA = -1;
		int nSum = 0, nCount = 0;
		for ( int i = Begin() + 1; i < End(); i++ )
		{
			int a = Raw( i, nSource ), b = Raw( i, nOther );
			if ( !bHighA && a >= nHighA )
			{
				bHighA = true;
				nEdgeA = i;
			} else
			if ( bHighA && a <= nLowA )
				bHighA = false;
			if ( !bHighB && b >= nHighB )
			{
				bHighB = true;
				if ( nEdgeA >= 0 )
				{
					nSum += i - nEdgeA;
					nCount++;
					nEdgeA = -1;
				}
			} else
			if ( bHighB && b <= nLowB )
				bHighB = false;
		}
		if ( nCount == 0 )
			return false;
		fSamples = (float)nSum / nCount;
		return true;
	}

	// ------------------------------------------------------------ FPGA counters
	static int s_nFpga = -1;            // 0 none, 1 CH_C / CH_D, 2 also CH_A / CH_B
	static uint32_t s_arrCount[SourceCount];
	static uint32_t s_arrTick[SourceCount];
	static float s_arrHw[SourceCount];
	static bool s_arrHwValid[SourceCount];
	static uint8_t s_arrThr[2] = { 0, 0 }, s_arrHys[2] = { 0, 0 };

	static int FpgaLevel()
	{
		if ( s_nFpga < 0 )
		{
			Fpga::Init();
			if ( Fpga::Read( 0x1A ) == 0xC0D6 )
				s_nFpga = 2;
			else if ( Fpga::Read( 0x18 ) == 0xC0D2 )
				s_nFpga = 1;
			else
				s_nFpga = 0;
		}
		return s_nFpga;
	}

	static uint32_t Counter( int nSource )
	{
		static const uint8_t arrSel[SourceCount] = { 0x40, 0x42, 0x10, 0x14 };
		uint8_t nSel = arrSel[nSource];
		uint32_t lo = Fpga::Read( nSel );     // latches the whole value
		uint32_t hi = Fpga::Read( nSel + 1 );
		return Fpga::Gray2Bin( ( hi << 16 ) | lo );
	}

	static bool HwUsed( int nSource )
	{
		for ( int i = 0; i < CSettings::TExtra::MeasSlots; i++ )
			if ( Settings.Extra.arrMeasType[i] == FreqHw && Settings.Extra.arrMeasSrc[i] == nSource )
				return true;
		return false;
	}

	void Tick()
	{
		uint32_t nNow = BIOS::SYS::GetTick();
		for ( int s = 0; s < SourceCount; s++ )
		{
			if ( !HwUsed( s ) )
			{
				s_arrTick[s] = 0;
				continue;
			}
			int nNeed = IsAnalog( s ) ? 2 : 1;
			if ( FpgaLevel() < nNeed )
				continue;
			if ( s_arrTick[s] == 0 )
			{
				s_arrCount[s] = Counter( s );
				s_arrTick[s] = nNow;
				continue;
			}
			uint32_t nDt = nNow - s_arrTick[s];
			if ( nDt < 1000 )   // gate 1 s
				continue;
			uint32_t nCount = Counter( s );
			uint32_t nEdges = nCount - s_arrCount[s];
			float fHz = nEdges * 1000.0f / nDt;
			if ( IsAnalog( s ) )
				fHz *= 0.5f;    // both edges are counted
			s_arrHw[s] = fHz;
			s_arrHwValid[s] = true;
			s_arrCount[s] = nCount;
			s_arrTick[s] = nNow;
		}
	}

	static bool HwFrequency( int nSource, float& fValue )
	{
		int nNeed = IsAnalog( nSource ) ? 2 : 1;
		if ( FpgaLevel() < nNeed )
			return false;
		if ( IsAnalog( nSource ) )
		{
			// comparator of the FPGA at the middle of the signal (raw values as in
			// the FIFO), hysteresis 1/8 of the swing; shared with the quadrature decoder
			TAnalog a;
			if ( ScanAnalog( nSource, a ) && a.nMax - a.nMin >= 8 )
			{
				uint8_t nThr = ( a.nMin + a.nMax ) / 2;
				uint8_t nHys = max( 2, ( a.nMax - a.nMin ) / 8 );
				int k = nSource;
				if ( nThr > s_arrThr[k] + 2 || nThr + 2 < s_arrThr[k] || nHys != s_arrHys[k] )
				{
					s_arrThr[k] = nThr;
					s_arrHys[k] = nHys;
					Fpga::SetParam( 0x10 + 2 * k, nThr );
					Fpga::SetParam( 0x11 + 2 * k, nHys );
				}
			}
		}
		if ( !s_arrHwValid[nSource] )
			return false;
		fValue = s_arrHw[nSource];
		return true;
	}

	// ------------------------------------------------------------ measure
	static bool SourceEnabled( int nSource )
	{
		switch ( nSource )
		{
			case CH1: return Settings.CH1.Enabled == CSettings::AnalogChannel::_YES;
			case CH2: return Settings.CH2.Enabled == CSettings::AnalogChannel::_YES;
			case CH3: return Settings.CH3.Enabled == CSettings::DigitalChannel::_YES;
			default:  return Settings.CH4.Enabled == CSettings::DigitalChannel::_YES;
		}
	}

	bool Measure( int nType, int nSource, float& fValue )
	{
		if ( !Allowed( nType, nSource ) || !SourceEnabled( nSource ) )
			return false;
		// delay / phase need the other analog channel too
		if ( ( nType == Delay || nType == Phase ) && !SourceEnabled( nSource == CH1 ? CH2 : CH1 ) )
			return false;
		float fs = SampleRate();

		switch ( nType )
		{
			case Vpp: case Vmax: case Vmin: case Vavg: case Vrms:
			{
				TAnalog a;
				if ( !ScanAnalog( nSource, a ) )
					return false;
				float fA, fB;
				Scale( nSource, fA, fB );
				float v1 = fA + fB * a.nMin, v2 = fA + fB * a.nMax;
				switch ( nType )
				{
					case Vpp:  fValue = v2 > v1 ? v2 - v1 : v1 - v2; break;
					case Vmax: fValue = v2 > v1 ? v2 : v1; break;
					case Vmin: fValue = v2 > v1 ? v1 : v2; break;
					case Vavg: fValue = fA + fB * a.fMean; break;
					default:
					{
						float fSq = fA * fA + 2 * fA * fB * a.fMean + fB * fB * a.fMeanSq;
						fValue = Sqrt( fSq );
					}
				}
				return true;
			}
			case Freq: case Period: case Duty: case WidthH: case WidthL:
			{
				TEdges e;
				if ( !ScanEdges( nSource, e ) )
					return false;
				float fPeriod = (float)( e.nLast - e.nFirst ) / ( e.nRise - 1 );   // samples
				float fDuty = (float)e.nHigh / ( e.nLast - e.nFirst );
				switch ( nType )
				{
					case Freq:   fValue = fs / fPeriod; break;
					case Period: fValue = fPeriod / fs; break;
					case Duty:   fValue = fDuty * 100.0f; break;
					case WidthH: fValue = fDuty * fPeriod / fs; break;
					default:     fValue = ( 1.0f - fDuty ) * fPeriod / fs; break;
				}
				return true;
			}
			case Rise: case Fall:
			{
				float fSamples;
				if ( !Transition( nSource, nType == Rise, fSamples ) )
					return false;
				fValue = fSamples / fs;
				return true;
			}
			case Delay: case Phase:
			{
				float fSamples;
				if ( !EdgeDelay( nSource, fSamples ) )
					return false;
				if ( nType == Delay )
				{
					fValue = fSamples / fs;
					return true;
				}
				TEdges e;
				if ( !ScanEdges( nSource, e ) )
					return false;
				float fPeriod = (float)( e.nLast - e.nFirst ) / ( e.nRise - 1 );
				float fDeg = fSamples / fPeriod * 360.0f;
				while ( fDeg >= 360.0f )
					fDeg -= 360.0f;
				fValue = fDeg;
				return true;
			}
			case FreqHw:
				return HwFrequency( nSource, fValue );
		}
		return false;
	}

	// ------------------------------------------------------------ format
	static char* PutInt( char* p, uint32_t n )
	{
		char tmp[11];
		int i = 0;
		do { tmp[i++] = '0' + n % 10; n /= 10; } while ( n );
		while ( i )
			*p++ = tmp[--i];
		return p;
	}

	// 3 significant digits: 1.23, 12.3, 123
	static char* PutValue( char* p, float a )
	{
		int nDec = a < 9.995f ? 2 : a < 99.95f ? 1 : 0;
		uint32_t nScale = nDec == 2 ? 100 : nDec == 1 ? 10 : 1;
		uint32_t n = (uint32_t)( a * nScale + 0.5f );
		p = PutInt( p, n / nScale );
		if ( nDec )
		{
			*p++ = '.';
			uint32_t f = n % nScale;
			if ( nDec == 2 && f < 10 )
				*p++ = '0';
			p = PutInt( p, f );
		}
		return p;
	}

	void FormatSI( float fValue, const char* pUnit, char* pOut )
	{
		static const char arrPrefix[] = { 'n', 'u', 'm', 0, 'k', 'M', 'G' };
		char* p = pOut;
		float a = fValue;
		if ( a < 0 )
		{
			*p++ = '-';
			a = -a;
		}
		int nPrefix = 3;
		if ( a > 0 )
		{
			while ( a >= 999.5f && nPrefix < 6 ) { a *= 1e-3f; nPrefix++; }
			while ( a < 0.9995f && nPrefix > 0 ) { a *= 1e3f; nPrefix--; }
		}
		p = PutValue( p, a );
		if ( arrPrefix[nPrefix] )
			*p++ = arrPrefix[nPrefix];
		while ( *pUnit )
			*p++ = *pUnit++;
		*p = 0;
	}

	void Format( int nType, float fValue, char* pOut )
	{
		switch ( nType )
		{
			case Vpp: case Vmax: case Vmin: case Vavg: case Vrms:
				FormatSI( fValue, "V", pOut ); break;
			case Freq: case FreqHw:
				FormatSI( fValue, "Hz", pOut ); break;
			case Duty:
			{
				char* p = PutValue( pOut, fValue < 0 ? 0 : fValue );
				*p++ = '%'; *p = 0;
				break;
			}
			case Phase:
			{
				char* p = PutValue( pOut, fValue );
				*p++ = 'd'; *p++ = 'e'; *p++ = 'g'; *p = 0;
				break;
			}
			default:
				FormatSI( fValue, "s", pOut );
		}
	}
}
