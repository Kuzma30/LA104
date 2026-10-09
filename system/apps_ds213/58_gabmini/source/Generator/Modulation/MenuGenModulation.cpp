#include "MenuGenModulation.h"
#include <MainWnd.h>

static const char* const ppszMode[] = {"Off", "AM", "FM", "Sweep"};
static const char* const ppszSynth[] = {"Normal", "DDS"};
static const char* const ppszScale[] = {"Linear", "Log"};

/*virtual*/ void CWndMenuGeneratorMod::Create(CWnd *pParent, ui16 dwFlags)
{
	CWnd::Create("CWndMenuGeneratorMod", dwFlags, CRect(280-CWndMenuItem::MarginLeft, 20, 400, 240), pParent);

	m_proMode.Create( (const char**)ppszMode, &Settings.Gen.nModMode, CCoreGenerator::ModSweep );
	m_proSynth.Create( (const char**)ppszSynth, &Settings.Gen.nDds, 1 );
	m_proScale.Create( (const char**)ppszScale, &Settings.Gen.nSweepLog, 1 );

	// order matters: items hidden in some modes are at the bottom
	m_itmMode.Create( &m_proMode, "Modulation", RGB565(b00040), this );
	m_itmSynth.Create( &m_proSynth, "Synthesis", RGB565(b00040), this );
	m_itmA.Create( "", RGB565(b00040), 2, this );
	m_itmB.Create( "", RGB565(b00040), 2, this );
	m_itmC.Create( "", RGB565(b00040), 2, this );
	m_itmScale.Create( &m_proScale, "Sweep scale", RGB565(b00040), this );
	_ConfigureItems();
}

void CWndMenuGeneratorMod::_ConfigureItems()
{
	int nMode = Settings.Gen.nModMode;
	float fMax = 12000000.0f;

	switch ( nMode )
	{
		case CCoreGenerator::ModAm:
			m_itmA.Setup( "Rate", &Settings.Gen.fModRate, 0.1f, 100000.0f, 1, "Hz" );
			m_itmB.Setup( "Depth", &Settings.Gen.fModDepth, 0, 100, 0, "%" );
			break;
		case CCoreGenerator::ModFm:
			m_itmA.Setup( "Rate", &Settings.Gen.fModRate, 0.1f, 100000.0f, 1, "Hz" );
			m_itmB.Setup( "Deviation", &Settings.Gen.fModDepth, 0, 100, 0, "%" );
			break;
		case CCoreGenerator::ModSweep:
			m_itmA.Setup( "From", &Settings.Gen.fSweepFrom, 0.1f, fMax, 1, "Hz" );
			m_itmB.Setup( "To", &Settings.Gen.fSweepTo, 0.1f, fMax, 1, "Hz" );
			m_itmC.Setup( "Time", &Settings.Gen.fSweepTime, 0.1f, 999.9f, 1, "s" );
			break;
	}
	bool bParams = nMode != CCoreGenerator::ModOff;
	bool bSweep = nMode == CCoreGenerator::ModSweep;
	m_itmA.ShowWindow( bParams );
	m_itmB.ShowWindow( bParams );
	m_itmC.ShowWindow( bSweep );
	m_itmScale.ShowWindow( bSweep );
}

void CWndMenuGeneratorMod::_UpdatePreview()
{
	if ( Settings.Gen.Wave == CSettings::Generator::_Dc )
		MainWnd.m_wndSignalGraph.Setup( NULL, 0 );
	else if ( Settings.Gen.Wave == CSettings::Generator::_Square )
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetWave(Settings.Gen.Wave)->pWave,
			CCoreGenerator::GetWave(Settings.Gen.Wave)->nCount );
	else
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
}

/*virtual*/ void CWndMenuGeneratorMod::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if ( code == ToWord('L', 'D') )
	{
		MainWnd.m_wndSignalGraph.ShowWindow( false );
		return;
	}
	if ( code == ToWord('L', 'E') )
	{
		_ConfigureItems();
		_UpdatePreview();
		MainWnd.m_wndSignalGraph.ShowWindow( true );
		return;
	}
	if ( code == ToWord('i', 'u') )
	{
		if ( pSender == &m_itmMode )
		{
			_ConfigureItems();
			MainWnd.Invalidate();
		}
		CCoreGenerator::Update();
		_UpdatePreview();
	}
}
