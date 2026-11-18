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
	ResetStat( -1 );
	// 4 measurements of 2 rows, statistics of 4 rows
	CWndMenuItem::Create( NULL, RGB565(b0b0b0), 2 * Slots + 4, pParent );
}

void CItemMeas::OnPaint()
{
	CWndMenuItem::OnPaint();
	int x0 = m_rcClient.left + 12 + MarginLeft - 9;
	char str[16];

	for ( int i = 0; i < Slots; i++ )
	{
		int y = m_rcClient.top + i * 34;
		int nType = Settings.Extra.arrMeasType[i];
		int nSource = Settings.Extra.arrMeasSrc[i];
		ui16 clr = RGB565(000000);
		if ( HasFocus() && m_nSel == i )
		{
			BIOS::LCD::Bar( x0 - 2, y, m_rcClient.right - 3, y + 33, RGB565(000000) );
			clr = RGB565(ffffff);
		}
		int x = x0;
		x += BIOS::LCD::Print( x, y, SourceColor( nSource ), RGBTRANS, MeasCore::SourceName( nSource ) );
		BIOS::LCD::Print( x + 4, y, clr, RGBTRANS, MeasCore::Name( nType ) );
		if ( m_arrValid[i] )
			MeasCore::Format( nType, m_arrValue[i], str );
		else
			strcpy( str, "--" );
		BIOS::LCD::Print( x0 + 4, y + 16, clr, RGBTRANS, str );
	}

	// statistics
	int y = m_rcClient.top + Slots * 34;
	ui16 clr = RGB565(000000), clr2 = RGB565(404040);
	if ( HasFocus() && m_nSel == StatRow )
	{
		BIOS::LCD::Bar( x0 - 2, y, m_rcClient.right - 3, y + 16 * 4 + 3, RGB565(000000) );
		clr = clr2 = RGB565(ffffff);
	}
	if ( !Settings.Extra.nStat )
	{
		BIOS::LCD::Print( x0, y, clr, RGBTRANS, "Stat: Off" );
		return;
	}
	int s = m_nStatSlot;
	int x = x0 + BIOS::LCD::Printf( x0, y, clr, RGBTRANS, "Stat M%d", s + 1 );
	BIOS::LCD::Printf( x + 4, y, clr2, RGBTRANS, "%d", m_arrCount[s] );
	if ( m_arrCount[s] == 0 )
		return;
	static const char* const arrLabel[3] = { "<", ">", "~" };
	float arrV[3] = { m_arrMin[s], m_arrMax[s], m_arrSum[s] / m_arrCount[s] };
	int nType = Settings.Extra.arrMeasType[s];
	for ( int i = 0; i < 3; i++ )
	{
		MeasCore::Format( nType, arrV[i], str );
		int _x = x0 + BIOS::LCD::Print( x0, y + 16 * ( i + 1 ), clr2, RGBTRANS, arrLabel[i] );
		BIOS::LCD::Print( _x + 2, y + 16 * ( i + 1 ), clr, RGBTRANS, str );
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

void CItemMeas::Update()
{
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
	Invalidate();
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
		m_itmMeas.Update();
		return;
	}
}
