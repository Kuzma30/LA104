#include "MenuGenEdit.h"
#include <MainWnd.h>

/*virtual*/ void CWndMenuGeneratorEdit::Create(CWnd *pParent, ui16 dwFlags) 
{
	CWnd::Create("CWndMenuGeneratorEdit", dwFlags, CRect(280-CWndMenuItem::MarginLeft, 20, 400, 240), pParent);
	m_itmCopy.Create("Copy\nCH1", RGB565(b00040), 2, this );
	m_itmNormalize.Create("Normalize", RGB565(b00040), 1, this );
	m_itmInvert.Create("Invert", RGB565(b00040), 1, this );
	m_itmEdit.Create("Edit", RGB565(b00040), 1, this );
	m_itmLoad.Create("~Load", RGB565(b00040), 1, this );
	m_itmSave.Create("~Save", RGB565(b00040), 1, this );
}

/*virtual*/ void CWndMenuGeneratorEdit::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if (code == ToWord('L', 'D') )
	{
		MainWnd.m_wndSignalGraph.ShowWindow( false );
		return;
	}

	if (code == ToWord('L', 'E') )
	{
		if ( Settings.Gen.Wave != CSettings::Generator::_Volatile )
		{
			CCoreGenerator::CopyToVolatile( Settings.Gen.Wave );
			Settings.Gen.Wave = CSettings::Generator::_Volatile;
			CCoreGenerator::Update();
		}
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		MainWnd.m_wndSignalGraph.ShowWindow( true );
		return;
	}

	// copy from oscilloscope
	if ( code == ToWord('m', 'o') && pSender == &m_itmCopy )
	{
		// The scope keeps the ADC configured (timebase, range, trigger) when the
		// generator app is started, so the last scope setup is reused here.
		if ( CCoreGenerator::CopyFromAdc() )
		{
			Settings.Gen.Wave = CSettings::Generator::_Volatile;
			CCoreGenerator::Update();
			MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		} else
		{
			// capture buffer was reused, restore the wave
			CCoreGenerator::Update();
		}
		MainWnd.m_wndSignalGraph.Invalidate();
	}

	// normalize
	if ( code == ToWord('m', 'o') && pSender == &m_itmNormalize )
	{
		int nLength = CCoreGenerator::GetVolatileLen();
		ui8* pVolatile = CCoreGenerator::GetVolatile();
		int nMin = 0, nMax = 0;
		for ( int i = 0; i < nLength; i++ )
		{
			int nValue = *pVolatile++;
			if ( i == 0 )
			{
				nMin = nMax = nValue;
			} else
			{
				nMin = min(nMin, nValue);
				nMax = max(nMax, nValue);
			}
		}

		if ( nMax == nMin )
			return; // flat signal can not be stretched

		pVolatile = CCoreGenerator::GetVolatile();
		for ( int i = 0; i < nLength; i++ )
		{
			int nValue = *pVolatile;
			nValue = (nValue - nMin) * 255 / (nMax - nMin);
			_ASSERT( nValue >= 0 && nValue <= 255 );
			*pVolatile++ = nValue;
		}
		CCoreGenerator::Update();
		MainWnd.m_wndSignalGraph.Invalidate();
	}

	// invert
	if ( code == ToWord('m', 'o') && pSender == &m_itmInvert )
	{
		int nLength = CCoreGenerator::GetVolatileLen();
		ui8* pVolatile = CCoreGenerator::GetVolatile();
		for ( int i = 0; i < nLength; i++, pVolatile++ )
			*pVolatile = 255-*pVolatile;

		CCoreGenerator::Update();
		MainWnd.m_wndSignalGraph.Invalidate();
	}

	// edit
	if ( code == ToWord('m', 'o') && pSender == &m_itmEdit )
	{
		Settings.Gen.Wave = CSettings::Generator::_Volatile;
		CCoreGenerator::Update();
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		MainWnd.m_wndSignalGraph.m_dwFlags &= ~CWnd::WsNoActivate;
		MainWnd.m_wndSignalGraph.SetFocus();
		MainWnd.m_wndSignalGraph.Invalidate();
		MainWnd.m_wndSignalGraph.ExitHandler( this );
		m_itmEdit.Invalidate();
	}
	if ( code == ToWord('e', 'x') && pSender == &MainWnd.m_wndSignalGraph )
	{
		m_itmEdit.SetFocus();
		m_itmEdit.Invalidate();
		MainWnd.m_wndSignalGraph.m_dwFlags |= CWnd::WsNoActivate;
		MainWnd.m_wndSignalGraph.Invalidate();
	}
}

