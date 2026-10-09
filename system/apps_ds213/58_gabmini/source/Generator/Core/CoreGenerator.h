#ifndef __COREGENERATOR_H__
#define __COREGENERATOR_H__

#include <library.h>

class CCoreGenerator
{
public:
	// DAC buffer size, one period uses at most PeriodSamples. Shares the RAM
	// with the oscilloscope, so the buffer for DDS / modulation (multiple
	// periods) is half of the standalone generator
	enum { MaxSamples = 1024, PeriodSamples = 512 };
	enum { ModOff = 0, ModAm, ModFm, ModSweep };

private:
	static const struct TWaveData {
	  char* pszName;
	  ui16 nCount;
	  const ui16* pWave;
	  ui16 nOversampling;
	} Waves[];

	// timer setting: sample rate = 72 MHz / nDiv / nCnt, buffer of nSamples
	// holding nCycles periods (more than one with DDS / modulation)
	struct TPlan {
		int nDiv;
		int nCnt;
		int nSamples;
		int nCycles;
		float fReal;
	};

	// NOTE: big buffer, DAC samples of one period
	static ui16 m_pRamWave[MaxSamples];
	static int m_nRamLen;
	// buffer for editing custom waves and for copying signal
	// from oscilloscope
	static ui8 m_pVolatile[256];
	static int m_nVolatileLen;
	// generated frequency
	static float m_fReal;
	// samples per period of hardware generated waves (noise, triangle)
	static int m_nFixedSamples;
	// modulation / sweep done in Tick()
	static int m_nSwMode;
	static int m_nSwSamples;
	static float m_fSwBase;
	static float m_fSwPhase;
	static uint32_t m_nSwLast;

public:
	static ui16*	GetRamDac();
	static int		GetRamLen();

	// Applies waveform, amplitude, offset, frequency and duty from Settings.Gen.
	// Frequency is taken from Settings.Gen.nFrequency (the requested value);
	// timer prescaler, reload and samples per period are chosen to match it
	// as close as possible
	static void Update();

	static ui8*		GetVolatile();
	static int&		GetVolatileLen();

	static int GetDuty();
	static void SetDuty(int nPercent);

	// Generated frequency
	static float GetFrequency();
	// Moves the frequency by fStep (fStep <= 0: three significant digits),
	// guaranteeing the generated frequency changes in that direction
	static void StepFrequency(int nDir, float fStep = 0);
	static float AutoStep(float fFreq);
	static float GetMaxFrequency() { return _MaxFrequency(); }
	// Call often from the main loop, runs slow modulations and sweep
	static void Tick();

	static const TWaveData* GetWave( int nWave );
	static void CopyToVolatile( int nWave );
	// Captures CH1 from the ADC and stores one period into the volatile buffer
	static bool CopyFromAdc();

protected:
	static int _Sample(int nWave, uint32_t nPhase);
	static int _Sin(uint32_t nPhase);
	static void _Generate(int nWave, int nSamples, int nCycles = 1, int nMod = ModOff, float fDepth = 0, int nScale = -1);
	static void _SampleRange(int nWave, int& nMin, int& nMax);
	static float _MaxFrequency();
	static void _Timer(float fTicks, int nMinCnt, int& nDiv, int& nCnt);
	static void _Plan(float fFreq, TPlan& plan);
	static void _PlanCounts(float fFreq, int nMin, int nMax, int nUnit, TPlan& plan);
	static bool _PlanDds(float fFreq, int nMinPeriod, TPlan& plan);
	static float _Err(float fReal, float fFreq);
	static int _ModMode();
	static void _StartSoftware(int nMod, const TPlan& plan);
	static void _Retime(float fFreq);
	static void _ApplyAnalog(const TPlan& plan);
	static void _ApplySquare(const TPlan& plan);
	static void _ConfigureDc(int nDac);
	static void _ConfigureWave(uint16_t* p, int n);
};

#endif
