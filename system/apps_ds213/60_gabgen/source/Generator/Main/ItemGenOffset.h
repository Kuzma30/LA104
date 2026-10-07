#ifndef __ITEMGENOFFSET_H__
#define __ITEMGENOFFSET_H__

// Approximate voltage on DS203 Wave Out for DAC code 0xfff. Estimated from
// measurements of clipped sine (about 0.55 mV per DAC code), used only for
// the displayed DC level. Measure DC at 100 % and put the value here.
#ifndef WAVEOUT_FULLSCALE_MV
#define WAVEOUT_FULLSCALE_MV 2250
#endif

class CItemOffset : public CWndMenuItem
{
	int m_nPercent;     // 0..100
	bool m_bDc;

public:
	virtual void Create(const char* pszId, ui16 clr, ui8 rows, CWnd *pParent)
	{
		m_nPercent = 50;
		m_bDc = false;
		CWndMenuItem::Create(pszId, clr, rows, pParent);
	}

	// In DC mode the offset is the output level: finer steps, shown in volts
	void SetDcMode(bool bDc)
	{
		if ( m_bDc == bDc )
			return;
		m_bDc = bDc;
		m_pszId = bDc ? "Level" : "Offset";
		Invalidate();
	}

	virtual void OnKey(int nKey) override
	{
		int nStep = m_bDc ? 1 : 5;
		int nNew = m_nPercent;
		if ( nKey == BIOS::KEY::Left )
			nNew -= nStep;
		if ( nKey == BIOS::KEY::Right )
			nNew += nStep;
		if ( nNew < 0 )
			nNew = 0;
		if ( nNew > 100 )
			nNew = 100;
		if ( nNew != m_nPercent )
		{
			m_nPercent = nNew;
			Invalidate();
			SendMessage(m_pParent, ToWord('i', 'u'), 0);
		}
		CWndMenuItem::OnKey( nKey );
	}

	virtual void OnPaint()
	{
		CWndMenuItem::OnPaint();
		int x = m_rcClient.left + 12 + 16;
		int y = m_rcClient.top + 16;
		if ( m_bDc )
		{
			int nMv = m_nPercent * WAVEOUT_FULLSCALE_MV / 100;
			BIOS::LCD::Printf( x, y, RGB565(000000), RGBTRANS, "%d.%02dV", nMv / 1000, (nMv % 1000) / 10 );
		} else
			BIOS::LCD::Printf( x, y, RGB565(000000), RGBTRANS, "%d%%", m_nPercent );
	}

	// 0.0 .. 2.0 (2.0 = 100 %), scale kept for existing callers
	float GetOffset()
	{
		return m_nPercent * 0.02f;
	}
	void SetOffset(float fValue)
	{
		m_nPercent = (int)(fValue * 50.0f + 0.5f);
		if ( m_nPercent < 0 )
			m_nPercent = 0;
		if ( m_nPercent > 100 )
			m_nPercent = 100;
	}

};

#endif
