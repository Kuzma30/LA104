#ifndef __DIGITEDIT_H__
#define __DIGITEDIT_H__

// Digit by digit editing of a number:
//   Left / Right  - select digit
//   Up / Down     - change selected digit (with carry)
//   F4 (Enter) / F2 - finish
class CDigitEdit
{
	bool m_bActive{false};
	int m_nPos{0};      // selected digit: 0 = units, -1 = tenths, 2 = hundreds...
	int m_nInt{1};      // digits before decimal point
	int m_nDec{0};      // digits after decimal point
	float m_fMin{0};
	float m_fMax{0};

	static float _Pow10(int n)
	{
		float f = 1.0f;
		for ( ; n > 0; n-- )
			f *= 10.0f;
		for ( ; n < 0; n++ )
			f *= 0.1f;
		return f;
	}

public:
	bool IsActive() { return m_bActive; }

	void Begin(float fValue, float fMin, float fMax, int nDec)
	{
		m_fMin = fMin;
		m_fMax = fMax;
		m_nDec = nDec;
		m_nInt = 1;
		while ( m_nInt < 9 && _Pow10(m_nInt) <= fMax )
			m_nInt++;
		// start at the third significant digit
		int nMsd = 0;
		while ( nMsd < m_nInt - 1 && _Pow10(nMsd + 1) <= fValue )
			nMsd++;
		m_nPos = max(nMsd - 2, -m_nDec);
		m_bActive = true;
	}

	void End()
	{
		m_bActive = false;
	}

	// returns true when the key was consumed, fValue may be changed
	bool OnKey(int nKey, float& fValue, bool& bChanged)
	{
		bChanged = false;
		if ( !m_bActive )
			return false;
		switch ( nKey )
		{
			case BIOS::KEY::Left:
				if ( m_nPos < m_nInt - 1 )
					m_nPos++;
				return true;
			case BIOS::KEY::Right:
				if ( m_nPos > -m_nDec )
					m_nPos--;
				return true;
			case BIOS::KEY::Up:
			case BIOS::KEY::Down:
			{
				float fStep = _Pow10( m_nPos );
				float fNew = fValue + (nKey == BIOS::KEY::Up ? fStep : -fStep);
				// round to the displayed resolution, float steps accumulate errors
				float fRes = _Pow10( -m_nDec );
				fNew = (int)(fNew / fRes + (fNew >= 0 ? 0.5f : -0.5f)) * fRes;
				if ( fNew > m_fMax )
					fNew = m_fMax;
				if ( fNew < m_fMin )
					fNew = m_fMin;
				if ( fNew != fValue )
				{
					fValue = fNew;
					bChanged = true;
				}
				return true;
			}
			case BIOS::KEY::Enter:
			case BIOS::KEY::Escape:
				m_bActive = false;
				return true;
		}
		return false;
	}

	// Draws the number with the selected digit highlighted, returns width
	int Draw(int x, int y, float fValue, const char* pszUnit)
	{
		int x0 = x;
		uint32_t nValue = (uint32_t)(fValue * _Pow10(m_nDec) + 0.5f);
		char digits[16];
		int nTotal = m_nInt + m_nDec;
		for ( int i = nTotal - 1; i >= 0; i-- )
		{
			digits[i] = '0' + nValue % 10;
			nValue /= 10;
		}
		bool bLeading = true;
		for ( int i = 0; i < nTotal; i++ )
		{
			int nPos = m_nInt - 1 - i;
			if ( nPos == -1 )
				x += BIOS::LCD::Print( x, y, RGB565(000000), RGBTRANS, '.' );
			if ( digits[i] != '0' || nPos <= 0 )
				bLeading = false;
			ui16 clr = bLeading ? RGB565(808080) : RGB565(000000);
			if ( nPos == m_nPos )
			{
				BIOS::LCD::Bar( x, y, x + 8, y + 14, RGB565(000000) );
				clr = RGB565(ffffff);
			}
			x += BIOS::LCD::Print( x, y, clr, RGBTRANS, digits[i] );
		}
		x += BIOS::LCD::Print( x, y, RGB565(000000), RGBTRANS, pszUnit );
		return x - x0;
	}
};

#endif
