#include "MenuPid.h"
#include <MainWnd.h>

static const ui16 clrBack = RGB565(000000);
static const ui16 clrGrid = RGB565(303030);
static const ui16 clrTarget = RGB565(a0a0a0);
static const ui16 clrMeasured = RGB565(ffff00);
static const ui16 clrOutput = RGB565(00ff00);

// "-12.345" style, 3 decimals
static char* _Fmt(char* buf, float f)
{
	char* p = buf;
	if ( f < 0 )
	{
		*p++ = '-';
		f = -f;
	}
	int n = (int)(f * 1000.0f + 0.5f);
	strcpy( p, CUtils::itoa(n / 1000) );
	p += strlen(p);
	*p++ = '.';
	int nFrac = n % 1000;
	*p++ = '0' + nFrac / 100;
	*p++ = '0' + nFrac / 10 % 10;
	*p++ = '0' + nFrac % 10;
	*p = 0;
	return buf;
}

float CWndPidGraph::Scale()
{
	// whole range of the output, set point with some room above
	float fScale = 2.5f;
	while ( fScale < Settings.Pid.fTarget * 1.25f && fScale < 40.0f )
		fScale += 2.5f;
	return fScale;
}

int CWndPidGraph::_Y(float fVolts)
{
	int nHeight = m_rcClient.Height() - 16;   // top line has the values
	int y = (int)(fVolts / m_fScale * nHeight);
	if ( y < 0 ) y = 0;
	if ( y > nHeight - 1 ) y = nHeight - 1;
	return m_rcClient.bottom - 1 - y;
}

// clears column x and draws the grid into it
void CWndPidGraph::_Grid(int x)
{
	int nLeft = m_rcClient.left + x;
	BIOS::LCD::Bar( nLeft, m_rcClient.top + 16, nLeft + 1, m_rcClient.bottom, clrBack );
	float fStep = m_fScale <= 2.5f ? 0.5f : 2.5f;
	if ( (x & 3) == 0 )
		for ( float f = fStep; f < m_fScale; f += fStep )
			BIOS::LCD::PutPixel( nLeft, _Y(f), clrGrid );
}

void CWndPidGraph::_Values()
{
	char strS[12], strI[12], strO[12];
	int x = m_rcClient.left + 2, y = m_rcClient.top;
	BIOS::LCD::Bar( m_rcClient.left, y, m_rcClient.right, y + 16, clrBack );
	if ( Settings.Pid.Mode == CorePid::Off )
	{
		BIOS::LCD::Print( x, y, RGB565(808080), clrBack, "Regulator off" );
		return;
	}
	if ( CorePid::Stalled() )
	{
		BIOS::LCD::Print( x, y, RGB565(ff4040), clrBack, "Scope stopped!" );
		return;
	}
	x += BIOS::LCD::Print( x, y, clrTarget, clrBack, _Fmt(strS, Settings.Pid.fTarget) );
	x += 8;
	x += BIOS::LCD::Print( x, y, clrMeasured, clrBack, _Fmt(strI, CorePid::Measured()) );
	x += 8;
	x += BIOS::LCD::Print( x, y, clrOutput, clrBack, _Fmt(strO, CorePid::Pid().m_fOutput) );
	BIOS::LCD::Print( x, y, RGB565(808080), clrBack, " V" );
}

/*virtual*/ void CWndPidGraph::OnPaint()
{
	CRect rc = m_rcClient;
	rc.Inflate( 1, 1, 1, 1 );
	BIOS::LCD::Rectangle( rc, RGB565(b0b0b0) );
	BIOS::LCD::Bar( m_rcClient, clrBack );

	m_fScale = Scale();
	for ( int x = 0; x < m_rcClient.Width(); x++ )
		_Grid( x );
	// scale labels
	float fStep = m_fScale <= 2.5f ? 0.5f : 2.5f;
	for ( float f = fStep; f < m_fScale; f += fStep )
	{
		char str[12];
		_Fmt( str, f );
		str[strlen(str) - 2] = 0; // one decimal
		BIOS::LCD::Print( m_rcClient.right - 8*(int)strlen(str) - 2, _Y(f) - 14, clrGrid, RGBTRANS, str );
	}
	BIOS::LCD::Print( m_rcClient.left + 2, m_rcClient.bottom - 16, RGB565(606060), RGBTRANS,
		Settings.Pid.Mode == CorePid::Simulation ? "Simulated plant" : "CH1 -> Wave Out" );

	m_nX = 0;
	m_nPrev[0] = m_nPrev[1] = m_nPrev[2] = -1;
	_Values();
}

bool CWndPidGraph::CheckScale()
{
	if ( m_fScale == Scale() )
		return true;
	Invalidate();   // new range, redraw the whole chart
	return false;
}

void CWndPidGraph::AddPoint()
{
	if ( !CheckScale() )
		return;
	int nY[3] = { _Y(Settings.Pid.fTarget), _Y(CorePid::Measured()), _Y(CorePid::Pid().m_fOutput) };
	const ui16 arrClr[3] = { clrTarget, clrMeasured, clrOutput };
	const int arrOrder[3] = { 0, 2, 1 };    // measured value drawn last, on top

	_Grid( m_nX );
	// gap in front of the pen
	int nNext = (m_nX + 1) % m_rcClient.Width();
	_Grid( nNext );
	int nLeft = m_rcClient.left + m_nX;
	for ( int k = 0; k < 3; k++ )
	{
		int i = arrOrder[k];
		int nPrev = m_nPrev[i] < 0 ? nY[i] : m_nPrev[i];
		int y1 = min(nPrev, nY[i]), y2 = max(nPrev, nY[i]);
		BIOS::LCD::Bar( nLeft, y1, nLeft + 1, y2 + 1, arrClr[i] );
		m_nPrev[i] = nY[i];
	}
	m_nX = nNext;

	// numbers 4 times per second
	uint32_t nNow = BIOS::SYS::GetTick();
	if ( nNow - m_nLastText > 250 )
	{
		m_nLastText = nNow;
		_Values();
	}
}

// ---------------------------------------------------------------------------

/*virtual*/ void CWndMenuPid::Create(CWnd *pParent, ui16 dwFlags)
{
	CWnd::Create("CWndMenuPid", dwFlags, CRect(280-CWndMenuItem::MarginLeft, 20, 400, 240), pParent);

	m_proMode.Create( (const char**)CSettings::PidRegulator::ppszTextMode, &Settings.Pid.Mode, CorePid::ModeMax );
	m_itmMode.Create( &m_proMode, "Regulator", RGB565(b00040), this );
	m_itmTarget.Create( "", RGB565(b00040), 2, this );
	m_itmKp.Create( "", RGB565(b00040), 2, this );
	m_itmKi.Create( "", RGB565(b00040), 2, this );
	m_itmKd.Create( "", RGB565(b00040), 2, this );
	m_itmPeriod.Create( "", RGB565(b00040), 2, this );

	m_itmTarget.Setup( "Set point", &Settings.Pid.fTarget, 0, 30.0f, 3, "V" );
	m_itmKp.Setup( "Kp", &Settings.Pid.fKp, 0, 100.0f, 3, "" );
	m_itmKi.Setup( "Ki", &Settings.Pid.fKi, 0, 1000.0f, 3, "/s" );
	m_itmKd.Setup( "Kd", &Settings.Pid.fKd, 0, 10.0f, 3, "s" );
	m_itmPeriod.Setup( "Period", &Settings.Pid.fPeriod, 10.0f, 5000.0f, 0, "ms" );
}

/*virtual*/ void CWndMenuPid::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if ( code == ToWord('L', 'D') )
	{
		MainWnd.m_wndPidGraph.ShowWindow( false );
		return;
	}
	if ( code == ToWord('L', 'E') )
	{
		MainWnd.m_wndPidGraph.ShowWindow( true );
		return;
	}
	if ( code == ToWord('i', 'u') && pSender == &m_itmMode )
	{
		CorePid::Restart();
		MainWnd.m_wndPidGraph.Invalidate();
		return;
	}
	if ( code == ToWord('i', 'u') && pSender == &m_itmTarget )
	{
		MainWnd.m_wndPidGraph.CheckScale();
	}
}
