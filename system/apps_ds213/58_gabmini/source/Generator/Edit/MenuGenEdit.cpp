#include "MenuGenEdit.h"
#include <MainWnd.h>

// The edit operations work with the custom (volatile) wave, the current wave
// is converted only when an operation is used, browsing the tabs keeps it
static void _EnsureVolatile()
{
	if ( Settings.Gen.Wave != CSettings::Generator::_Volatile )
	{
		CCoreGenerator::CopyToVolatile( Settings.Gen.Wave );
		Settings.Gen.Wave = CSettings::Generator::_Volatile;
		CCoreGenerator::Update();
	}
}

// the custom wave in a file: "GWV1", length (1..256), the samples (0..255)
static const char* const WaveFile = "GENWAVE.DAT";
enum { WaveMagic = 0x31565747 };   // "GWV1"

static bool _SaveWave()
{
	uint8_t* p = MainWnd.m_wndGraph.GetScratch();
	memset( p, 0, BIOS::FAT::SectorSize );
	int nLength = CCoreGenerator::GetVolatileLen();
	uint32_t* pHead = (uint32_t*)p;
	pHead[0] = WaveMagic;
	pHead[1] = (uint32_t)nLength;
	memcpy( p + 8, CCoreGenerator::GetVolatile(), nLength );
	bool bOk = BIOS::FAT::Open( WaveFile, BIOS::FAT::IoWrite ) == BIOS::FAT::EOk &&
		BIOS::FAT::Write( p ) == BIOS::FAT::EOk;
	if ( BIOS::FAT::Close( 8 + nLength ) != BIOS::FAT::EOk )
		bOk = false;
	MainWnd.m_wndGraph.ClearAverage();
	return bOk;
}

static bool _LoadWave()
{
	uint8_t* p = MainWnd.m_wndGraph.GetScratch();
	if ( BIOS::FAT::Open( WaveFile, BIOS::FAT::IoRead ) != BIOS::FAT::EOk )
		return false;
	bool bOk = BIOS::FAT::Read( p ) == BIOS::FAT::EOk;
	BIOS::FAT::Close();
	uint32_t* pHead = (uint32_t*)p;
	int nLength = (int)pHead[1];
	bOk = bOk && pHead[0] == WaveMagic && nLength >= 1 && nLength <= 256;
	if ( bOk )
	{
		memcpy( CCoreGenerator::GetVolatile(), p + 8, nLength );
		CCoreGenerator::GetVolatileLen() = nLength;
	}
	MainWnd.m_wndGraph.ClearAverage();
	return bOk;
}

/*virtual*/ void CWndMenuGeneratorEdit::Create(CWnd *pParent, ui16 dwFlags)
{
	CWnd::Create("CWndMenuGeneratorEdit", dwFlags, CRect(280-CWndMenuItem::MarginLeft, 20, 400, 240), pParent);
	m_itmCopy.Create("Copy\nCH1", RGB565(b00040), 2, this );
	m_itmNormalize.Create("Normalize", RGB565(b00040), 1, this );
	m_itmInvert.Create("Invert", RGB565(b00040), 1, this );
	m_itmEdit.Create("Edit", RGB565(b00040), 1, this );
	m_itmLoad.Create("Load\nGENWAVE", RGB565(b00040), 2, this );
	m_itmSave.Create("Save\nGENWAVE", RGB565(b00040), 2, this );
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
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		MainWnd.m_wndSignalGraph.ShowWindow( true );
		return;
	}

	// copy from oscilloscope
	if ( code == ToWord('m', 'o') && pSender == &m_itmCopy )
	{
		// one period of CH1 from the oscilloscope capture
		if ( CCoreGenerator::CopyFromAdc() )
		{
			Settings.Gen.Wave = CSettings::Generator::_Volatile;
			CCoreGenerator::Update();
			MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		}
		MainWnd.m_wndSignalGraph.Invalidate();
	}

	// normalize
	if ( code == ToWord('m', 'o') && pSender == &m_itmNormalize )
	{
		_EnsureVolatile();
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
		_EnsureVolatile();
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
		_EnsureVolatile();
		MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
		MainWnd.m_wndSignalGraph.m_dwFlags &= ~CWnd::WsNoActivate;
		MainWnd.m_wndSignalGraph.SetFocus();
		MainWnd.m_wndSignalGraph.Invalidate();
		MainWnd.m_wndSignalGraph.ExitHandler( this );
		m_itmEdit.Invalidate();
	}
	// the custom wave to / from GENWAVE.DAT; the result in the label
	if ( code == ToWord('m', 'o') && pSender == &m_itmSave )
	{
		_EnsureVolatile();
		m_itmSave.m_pszId = _SaveWave() ? "Save\nsaved" : "Save\nerror";
		m_itmSave.Invalidate();
		m_itmLoad.m_pszId = "Load\nGENWAVE";
		m_itmLoad.Invalidate();
	}
	if ( code == ToWord('m', 'o') && pSender == &m_itmLoad )
	{
		bool bOk = _LoadWave();
		if ( bOk )
		{
			Settings.Gen.Wave = CSettings::Generator::_Volatile;
			CCoreGenerator::Update();
			MainWnd.m_wndSignalGraph.Setup( CCoreGenerator::GetRamDac(), CCoreGenerator::GetRamLen() );
			MainWnd.m_wndSignalGraph.Invalidate();
		}
		m_itmLoad.m_pszId = bOk ? "Load\nloaded" : "Load\nno file";
		m_itmLoad.Invalidate();
		m_itmSave.m_pszId = "Save\nGENWAVE";
		m_itmSave.Invalidate();
	}

	if ( code == ToWord('e', 'x') && pSender == &MainWnd.m_wndSignalGraph )
	{
		m_itmEdit.SetFocus();
		m_itmEdit.Invalidate();
		MainWnd.m_wndSignalGraph.m_dwFlags |= CWnd::WsNoActivate;
		MainWnd.m_wndSignalGraph.Invalidate();
	}
}
