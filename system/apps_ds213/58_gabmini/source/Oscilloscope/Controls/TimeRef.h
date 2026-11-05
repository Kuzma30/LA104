class CWndTimeReferences : public CWnd
{
public:
	virtual void Create(CWnd *pParent, int dwFlags) 
	{
		CWnd::Create("CWndTimeReferences", dwFlags | CWnd::WsNoActivate, 
			CRect(12, 16, 12+CWndGraph::DivsX*CWndGraph::BlkX, 21), pParent);
	}

	virtual void OnPaint()
	{
		//BIOS::LCD::Bar( m_rcClient, RGB565(000000) );
		GUI::Background(m_rcClient, RGB565(101010), RGB565(404040));
		// cursors X1, X2 as small marks above the graph
		const CSettings::Marker* arrT[2] = { &Settings.MarkT1, &Settings.MarkT2 };
		for ( int i = 0; i < 2; i++ )
		{
			if ( arrT[i]->Mode == CSettings::Marker::_Off )
				continue;
			int xm = arrT[i]->nValue - Settings.Time.Shift;
			if ( xm >= 1 && xm < m_rcClient.Width() - 1 )
				BIOS::LCD::Bar( m_rcClient.left + xm - 1, m_rcClient.top + 1,
					m_rcClient.left + xm + 2, m_rcClient.bottom, arrT[i]->u16Color );
		}
		si16 x = CSettings::Trigger::Position - Settings.Time.Shift;
		if ( x >= 0 && x < m_rcClient.Width() )
			BIOS::LCD::Draw( m_rcClient.left + x-4, m_rcClient.top, 
				RGB565(606060), RGBTRANS, CShapes::trig_time );
	}
};
