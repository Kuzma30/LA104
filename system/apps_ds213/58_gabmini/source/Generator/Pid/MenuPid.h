#pragma once
#include <Framework.h>
#include <Settings.h>
#include <Generator/Modulation/MenuGenModulation.h>
#include "CorePid.h"

// Chart recorder: set point (gray), measured CH1 (yellow) and output (green).
// A column is drawn with every regulator step, the chart wraps around.
class CWndPidGraph : public CWnd
{
	int m_nX{0};
	int m_nPrev[3]{-1, -1, -1};
	float m_fScale{2.5f};       // volts at the top of the chart
	uint32_t m_nLastText{0};

	int _Y(float fVolts);
	void _Grid(int x);
	void _Values();

public:
	void Create(CWnd *pParent, ui16 dwFlags)
	{
		CWnd::Create("CWndPidGraph", dwFlags, CRect(12, 22, 262, 222), pParent);
	}
	float Scale();
	bool CheckScale();
	virtual void OnPaint() override;
	void AddPoint();
};

class CWndMenuPid : public CWnd
{
public:
	CProviderEnum	m_proMode;
	CItemProvider	m_itmMode;
	CItemNumber		m_itmTarget;
	CItemNumber		m_itmKp;
	CItemNumber		m_itmKi;
	CItemNumber		m_itmKd;
	CItemNumber		m_itmPeriod;

	virtual void Create(CWnd *pParent, ui16 dwFlags);
	virtual void OnMessage(CWnd* pSender, int code, uintptr_t data) override;
};
