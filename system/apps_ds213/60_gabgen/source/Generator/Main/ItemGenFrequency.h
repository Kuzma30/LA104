#ifndef __ITEMGENFREQUENCY_H__
#define __ITEMGENFREQUENCY_H__

class CItemFrequency : public CWndMenuItem
{
	// 0 = automatic (three significant digits), otherwise 10^(n-2) Hz
	int m_nStep;
	char m_strLabel[16];

public:
	virtual void Create(const char* pszId, ui16 clr, ui8 rows, CWnd *pParent)
	{
		m_nStep = 0;
		CWndMenuItem::Create(pszId, clr, rows, pParent);
		_UpdateLabel();
	}

	float GetStep()
	{
		if ( m_nStep == 0 )
			return 0;
		float fStep = 0.1f;
		for ( int i = 1; i < m_nStep; i++ )
			fStep *= 10.0f;
		return fStep;
	}

	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right )
		{
			CCoreGenerator::StepFrequency( nKey == BIOS::KEY::Right ? +1 : -1, GetStep() );
			Invalidate();
			SendMessage(m_pParent, ToWord('i', 'u'), 0);
		}
		if ( nKey == BIOS::KEY::Enter )
		{
			// auto, 0.1 Hz, 1 Hz ... 1 MHz
			if ( ++m_nStep > 8 )
				m_nStep = 0;
			_UpdateLabel();
			Invalidate();
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void OnPaint()
	{
		CWndMenuItem::OnPaint();
		float fFrequency = CCoreGenerator::GetFrequency();
		BIOS::LCD::Print( m_rcClient.left + 12 + 16, m_rcClient.top + 16, RGB565(000000), RGBTRANS,
			CUtils::FormatFrequency(fFrequency, 11) );
	}

private:
	void _UpdateLabel()
	{
		if ( m_nStep == 0 )
		{
			m_pszId = "Frequency";
			return;
		}
		static const char* const steps[] = {"0.1Hz", "1Hz", "10Hz", "100Hz", "1kHz", "10kHz", "100kHz", "1MHz"};
		strcpy( m_strLabel, "Step " );
		strcat( m_strLabel, steps[m_nStep-1] );
		m_pszId = m_strLabel;
	}
};

#endif
