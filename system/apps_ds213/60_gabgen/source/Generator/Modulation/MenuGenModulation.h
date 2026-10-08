#ifndef __GENERATOR_MENUMAINMODULATION_H__
#define __GENERATOR_MENUMAINMODULATION_H__

#include <Generator/Core/CoreGenerator.h>
#include <Generator/Main/DigitEdit.h>
#include <Settings.h>
#include <Framework.h>

// Number bound to a float setting. Left / Right: step (three significant
// digits, or 1 for values without decimals), F1: digit by digit editing
class CItemNumber : public CWndMenuItem
{
	CDigitEdit m_edit;
	float* m_pValue{nullptr};
	float m_fMin{0}, m_fMax{1};
	int m_nDec{0};
	const char* m_pszUnit{""};

public:
	void Setup(const char* pszLabel, float* pValue, float fMin, float fMax, int nDec, const char* pszUnit)
	{
		m_edit.End();
		m_pszId = pszLabel;
		m_pValue = pValue;
		m_fMin = fMin;
		m_fMax = fMax;
		m_nDec = nDec;
		m_pszUnit = pszUnit;
	}

	virtual void OnKey(int nKey) override
	{
		if ( m_pValue && m_edit.IsActive() )
		{
			bool bChanged;
			if ( m_edit.OnKey( nKey, *m_pValue, bChanged ) )
			{
				if ( bChanged )
					SendMessage(m_pParent, ToWord('i', 'u'), 0);
				Invalidate();
				return;
			}
		}
		if ( m_pValue && (nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right) )
		{
			float fValue = *m_pValue;
			float fStep = m_nDec == 0 ? 1.0f : CCoreGenerator::AutoStep( fValue );
			int nDir = nKey == BIOS::KEY::Right ? 1 : -1;
			fValue = ((int)(fValue / fStep + 0.5f) + nDir) * fStep;
			if ( fValue > m_fMax )
				fValue = m_fMax;
			if ( fValue < m_fMin )
				fValue = m_fMin;
			if ( fValue != *m_pValue )
			{
				*m_pValue = fValue;
				Invalidate();
				SendMessage(m_pParent, ToWord('i', 'u'), 0);
			}
		}
		if ( m_pValue && nKey == BIOS::KEY::Enter )
		{
			m_edit.Begin( *m_pValue, m_fMin, m_fMax, m_nDec );
			Invalidate();
			return;
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void WindowMessage(int nMsg, int nParam) override
	{
		if ( nMsg == CWnd::WmWillLoseFocus )
			m_edit.End();
		CWndMenuItem::WindowMessage( nMsg, nParam );
	}

	virtual void OnPaint() override
	{
		CWndMenuItem::OnPaint();
		if ( !m_pValue )
			return;
		int x = m_rcClient.left + 12 + 16;
		int y = m_rcClient.top + 16;
		if ( m_edit.IsActive() )
		{
			m_edit.Draw( x - 8, y, *m_pValue, m_pszUnit );
			return;
		}
		if ( m_pszUnit[0] == 'H' )
			BIOS::LCD::Print( x, y, RGB565(000000), RGBTRANS, CUtils::FormatFrequency( *m_pValue, 11 ) );
		else if ( m_nDec == 0 )
			BIOS::LCD::Printf( x, y, RGB565(000000), RGBTRANS, "%d %s", (int)(*m_pValue + 0.5f), m_pszUnit );
		else
		{
			int nTenths = (int)(*m_pValue * 10.0f + 0.5f);
			BIOS::LCD::Printf( x, y, RGB565(000000), RGBTRANS, "%d.%d %s", nTenths / 10, nTenths % 10, m_pszUnit );
		}
	}
};

class CWndMenuGeneratorMod : public CWnd
{
public:
	CProviderEnum	m_proMode;
	CProviderEnum	m_proSynth;
	CProviderEnum	m_proScale;
	CItemProvider	m_itmMode;
	CItemProvider	m_itmSynth;
	CItemNumber		m_itmA;
	CItemNumber		m_itmB;
	CItemNumber		m_itmC;
	CItemProvider	m_itmScale;

	virtual void Create(CWnd *pParent, ui16 dwFlags);
	virtual void OnMessage(CWnd* pSender, int code, uintptr_t data) override;

private:
	void _ConfigureItems();
	void _UpdatePreview();
};

#endif
