#include "GraphOsc.h"
#include <MainWnd.h>
#include "Phosphor.h"
#include <Oscilloscope/Core/CoreOscilloscope.h>
#include <Oscilloscope/Tools/Fft.h>
#include <Oscilloscope/Tools/MenuTools.h>

// beam width of an analog trace (channel list: Beam): the segment of the
// column is joined with the segments of the previous columns, so steep edges
// get wider too, and it is widened vertically for flat parts
struct TBeam
{
	int nB[2], nT[2];
	bool bInit;
};

static void DrawBeam( ui16* column, TBeam& b, int nBottom, int nTop, int nWidth, ui16 clr )
{
	if ( !b.bInit )
	{
		b.nB[0] = b.nB[1] = nBottom;
		b.nT[0] = b.nT[1] = nTop;
		b.bInit = true;
	}
	int nB = nBottom, nT = nTop;
	for ( int i = 0; i < nWidth - 1 && i < 2; i++ )
	{
		nB = min( nB, b.nB[i] );
		nT = max( nT, b.nT[i] );
	}
	if ( nWidth >= 2 )
		nT++;
	if ( nWidth >= 3 )
		nB--;
	if ( nB < 0 )
		nB = 0;
	if ( nT > CWndGraph::DivsY * CWndGraph::BlkY - 1 )
		nT = CWndGraph::DivsY * CWndGraph::BlkY - 1;
	for ( int _y = nB; _y <= nT; _y++ )
		column[_y] = clr;
	b.nB[1] = b.nB[0]; b.nT[1] = b.nT[0];
	b.nB[0] = nBottom; b.nT[0] = nTop;
}

CWndOscGraph::CWndOscGraph()
{
	ClearAverage();
	m_bPersistReset = false;
	m_bNeedRedraw = true;
	m_nPhosphorSeq = 0;
	m_nXyKey = 0;
}

// settings that change the meaning of the stored history
uint32_t CWndOscGraph::_PersistKey()
{
	const int arr[] = { Settings.Time.Resolution, Settings.Time.Shift, Settings.Disp.Axes,
		Settings.Disp.Persist, Settings.Disp.Average,
		Settings.CH1.Enabled, Settings.CH1.Resolution, Settings.CH1.u16Position, Settings.CH1.Coupling,
		Settings.CH2.Enabled, Settings.CH2.Resolution, Settings.CH2.u16Position, Settings.CH2.Coupling };
	uint32_t nKey = 5381;
	for ( int i = 0; i < (int)COUNT(arr); i++ )
		nKey = nKey * 33 + (uint32_t)arr[i];
	return nKey;
}

// analog channels of one column from the history: phosphor (graded) or the
// envelope of the last frames
void CWndOscGraph::_PhosphorColumn( ui16* column, int x, bool bLines, bool bGraded,
	bool en1, bool en2, ui16 clr1, ui16 clr2 )
{
	static ui16 arrLut1[Phosphor::LutSize], arrLut2[Phosphor::LutSize];
	static ui16 clrLut1 = 0, clrLut2 = 0;
	if ( bGraded && (clrLut1 != clr1 || arrLut1[Phosphor::LutSize-1] == 0) )
	{
		clrLut1 = clr1;
		Phosphor::MakeLut( clr1, arrLut1 );
	}
	if ( bGraded && (clrLut2 != clr2 || arrLut2[Phosphor::LutSize-1] == 0) )
	{
		clrLut2 = clr2;
		Phosphor::MakeLut( clr2, arrLut2 );
	}

	uint8_t* arrSum1 = Phosphor::Scratch();
	uint8_t* arrSum2 = arrSum1 + Phosphor::Height;
	if ( en1 )
		Phosphor::SumColumn( x, 0, bLines, bGraded, arrSum1 );
	else
		memset( arrSum1, 0, Phosphor::Height );
	if ( en2 )
		Phosphor::SumColumn( x, 1, bLines, bGraded, arrSum2 );
	else
		memset( arrSum2, 0, Phosphor::Height );

	for ( int y = 0; y < Phosphor::Height; y++ )
	{
		int v1 = arrSum1[y], v2 = arrSum2[y];
		if ( v1 == 0 && v2 == 0 )
			continue;
		if ( bGraded )
			column[y] = v1 >= v2 ? arrLut1[Phosphor::LutIndex(v1)] : arrLut2[Phosphor::LutIndex(v2)];
		else
			column[y] = v1 ? clr1 : clr2;   // CH1 on top
	}
}

void CWndOscGraph::_PrepareColumn( ui16 *column, ui16 n, ui16 clr )
{
	for (ui16 y=0; y<DivsY*BlkY; y++)
		column[y] = clr;
//		memset( column, clr, DivsY*BlkY*2 );
	if ( n == 0 )
		return;
	// Grid display logic
	if(Settings.Disp.Grid != CSettings::Display::_GridNone)
	{
		if ( (n % BlkX) == 0)
		{	// Display vertical line or dots
			ui8 inc = (Settings.Disp.Grid == CSettings::Display::_GridLines) ? 1 : 5;
			for (ui16 y=0; y<DivsY*BlkY; y += inc)
				column[y] = RGB565(808080);
		} else
		if ((Settings.Disp.Grid == CSettings::Display::_GridLines) || ((n%6) == 0 ))
		{	// Display horizontal line or dots
			for (ui16 y=BlkY; y<DivsY*BlkY-1; y += BlkY)
				column[y] = RGB565(808080);
		}
	}
	// Axis display logic
	if(Settings.Disp.Axis != CSettings::Display::_AxisNone)
	{
		if (n == CenterX)
		{	// Display center X axis
			for (ui16 y=0; y<DivsY*BlkY; y += 1)
				column[y] = RGB565(808080);
		}
		else if( (n==1) || (n == (MaxX-1)) || (n == CenterX-1) || (n == CenterX+1))
		{	// Display X axis sub divisions
			for (ui16 y=5; y<DivsY*BlkY; y += 5)
				column[y] = RGB565(808080);
		}
		else if( (n==2) || (n == (MaxX-2)) || (n == CenterX-2) || (n == CenterX+2))
		{	// Display X axis main divisions
			for (ui16 y=BlkY; y<DivsY*BlkY; y += BlkY)
				column[y] = RGB565(808080);
		}
		// Display horizontal main axis
		column[CenterY] = RGB565(808080);
		if (Settings.Disp.Axis == CSettings::Display::_AxisDouble)
		{	// Display horizontal double axis
			column[CenterTop] = RGB565(808080);
			column[CenterBottom] = RGB565(808080);
		}
		if((n%6) == 0 )
		{	// Displau horizontal sub divisions
			column[0] = RGB565(808080);
			column[CenterY-1] = RGB565(808080);
			column[CenterY+1] = RGB565(808080);
			column[MaxY-1] = RGB565(808080);
			if (Settings.Disp.Axis == CSettings::Display::_AxisDouble)
			{
				column[CenterTop+1] = RGB565(808080);
				column[CenterTop-1] = RGB565(808080);
				column[CenterBottom+1] = RGB565(808080);
				column[CenterBottom-1] = RGB565(808080);
			}
		}
		if((n%BlkX) == 0 )
		{	// Display horizontal main divisions
			column[1] = RGB565(808080);
			column[CenterY+2] = RGB565(808080);
			column[CenterY-2] = RGB565(808080);
			column[MaxY-2] = RGB565(808080);
			if (Settings.Disp.Axis == CSettings::Display::_AxisDouble)
			{
				column[CenterTop+2] = RGB565(808080);
				column[CenterTop-2] = RGB565(808080);
				column[CenterBottom+2] = RGB565(808080);
				column[CenterBottom-2] = RGB565(808080);
			}
		}
	}
}

ui16 CWndOscGraph::_Interpolate( ui16 clrA, ui16 clrB )
{
	int ar = Get565R(clrA);
	int ag = Get565G(clrA);
	int ab = Get565B(clrA);
	int br = Get565R(clrB);
	int bg = Get565G(clrB);
	int bb = Get565B(clrB);
	ar = (ar+br)/2;
	ag = (ag+bg)/2;
	ab = (ab+bb)/2;
	return RGB565RGB(ar, ag, ab);
}

/*virtual*/ void CWndOscGraph::Create(CWnd *pParent, int dwFlags)
{
	CWndGraph::Create( pParent, dwFlags | CWnd::WsListener );
}

/*virtual*/ void CWndOscGraph::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if ( pSender == NULL && code == WmBroadcast && data == ToWord('d', 'g') )
	{
		DecodeUi::Update();
		Invalidate();
		CCoreOscilloscope::UpdateConstants();
		return;
	}
	if ( pSender == this && code == ToWord('S', 'H') && data == 1 )
	{
		m_bNeedRedraw = true;
	}
}

/*virtual*/ void CWndOscGraph::OnPaint()
{
	if ( Settings.Extra.nFft )
		Fft::Paint( this, Settings.Extra.nFft, GetScratch() );
	else
	if ( Settings.Disp.Axes == CSettings::Display::_TY )
		OnPaintTY();
	else
		OnPaintXY();

	// acquisition stopped (F1 or Single), the waveform is frozen
	if ( Settings.Trig.State == CSettings::Trigger::_Stop && !CWnd::GetOverlay().IsValid() )
	{
		int x = m_rcClient.right - 4*8 - 4, y = m_rcClient.top + 2;
		BIOS::LCD::Bar( x - 2, y, x + 4*8 + 2, y + 14, RGB565(c00000) );
		UI::Print( x, y, RGB565(ffffff), RGBTRANS, "STOP" );
	} else
		PaintTrigState();
}

extern long m_lLastAcquired;

void CWndOscGraph::PaintTrigState()
{
	bool bNorm = Settings.Trig.Sync == CSettings::Trigger::_Norm ||
		Settings.Trig.Sync == CSettings::Trigger::_Single;
	if ( !bNorm || Settings.Trig.State == CSettings::Trigger::_Stop || CWnd::GetOverlay().IsValid() ||
		Settings.Extra.nFft || Settings.Disp.Axes != CSettings::Display::_TY )
		return;
	// no trigger for longer than one buffer (+ 0.3 s): the level or the edge
	// does not match the signal (the FPGA needs the level +-12 counts inside it)
	float fBufferMs = CCoreOscilloscope::BufferLength() * 1000.0f * Settings.Runtime.m_fTimeRes / BlkX;
	bool bWait = BIOS::SYS::GetTick() - m_lLastAcquired > (long)fBufferMs + 300;
	int x = m_rcClient.right - 4*8 - 4, y = m_rcClient.top + 2;
	BIOS::LCD::Bar( x - 2, y, x + 4*8 + 2, y + 14, bWait ? RGB565(c0a000) : RGB565(00a000) );
	UI::Print( x, y, RGB565(ffffff), RGBTRANS, bWait ? "WAIT" : "TRIG" );
}

// X-Y (Lissajous figures): CH1 horizontal and CH2 vertical (Y-X swapped) in
// a square of 8 x 8 divisions in the middle of the graph, all samples of the
// acquisition, dots or lines between the samples.
// Persist No: every acquisition alone; Yes: accumulates on the screen until
// a setting changes; Phosphor: the last frames fade out
void CWndOscGraph::_XyGrid( const CRect& rc )
{
	ui16 clrGrid = RGB565(404040), clrAxis = RGB565(707070);
	if ( Settings.Disp.Grid != CSettings::Display::_GridNone )
	{
		int nStep = Settings.Disp.Grid == CSettings::Display::_GridLines ? 1 : 5;
		for ( int i = 1; i < DivsY; i++ )
		{
			int nPos = i * BlkY;
			ui16 clr = i == DivsY/2 ? clrAxis : clrGrid;
			for ( int j = 0; j < rc.Height(); j += nStep )
			{
				BIOS::LCD::PutPixel( rc.left + nPos, rc.top + j, clr );
				BIOS::LCD::PutPixel( rc.left + j, rc.top + nPos, clr );
			}
		}
	}
	BIOS::LCD::Rectangle( rc, RGB565(505050) );
}

void CWndOscGraph::_XyLine( const CRect& rc, int x1, int y1, int x2, int y2, ui16 clr )
{
	// coordinates 0..199, y up
	int dx = x2 > x1 ? x2 - x1 : x1 - x2, sx = x1 < x2 ? 1 : -1;
	int dy = y2 > y1 ? y1 - y2 : y2 - y1, sy = y1 < y2 ? 1 : -1;
	int err = dx + dy;
	while ( true )
	{
		BIOS::LCD::PutPixel( rc.left + x1, rc.bottom - 1 - y1, clr );
		if ( x1 == x2 && y1 == y2 )
			break;
		int e2 = 2 * err;
		if ( e2 >= dy ) { err += dy; x1 += sx; }
		if ( e2 <= dx ) { err += dx; y1 += sy; }
	}
}

void CWndOscGraph::OnPaintXY()
{
	if ( CWnd::GetOverlay().IsValid() )
		return;

	ui8 bEnabled1 = Settings.CH1.Enabled == CSettings::AnalogChannel::_YES;
	ui8 bEnabled2 = Settings.CH2.Enabled == CSettings::AnalogChannel::_YES;
	ui16 clrPoint = _Interpolate( Settings.CH1.u16Color, Settings.CH2.u16Color );
	bool bLines = Settings.Disp.Draw != CSettings::Display::_Points;
	bool bSwap = Settings.Disp.Axes == CSettings::Display::_YX;

	CSettings::Calibrator::FastCalc calCh1, calCh2;
	Settings.CH1Calib.Prepare( &Settings.CH1, calCh1 );
	Settings.CH2Calib.Prepare( &Settings.CH2, calCh2 );

	{
		CRect rc = m_rcClient;
		rc.Inflate( 1, 1, 1, 1 );
		BIOS::LCD::Rectangle( rc, RGB565(b0b0b0) );
	}
	const int nSize = DivsY * BlkY;
	CRect rcXy( m_rcClient.left + (m_rcClient.Width() - nSize) / 2, m_rcClient.top,
		m_rcClient.left + (m_rcClient.Width() - nSize) / 2 + nSize, m_rcClient.top + nSize );

	int nPersist = Settings.Disp.Persist;
	uint32_t nKey = _PersistKey();
	bool bClear = nPersist != CSettings::Display::_PerYes || m_bPersistReset || m_bNeedRedraw || nKey != m_nXyKey;
	m_nXyKey = nKey;
	m_bPersistReset = false;
	m_bNeedRedraw = false;
	if ( bClear )
	{
		BIOS::LCD::Bar( m_rcClient, 0x0101 );
		_XyGrid( rcXy );
	}

	if ( !bEnabled1 || !bEnabled2 )
	{
		UI::Print( m_rcClient.left + 4, m_rcClient.top + 4, RGB565(ffffff), RGBTRANS, "Enable CH1 and CH2" );
		return;
	}

	// coordinates of a sample, 0..nSize-1
	auto Point = [&](int nIndex, int& x, int& y) -> bool
	{
		ui32 nSample = Sampler::GetAt( nIndex );
		if ( nSample == BIOS::ADC::NoData )
			return false;
		int ch1 = Settings.CH1Calib.Correct( calCh1, (ui8)(nSample & 0xff) );
		int ch2 = Settings.CH2Calib.Correct( calCh2, (ui8)((nSample >> 8) & 0xff) );
		ch1 = max(0, min(ch1, 255));
		ch2 = max(0, min(ch2, 255));
		x = (ch1 * nSize) >> 8;
		y = (ch2 * nSize) >> 8;
		if ( bSwap )
		{
			int t = x; x = y; y = t;
		}
		return true;
	};

	int nFirst = Settings.Time.InvalidFirst;
	int nLast = Sampler::GetValid();

	if ( nPersist == CSettings::Display::_PerPhosphor )
	{
		// store this acquisition decimated to 256 points, draw the frames
		// from the oldest (dim) to the newest (full color)
		uint32_t nSeq = Sampler::GetSequence();
		bool bNew = nSeq != m_nPhosphorSeq;
		m_nPhosphorSeq = nSeq;
		int nBytes;
		uint8_t* pArea = Sampler::HistoryArea( nBytes );
		if ( Phosphor::BeginXY( pArea, nBytes, nKey, bNew ) )
		{
			if ( bNew )
			{
				uint8_t* arrX = Phosphor::Scratch();
				uint8_t* arrY = arrX + Phosphor::MaxPoints;
				int nCount = nLast - nFirst;
				int nStep = (nCount + Phosphor::MaxPoints - 1) / Phosphor::MaxPoints;
				if ( nStep < 1 ) nStep = 1;
				int nPoints = 0;
				for ( int i = nFirst; i < nLast && nPoints < Phosphor::MaxPoints; i += nStep )
				{
					int x, y;
					if ( !Point( i, x, y ) )
						break;
					arrX[nPoints] = (uint8_t)x;
					arrY[nPoints] = (uint8_t)y;
					nPoints++;
				}
				Phosphor::StoreXY( nPoints, arrX, arrY );
			}
			ui16 arrLut[Phosphor::LutSize];
			Phosphor::MakeLut( clrPoint, arrLut );
			int nFrames = Phosphor::Frames();
			for ( int nAge = nFrames - 1; nAge >= 0; nAge-- )
			{
				int nPoints;
				const uint8_t* p = Phosphor::FrameXY( nAge, nPoints );
				ui16 clr = arrLut[Phosphor::AgeIndex( nAge )];
				for ( int i = 0; i < nPoints; i++ )
				{
					if ( bLines && i > 0 )
						_XyLine( rcXy, p[2*i-2], p[2*i-1], p[2*i], p[2*i+1], clr );
					else
						BIOS::LCD::PutPixel( rcXy.left + p[2*i], rcXy.bottom - 1 - p[2*i+1], clr );
				}
			}
			return;
		}
		// no room for the history: as without persistence
	}

	int xPrev = -1, yPrev = -1;
	for ( int i = nFirst; i < nLast; i++ )
	{
		int x, y;
		if ( !Point( i, x, y ) )
			break;
		if ( bLines && xPrev >= 0 )
			_XyLine( rcXy, xPrev, yPrev, x, y, clrPoint );
		else
			BIOS::LCD::PutPixel( rcXy.left + x, rcXy.bottom - 1 - y, clrPoint );
		xPrev = x;
		yPrev = y;
	}
}

void CWndOscGraph::OnPaintTY()
{
	ui16 column[CWndGraph::DivsY*CWndGraph::BlkY];
	if ( !CWnd::GetOverlay().IsValid() /*&& m_bNeedRedraw*/ )
	{
		CRect rc = m_rcClient;
		rc.Inflate( 1, 1, 1, 1 );
		BIOS::LCD::Rectangle( rc, RGB565(b0b0b0) );
		m_bNeedRedraw = false;
	}

	ui16 clr1 = Settings.CH1.u16Color;
	ui16 clr2 = Settings.CH2.u16Color;
	ui16 clr3 = Settings.CH3.u16Color;
	ui16 clr4 = Settings.CH4.u16Color;
	ui8 en1 = Settings.CH1.Enabled == CSettings::AnalogChannel::_YES;
	ui8 en2 = Settings.CH2.Enabled == CSettings::AnalogChannel::_YES;
	ui8 en3 = Settings.CH3.Enabled == CSettings::DigitalChannel::_YES;
	ui8 en4 = Settings.CH4.Enabled == CSettings::DigitalChannel::_YES;
	ui16 clrShade1 = 0, clrShade2 = 0, clrShade12 = 0;

	ui8 bTrigger = (BIOS::SYS::GetTick() - Settings.Trig.nLastChange) < 5000;
	ui16 nTriggerTime = (CSettings::Trigger::Position - Settings.Time.Shift);
	// the trigger position is always marked by a small triangle at the top
	// (the dotted line only for 5 s after a change of the trigger)
	int nTrigMark = CSettings::Trigger::Position - Settings.Time.Shift;
	if (!bTrigger)
		nTriggerTime = -1;

	int nCut = CWnd::GetOverlay().IsValid() ? CWnd::GetOverlay().left - m_rcClient.left : m_rcClient.Width();
	int nCutTop = CWnd::GetOverlay().IsValid() ? CWnd::GetOverlay().bottom - m_rcClient.top : 0;
	if ( nCutTop >= m_rcClient.Height() )
		nCut = m_rcClient.Width();
	int nFirstTop = CWnd::GetOverlay().IsValid() ? CWnd::GetOverlay().top - m_rcClient.top : 0;

	int nMax = m_rcClient.Width();
	if ( CWnd::GetOverlay().IsValid() && CWnd::GetOverlay().left - m_rcClient.left <= 0 )
		return;

	CSettings::Calibrator::FastCalc Ch1fast, Ch2fast;
	Settings.CH1Calib.Prepare( &Settings.CH1, Ch1fast );
	Settings.CH2Calib.Prepare( &Settings.CH2, Ch2fast );

	// if window is outside...
	int nMaxIndex = BIOS::ADC::NumSamples;
	bool bLines = Settings.Disp.Draw != CSettings::Display::_Points;
	bool bFill = Settings.Disp.Draw == CSettings::Display::_Fill;
	bool bAverage1 = Settings.Disp.Average == CSettings::Display::_AvgCh1;
	bool bAverage2 = Settings.Disp.Average == CSettings::Display::_AvgCh2;
	int nPrev1 = -1, nPrev2 = -1, nPrevm = -1, nPrev3 = -1, nPrev4 = -1;
	TBeam beam1, beam2;
	beam1.bInit = beam2.bInit = false;
	int nBeam1 = Settings.Extra.arrBeam[0] + 1, nBeam2 = Settings.Extra.arrBeam[1] + 1;

	int nMarkerT1 = -1, nMarkerT2 = -1, nMarkerY1 = -1, nMarkerY2 = -1;
	bool bAreaT = false;
	int nSampleY2 = 0, nSampleY1 = 0;
	// cursors were declared but never filled, so they were never drawn
	SetupMarkers( Ch1fast, Ch2fast, nMarkerT1, nMarkerT2, nMarkerY1, nMarkerY2 );
	SetupSelection( bAreaT, nMarkerT1, nMarkerT2 );

	int nY3High = 0;
	int nY3Low = 0;
	int nY4High = 0;
	int nY4Low = 0;
	int nSampleY3 = 0, nSampleY4 = 0;

	if ( en3 )
	{
		nY3Low = Settings.CH3.u16Position;
		nY3High = Settings.CH3.Polarity == CSettings::DigitalChannel::_POS ?
			nY3Low + 32 : nY3Low - 32;
		CUtils::Clamp<int>( nY3Low, 0, 255 );
		CUtils::Clamp<int>( nY3High, 0, 255 );
		nY3Low = (nY3Low*(DivsY*BlkY))>>8;
		nY3High = (nY3High*(DivsY*BlkY))>>8;
	}
	if ( en4 )
	{
		nY4Low = Settings.CH4.u16Position;
		nY4High = Settings.CH4.Polarity == CSettings::DigitalChannel::_POS ?
			nY4Low + 32 : nY4Low - 32;
		CUtils::Clamp<int>( nY4Low, 0, 255 );
		CUtils::Clamp<int>( nY4High, 0, 255 );
		nY4Low = (nY4Low*(DivsY*BlkY))>>8;
		nY4High = (nY4High*(DivsY*BlkY))>>8;
	}

	int nZeroY1 = 0, nZeroY2 = 0;
	if ( bFill )
	{
		int nZero1 = Settings.CH1Calib.GetZero( Ch1fast ), nZero2 = Settings.CH2Calib.GetZero( Ch2fast );
		CUtils::Clamp<int>( nZero1, 0, 255 );
		CUtils::Clamp<int>( nZero2, 0, 255 );
		nZeroY1 = ( nZero1 * ( DivsY * BlkY ) ) >> 8;
		nZeroY2 = ( nZero2 * ( DivsY * BlkY ) ) >> 8;
		clrShade1 = _Interpolate( clr1, 0x0101 );
		clrShade2 = _Interpolate( clr2, 0x0101 );
		clrShade12 = _Interpolate( _Interpolate(clr2, 0x0101), clr1 );
	}

	ui16 clrm = Settings.Math.uiColor;

	// LA104: math trace (Tools), A = CH1, B = CH2 in volts, scale Extra.nMathRes
	int nMath = Settings.Extra.nMath;
	float fA1 = 0, fB1 = 0, fA2 = 0, fB2 = 0, fMathK = 0;
	if ( nMath )
	{
		float fP1 = CSettings::AnalogChannel::pfValueProbe[ Settings.CH1.Probe ];
		float fP2 = CSettings::AnalogChannel::pfValueProbe[ Settings.CH2.Probe ];
		fA1 = Ch1fast.Q * Ch1fast.fMultiplier / 65536.0f * fP1;
		fB1 = Ch1fast.K * Ch1fast.fMultiplier / 65536.0f * fP1;
		fA2 = Ch2fast.Q * Ch2fast.fMultiplier / 65536.0f * fP2;
		fB2 = Ch2fast.K * Ch2fast.fMultiplier / 65536.0f * fP2;
		// volts -> pixels (25 per division)
		fMathK = BlkY / CSettings::AnalogChannel::pfValueResolution[ Settings.Extra.nMathRes ];
	}
	bool bRef = Settings.Extra.nRef != 0;
	int nIndex = Settings.Time.Shift;
	bool bDecode = DecodeUi::BeginFrame( nIndex, nMax );

	// persistence / phosphor: the analog traces come from the history of the
	// last acquisitions, a new acquisition is stored first
	bool bHist = Settings.Disp.Persist != CSettings::Display::_PerNo && !CCoreOscilloscope::Rolling();
	bool bGraded = Settings.Disp.Persist == CSettings::Display::_PerPhosphor;
	bool bStore = false;
	if ( bHist )
	{
		uint32_t nSeq = Sampler::GetSequence();
		bStore = nSeq != m_nPhosphorSeq;
		m_nPhosphorSeq = nSeq;
		int nBytes;
		uint8_t* pArea = Sampler::HistoryArea( nBytes );
		bHist = Phosphor::BeginTY( pArea, nBytes, _PersistKey(), bStore );
	}
	bool bHistEn1 = en1, bHistEn2 = en2;
	// channels of the columns with data: a column without data (before the
	// start of the samples when a stopped capture is shown at a slower time
	// base, or after their end) hides only itself
	const ui8 en1All = en1, en2All = en2, en3All = en3, en4All = en4;

	for (ui16 x=0; x<nMax; x++, nIndex++)
	{
		int clrCol = 0x0101;

		// area between the cursors X1 and X2, the cursors themselves are drawn
		// over the traces below
		if ( bAreaT && nIndex > nMarkerT1 && nIndex < nMarkerT2 )
			clrCol = RGB565(101060);

		_PrepareColumn( column, x, clrCol );


		BIOS::ADC::TSample Sample;
		Sample.value = nIndex < nMaxIndex ? Sampler::GetAt(nIndex) : 0;

		en1 = en1All; en2 = en2All; en3 = en3All; en4 = en4All;
		if (Sample.value == BIOS::ADC::NoData)
		{
			en1 = en2 = en3 = en4 = false;
			nPrev1 = nPrev2 = nPrev3 = nPrev4 = -1;
			beam1.bInit = beam2.bInit = false;
		}

		if ( en1 )
		{
			si16 ch1 = Sample.CH1;
			ch1 = Settings.CH1Calib.Correct( Ch1fast, ch1 );
			CUtils::Clamp<si16>( ch1, 0, 255 );

			if ( bAverage1 )
			{
				ui16& nMemory = m_arrAverageBuf[x];
				int nNew = (nMemory*7 + (ch1<<8)*1)/8;  // 7:1
				nMemory = (ui16)nNew;
				ch1 = nMemory >> 8;
			}

			nSampleY1 = (ch1*(DivsY*BlkY))>>8;
		}
		if ( en2 )
		{
			si16 ch2 = Sample.CH2;
			ch2 = Settings.CH2Calib.Correct( Ch2fast, ch2 );
			CUtils::Clamp<si16>( ch2, 0, 255 );

			if ( bAverage2 )
			{
				ui16& nMemory = m_arrAverageBuf[x];
				int nNew = (nMemory*7 + (ch2<<8)*1)/8;
				nMemory = (ui16)nNew;
				ch2 = nMemory >> 8;
			}

			nSampleY2 = (ch2*(DivsY*BlkY))>>8;
		}
		if ( en3 )
		{
			nSampleY3 = Sample.CH3 ? nY3High : nY3Low;
		}
		if ( en4 )
		{
			nSampleY4 = Sample.CH4 ? nY4High : nY4Low;
		}

		if ( bHist && bStore )
		{
			Phosphor::StoreTY( x, 0, en1 ? nSampleY1 : Phosphor::None );
			Phosphor::StoreTY( x, 1, en2 ? nSampleY2 : Phosphor::None );
		}

		if ( bFill && !bHist )
		{
			// area between the trace and its 0 V level, mixed where both overlap
			int nB1 = 1, nT1 = 0, nB2 = 1, nT2 = 0;
			if ( en1 )
			{
				nB1 = min( nSampleY1, nZeroY1 );
				nT1 = max( nSampleY1, nZeroY1 );
			}
			if ( en2 )
			{
				nB2 = min( nSampleY2, nZeroY2 );
				nT2 = max( nSampleY2, nZeroY2 );
			}
			for ( int _y = nB2; _y <= nT2; _y++ )
				column[_y] = ( _y >= nB1 && _y <= nT1 ) ? clrShade12 : clrShade2;
			for ( int _y = nB1; _y <= nT1; _y++ )
				if ( _y < nB2 || _y > nT2 )
					column[_y] = clrShade1;
		}

		if ( en3 )
		{
			if ( !bLines )
				column[nSampleY3] = clr3;
			else
			{
				if ( nPrev3 == -1 )
					nPrev3 = nSampleY3;
				int nBottom = min(nSampleY3, nPrev3);
				int nTop = max(nSampleY3, nPrev3);
				for ( int _y = nBottom; _y <= nTop; _y++)
					column[_y] = clr3;
				nPrev3 = nSampleY3;
			}
		}
		if ( en4 )
		{
			if ( !bLines )
				column[nSampleY4] = clr4;
			else
			{
				if ( nPrev4 == -1 )
					nPrev4 = nSampleY4;
				int nBottom = min(nSampleY4, nPrev4);
				int nTop = max(nSampleY4, nPrev4);
				for ( int _y = nBottom; _y <= nTop; _y++)
					column[_y] = clr4;
				nPrev4 = nSampleY4;
			}
		}
		if ( bRef && x < Tools::RefWidth && Tools::arrRef[x >> 1] != 0xff )
		{
			// stored for every second column: the gap is filled by a line
			int nA = Tools::arrRef[x >> 1];
			int nB = ( x & 1 ) && ( x >> 1 ) + 1 < Tools::RefWidth / 2 && Tools::arrRef[( x >> 1 ) + 1] != 0xff ?
				Tools::arrRef[( x >> 1 ) + 1] : nA;
			for ( int _y = min( nA, nB ); _y <= max( nA, nB ); _y++ )
				column[_y] = RGB565(a0a0a0);
		}

		if ( nMath && Sample.value != BIOS::ADC::NoData )
		{
			float v1 = fA1 + fB1 * Sample.CH1, v2 = fA2 + fB2 * Sample.CH2, m;
			switch ( nMath )
			{
				case 1: m = v1 + v2; break;
				case 2: m = v1 - v2; break;
				case 3: m = v2 - v1; break;
				default: m = v1 * v2; break;
			}
			int nY = DivsY * BlkY / 2 + (int)( m * fMathK );
			CUtils::Clamp<int>( nY, 0, DivsY * BlkY - 1 );
			if ( nPrevm == -1 )
				nPrevm = nY;
			int nBottom = min( nY, nPrevm ), nTop = max( nY, nPrevm );
			for ( int _y = nBottom; _y <= nTop; _y++ )
				column[_y] = clrm;
			nPrevm = nY;
		}

		if ( bHist )
		{
			_PhosphorColumn( column, x, bLines, bGraded, bHistEn1, bHistEn2, clr1, clr2 );
		} else
		{
		if ( en2 )
		{
			int nBottom = nSampleY2, nTop = nSampleY2;
			if ( bLines )
			{
				if ( nPrev2 == -1 )
					nPrev2 = nSampleY2;
				nBottom = min(nSampleY2, nPrev2);
				nTop = max(nSampleY2, nPrev2);
				nPrev2 = nSampleY2;
			}
			DrawBeam( column, beam2, nBottom, nTop, nBeam2, clr2 );
		}

		if ( en1 )
		{
			int nBottom = nSampleY1, nTop = nSampleY1;
			if ( bLines )
			{
				if ( nPrev1 == -1 )
					nPrev1 = nSampleY1;
				nBottom = min(nSampleY1, nPrev1);
				nTop = max(nSampleY1, nPrev1);
				nPrev1 = nSampleY1;
			}
			DrawBeam( column, beam1, nBottom, nTop, nBeam1, clr1 );
		}
		}

		// decoded protocol data
		if ( bDecode )
			DecodeUi::RenderColumn( column, DivsY*BlkY, nIndex );

		// trigger cursor drawn over the traces in white, so it stays visible
		// while the level (Y) or position (X) is being changed
		if ( bTrigger && (x & 1) == 1 )
		{
			ui16 y = (Settings.Trig.nLevel*(DivsY*BlkY))>>8;
			column[y] = RGB565(ffffff);
		}
		{
			int d = (int)x - nTrigMark;
			if ( d >= -3 && d <= 3 )
			{
				int h = 4 - ( d < 0 ? -d : d );
				for ( int k = 0; k < h; k++ )
					column[DivsY*BlkY - 1 - k] = RGB565(ffffff);
			}
		}
		if ( nTriggerTime == x )
		{
			for ( int _y = 0; _y < DivsY*BlkY; _y += 3 )
				column[_y] = RGB565(ffffff);
		}
		// cursors: dashed lines in their own colors over everything, they
		// were gray full lines that could not be told from the grid
		if ( nMarkerY1 > 0 && (x & 3) < 2 )
			column[nMarkerY1] = Settings.MarkY1.u16Color;
		if ( nMarkerY2 > 0 && (x & 3) >= 2 )
			column[nMarkerY2] = Settings.MarkY2.u16Color;
		if ( nMarkerT1 == nIndex )
			for ( int _y = 0; _y < DivsY*BlkY; _y += 4 )
				column[_y] = column[_y+1] = Settings.MarkT1.u16Color;
		if ( nMarkerT2 == nIndex )
			for ( int _y = 2; _y < DivsY*BlkY; _y += 4 )
				column[_y] = column[_y+1] = Settings.MarkT2.u16Color;

		if ( x <= nCut )
		{
			BIOS::LCD::BufferBegin(CRect(m_rcClient.left + x, m_rcClient.top, m_rcClient.left + x + 1, m_rcClient.top + DivsY*BlkY));
			BIOS::LCD::BufferWrite( column, DivsY*BlkY );
			BIOS::LCD::BufferEnd();
		}
		else
		{
			if ( nCutTop > 0 )
			{
				BIOS::LCD::BufferBegin( CRect(m_rcClient.left + x, m_rcClient.top + nCutTop, m_rcClient.left + x + 1, m_rcClient.top + nCutTop + DivsY*BlkY - nCutTop));
				BIOS::LCD::BufferWrite( column, DivsY*BlkY - nCutTop);
				BIOS::LCD::BufferEnd();
			}
			if ( nFirstTop > 0 )
			{
				BIOS::LCD::BufferBegin( CRect(m_rcClient.left + x, m_rcClient.top, m_rcClient.left + x + 1, m_rcClient.top + nFirstTop));
				BIOS::LCD::BufferWrite( column + DivsY*BlkY - nFirstTop, nFirstTop );
				BIOS::LCD::BufferEnd();
			}
		}
	}
}

void CWndOscGraph::SetupSelection( bool& bSelection, int& nMarkerT1, int& nMarkerT2 )
{
	bSelection = ( Settings.MarkT1.Mode == CSettings::Marker::_On &&
		Settings.MarkT2.Mode == CSettings::Marker::_On );

	if ( !bSelection )
		return;
	if ( Settings.MarkT1.nValue >= Settings.MarkT2.nValue )
		return;

	nMarkerT1 = Settings.MarkT1.nValue;
	nMarkerT2 = Settings.MarkT2.nValue;
}

void CWndOscGraph::SetupMarkers( CSettings::Calibrator::FastCalc& Ch1fast, CSettings::Calibrator::FastCalc& Ch2fast,
	int& nMarkerT1, int& nMarkerT2, int& nMarkerY1, int& nMarkerY2 )
{
	if ( Settings.MarkT1.Mode == CSettings::Marker::_On )
	{
		nMarkerT1 = Settings.MarkT1.nValue;
	}
	if ( Settings.MarkT2.Mode == CSettings::Marker::_On )
	{
		nMarkerT2 = Settings.MarkT2.nValue;
	}
	if ( Settings.MarkY1.Mode == CSettings::Marker::_On )
	{
		nMarkerY1 = Settings.MarkY1.nValue;
		if ( Settings.MarkY1.Source == CSettings::Marker::_CH1 )
		{
			nMarkerY1 = Settings.CH1Calib.Correct( Ch1fast, nMarkerY1 );
		} else
		{
			nMarkerY1 = Settings.CH2Calib.Correct( Ch2fast, nMarkerY1 );
		}
		nMarkerY1 = (nMarkerY1*(DivsY*BlkY))>>8;
	}
	if ( Settings.MarkY2.Mode == CSettings::Marker::_On )
	{
		nMarkerY2 = Settings.MarkY2.nValue;
		if ( Settings.MarkY2.Source == CSettings::Marker::_CH1 )
		{
			nMarkerY2 = Settings.CH1Calib.Correct( Ch1fast, nMarkerY2 );
		} else
		{
			nMarkerY2 = Settings.CH2Calib.Correct( Ch2fast, nMarkerY2 );
		}
		nMarkerY2 = (nMarkerY2*(DivsY*BlkY))>>8;
	}
	if ( Settings.MarkY1.Mode == CSettings::Marker::_Auto )
	{
		nMarkerY1 = Sampler::GetAt( Settings.MarkT1.nValue );
		if ( Settings.MarkY1.Source == CSettings::Marker::_CH1 )
		{
			nMarkerY1 = (ui8)((nMarkerY1) & 0xff);
			Settings.MarkY1.nValue = nMarkerY1;
			nMarkerY1 = Settings.CH1Calib.Correct( Ch1fast, nMarkerY1 );
		} else
		{
			nMarkerY1 = (ui8)((nMarkerY1>>8) & 0xff);
			Settings.MarkY1.nValue = nMarkerY1;
			nMarkerY1 = Settings.CH2Calib.Correct( Ch2fast, nMarkerY1 );
		}
		nMarkerY1 = (nMarkerY1*(DivsY*BlkY))>>8;
	}
	if ( Settings.MarkY2.Mode == CSettings::Marker::_Auto )
	{
		nMarkerY2 = Sampler::GetAt( Settings.MarkT2.nValue );
		if ( Settings.MarkY2.Source == CSettings::Marker::_CH1 )
		{
			nMarkerY2 = (ui8)((nMarkerY2) & 0xff);
			Settings.MarkY2.nValue = nMarkerY2;
			nMarkerY2 = Settings.CH1Calib.Correct( Ch1fast, nMarkerY2 );
		} else
		{
			nMarkerY2 = (ui8)((nMarkerY2>>8) & 0xff);
			Settings.MarkY2.nValue = nMarkerY2;
			nMarkerY2 = Settings.CH2Calib.Correct( Ch2fast, nMarkerY2 );
		}
		nMarkerY2 = (nMarkerY2*(DivsY*BlkY))>>8;
	}
	if ( nMarkerY1 >= DivsY*BlkY )
		nMarkerY1 = -1;
	if ( nMarkerY2 >= DivsY*BlkY )
		nMarkerY2 = -1;
}

void CWndOscGraph::GetCurrentRange(int& nBegin, int& nEnd)
{
	nBegin = Settings.Time.Shift;
	nEnd = nBegin + m_rcClient.Width();
	nEnd = min( nEnd, (int)BIOS::ADC::NumSamples );
}

// Move to better place

void CWndOscGraph::ClearAverage()
{
	memset(m_arrAverageBuf, 0, sizeof(m_arrAverageBuf));
}
void CWndOscGraph::ClearPersist()
{
	m_bPersistReset = true;
	Phosphor::Clear();
}
