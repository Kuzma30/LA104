#include "MenuMeas.h"
#include <MainWnd.h>

static ui16 SourceColor( int nSource )
{
	switch ( nSource )
	{
		case MeasCore::CH1: return Settings.CH1.u16Color;
		case MeasCore::CH2: return Settings.CH2.u16Color;
		case MeasCore::CH3: return Settings.CH3.u16Color;
		default:            return Settings.CH4.u16Color;
	}
}

// ---------------------------------------------------------------- item
void CItemMeas::Create( CWnd* pParent )
{
	for ( int i = 0; i < Slots; i++ )
	{
		// values out of range (old settings file)
		if ( !MeasCore::Allowed( Settings.Extra.arrMeasType[i], Settings.Extra.arrMeasSrc[i] ) )
		{
			Settings.Extra.arrMeasSrc[i] = MeasCore::CH1;
			Settings.Extra.arrMeasType[i] = MeasCore::Vpp;
		}
		m_arrValid[i] = false;
	}
	if ( Settings.Extra.nStat > 1 )
		Settings.Extra.nStat = 0;
	m_nSel = 0;
	m_nStatSlot = 0;
	m_nLastPaint = 0;
	ResetStat( -1 );
	// 4 measurements of 2 rows, statistics of 4 rows
	CWndMenuItem::Create( NULL, RGB565(b0b0b0), 2 * Slots + 4, pParent );
}

// background of a row as CWndMenuItem / the selection bar paint it
ui16 CItemMeas::RowBackground( bool bSelected )
{
	return HasFocus() && bSelected ? RGB565(000000) : CDesignPalette::gray;
}

void CItemMeas::OnPaint()
{
	PaintGrayPanel( this, RGB565(b0b0b0) );
	int x0 = m_rcClient.left + 12 + MarginLeft - 9;

	for ( int i = 0; i < Slots; i++ )
	{
		int y = m_rcClient.top + i * 34;
		int nType = Settings.Extra.arrMeasType[i];
		int nSource = Settings.Extra.arrMeasSrc[i];
		bool bSel = HasFocus() && m_nSel == i;
		if ( bSel )
			BIOS::LCD::Bar( x0 - 2, y, m_rcClient.right - 3, y + 33, RGB565(000000) );
		int x = x0;
		x += BIOS::LCD::Print( x, y, SourceColor( nSource ), RGBTRANS, MeasCore::SourceName( nSource ) );
		BIOS::LCD::Print( x + 4, y, bSel ? RGB565(ffffff) : RGB565(000000), RGBTRANS, MeasCore::Name( nType ) );
		PaintValue( i );
	}
	if ( HasFocus() && m_nSel == StatRow )
	{
		int y = m_rcClient.top + Slots * 34;
		BIOS::LCD::Bar( x0 - 2, y, m_rcClient.right - 3, y + 16 * 4 + 3, RGB565(000000) );
	}
	PaintStat();
}

void CItemMeas::PaintValue( int i )
{
	int x0 = m_rcClient.left + 12 + MarginLeft - 9;
	int y = m_rcClient.top + i * 34 + 16;
	bool bSel = HasFocus() && m_nSel == i;
	ui16 clrBack = RowBackground( bSel );
	ui16 clr = bSel ? RGB565(ffffff) : RGB565(000000);
	char str[16];
	if ( m_arrValid[i] )
		MeasCore::Format( Settings.Extra.arrMeasType[i], m_arrValue[i], str );
	else
		strcpy( str, "--" );
	// text with its background, then the rest of the row: no flicker
	int x = x0 + 4 + BIOS::LCD::Print( x0 + 4, y, clr, clrBack, str );
	ClearRest( x, y, 16, clrBack );
}

// background from x to the right edge; nothing when the text reached it
// (a bar with right < left is drawn as a huge one and hangs the device)
void CItemMeas::ClearRest( int x, int y, int h, ui16 clr )
{
	if ( x < m_rcClient.right - 4 )
		BIOS::LCD::Bar( x, y, m_rcClient.right - 4, y + h, clr );
}

void CItemMeas::PaintStat()
{
	int x0 = m_rcClient.left + 12 + MarginLeft - 9;
	int y = m_rcClient.top + Slots * 34;
	bool bSel = HasFocus() && m_nSel == StatRow;
	ui16 clrBack = RowBackground( bSel );
	ui16 clr = bSel ? RGB565(ffffff) : RGB565(000000);
	ui16 clr2 = bSel ? RGB565(ffffff) : RGB565(404040);
	char str[16];
	int x;

	if ( !Settings.Extra.nStat )
	{
		x = x0 + BIOS::LCD::Print( x0, y, clr, clrBack, "Stat: Off" );
		ClearRest( x, y, 16 * 4, clrBack );
		BIOS::LCD::Bar( x0, y + 16, x, y + 16 * 4, clrBack );
		return;
	}
	int s = m_nStatSlot;
	// 9 characters fit to the row: "M4 n=" and at most 4 digits
	x = x0 + BIOS::LCD::Printf( x0, y, clr, clrBack, "M%d n=", s + 1 );
	x += BIOS::LCD::Printf( x, y, clr2, clrBack, "%d", min( m_arrCount[s], 9999 ) );
	ClearRest( x, y, 16, clrBack );
	static const char* const arrLabel[3] = { "<", ">", "~" };
	float arrV[3] = { m_arrMin[s], m_arrMax[s], m_arrCount[s] ? m_arrSum[s] / m_arrCount[s] : 0 };
	int nType = Settings.Extra.arrMeasType[s];
	for ( int i = 0; i < 3; i++ )
	{
		int _y = y + 16 * ( i + 1 );
		x = x0;
		if ( m_arrCount[s] )
		{
			MeasCore::Format( nType, arrV[i], str );
			x += BIOS::LCD::Print( x, _y, clr2, clrBack, arrLabel[i] );
			x += BIOS::LCD::Print( x, _y, clr, clrBack, " " );
			x += BIOS::LCD::Print( x, _y, clr, clrBack, str );
		}
		ClearRest( x, _y, 16, clrBack );
	}
}

void CItemMeas::OnKey( int nKey )
{
	if ( nKey == BIOS::KEY::Up && m_nSel > 0 )
	{
		m_nSel--;
		if ( m_nSel < Slots )
			m_nStatSlot = m_nSel;
		Invalidate();
		return;
	}
	if ( nKey == BIOS::KEY::Down && m_nSel < StatRow )
	{
		m_nSel++;
		if ( m_nSel < Slots )
			m_nStatSlot = m_nSel;
		Invalidate();
		return;
	}

	bool bLeftRight = nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right;
	bool bEnter = nKey == BIOS::KEY::Enter;
	if ( m_nSel == StatRow )
	{
		if ( bLeftRight )
			Settings.Extra.nStat = !Settings.Extra.nStat;
		if ( bLeftRight || bEnter )
		{
			ResetStat( -1 );
			Invalidate();
			return;
		}
	} else
	if ( bLeftRight || bEnter )
	{
		uint8_t& nType = Settings.Extra.arrMeasType[m_nSel];
		uint8_t& nSource = Settings.Extra.arrMeasSrc[m_nSel];
		if ( bLeftRight )
		{
			int nDir = nKey == BIOS::KEY::Right ? 1 : -1;
			int t = nType;
			for ( int i = 0; i < MeasCore::TypeCount; i++ )
			{
				t = ( t + nDir + MeasCore::TypeCount ) % MeasCore::TypeCount;
				if ( MeasCore::Allowed( t, nSource ) )
					break;
			}
			nType = t;
		} else
		{
			nSource = ( nSource + 1 ) % MeasCore::SourceCount;
			if ( !MeasCore::Allowed( nType, nSource ) )
				nType = MeasCore::Freq;
		}
		ResetStat( m_nSel );
		Update();
		return;
	}
	CWndMenuItem::OnKey( nKey );
}

void CItemMeas::ResetStat( int nSlot )
{
	for ( int i = 0; i < Slots; i++ )
		if ( nSlot < 0 || nSlot == i )
		{
			m_arrCount[i] = 0;
			m_arrSum[i] = 0;
		}
}

void CItemMeas::Update( bool bForce )
{
	// at most 4 times a second (the roll mode sends 10 frames a second)
	uint32_t nNow = BIOS::SYS::GetTick();
	if ( !bForce && nNow - m_nLastPaint < 250 )
		return;
	m_nLastPaint = nNow;

	for ( int i = 0; i < Slots; i++ )
	{
		float f = 0;
		m_arrValid[i] = MeasCore::Measure( Settings.Extra.arrMeasType[i], Settings.Extra.arrMeasSrc[i], f );
		m_arrValue[i] = f;
		if ( m_arrValid[i] && Settings.Extra.nStat )
		{
			if ( m_arrCount[i] == 0 || f < m_arrMin[i] ) m_arrMin[i] = f;
			if ( m_arrCount[i] == 0 || f > m_arrMax[i] ) m_arrMax[i] = f;
			m_arrSum[i] += f;
			m_arrCount[i]++;
		}
	}
	if ( !IsVisible() )
		return;
	if ( bForce )
	{
		Invalidate();
		return;
	}
	// only the values: the labels and the frame stay
	for ( int i = 0; i < Slots; i++ )
		PaintValue( i );
	PaintStat();
}

// ---------------------------------------------------------------- menu
/*virtual*/ void CWndMenuMeas::Create( CWnd* pParent, int dwFlags )
{
	CWnd::Create( "CWndMenuMeas", dwFlags | CWnd::WsListener,
		CRect( 320 - CWndMenuItem::MarginLeft, 20, 400, 240 ), pParent );
	m_itmMeas.Create( this );
}

/*virtual*/ void CWndMenuMeas::OnMessage( CWnd* pSender, int code, uintptr_t data )
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
		m_itmMeas.Update();
		return;
	}
	// new waveform
	if ( pSender == NULL && code == WmBroadcast && data == ToWord('d', 'g') && IsVisible() )
	{
		m_itmMeas.Update( false );
		return;
	}
}
