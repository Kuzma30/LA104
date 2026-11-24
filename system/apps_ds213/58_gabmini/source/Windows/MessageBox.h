#ifndef __MESSAGEBOX_H__
#define __MESSAGEBOX_H__
#include "../../../common/system.h"

class CWndMessageBox : public CWnd
{
	ui16 m_clrFrame;
	PCSTR m_strMessage;

public:
	void Show(CWnd* pParent, PCSTR strCaption, PCSTR strMessage, ui16 nColor)
	{
		m_clrFrame = nColor;
		m_strMessage = strMessage;

		if ( IsWindow() )
		{
			KillTimer();
			StopModal();	// will destroy this window
		}

		CWnd::Create(strCaption, CWnd::WsVisible, CRect(40, 100, 360, 160), pParent);
		StartModal();
		System::Beep(100);
		SetTimer(3000);
	}

	virtual void OnPaint() override
	{
		CRect rcClient = m_rcClient;
		CDesign::Window(m_rcClient, m_clrFrame);

		UI::Print( rcClient.CenterX()-(UI::Width(m_pszId)>>1), rcClient.top+2, 
			RGB565(000000), RGBTRANS, m_pszId);

		rcClient.Deflate(2+12+4, 2+14+2, 2+12+4, 2+2+0);
		int nExtentW = UI::Width(m_strMessage);
		int nExtentH = 14;

		UI::Print( rcClient.Center().x - (nExtentW >> 1), rcClient.Center().y - (nExtentH >> 1 ),
			RGB565(000000), RGBTRANS, m_strMessage);		
	}

	void Hide()
	{
		KillTimer();
		StopModal();
	}

	virtual void OnKey(int nKey) override
	{
		Hide();
	}

	virtual void OnTimer() override
	{		
		Hide();
	}
};


#endif