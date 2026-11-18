#include "MenuTools.h"
#include <MainWnd.h>
#include <Oscilloscope/Meas/MeasCore.h>
#include <Oscilloscope/Core/CoreOscilloscope.h>

namespace Tools
{
	uint8_t arrRef[RefWidth / 2];

	// ------------------------------------------------------------ file output
	// sector buffer: the averaging buffer of the graph (600 bytes, cleared after)
	struct TWriter
	{
		uint8_t* pBuf;
		int nPos, nTotal;
		bool bOk;

		void Put( uint8_t c )
		{
			pBuf[nPos++] = c;
			nTotal++;
			if ( nPos == BIOS::FAT::SectorSize )
			{
				if ( BIOS::FAT::Write( pBuf ) != BIOS::FAT::EOk )
					bOk = false;
				nPos = 0;
			}
		}
		void Str( const char* p )
		{
			while ( *p )
				Put( *p++ );
		}
		void Int( int n )
		{
			char tmp[12];
			int i = 0;
			if ( n < 0 )
			{
				Put( '-' );
				n = -n;
			}
			do { tmp[i++] = '0' + n % 10; n /= 10; } while ( n );
			while ( i )
				Put( tmp[--i] );
		}
		// fixed point with 3 decimals
		void Fixed3( float f )
		{
			if ( f < 0 )
			{
				Put( '-' );
				f = -f;
			}
			int n = (int)( f * 1000.0f + 0.5f );
			Int( n / 1000 );
			Put( '.' );
			int r = n % 1000;
			Put( '0' + r / 100 );
			Put( '0' + r / 10 % 10 );
			Put( '0' + r % 10 );
		}
		void Le16( uint16_t v ) { Put( v & 0xff ); Put( v >> 8 ); }
		void Le32( uint32_t v ) { Le16( v & 0xffff ); Le16( v >> 16 ); }
	};

	// first free name: pPattern "WAV000.CSV", the digits are replaced
	static bool OpenNew( char* pName, const char* pPattern )
	{
		strcpy( pName, pPattern );
		for ( int i = 0; i < 1000; i++ )
		{
			pName[3] = '0' + i / 100;
			pName[4] = '0' + i / 10 % 10;
			pName[5] = '0' + i % 10;
			if ( BIOS::FAT::Open( pName, BIOS::FAT::IoRead ) == BIOS::FAT::EOk )
			{
				BIOS::FAT::Close();
				continue;
			}
			return BIOS::FAT::Open( pName, BIOS::FAT::IoWrite ) == BIOS::FAT::EOk;
		}
		return false;
	}

	static bool Finish( TWriter& w )
	{
		if ( w.nPos > 0 )
		{
			memset( w.pBuf + w.nPos, 0, BIOS::FAT::SectorSize - w.nPos );
			if ( BIOS::FAT::Write( w.pBuf ) != BIOS::FAT::EOk )
				w.bOk = false;
		}
		if ( BIOS::FAT::Close( w.nTotal ) != BIOS::FAT::EOk )
			w.bOk = false;
		MainWnd.m_wndGraph.ClearAverage();
		return w.bOk;
	}

	static void Scale( CSettings::AnalogChannel* pCh, CSettings::Calibrator& cal, float& fA, float& fB )
	{
		CSettings::Calibrator::FastCalc fast;
		cal.Prepare( pCh, fast );
		float fProbe = CSettings::AnalogChannel::pfValueProbe[ pCh->Probe ];
		fA = fast.Q * fast.fMultiplier / 65536.0f * fProbe;
		fB = fast.K * fast.fMultiplier / 65536.0f * fProbe;
	}

	bool SaveCsv( char* pName )
	{
		if ( !OpenNew( pName, "WAV000.CSV" ) )
			return false;
		TWriter w = { MainWnd.m_wndGraph.GetScratch(), 0, 0, true };
		float fA1, fB1, fA2, fB2;
		Scale( &Settings.CH1, Settings.CH1Calib, fA1, fB1 );
		Scale( &Settings.CH2, Settings.CH2Calib, fA2, fB2 );

		char str[16];
		w.Str( "# DS203 LA104 gabmini, sample rate " );
		MeasCore::FormatSI( MeasCore::SampleRate(), "Hz", str );
		w.Str( str );
		w.Str( ", trigger at sample " );
		w.Int( CSettings::Trigger::Position );
		w.Str( "\r\nsample,CH1_V,CH2_V,CH3,CH4\r\n" );
		int nEnd = Sampler::GetValid();
		for ( int i = Settings.Time.InvalidFirst; i < nEnd && w.bOk; i++ )
		{
			uint32_t v = Sampler::GetAt( i );
			w.Int( i );
			w.Put( ',' );
			w.Fixed3( fA1 + fB1 * ( v & 0xff ) );
			w.Put( ',' );
			w.Fixed3( fA2 + fB2 * ( ( v >> 8 ) & 0xff ) );
			w.Put( ',' );
			w.Put( '0' + ( ( v >> 16 ) & 1 ) );
			w.Put( ',' );
			w.Put( '0' + ( ( v >> 17 ) & 1 ) );
			w.Str( "\r\n" );
		}
		return Finish( w );
	}

	bool SaveBmp( char* pName )
	{
		const int W = BIOS::LCD::Width, H = BIOS::LCD::Height;
		if ( !OpenNew( pName, "SCR000.BMP" ) )
			return false;
		TWriter w = { MainWnd.m_wndGraph.GetScratch(), 0, 0, true };
		const uint32_t nHeader = 14 + 40 + 12;
		const uint32_t nData = W * H * 2;
		// file header
		w.Put( 'B' ); w.Put( 'M' );
		w.Le32( nHeader + nData );
		w.Le32( 0 );
		w.Le32( nHeader );
		// BITMAPINFOHEADER, 16 bit RGB565 (BI_BITFIELDS), rows bottom up
		w.Le32( 40 );
		w.Le32( W );
		w.Le32( H );
		w.Le16( 1 );
		w.Le16( 16 );
		w.Le32( 3 );
		w.Le32( nData );
		w.Le32( 2835 ); w.Le32( 2835 );
		w.Le32( 0 ); w.Le32( 0 );
		w.Le32( 0xf800 ); w.Le32( 0x07e0 ); w.Le32( 0x001f );
		for ( int y = H - 1; y >= 0 && w.bOk; y-- )
			for ( int x = 0; x < W; x++ )
				w.Le16( BIOS::LCD::GetPixel( x, y ) );
		return Finish( w );
	}

	// ------------------------------------------------------------ zero calibration
	static const char* const CalFile = "MINICAL.CFG";
	enum { CalMagic = 0x41434d47, CalVersion = 1 };   // "GMCA"
	enum { CalPos1 = 40, CalPos2 = 200 };

	// mean ADC value of a channel in 1/2048 (as the calibration Q), -1 when
	// the samples are clipped
	static int32_t Mean2048( int ch )
	{
		int nBegin = 160, nEnd = Sampler::GetValid();
		if ( nEnd - nBegin < 100 )
			return -1;
		int32_t nSum = 0;
		for ( int i = nBegin; i < nEnd; i++ )
		{
			int v = ( Sampler::GetAt( i ) >> ( ch * 8 ) ) & 0xff;
			if ( v <= 1 || v >= 254 )
				return -1;
			nSum += v;
		}
		return (int32_t)( (int64_t)nSum * 2048 / ( nEnd - nBegin ) );
	}

	static void Capture()
	{
		CCoreOscilloscope::ConfigureAdc();
		BIOS::ADC::ConfigureTrigger( 0, 0, BIOS::ADC::ETriggerType::None, BIOS::ADC::EInput::CH1 );
		BIOS::SYS::DelayMs( 60 );       // offset DAC and input settle
		BIOS::ADC::Restart( 0 );
		uint32_t t0 = BIOS::SYS::GetTick();
		while ( BIOS::ADC::GetState() != BIOS::ADC::EState::Full && BIOS::SYS::GetTick() - t0 < 200 );
		Sampler::Copy();
	}

	static bool SaveCalibration()
	{
		uint8_t* p = MainWnd.m_wndGraph.GetScratch();
		memset( p, 0, BIOS::FAT::SectorSize );
		uint32_t* pHead = (uint32_t*)p;
		pHead[0] = CalMagic;
		pHead[1] = CalVersion;
		uint8_t* q = p + 12;
		CSettings::Calibrator* arrCal[2] = { &Settings.CH1Calib, &Settings.CH2Calib };
		for ( int c = 0; c < 2; c++ )
			for ( int r = 0; r <= CSettings::AnalogChannel::_ResolutionMax; r++ )
			{
				memcpy( q, arrCal[c]->CalData[r].m_arrCurveQin, 4 ); q += 4;
				memcpy( q, arrCal[c]->CalData[r].m_arrCurveQout, 8 ); q += 8;
			}
		uint32_t nSum = 5381;
		for ( uint8_t* i = p + 12; i < q; i++ )
			nSum = nSum * 33 + *i;
		pHead[2] = nSum;
		bool bOk = BIOS::FAT::Open( CalFile, BIOS::FAT::IoWrite ) == BIOS::FAT::EOk &&
			BIOS::FAT::Write( p ) == BIOS::FAT::EOk;
		if ( BIOS::FAT::Close( q - p ) != BIOS::FAT::EOk )
			bOk = false;
		MainWnd.m_wndGraph.ClearAverage();
		return bOk;
	}

	bool LoadCalibration()
	{
		uint8_t* p = MainWnd.m_wndGraph.GetScratch();
		if ( BIOS::FAT::Open( CalFile, BIOS::FAT::IoRead ) != BIOS::FAT::EOk )
			return false;
		bool bRead = BIOS::FAT::Read( p ) == BIOS::FAT::EOk;
		BIOS::FAT::Close();
		uint32_t* pHead = (uint32_t*)p;
		const int nData = 2 * ( CSettings::AnalogChannel::_ResolutionMax + 1 ) * 12;
		uint32_t nSum = 5381;
		for ( uint8_t* i = p + 12; i < p + 12 + nData; i++ )
			nSum = nSum * 33 + *i;
		bool bOk = bRead && pHead[0] == CalMagic && pHead[1] == CalVersion && pHead[2] == nSum;
		if ( bOk )
		{
			uint8_t* q = p + 12;
			CSettings::Calibrator* arrCal[2] = { &Settings.CH1Calib, &Settings.CH2Calib };
			for ( int c = 0; c < 2; c++ )
				for ( int r = 0; r <= CSettings::AnalogChannel::_ResolutionMax; r++ )
				{
					memcpy( arrCal[c]->CalData[r].m_arrCurveQin, q, 4 ); q += 4;
					memcpy( arrCal[c]->CalData[r].m_arrCurveQout, q, 8 ); q += 8;
				}
		}
		MainWnd.m_wndGraph.ClearAverage();
		return bOk;
	}

	bool ZeroCalibrate()
	{
		CSettings::AnalogChannel* arrCh[2] = { &Settings.CH1, &Settings.CH2 };
		CSettings::Calibrator* arrCal[2] = { &Settings.CH1Calib, &Settings.CH2Calib };
		CSettings::AnalogChannel arrSaved[2] = { Settings.CH1, Settings.CH2 };
		CSettings::TimeBase::EResolution savedTime = Settings.Time.Resolution;

		// 100 us/div: 4096 samples in 14 ms, the mean of them
		Settings.Time.Resolution = CSettings::TimeBase::_100us;
		bool bOk = true;
		for ( int r = 0; r <= CSettings::AnalogChannel::_ResolutionMax && bOk; r++ )
		{
			int32_t arrMean[2][2];
			for ( int k = 0; k < 2 && bOk; k++ )
			{
				for ( int c = 0; c < 2; c++ )
				{
					arrCh[c]->Resolution = (CSettings::AnalogChannel::eResolution)r;
					arrCh[c]->Coupling = CSettings::AnalogChannel::_DC;
					arrCh[c]->u16Position = k ? CalPos2 : CalPos1;
				}
				Capture();
				for ( int c = 0; c < 2; c++ )
				{
					arrMean[c][k] = Mean2048( c );
					if ( arrMean[c][k] < 0 )
						bOk = false;
				}
			}
			if ( !bOk )
				break;
			// 0 V: Q + raw * K = 0 with K = 2048
			for ( int c = 0; c < 2; c++ )
			{
				CSettings::LinCalibCurve& cur = arrCal[c]->CalData[r];
				cur.m_arrCurveQin[0] = CalPos1;
				cur.m_arrCurveQin[1] = CalPos2;
				cur.m_arrCurveQout[0] = -arrMean[c][0];
				cur.m_arrCurveQout[1] = -arrMean[c][1];
			}
		}

		Settings.CH1 = arrSaved[0];
		Settings.CH2 = arrSaved[1];
		Settings.Time.Resolution = savedTime;
		if ( !bOk )
			LoadCalibration();          // back to the stored (or default) values
		CCoreOscilloscope::ConfigureAdc();
		CCoreOscilloscope::ConfigureTrigger();
		return bOk && SaveCalibration();
	}

	void StoreRef()
	{
		CSettings::Calibrator::FastCalc fast;
		Settings.CH1Calib.Prepare( &Settings.CH1, fast );
		int nIndex = Settings.Time.Shift;
		for ( int x = 0; x < RefWidth / 2; x++, nIndex += 2 )
		{
			uint32_t v = Sampler::GetAt( nIndex );
			if ( v == BIOS::ADC::NoData )
			{
				arrRef[x] = 0xff;
				continue;
			}
			int y = Settings.CH1Calib.Correct( fast, (int)( v & 0xff ) );
			if ( y < 0 ) y = 0;
			if ( y > 255 ) y = 255;
			arrRef[x] = ( y * ( CWndGraph::DivsY * CWndGraph::BlkY ) ) >> 8;
		}
	}
}

// ---------------------------------------------------------------- item
static const char* const s_arrMath[] = { "Off", "A+B", "A-B", "B-A", "A*B" };
static const char* const s_arrFft[] = { "Off", "CH1", "CH2" };
static const char* const s_arrIds[CItemTools::Count] = { "Math", "V/d", "FFT", "Ref", "Save CSV", "Save BMP", "Zero cal" };
// rows of a tool: label above the value, or both on one row
static const uint8_t s_arrRows[CItemTools::Count] = { 2, 1, 1, 1, 2, 2, 2 };

void CItemTools::Create( CWnd* pParent )
{
	m_nSel = 0;
	m_bArmed = false;
	m_strInfo[0][0] = m_strInfo[1][0] = m_strInfo[2][0] = 0;
	int nRows = 0;
	for ( int i = 0; i < Count; i++ )
		nRows += s_arrRows[i];
	CWndMenuItem::Create( NULL, RGB565(b0b0b0), nRows, pParent );
}

void CItemTools::OnPaint()
{
	CWndMenuItem::OnPaint();
	int x = m_rcClient.left + 12 + MarginLeft - 9;
	int y = m_rcClient.top;
	for ( int i = 0; i < Count; i++ )
	{
		int h = s_arrRows[i] * 17;
		ui16 clr = RGB565(000000);
		if ( HasFocus() && i == m_nSel )
		{
			BIOS::LCD::Bar( x - 2, y, m_rcClient.right - 3, y + h - 1, RGB565(000000) );
			clr = RGB565(ffffff);
		}
		int w = BIOS::LCD::Print( x, y, clr, RGBTRANS, s_arrIds[i] );
		const char* p = "";
		switch ( i )
		{
			case Math: p = s_arrMath[ Settings.Extra.nMath ]; break;
			case MathRes: p = CSettings::AnalogChannel::ppszTextResolution[ Settings.Extra.nMathRes ]; break;
			case Fft: p = s_arrFft[ Settings.Extra.nFft ]; break;
			case Ref: p = Settings.Extra.nRef ? "On" : "Off"; break;
			case Zero:
				p = m_bArmed ? "sure? F4" : m_strInfo[2][0] ? m_strInfo[2] : "F4";
				break;
			default: p = m_strInfo[i - Csv][0] ? m_strInfo[i - Csv] : "F4: save"; break;
		}
		if ( s_arrRows[i] == 2 )
			BIOS::LCD::Print( x + 8, y + 16, clr, RGBTRANS, p );
		else
			BIOS::LCD::Print( x + w + 8, y, clr, RGBTRANS, p );
		y += h;
	}
}

void CItemTools::OnKey( int nKey )
{
	bool bLeft = nKey == BIOS::KEY::Left, bRight = nKey == BIOS::KEY::Right;
	bool bEnter = nKey == BIOS::KEY::Enter;
	CSettings::TExtra& e = Settings.Extra;
	bool bGraph = false;

	if ( nKey != BIOS::KEY::Enter )
		m_bArmed = false;
	if ( nKey == BIOS::KEY::Up && m_nSel > 0 )
	{
		m_nSel--;
		Invalidate();
		return;
	}
	if ( nKey == BIOS::KEY::Down && m_nSel < Count - 1 )
	{
		m_nSel++;
		Invalidate();
		return;
	}

	switch ( m_nSel )
	{
		case Math:
			if ( bLeft || bRight )
			{
				e.nMath = ( e.nMath + ( bRight ? 1 : 4 ) ) % 5;
				bGraph = true;
			}
			break;
		case MathRes:
			if ( bLeft && e.nMathRes > 0 ) { e.nMathRes--; bGraph = true; }
			if ( bRight && e.nMathRes < CSettings::AnalogChannel::_ResolutionMax ) { e.nMathRes++; bGraph = true; }
			break;
		case Fft:
			if ( bLeft || bRight )
			{
				e.nFft = ( e.nFft + ( bRight ? 1 : 2 ) ) % 3;
				MainWnd.m_wndGraph.ClearAverage();
				bGraph = true;
			}
			break;
		case Ref:
			if ( bLeft || bRight )
			{
				e.nRef = !e.nRef;
				bGraph = true;
			}
			if ( bEnter )
			{
				Tools::StoreRef();
				e.nRef = 1;
				bGraph = true;
			}
			break;
		case Zero:
			if ( bEnter )
			{
				if ( !m_bArmed )
				{
					m_bArmed = true;
					Invalidate();
					return;
				}
				m_bArmed = false;
				strcpy( m_strInfo[2], "running" );
				Invalidate();
				WindowMessage( CWnd::WmPaint );
				strcpy( m_strInfo[2], Tools::ZeroCalibrate() ? "done" : "Error" );
				Invalidate();
				MainWnd.m_wndGraph.Invalidate();
				MainWnd.m_wndLReferences.Invalidate();
				return;
			}
			break;
		case Csv:
		case Bmp:
			if ( bEnter )
			{
				char strName[14];
				Invalidate();   // the screen as it is, without the result text
				bool bOk = m_nSel == Csv ? Tools::SaveCsv( strName ) : Tools::SaveBmp( strName );
				strcpy( m_strInfo[m_nSel - Csv], bOk ? strName : "Error" );
				Invalidate();
				return;
			}
			break;
	}
	if ( bGraph )
	{
		Invalidate();
		MainWnd.m_wndGraph.Invalidate();
		return;
	}
	if ( bLeft || bRight || bEnter )
		return;
	CWndMenuItem::OnKey( nKey );
}

// ---------------------------------------------------------------- menu
/*virtual*/ void CWndMenuTools::Create( CWnd* pParent, int dwFlags )
{
	CWnd::Create( "CWndMenuTools", dwFlags, CRect( 320 - CWndMenuItem::MarginLeft, 20, 400, 240 ), pParent );
	CSettings::TExtra& e = Settings.Extra;
	if ( e.nMath > 4 ) e.nMath = 0;
	if ( e.nMathRes > CSettings::AnalogChannel::_ResolutionMax ) e.nMathRes = CSettings::AnalogChannel::_1V;
	if ( e.nFft > 2 ) e.nFft = 0;
	e.nRef = 0;     // the reference trace is not stored
	memset( Tools::arrRef, 0xff, sizeof( Tools::arrRef ) );
	m_itmTools.Create( this );
}

/*virtual*/ void CWndMenuTools::OnMessage( CWnd* pSender, int code, uintptr_t data )
{
	if ( code == ToWord('L', 'D') )
	{
		MainWnd.m_wndGraph.ShowWindow( false );
		MainWnd.m_wndLReferences.ShowWindow( false );
		MainWnd.m_wndTReferences.ShowWindow( false );
		return;
	}
	if ( code == ToWord('L', 'E') )
	{
		MainWnd.m_wndGraph.ShowWindow( true );
		MainWnd.m_wndLReferences.ShowWindow( true );
		MainWnd.m_wndTReferences.ShowWindow( true );
		return;
	}
}
