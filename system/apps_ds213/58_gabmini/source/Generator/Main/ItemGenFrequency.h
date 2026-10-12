#ifndef __ITEMGENFREQUENCY_H__
#define __ITEMGENFREQUENCY_H__

#include "DigitEdit.h"

// Left / Right: step by three significant digits
// F4 (Enter): digit by digit editing (Left / Right select digit, Up / Down change it,
//     F4 / F2 finish), the label shows the really generated frequency
class CItemFrequency : public CWndMenuItem
{
	CDigitEdit m_edit;
	char m_strLabel[20];

public:
	virtual void Create(const char* pszId, ui16 clr, ui8 rows, CWnd *pParent)
	{
		CWndMenuItem::Create(pszId, clr, rows, pParent);
	}

	virtual void OnKey(int nKey) override
	{
		if ( m_edit.IsActive() )
		{
			bool bChanged;
			float fValue = Settings.Gen.nFrequency;
			if ( m_edit.OnKey( nKey, fValue, bChanged ) )
			{
				if ( bChanged )
				{
					Settings.Gen.nFrequency = fValue;
					SendMessage(m_pParent, ToWord('i', 'u'), 0);
				}
				_UpdateLabel();
				Invalidate();
				return;
			}
		}

		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right )
		{
			CCoreGenerator::StepFrequency( nKey == BIOS::KEY::Right ? +1 : -1 );
			Invalidate();
			SendMessage(m_pParent, ToWord('i', 'u'), 0);
		}
		if ( nKey == BIOS::KEY::Enter )
		{
			// start from the generated value rounded to 0.1 Hz
			Settings.Gen.nFrequency = (int)(CCoreGenerator::GetFrequency() * 10.0f + 0.5f) * 0.1f;
			m_edit.Begin( Settings.Gen.nFrequency, 0.1f, CCoreGenerator::GetMaxFrequency(), 1 );
			_UpdateLabel();
			Invalidate();
			return;
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void WindowMessage(int nMsg, int nParam) override
	{
		// leaving the item finishes editing
		if ( nMsg == CWnd::WmWillLoseFocus && m_edit.IsActive() )
		{
			m_edit.End();
			_UpdateLabel();
		}
		CWndMenuItem::WindowMessage( nMsg, nParam );
	}

	virtual void OnPaint()
	{
		CWndMenuItem::OnPaint();
		int x = m_rcClient.left + 12 + 16;
		int y = m_rcClient.top + 16;
		if ( m_edit.IsActive() )
			m_edit.Draw( x - 8, y, Settings.Gen.nFrequency, "Hz" );
		else
			BIOS::LCD::Print( x, y, RGB565(000000), RGBTRANS,
				CUtils::FormatFrequency(CCoreGenerator::GetFrequency(), 11) );
	}

	void Refresh()
	{
		_UpdateLabel();
		Invalidate();
	}

private:
	void _UpdateLabel()
	{
		if ( !m_edit.IsActive() )
		{
			m_pszId = "Frequency";
			return;
		}
		strcpy( m_strLabel, "=" );
		strcat( m_strLabel, CUtils::FormatFrequency( CCoreGenerator::GetFrequency(), 11 ) );
		m_pszId = m_strLabel;
	}
};

#endif
