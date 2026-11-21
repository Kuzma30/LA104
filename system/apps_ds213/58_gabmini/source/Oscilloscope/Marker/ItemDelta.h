#ifndef __MENUITEMMARKERDELTA_H__
#define __MENUITEMMARKERDELTA_H__

#include "MarkerUnits.h"

class CItemMarkerDelta : public CWndMenuItem
{
	CSettings::Marker* m_pMarker1;
	CSettings::Marker* m_pMarker2;
	PSTR	m_strId;

public:
	virtual void Create(PSTR strId, CSettings::Marker* pMarker1, CSettings::Marker* pMarker2, CWnd *pParent) 
	{
		_ASSERT( pMarker1 );
		_ASSERT( pMarker2 );
		m_pMarker1 = pMarker1;
		m_pMarker2 = pMarker2;
		m_strId = strId;
	
		if ( m_pMarker1->Type == CSettings::Marker::_Voltage )
			CWndMenuItem::Create( NULL, RGB565(8080b0), 2, pParent);
		else
			CWndMenuItem::Create( NULL, RGB565(8080b0), 3, pParent);
	}

	virtual void OnPaint()
	{
		bool bDisabled = ( m_pMarker1->Mode == CSettings::Marker::_Off ) ||
			( m_pMarker2->Mode == CSettings::Marker::_Off );

		ui16 clr = bDisabled ? RGB565(808080) : RGB565(000000);

		CWndMenuItem::OnPaint();
		int x = m_rcClient.left + 12 + MarginLeft;
		int y = m_rcClient.top;
		int _x = x;
		x += 8;
		x += BIOS::LCD::Print( x, y, clr, RGBTRANS, m_strId );
		if ( bDisabled )
			return;

		x = _x;
		y += 16;
		//x += BIOS::LCD::Print( x, y, clr, RGBTRANS, CUtils::itoa(nResult) );

		if ( m_pMarker1->Display == CSettings::Marker::_Raw )
		{
			int nResult = m_pMarker1->nValue - m_pMarker2->nValue;
			x += BIOS::LCD::Print( x, y, clr, RGBTRANS, CUtils::itoa(nResult) );
		}
		else
		{
			if ( m_pMarker1->Type == CSettings::Marker::_Time && Fft::Shown() )
			{
				// FFT: frequency difference and level difference X2 - X1
				int x1 = Fft::CursorColumn( m_pMarker1->nValue ), x2 = Fft::CursorColumn( m_pMarker2->nValue );
				float fDf = Fft::ColumnHz( x2 ) - Fft::ColumnHz( x1 );
				if ( fDf < 0 ) fDf = -fDf;
				CMarkerUnit funit = MarkerFreqUnit( fDf > 0 ? fDf : 1.0f );
				x += MarkerPrint( x, y, clr, fDf * funit.fScale );
				x += 2;
				BIOS::LCD::Print( x, y, RGB565(404040), RGBTRANS, funit.strUnit );
				x = _x;
				y += 16;
				float fDb1, fDb2;
				if ( Fft::ColumnDb( x1, fDb1 ) && Fft::ColumnDb( x2, fDb2 ) )
				{
					x += MarkerPrint( x, y, clr, fDb2 - fDb1 );
					x += 2;
					BIOS::LCD::Print( x, y, RGB565(404040), RGBTRANS, "dB" );
				} else
					BIOS::LCD::Print( x, y, RGB565(808080), RGBTRANS, "--" );
			} else
			if ( m_pMarker1->Type == CSettings::Marker::_Time )
			{ 
				float fTimeRes = Settings.Runtime.m_fTimeRes / CWndGraph::BlkX;
				float fValue = fTimeRes * ( m_pMarker2->nValue - m_pMarker1->nValue );
				CMarkerUnit unit = MarkerTimeUnit();
				x += MarkerPrint( x, y, clr, fValue * unit.fScale );
				x += 2;
				BIOS::LCD::Print( x, y, RGB565(404040), RGBTRANS, unit.strUnit );
				x = _x;
				y += 16;
				if ( fValue == 0 )
					BIOS::LCD::Print( x, y, clr, RGBTRANS, "Error");
				else
				{
					float fFreq = 1.0f / ( fValue < 0 ? -fValue : fValue );
					CMarkerUnit funit = MarkerFreqUnit( fFreq );
					x += MarkerPrint( x, y, clr, fFreq * funit.fScale );
					x += 2;
					BIOS::LCD::Print( x, y, RGB565(404040), RGBTRANS, funit.strUnit );
				}
			}
			if ( m_pMarker1->Type == CSettings::Marker::_Voltage )
			{
				if ( m_pMarker1->Source != m_pMarker2->Source )
				{
					BIOS::LCD::Print( x, y, RGB565(808080), RGBTRANS, "Error");
				} else
				{
					CSettings::Calibrator::FastCalc fastCalc;
					if (m_pMarker1->Source == CSettings::Marker::_CH1)
						Settings.CH1Calib.Prepare( &Settings.CH1, fastCalc );
					else
						Settings.CH2Calib.Prepare( &Settings.CH2, fastCalc );

					float fValue1 = Settings.CH1Calib.Voltage( fastCalc, (float)m_pMarker1->nValue );
					float fValue2 = Settings.CH1Calib.Voltage( fastCalc, (float)m_pMarker2->nValue );
					float fResult = fValue2 - fValue1;
					x += BIOS::LCD::Printf( x, y, clr, RGBTRANS, "%f", F2D(fResult) );
					x += 2;
					BIOS::LCD::Print( x, y, RGB565(404040), RGBTRANS, "V");
				}
			}
		}

		

	}
};

#endif
