#pragma once
#include <Framework.h>
#include <Settings.h>
#include "MeasCore.h"

// LA104: tab Meas, four automatic measurements and the statistics of the
// selected one, in one menu item (RAM: one window instead of five).
//   Up / Down     measurement M1..M4, then Stat
//   Left / Right  type of the measurement; on Stat: on / off
//   Enter (F4)    source of the measurement (CH1..CH4); on Stat: reset

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

	void Update();
	void ResetStat( int nSlot );   // -1: all
};

class CWndMenuMeas : public CWnd
{
public:
	CItemMeas m_itmMeas;

	virtual void Create( CWnd* pParent, int dwFlags );
	virtual void OnMessage( CWnd* pSender, int code, uintptr_t data ) override;
};
