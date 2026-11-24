#include "ToolBar.h"
#include "MainWnd.h"
#include "../../common/battery.h"

/*static*/ const CWndToolBar::CBarItem* CWndToolBar::GetMenuItems()
{
	static const CBarItem arrMenuItems[] = {

		{ CBarItem::IMain,	(PSTR)"Oscilloscope", nullptr},
		{ CBarItem::ISub,	(PSTR)"Input", &MainWnd.m_wndMenuInput},
		{ CBarItem::ISub,	(PSTR)"Cursor", &MainWnd.m_wndMenuCursor},
		{ CBarItem::ISub,	(PSTR)"Disp", &MainWnd.m_wndMenuDisplay},
		{ CBarItem::ISub,	(PSTR)"Decode", &MainWnd.m_wndMenuDecode},
		{ CBarItem::ISub,	(PSTR)"Meas", &MainWnd.m_wndMenuMeas},
		{ CBarItem::ISub,	(PSTR)"Tools", &MainWnd.m_wndMenuTools},
		{ CBarItem::ISub,	(PSTR)"System", &MainWnd.m_wndMenuSystem},
		{ CBarItem::IMain,	(PSTR)"Generator", nullptr},
		{ CBarItem::ISub,	(PSTR)"Wave", &MainWnd.m_wndMenuGenerator},
		{ CBarItem::ISub,	(PSTR)"Mod", &MainWnd.m_wndMenuGeneratorMod},
		{ CBarItem::ISub,	(PSTR)"Edit", &MainWnd.m_wndMenuGeneratorEdit},
		{ CBarItem::ISub,	(PSTR)"PID", &MainWnd.m_wndMenuPid},
		{ CBarItem::IEnd,		NULL, NULL }
	};

	return arrMenuItems;
}

// battery of the DS203 (one Li-ion cell): PC2 / ADC2 (common/battery.h; the
// SYS attribute BatteryVoltage gives nothing under this OS)
static int s_nBatteryMv = -1;
static long s_lBatteryRead = 0;

static int _BatteryMv()
{
#if defined(DS203)
	return Battery::ReadMv();
#else
	return 0;
#endif
}

// fill 0..4 segments: 3.40 V empty .. 4.10 V full
static int _BatterySegments(int mV)
{
	if ( mV <= 0 ) return -1;
	int n = ( mV - 3400 + 87 ) / 175;
	return n < 0 ? 0 : n > 4 ? 4 : n;
}

void CWndToolBar::UpdateBattery()
{
	long lNow = BIOS::SYS::GetTick();
	if ( s_nBatteryMv >= 0 && lNow - s_lBatteryRead < 5000 )
		return;
	s_lBatteryRead = lNow;
	int mV = _BatteryMv();
	bool bChanged = s_nBatteryMv < 0 || _BatterySegments( mV ) != _BatterySegments( s_nBatteryMv );
	s_nBatteryMv = mV;
	if ( bChanged && IsVisible() )
		PaintBattery();
}

void CWndToolBar::PaintBattery()
{
	if ( s_nBatteryMv < 0 )
	{
		s_lBatteryRead = BIOS::SYS::GetTick();
		s_nBatteryMv = _BatteryMv();
	}
	int x = m_rcClient.left, y = m_rcClient.top;
	BIOS::LCD::Bar( x, y, x + BatteryW, m_rcClient.bottom, RGB565(000000) );
	int nSeg = _BatterySegments( s_nBatteryMv );
	if ( nSeg < 0 )
		return;
	// body 15 x 9 with a tip of 2 x 3, 4 segments of 2 px
	uint16_t clrFrame = RGB565(ffffff);
	uint16_t clrFill = nSeg >= 3 ? RGB565(00e000) : nSeg == 2 ? RGB565(e0e000) : RGB565(ff3030);
	int bx = x + 1, by = y + 3;
	BIOS::LCD::Bar( bx, by, bx + 15, by + 1, clrFrame );
	BIOS::LCD::Bar( bx, by + 8, bx + 15, by + 9, clrFrame );
	BIOS::LCD::Bar( bx, by, bx + 1, by + 9, clrFrame );
	BIOS::LCD::Bar( bx + 14, by, bx + 15, by + 9, clrFrame );
	BIOS::LCD::Bar( bx + 15, by + 3, bx + 17, by + 6, clrFrame );
	if ( nSeg == 0 )
		BIOS::LCD::Bar( bx + 2, by + 2, bx + 3, by + 7, clrFill );   // empty: a red line
	for ( int i = 0; i < nSeg; i++ )
		BIOS::LCD::Bar( bx + 2 + i * 3, by + 2, bx + 4 + i * 3, by + 7, clrFill );
}

/*virtual*/ void CWndToolBar::OnPaint()
{
	PaintBattery();
	const CWndToolBar::CBarItem* pItems = GetMenuItems();
	int nFocus = m_nFocus;

	// find nearest sub menu
	int nMenu = nFocus;
	while ( pItems[nMenu].m_eType == CBarItem::ISub && nMenu > 0 )
		nMenu--;

	_ASSERT( pItems[nMenu].m_eType == CBarItem::IMain );

	int x = m_rcClient.left + BatteryW;
	int nExtentx = UI::Width(pItems[nMenu].m_pName);

	if ( nFocus == nMenu && HasFocus() )
	{
		x += BIOS::LCD::Draw(x, 0, RGB565(ffffff), RGB565(000000), CShapes::corner_left);
		BIOS::LCD::Bar( x, m_rcClient.top, x + nExtentx, m_rcClient.bottom, RGB565(FFFFFF));
		x += UI::Print(x, 0, RGB565(000000), RGBTRANS, pItems[nMenu].m_pName);
	} else {
		x += BIOS::LCD::Draw(x, 0, RGB565(0020ff), RGB565(000000), CShapes::corner_left);
		BIOS::LCD::Bar( x, m_rcClient.top, x + nExtentx, m_rcClient.bottom, RGB565(0020ff));
		x += UI::Print(x, 0, RGB565(ffffff), RGBTRANS, pItems[nMenu].m_pName);
	}

	CRect rcSub( m_rcClient );
	rcSub.left = x;

	// gradient background
	CDesign::GradientTop( rcSub );

	if ( nFocus == nMenu && HasFocus() )
		x += BIOS::LCD::Draw(x, 0, RGB565(ffffff), RGBTRANS, CShapes::corner_right);
	else
		x += BIOS::LCD::Draw(x, 0, RGB565(0020ff), RGBTRANS, CShapes::corner_right);
	x += 10;

	int nIgnoreFirst = 1; // 1 -> first sub menu

	// calculate how many items we need to hide from left to reach the selected one
	int nRequired = 0;
	int nAvailable = BIOS::LCD::Width - 16 - x; // 16px reserved for arrows
	for ( int i = nFocus; i > 0 && pItems[i].m_eType != CBarItem::IMain; i-- )
	{
		nRequired += UI::Width(pItems[i].m_pName) + 16;
		if ( nRequired > nAvailable )
			nIgnoreFirst++;
	}

	if ( nIgnoreFirst > 1 )
		x += UI::Print( x, m_rcClient.top, RGB565(b0b0b0), RGBTRANS, "\x11");

	for ( int i = nMenu+nIgnoreFirst; pItems[i].m_eType == CBarItem::ISub; i++ )
	{
		ui8 bSelected = (i==nFocus);
		u16 clr = bSelected ? clrSelected : clrNormal;
		u16 bgr = bSelected ? 0 : RGBTRANS;

		if ( HasFocus() && bSelected )
		{
			clr = clrSelectedFocus;
			bgr = bgrSelectedFocus;
		}

		if ( x + 16 + UI::Width(pItems[i].m_pName) >= BIOS::LCD::Width )
		{
			x += UI::Print( x, m_rcClient.top, RGB565(b0b0b0), RGBTRANS, "\x10");
			break;
		}

		if ( bSelected )
			x += BIOS::LCD::Draw( x, m_rcClient.top, bgr, RGBTRANS, CShapes::corner_left);
		else
			x += 8;

		if ( bSelected )
			BIOS::LCD::Bar( x, m_rcClient.top, x + UI::Width(pItems[i].m_pName), m_rcClient.bottom, bgr);

		x += UI::Print( x, m_rcClient.top, clr, bgr, pItems[i].m_pName);

		if ( bSelected )
			x += BIOS::LCD::Draw( x, m_rcClient.top, bgr, RGBTRANS, CShapes::corner_right);
		else
			x += 8;
	}
}

/*virtual*/ void CWndToolBar::OnKey(int nKey)
{
	const CWndToolBar::CBarItem* pItems = GetMenuItems();
	ui8 oldFocus = m_nFocus;
	if ( nKey == BIOS::KEY::Left)
	{
		if ( pItems[m_nFocus].m_eType == CBarItem::IMain )
		{
			m_nFocus++;
			while((pItems[m_nFocus].m_eType != CBarItem::IMain)&&(pItems[m_nFocus].m_eType != CBarItem::IEnd))
			{	// Find the last item
				m_nFocus++;
			}
			m_nFocus--;
		}
		else
		if ( m_nFocus > 1 && pItems[m_nFocus-1].m_eType == CBarItem::IMain && m_nFocus-1 > 0 )
		{
			// first item of a group: continue with the last item of previous group
			m_nFocus -= 2;
		}
		else
		{
			m_nFocus--;
		}
		CWndToolBar::ChangeFocus(oldFocus);
	}

	if ( nKey == BIOS::KEY::Right)
	{
		if ( pItems[m_nFocus].m_eType != CBarItem::IMain &&
			 pItems[m_nFocus+1].m_eType == CBarItem::IMain && pItems[m_nFocus+2].m_eType == CBarItem::ISub )
		{
			// last item of a group: continue with the next group
			m_nFocus += 2;
		}
		else
		if ((pItems[m_nFocus+1].m_eType == CBarItem::IMain)||((pItems[m_nFocus+1].m_eType == CBarItem::IEnd)))
		{
			/*
			m_nFocus--;
			while(pItems[m_nFocus].m_eType != CBarItem::IMain)
			{	// Find the first item
				m_nFocus--;
			}*/
		}
		else
		{
			m_nFocus++;
		}
		CWndToolBar::ChangeFocus(oldFocus);
	}

	if( nKey == BIOS::KEY::Enter )
	{
		CWnd::OnKey( BIOS::KEY::Down );
	}

	if( nKey == BIOS::KEY::Escape )
	{	// Focus on first item
		if (pItems[m_nFocus].m_eType != CBarItem::IMain)
		{
			m_nFocus--;
			while(pItems[m_nFocus].m_eType != CBarItem::IMain)
			{	// Find the first item
				m_nFocus--;
			}
			CWndToolBar::ChangeFocus(oldFocus);
		}
	}
	CWnd::OnKey( nKey );
}

/*virtual*/ void CWndToolBar::ChangeFocus(ui8 oldFocus)
{
	const CWndToolBar::CBarItem* pItems = GetMenuItems();
	SendMessage( GetParent(), ToWord('L', 'D'), (NATIVEPTR)pItems[oldFocus].m_pWndMenu );
	SendMessage( GetParent(), ToWord('L', 'E'), (NATIVEPTR)pItems[m_nFocus].m_pWndMenu );
	SendMessage( GetParent(), ToWord('L', 'R'), 0 );
	Settings.Runtime.m_nMenuItem = m_nFocus;
Invalidate();
}

/*virtual*/ void CWndToolBar::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if ( code == ToWord('g', 'i') )
	{
		const CBarItem *pItems = GetMenuItems();
		m_nFocus = (ui8)data;
		CWnd* pFocus = GetFocus();
		SendMessage( GetParent(), ToWord('L', 'E'), (NATIVEPTR)pItems[m_nFocus].m_pWndMenu );
		if ( GetFocus() == pFocus )
		{
			// else the focus was stolen by the current wnd
			SetFocus();
		}
	}

	if ( code == ToWord('g', 'o') )
	{
		const char* strId = (const char*)data;
		int nItem = Find( strId );

		code = ToWord('g', '2');
		data = nItem;
	}

	if ( code == ToWord('g', '2') )
	{
		const CBarItem *pItems = GetMenuItems();

		int nItem = data;
		if ( nItem == -1 )
			return;

		SendMessage( GetParent(), ToWord('L', 'D'), (NATIVEPTR)pItems[m_nFocus].m_pWndMenu );

		if ( pItems[nItem].m_eType == CBarItem::IMain )
			m_nFocus = nItem+1;
		else
			m_nFocus = nItem;

		SendMessage( GetParent(), ToWord('L', 'E'), (NATIVEPTR)pItems[m_nFocus].m_pWndMenu );
		SetFocus();
		SendMessage( GetParent(), ToWord('L', 'R'), 0 );
		Settings.Runtime.m_nMenuItem = m_nFocus;
		Invalidate();
	}
}

CWnd* CWndToolBar::GetCurrentLayout()
{
	const CWndToolBar::CBarItem* pItems = GetMenuItems();
	return (CWnd*)pItems[m_nFocus].m_pWndMenu;
}

int CWndToolBar::Find(const char* strId)
{
	const CBarItem *pItems = GetMenuItems();

	for (int i=0; pItems[i].m_eType != CBarItem::IEnd; i++ )
	{
		if ( /*pItems[i].m_eType == CBarItem::IMain &&*/
				strcmp(pItems[i].m_pName, strId) == 0 )
		{
			return i;
		}
	}
	_ASSERT( 0 );
	return -1;
}
