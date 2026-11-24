#pragma once
#include "Framework.h"

class CWndToolBar : public CWnd
{
public:
	ui8	m_nFocus;
	enum {
		clrSelected = RGB565(ffffff),
		clrNormal = RGB565(b0b0b0),
		
		clrSelectedFocus = RGB565(000000),
		bgrSelectedFocus = RGB565(ffffff),
	};

	struct CBarItem
	{
		enum EType {
			IMain,
			ISub,
			IEnd
		};

		EType m_eType;                                   
		PSTR m_pName;
		const CWnd* m_pWndMenu; 
	};

	static const CBarItem m_arrMenuItems[];


public:
	
	virtual void Create(CWnd *pParent) 
	{
		m_nFocus = 0;
		CWnd::Create("CToolbarWnd", WsVisible, CRect(0, 0, BIOS::LCD::Width, 16), pParent);
	}

	virtual void	OnKey(int nKey) override;
	virtual void	OnPaint() override;
	virtual void	OnMessage(CWnd* pSender, int code, uintptr_t data) override;
	CWnd*			GetCurrentLayout();
	void			ChangeFocus(ui8 oldFocus);

public:
	static const CBarItem* GetMenuItems();
	// battery: icon left of the first item, the voltage is read every 5 s,
	// the icon is drawn again when its level changes
	enum { BatteryW = 20 };
	void UpdateBattery();
	void PaintBattery();
	static int Find( const char* strId );
};
