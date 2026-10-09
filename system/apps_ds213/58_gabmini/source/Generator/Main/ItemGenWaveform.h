#ifndef __ITEMGENWAVEFORM_H__
#define __ITEMGENWAVEFORM_H__

class CItemGenWaveform : public CItemProvider
{
	CProviderEnum	m_proWave;

public:
	void Create( CWnd* pParent )
	{
		m_proWave.Create( (const char**)CSettings::Generator::ppszTextWave,
			(NATIVEENUM*)&Settings.Gen.Wave, CSettings::Generator::_WaveMax );
		CItemProvider::Create( &m_proWave, "Waveform", RGB565(b00040), pParent );
	}

	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right )
		{
			int nWave = Settings.Gen.Wave;
			int nNew = nWave + (nKey == BIOS::KEY::Right ? 1 : -1);
#ifndef DS203
			// hardware triangle needs direct access to DS203 DAC
			if ( nNew == CSettings::Generator::_TriHw )
				nNew += nKey == BIOS::KEY::Right ? 1 : -1;
#endif
			if ( nNew >= 0 && nNew <= CSettings::Generator::_WaveMax )
			{
				*(NATIVEENUM*)&Settings.Gen.Wave = nNew;
				Invalidate();
				SendMessage(m_pParent, ToWord('i', 'u'), 0);
			}
		}
		CWndMenuItem::OnKey( nKey );
	}
};

#endif
