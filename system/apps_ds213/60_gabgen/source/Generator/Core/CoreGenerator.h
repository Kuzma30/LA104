#ifndef __COREGENERATOR_H__
#define __COREGENERATOR_H__

#include <library.h>

class CCoreGenerator
{
	static const struct TWaveData {
	  char* pszName;
	  ui16 nCount;
	  const ui16* pWave;
	  ui16 nOversampling;
	} Waves[];

	// NOTE: big buffer
	static ui16 m_pRamWave[128*4];
	static int m_nRamLen;
	// buffer for editing custom waves and for copying signal
	// from oscilloscope
	static ui8 m_pVolatile[256];
	static int m_nVolatileLen;

public:
	static ui16*	GetRamDac();
	static int		GetRamLen();

	// Applies waveform, amplitude, offset, frequency and duty from Settings.Gen.
	// Frequency is taken from Settings.Gen.nFrequency (the requested value), so
	// switching waveforms keeps the output frequency instead of reusing a timer
	// reload value computed for a different waveform.
	static void Update();

	static ui8*		GetVolatile();
	static int&		GetVolatileLen();

	static int GetDuty();
	static void SetDuty(int nPercent);

	// Real output frequency, computed with the same integer math the BIOS uses
	static float GetFrequency();
	// Moves the frequency one step up (dir>0) or down (dir<0), guaranteeing
	// that the real output frequency changes even when the timer resolution
	// is coarser than the step
	static void StepFrequency(int nDir);

	static const TWaveData* GetWave( int nWave );
	static void CopyToVolatile( int nWave );
	// Captures CH1 from the ADC and stores one period into the volatile buffer
	static bool CopyFromAdc();

protected:
	static ui16 _GetCount(ui8 nWaveIndex);
	static ui16* _GetWave(ui8 nWaveIndex);
	static int _GetRateSamples();
	static int _ClampHz(int nHz);
	static int _GetRateHz(float fFreq, int nSamples);
	static bool _Moves(int nHz, int nSamples, float fFrom, int nDir);
	static void _Model(int nHz, int& nPsc, int& nArr);
	static float _Real(int nPsc, int nArr, int nSamples);
	static void _ApplyRate(int nHz);
	static void _ConfigureDc(int nDac);
	static void _ConfigureWave(uint16_t* p, int n);
};

#endif
