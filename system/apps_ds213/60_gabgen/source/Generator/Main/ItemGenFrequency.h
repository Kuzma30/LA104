#ifndef __ITEMGENFREQUENCY_H__
#define __ITEMGENFREQUENCY_H__

#define Hz *1.0f
#define KHz *1000.0f
#define MHz *1000000.0f

class CItemFrequency : public CWndMenuItem
{
public:
	virtual void Create(const char* pszId, ui16 clr, ui8 rows, CWnd *pParent) 
	{
		CWndMenuItem::Create(pszId, clr, rows, pParent);
	}
/*
	void UpdateAdc()
	{
		float fVolt = m_nVolt / 10.0f;
		int nAdc = Settings.DacCalib.Get( fVolt );
		BIOS::GEN::ConfigureDc( nAdc );
	}
*/
	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right )
		{
			CCoreGenerator::StepFrequency( nKey == BIOS::KEY::Right ? +1 : -1 );
			Invalidate();
			SendMessage(m_pParent, ToWord('i', 'u'), 0);
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void OnPaint()
	{
		CWndMenuItem::OnPaint();
		float fFrequency = CCoreGenerator::GetFrequency();
		BIOS::LCD::Print( m_rcClient.left + 12 + 16, m_rcClient.top + 16, RGB565(000000), RGBTRANS,
			CUtils::FormatFrequency(fFrequency) );
	}

};

#undef Hz
#undef KHz
#undef MHz

#endif
