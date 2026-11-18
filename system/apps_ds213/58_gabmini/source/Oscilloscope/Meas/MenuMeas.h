#pragma once
#include <Framework.h>
#include <Settings.h>
#include "MeasCore.h"

// LA104: tab Meas, four automatic measurements and the statistics of the
// selected one, in one menu item (RAM: one window instead of five).
//   Up / Down     measurement M1..M4, then Stat
//   Left / Right  type of the measurement; on Stat: on / off
//   Enter (F4)    source of the measurement (CH1..CH4); on Stat: reset

// LA104: Meas and Tools are one menu item over the whole panel; the focused
// look of CWndMenuItem (white inside) turned the whole panel white. The panel
// stays gray, the left tab is white while focused, the row has a black bar.
inline void PaintGrayPanel( CWnd* pWnd, ui16 clrTab )
{
	CDesign::MenuItemClearDisabled( pWnd->m_rcClient );
	CRect rc( pWnd->m_rcClient );
	rc.left += CWndMenuItem::MarginLeft;
	CDesign::MenuItemDisabled( rc, clrTab );
}

class CItemMeas : public CWndMenuItem
{
public:
	enum { Slots = CSettings::TExtra::MeasSlots, StatRow = Slots };
	int m_nSel;         // 0..3 measurement, 4 statistics
	int m_nStatSlot;    // measurement shown in the statistics

	float m_arrValue[Slots];
	bool m_arrValid[Slots];
	float m_arrMin[Slots], m_arrMax[Slots], m_arrSum[Slots];
	int m_arrCount[Slots];

	void Create( CWnd* pParent );
	virtual void OnPaint() override;
	virtual void OnKey( int nKey ) override;

	void Update( bool bForce = true );
	void ResetStat( int nSlot );   // -1: all

private:
	uint32_t m_nLastPaint;
	ui16 RowBackground( bool bSelected );
	void PaintValue( int nSlot );   // only the value row, over the old one
	void PaintStat();
	void ClearRest( int x, int y, int h, ui16 clr );
};

class CWndMenuMeas : public CWnd
{
public:
	CItemMeas m_itmMeas;

	virtual void Create( CWnd* pParent, int dwFlags );
	virtual void OnMessage( CWnd* pSender, int code, uintptr_t data ) override;
};
