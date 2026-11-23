#include "AutoSet.h"
#include "CoreOscilloscope.h"
#include <Framework.h>
#include <Settings.h>

// The screen shows the 8 bit ADC value 0..255 as 8 divisions: 32 counts
// per division. Everything here works on raw counts; the V/div only scales
// them, so the probe factor does not matter.

namespace AutoSet
{
	enum
	{
		First = 160,        // samples before it are the pre-trigger part
		Center = 128,       // middle of the screen
		CountsPerDiv = 32,
		PosMin = -20, PosMax = 300   // range of AnalogChannel::u16Position (SettingsStore)
	};

	struct TStat
	{
		int nMin, nMax;
		int Span() const { return nMax - nMin; }
		int Mid() const { return (nMin + nMax) / 2; }
		bool Clipped() const { return nMin <= 2 || nMax >= 253; }
	};

	// sampling rate of a time base (as BIOS::ADC::ConfigureTimebase)
	static float SampleRate( int nRes )
	{
#if defined(DS203)
		static const uint32_t arrDiv[] = {1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
			12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000};
		if ( nRes >= (int)COUNT(arrDiv) )
			nRes = COUNT(arrDiv) - 1;
		return 72e6f / arrDiv[nRes];
#else
		return 30.0f / CSettings::TimeBase::pfValueResolution[nRes];
#endif
	}

	static CSettings::AnalogChannel* Channel( int ch )
	{
		return ch == 0 ? &Settings.CH1 : &Settings.CH2;
	}

	static bool Enabled( int ch )
	{
		return Channel(ch)->Enabled == CSettings::AnalogChannel::_YES;
	}

	// one acquisition without trigger, at most nTimeoutMs; returns the number
	// of valid samples
	static int Capture( int nTimeoutMs )
	{
		BIOS::ADC::ConfigureTrigger( 0, 0, BIOS::ADC::ETriggerType::None, BIOS::ADC::EInput::CH1 );
		BIOS::SYS::DelayMs( 20 );    // offset DAC and input stage settle
		BIOS::ADC::Restart( 0 );
		uint32_t t0 = BIOS::SYS::GetTick();
		while ( (int)(BIOS::SYS::GetTick() - t0) < nTimeoutMs )
		{
			BIOS::ADC::EState state = BIOS::ADC::GetState();
#ifdef DS203
			if ( state == BIOS::ADC::EState::Full )
#else
			if ( state == BIOS::ADC::EState::Full || state == BIOS::ADC::EState::Triggered )
#endif
				break;
		}
		Sampler::Copy();
		return Sampler::GetValid();
	}

	static int Sample( int i, int ch )
	{
		return ( Sampler::GetAt(i) >> (ch * 8) ) & 0xff;
	}

	static TStat Stat( int ch, int nValid )
	{
		TStat st = { 255, 0 };
		for ( int i = First; i < nValid; i++ )
		{
			int v = Sample( i, ch );
			st.nMin = min( st.nMin, v );
			st.nMax = max( st.nMax, v );
		}
		return st;
	}

	// acquisition time of the whole buffer + margin, at most 1.5 s
	static int Timeout()
	{
		float fMs = BIOS::ADC::NumSamples * 1000.0f / SampleRate( Settings.Time.Resolution );
		int n = (int)fMs * 6 / 5 + 50;
		return min( n, 1500 );
	}

	static int Clamp( int v, int lo, int hi )
	{
		return v < lo ? lo : v > hi ? hi : v;
	}

	// rising crossings of the middle with hysteresis: number, first, last
	static int Crossings( int ch, int nValid, const TStat& st, int& nFirst, int& nLast )
	{
		int nMid = st.Mid();
		int nHys = max( 2, st.Span() / 4 );
		int nHigh = nMid + nHys / 2, nLow = nMid - nHys / 2;
		bool bHigh = Sample( First, ch ) > nMid;
		int n = 0;
		nFirst = nLast = -1;
		for ( int i = First + 1; i < nValid; i++ )
		{
			int v = Sample( i, ch );
			if ( !bHigh && v >= nHigh )
			{
				bHigh = true;
				if ( nFirst < 0 )
					nFirst = i;
				nLast = i;
				n++;
			} else
			if ( bHigh && v <= nLow )
				bHigh = false;
		}
		return n;
	}

	EResult Run()
	{
		CSettings::AnalogChannel::eResolution arrOrigRes[2] = { Settings.CH1.Resolution, Settings.CH2.Resolution };
		si16 arrOrigPos[2] = { Settings.CH1.u16Position, Settings.CH2.u16Position };
		CSettings::TimeBase::EResolution origTime = Settings.Time.Resolution;

		// ---- vertical: start at 10 V/div, 1 ms/div (window of 136 ms, min / max
		// of a faster signal still come from its samples)
		Settings.Time.Resolution = CSettings::TimeBase::_1ms;
		for ( int ch = 0; ch < 2; ch++ )
			if ( Enabled(ch) )
				Channel(ch)->Resolution = CSettings::AnalogChannel::_10V;

		// counts per position step, measured: position +40 moves the trace
		float arrGain[2] = { 0, 0 };
		CCoreOscilloscope::ConfigureAdc();
		int nValid = Capture( Timeout() );
		TStat arrSt0[2] = { Stat(0, nValid), Stat(1, nValid) };
		// move up by 40, or down when there is no room
		int arrStep[2] = { 0, 0 };
		for ( int ch = 0; ch < 2; ch++ )
			if ( Enabled(ch) )
			{
				int nPos = Channel(ch)->u16Position;
				int nNew = nPos + 40 <= PosMax ? nPos + 40 : nPos - 40;
				arrStep[ch] = nNew - nPos;
				Channel(ch)->u16Position = nNew;
			}
		CCoreOscilloscope::ConfigureAdc();
		nValid = Capture( Timeout() );
		for ( int ch = 0; ch < 2; ch++ )
		{
			if ( !Enabled(ch) )
				continue;
			TStat st = Stat( ch, nValid );
			if ( arrStep[ch] != 0 )
				arrGain[ch] = (float)( st.Mid() - arrSt0[ch].Mid() ) / arrStep[ch];
		}

		TStat arrSt[2] = { {0, 0}, {0, 0} };
		for ( int nIter = 0; nIter < 8; nIter++ )
		{
			bool bChanged = false;
			for ( int ch = 0; ch < 2; ch++ )
			{
				if ( !Enabled(ch) )
					continue;
				CSettings::AnalogChannel* pCh = Channel(ch);
				TStat st = Stat( ch, nValid );
				arrSt[ch] = st;
				int nRes = (int)pCh->Resolution;

				// middle of the trace to the middle of the screen
				bool bCentered = st.Mid() > Center - 10 && st.Mid() < Center + 10;
				if ( !bCentered && ( arrGain[ch] > 0.2f || arrGain[ch] < -0.2f ) )
				{
					int nPos = pCh->u16Position + (int)( ( Center - st.Mid() ) / arrGain[ch] );
					nPos = Clamp( nPos, PosMin, PosMax );
					if ( nPos != pCh->u16Position )
					{
						pCh->u16Position = nPos;
						bChanged = true;
						continue;   // measure again before the range
					}
				}

				int nNewRes = nRes;
				if ( st.Clipped() )
				{
					// too large (it is centered): next range
					if ( nRes < CSettings::AnalogChannel::_10V )
						nNewRes = nRes + 1;
				} else
				if ( st.Span() < 3 )
				{
					// nothing measurable at this range: three ranges down (x 1/10)
					nNewRes = max( 0, nRes - 3 );
				} else
				{
					// smallest range where the signal takes at most 6 divisions
					float fVolts = (float)st.Span() / CountsPerDiv *
						CSettings::AnalogChannel::pfValueResolution[nRes];
					nNewRes = CSettings::AnalogChannel::_10V;
					for ( int r = 0; r <= CSettings::AnalogChannel::_10V; r++ )
						if ( fVolts <= 6.0f * CSettings::AnalogChannel::pfValueResolution[r] )
						{
							nNewRes = r;
							break;
						}
				}
				if ( nNewRes != nRes )
				{
					pCh->Resolution = (CSettings::AnalogChannel::eResolution)nNewRes;
					bChanged = true;
				}
			}
			if ( !bChanged )
				break;
			CCoreOscilloscope::ConfigureAdc();
			nValid = Capture( Timeout() );
		}

		// a channel without a signal keeps its old range and position
		int nSource = -1, nBestSpan = 0;
		for ( int ch = 0; ch < 2; ch++ )
		{
			if ( !Enabled(ch) )
				continue;
			if ( arrSt[ch].Span() < 16 )   // half a division at the smallest range
			{
				Channel(ch)->Resolution = arrOrigRes[ch];
				Channel(ch)->u16Position = arrOrigPos[ch];
			} else
			if ( arrSt[ch].Span() > nBestSpan )
			{
				nBestSpan = arrSt[ch].Span();
				nSource = ch;
			}
		}

		if ( nSource < 0 )
		{
			Settings.Time.Resolution = origTime;
			CCoreOscilloscope::ConfigureAdc();
			CCoreOscilloscope::ConfigureTrigger();
			return NoSignal;
		}

		// ---- time base: from fast to slow, so a fast signal is never undersampled;
		// the first rate with at least 3 periods in the buffer gives the period
		TStat stSrc = arrSt[nSource];
		int nRes = CSettings::TimeBase::_1us;
		float fPeriod = 0;
		for ( int nIter = 0; nIter < 8; nIter++ )
		{
			Settings.Time.Resolution = (CSettings::TimeBase::EResolution)nRes;
			CCoreOscilloscope::ConfigureAdc();
			nValid = Capture( Timeout() );
			int nFirst, nLast;
			int n = Crossings( nSource, nValid, stSrc, nFirst, nLast );
			if ( n >= 3 )
			{
				int nPeriod = ( nLast - nFirst ) / ( n - 1 );
				if ( nPeriod < 12 && nRes > CSettings::TimeBase::_200ns )
				{
					nRes = max( (int)CSettings::TimeBase::_200ns, nRes - 2 );
					continue;
				}
				fPeriod = (float)( nLast - nFirst ) / ( n - 1 ) / SampleRate( nRes );
				break;
			}
			if ( nRes >= CSettings::TimeBase::_20ms )
				break;   // slower than ~7 Hz, or not periodic
			nRes = min( nRes + 3, (int)CSettings::TimeBase::_20ms );
		}

		if ( fPeriod > 0 )
		{
			// smallest time base with at least 2.5 periods on the 10 divisions
			int nTime = CSettings::TimeBase::_ResolutionMax;
			for ( int r = 0; r <= CSettings::TimeBase::_ResolutionMax; r++ )
				if ( CSettings::TimeBase::pfValueResolution[r] * 10.0f >= fPeriod * 2.5f )
				{
					nTime = r;
					break;
				}
			Settings.Time.Resolution = (CSettings::TimeBase::EResolution)nTime;
		} else
			Settings.Time.Resolution = CSettings::TimeBase::_1ms;

		// ---- trigger: rising edge in the middle of the source channel
		Settings.Trig.Source = nSource == 0 ? CSettings::Trigger::_CH1 : CSettings::Trigger::_CH2;
		Settings.Trig.Type = CSettings::Trigger::_EdgeLH;
		Settings.Trig.nLevel = stSrc.Mid();
		Settings.Trig.Sync = CSettings::Trigger::_Auto;
		Settings.Trig.State = CSettings::Trigger::_Run;
		Settings.Trig.nLastChange = BIOS::SYS::GetTick();
		Settings.Time.InvalidFirst = 30;
		if ( Settings.Time.Shift < Settings.Time.InvalidFirst )
			Settings.Time.Shift = Settings.Time.InvalidFirst;

		CCoreOscilloscope::ConfigureAdc();
		CCoreOscilloscope::ConfigureTrigger();
		return Ok;
	}
}
