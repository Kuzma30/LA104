#ifndef __ITEMGENDUTY_H__
#define __ITEMGENDUTY_H__

class CItemDuty : public CWndMenuItem
{
public:
	virtual void Create(const char* pszId, ui16 clr, ui8 rows, CWnd *pParent)
	{
		CWndMenuItem::Create(pszId, clr, rows, pParent);
	}

	virtual void OnKey(int nKey) override
	{
		int nDuty = Settings.Gen.nDuty;
		if ( nKey == BIOS::KEY::Left && nDuty > 0 )
			nDuty -= 5;
		if ( nKey == BIOS::KEY::Right && nDuty < 100 )
			nDuty += 5;
		if ( nDuty != Settings.Gen.nDuty )
		{
			Settings.Gen.nDuty = max(0, min(nDuty, 100));
			Invalidate();
			SendMessage(m_pParent, ToWord('i', 'u'), 0);
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void OnPaint()
	{
		CWndMenuItem::OnPaint();
		int nDuty = CCoreGenerator::GetDuty();
		BIOS::LCD::Printf( m_rcClient.left + 12 + 16, m_rcClient.top + 16, RGB565(000000), RGBTRANS, "%d%%",
			nDuty );
	}
};

#endif
