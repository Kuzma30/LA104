#include "CoreGenerator.h"
#include <Settings.h>

#ifdef _ARM
#define REG32(a) (*(volatile uint32_t*)(a))
#else
// host builds (unit tests): peripheral registers go to a dummy array
static volatile uint32_t gDummyRegs[0x400];
#define REG32(a) (gDummyRegs[((uint32_t)(a) >> 2) & 0x3ff])
#endif

/*static*/ ui16 CCoreGenerator::m_pRamWave[CCoreGenerator::MaxSamples];
/*static*/ int CCoreGenerator::m_nRamLen = 0;
/*static*/ ui8 CCoreGenerator::m_pVolatile[256];
/*static*/ int CCoreGenerator::m_nVolatileLen = 0;
/*static*/ float CCoreGenerator::m_fReal = 0;
/*static*/ int CCoreGenerator::m_nFixedSamples = 1;
/*static*/ int CCoreGenerator::m_nSwMode = 0;
/*static*/ int CCoreGenerator::m_nSwSamples = 1;
/*static*/ float CCoreGenerator::m_fSwBase = 0;
/*static*/ float CCoreGenerator::m_fSwPhase = 0;
/*static*/ uint32_t CCoreGenerator::m_nSwLast = 0;

// quarter of sine period, 64 segments, amplitude 2047
static const ui16 pQuarterSine[65] = {
	0,50,100,151,201,251,300,350,399,449,497,546,594,642,690,737,783,830,875,920,965,1009,1052,1095,1137,1179,1219,1259,1299,1337,1375,1411,1447,1483,1517,1550,1582,1614,1644,1674,1702,1729,1756,1781,1805,1828,1850,1871,1891,1910,1927,1944,1959,1973,1986,1997,2008,2017,2025,2032,2037,2041,2045,2046,2047 };

static const ui16 pSquareWave[] = {0x000,0xfff};

const ui16 pCardiac128Wave[] = {
	0x3bd, 0x397, 0x391, 0x391, 0x396, 0x393, 0x3a2, 0x3b3, 0x3b1, 0x39f, 0x38f, 0x383, 0x39b, 0x3a3, 0x3a0, 0x3aa,
	0x3ac, 0x3bc, 0x3d0, 0x3f2, 0x41c, 0x439, 0x425, 0x401, 0x3f1, 0x3c5, 0x392, 0x37a, 0x374, 0x37a, 0x37f, 0x386,
	0x385, 0x381, 0x36c, 0x385, 0x3a1, 0x39c, 0x394, 0x321, 0x176, 0x1bf, 0x3ad, 0x728, 0xc38, 0xee3, 0xfff, 0x9f6,
	0x410, 0x181, 0x000, 0x0e0, 0x154, 0x1b8, 0x1e8, 0x219, 0x238, 0x24f, 0x288, 0x2b8, 0x2d6, 0x2ed, 0x319, 0x33a,
	0x34c, 0x371, 0x38f, 0x3aa, 0x3b7, 0x3c7, 0x3e4, 0x3fc, 0x413, 0x423, 0x437, 0x439, 0x450, 0x470, 0x47f, 0x48a,
	0x482, 0x48b, 0x49a, 0x499, 0x48e, 0x48a, 0x477, 0x44a, 0x426, 0x3e6, 0x386, 0x324, 0x2c3, 0x280, 0x25f, 0x244,
	0x234, 0x233, 0x235, 0x251, 0x275, 0x295, 0x2a6, 0x2b0, 0x2c8, 0x2e7, 0x300, 0x316, 0x328, 0x32e, 0x32f, 0x33f,
	0x350, 0x351, 0x352, 0x351, 0x34a, 0x356, 0x35f, 0x35d, 0x363, 0x367, 0x36b, 0x377, 0x384, 0x37a, 0x378, 0x369 };

// Only used for square preview, other waves are computed by _Sample()
/*static*/ const CCoreGenerator::TWaveData CCoreGenerator::Waves[] =
	{
		{(char*)"DC", 0, NULL, 0},
		{(char*)"sin - HQ", 0, NULL, 0 },
		{(char*)"sin - LQ", 0, NULL, 0 },
		{(char*)"triangle", 0, NULL, 0 },
		{(char*)"sawtooth", 0, NULL, 0 },
		{(char*)"square", 2, pSquareWave, 0 },
		{(char*)"cardio", 128, pCardiac128Wave, 0 },
		{(char*)"noise", 0, NULL, 0 },
		{(char*)"tri HW", 0, NULL, 0 },
		{(char*)"Volatile", 0, NULL, 0 },
		{NULL, 0, NULL, 0}
	};

#define CPUCLOCK 72000000

// Highest DAC sample rate (DMA transfers per second), the same Wildcat uses
// for 200 kHz sine. CPUCLOCK / MinTicks
#define MIN_TICKS 40
#define SQ_MAX_HZ 12000000.0f
#ifdef DS203
#define FREQ_MIN 0.1f
#else
// DS213 BIOS takes integer Hz and limits prescaler
#define FREQ_MIN 1.0f
#endif

#ifdef DS203
// Hardware noise / triangle generation by the STM32 DAC (application note AN3126).
// The BIOS feeds DAC channel 1 from DMA2 channel 4 triggered by TIM7 update. For
// hardware waveforms we stop that DMA, route TIM7 TRGO into the DAC trigger and
// let the DAC add LFSR noise or triangle counter to the DHR register.
namespace HwWave
{
	volatile uint32_t& DAC_CR = REG32(0x40007400);
	volatile uint32_t& DAC_DHR12R1 = REG32(0x40007408);
	volatile uint32_t& TIM7_CR2 = REG32(0x40001404);
	volatile uint32_t& DMA2_CCR4 = REG32(0x40020444);

	const uint32_t Ch1Mask = 0x1fff;
	const uint32_t EN1 = 1<<0, BOFF1 = 1<<1, TEN1 = 1<<2, TSEL1_TIM7 = 2<<3;

	bool bActive = false;
	uint32_t nSavedCr = 0;
	uint32_t nSavedCr2 = 0;

	// Analog output configured the same way as Wildcat firmware does it:
	// PA4 in analog mode, DAC channel 1 enabled WITHOUT the output buffer, no
	// trigger (DMA from TIM7 update writes DHR directly), TIM7 running.
	// With the buffer enabled (SYS default) the buffer saturates near both
	// rails and recovers with a delay - clipped waves got a step at the end
	// of every clipped part. The DAC is also re-enabled after logic output
	// mode (leds.elf) switched it off.
	void EnsureAnalog()
	{
		volatile uint32_t& GPIOA_CRL = REG32(0x40010800);
		volatile uint32_t& TIM7_CR1 = REG32(0x40001400);
		GPIOA_CRL &= ~(0xfu << 16);                 // PA4 analog
		if ( (DAC_CR & Ch1Mask) != (EN1 | BOFF1) )
		{
			DAC_CR &= ~Ch1Mask;                     // reconfigure disabled channel
			DAC_CR |= BOFF1;
			DAC_CR |= EN1;
		}
		TIM7_CR1 = 0x85;                            // ARPE, URS, CEN
	}

	// Square wave comes from PB6 which is tied to the DAC output (PA4) on the
	// board, release PA4 so the DAC does not load it (as Wildcat does)
	void ReleaseAnalog()
	{
		volatile uint32_t& GPIOA_CRL = REG32(0x40010800);
		volatile uint32_t& TIM7_CR1 = REG32(0x40001400);
		GPIOA_CRL = (GPIOA_CRL & ~(0xfu << 16)) | (0x4u << 16); // PA4 floating input
		TIM7_CR1 = 0x84;
		DAC_CR &= ~Ch1Mask;
	}

	// nMode: 1 = noise, 2 = triangle
	void Enable(int nMode, int nMamp, int nBase)
	{
		if ( !bActive )
		{
			nSavedCr = DAC_CR;
			nSavedCr2 = TIM7_CR2;
			bActive = true;
		}
		DMA2_CCR4 &= ~1;                            // stop sample stream
		TIM7_CR2 = (TIM7_CR2 & ~0x70) | 0x20;       // TRGO = update event
		uint32_t nCr = (DAC_CR & ~Ch1Mask) | (nSavedCr & BOFF1);
		DAC_CR = nCr;                               // TSEL can be changed only when disabled
		DAC_DHR12R1 = nBase;
		nCr |= TEN1 | TSEL1_TIM7 | (nMode << 6) | (nMamp << 8);
		DAC_CR = nCr;
		DAC_CR = nCr | EN1;
	}

	void Disable()
	{
		if ( !bActive )
			return;
		bActive = false;
		DAC_CR = (DAC_CR & ~Ch1Mask) | (nSavedCr & Ch1Mask);
		TIM7_CR2 = nSavedCr2;
		// BIOS keeps track of the DMA buffer and reconfigures it only when the
		// buffer changes, switching through square mode forces it to restart DMA
		BIOS::DAC::SetMode(BIOS::DAC::EMode::Square, nullptr, 0);
	}
}
#endif

/*static*/ const CCoreGenerator::TWaveData* CCoreGenerator::GetWave( int nWave )
{
	return &Waves[nWave];
}

/*static*/ ui16* CCoreGenerator::GetRamDac()
{
	return m_pRamWave;
}

/*static*/ int CCoreGenerator::GetRamLen()
{
	return m_nRamLen;
}

/*static*/ ui8* CCoreGenerator::GetVolatile()
{
	return m_pVolatile;
}

/*static*/ int& CCoreGenerator::GetVolatileLen()
{
	return m_nVolatileLen;
}

// ---------------------------------------------------------------- waveforms

static int _QuarterSine(int w) // w: 0..0x4000
{
	int i = w >> 8;
	if ( i >= 64 )
		return pQuarterSine[64];
	int a = pQuarterSine[i];
	return a + (((pQuarterSine[i+1] - a) * (w & 0xff)) >> 8);
}

// One period sampled at phase 0..0xffff, returns 0..0xfff before scaling
/*static*/ int CCoreGenerator::_Sample(int nWave, uint32_t nPhase)
{
	nPhase &= 0xffff;
	switch ( nWave )
	{
		case CSettings::Generator::_SinHq:
		case CSettings::Generator::_SinLq:
		{
			// starts at minimum like the original table: -cos
			ui32 p = (nPhase + 0xc000) & 0xffff;
			int w = p & 0x3fff;
			int v;
			switch ( p >> 14 )
			{
				case 0: v = _QuarterSine(w); break;
				case 1: v = _QuarterSine(0x4000 - w); break;
				case 2: v = -_QuarterSine(w); break;
				default: v = -_QuarterSine(0x4000 - w); break;
			}
			return 0x800 + v;
		}
		case CSettings::Generator::_Tri:
		case CSettings::Generator::_TriHw:
			if ( nPhase < 0x8000 )
				return (nPhase * 0xfff) >> 15;
			return ((0xffff - nPhase) * 0xfff) >> 15;
		case CSettings::Generator::_Saw:
			return (nPhase * 0xfff) >> 16;
		case CSettings::Generator::_Cardiac:
		case CSettings::Generator::_Volatile:
		{
			bool bCardio = nWave == CSettings::Generator::_Cardiac;
			int nLen = bCardio ? COUNT(pCardiac128Wave) : m_nVolatileLen;
			if ( nLen <= 0 )
				return 0x800;
			ui32 nPos = nPhase * nLen;     // 16.16 fixed point index
			int i = nPos >> 16;
			int j = (i + 1) % nLen;
			int fr = (nPos >> 8) & 0xff;
			int a = bCardio ? pCardiac128Wave[i] : m_pVolatile[i] << 4;
			int b = bCardio ? pCardiac128Wave[j] : m_pVolatile[j] << 4;
			return a + (((b - a) * fr) >> 8);
		}
		default:
			return 0x800;
	}
}

// sin(2 pi nPhase / 65536) * 2047
/*static*/ int CCoreGenerator::_Sin(uint32_t nPhase)
{
	nPhase &= 0xffff;
	int w = nPhase & 0x3fff;
	switch ( nPhase >> 14 )
	{
		case 0: return _QuarterSine(w);
		case 1: return _QuarterSine(0x4000 - w);
		case 2: return -_QuarterSine(w);
		default: return -_QuarterSine(0x4000 - w);
	}
}

// Fills the DAC buffer with nSamples samples holding nCycles periods of the
// waveform. With modulation, the buffer is exactly one modulation period:
//   AM: envelope (1 + m sin)/(1 + m), peaks keep the set amplitude
//   FM: phase deviation beta = (deviation / carrier) * nCycles
// nScale < 0 uses the amplitude from settings
/*static*/ void CCoreGenerator::_Generate(int nWave, int nSamples, int nCycles, int nMod, float fDepth, int nScale)
{
	_ASSERT( nSamples > 0 && nSamples <= MaxSamples );
	if ( nScale < 0 )
		nScale = Settings.Gen.nScale;
	int nOffset = Settings.Gen.nOffset >> 4;
	ui16 nLfsr = 0xACE1;
	// FM: phase offset in 1/65536 of period per unit of _Sin()
	float fFmGain = fDepth * nCycles * (65536.0f / (6.2831853f * 2047.0f));
	// AM: envelope in 1/65536 per unit of _Sin()
	int nAmGain = (int)(fDepth * 65536.0f / ((1.0f + fDepth) * 2047.0f));
	int nAmBase = (int)(65536.0f / (1.0f + fDepth));

	for (int i=0; i<nSamples; i++)
	{
		int nSample;
		if ( nWave == CSettings::Generator::_Noise )
		{
			// 16 bit galois LFSR, period 65535, longer than the buffer
			for (int j=0; j<12; j++)
				nLfsr = (nLfsr >> 1) ^ (-(nLfsr & 1) & 0xB400);
			nSample = nLfsr & 0xfff;
		} else
		{
			// carrier phase, nCycles periods over the buffer (i*nCycles < 2^20)
			uint32_t nPhase = ((uint32_t)((i * nCycles) % nSamples) << 16) / nSamples;
			if ( nMod == ModFm )
				nPhase += (int)(fFmGain * _Sin( ((uint32_t)i << 16) / nSamples ));
			nSample = _Sample( nWave, nPhase );
		}

		// scaling & offset
		nSample -= 0x800;
		int nGain = nScale;
		if ( nMod == ModAm )
			nGain = (int)(((int64_t)nScale * (nAmBase + nAmGain * _Sin( ((uint32_t)i << 16) / nSamples ))) >> 16);
		nSample = ( nSample * nGain ) >> 16;
		nSample += nOffset;
		if ( nSample < 0 )
			nSample = 0;
		if ( nSample > 0xfff )
			nSample = 0xfff;
		m_pRamWave[i] = nSample;
	}
	m_nRamLen = nSamples;
}

void CCoreGenerator::CopyToVolatile( int nWave )
{
	// DC, noise, square and volatile itself have no shape to copy from
	if ( nWave == CSettings::Generator::_Dc || nWave == CSettings::Generator::_Noise ||
		 nWave == CSettings::Generator::_Square || nWave == CSettings::Generator::_Volatile )
		nWave = CSettings::Generator::_SinHq;

	m_nVolatileLen = 128;
	for ( int i = 0; i < m_nVolatileLen; i++ )
		m_pVolatile[i] = _Sample( nWave, (i << 16) / m_nVolatileLen ) >> 4;
}

/*static*/ bool CCoreGenerator::CopyFromAdc()
{
	// The oscilloscope keeps the last capture in the sample buffer, the period
	// is searched from the left edge of the current view
	int nFirst = max((int)Settings.Time.Shift, Settings.Time.InvalidFirst);
	int nCapture = Sampler::GetValid() - nFirst;
	if ( nCapture < 16 )
		return false;

	auto ch1 = [&](int i) { return (int)(Sampler::GetAt(nFirst + i) & 0xff); };

	int nMin = 255, nMax = 0;
	for ( int i = 0; i < nCapture; i++ )
	{
		int nValue = ch1(i);
		nMin = min(nMin, nValue);
		nMax = max(nMax, nValue);
	}

	if ( nMax - nMin < 8 )
		return false; // flat line, nothing to copy

	// find one period between two rising edges, hysteresis rejects noise
	int nMid = (nMin + nMax) / 2;
	int nHyst = (nMax - nMin) / 8;
	int nEdge[2] = {-1, -1};
	int nEdges = 0;
	bool bLow = false;
	for ( int i = 0; i < nCapture && nEdges < 2; i++ )
	{
		int nValue = ch1(i);
		if ( nValue < nMid - nHyst )
			bLow = true;
		else if ( bLow && nValue > nMid + nHyst )
		{
			bLow = false;
			nEdge[nEdges++] = i;
		}
	}

	int nBegin = 0;
	int nPeriod = min(300, nCapture); // one screen when no period was found
	if ( nEdges == 2 )
	{
		nBegin = nEdge[0];
		nPeriod = nEdge[1] - nEdge[0];
	}

	int nLength = min(nPeriod, (int)COUNT(m_pVolatile));
	for ( int i = 0; i < nLength; i++ )
		m_pVolatile[i] = ch1(nBegin + i * nPeriod / nLength);
	m_nVolatileLen = nLength;
	return true;
}

// ---------------------------------------------------------------- timing

// Samples per period allowed for each waveform. More samples is a cleaner
// signal, fewer samples allow higher frequencies (Wildcat uses 9 for 200 kHz).
/*static*/ void CCoreGenerator::_SampleRange(int nWave, int& nMin, int& nMax)
{
	nMin = 8;
	nMax = PeriodSamples;
	switch ( nWave )
	{
		case CSettings::Generator::_SinLq: nMax = 32; break;
		case CSettings::Generator::_Cardiac: nMin = 32; break;
		case CSettings::Generator::_Volatile: nMin = 16; break;
	}
}

/*static*/ float CCoreGenerator::_MaxFrequency()
{
	int nMin, nMax;
	switch ( Settings.Gen.Wave )
	{
		case CSettings::Generator::_Square:
			return SQ_MAX_HZ;
		case CSettings::Generator::_Noise:
		case CSettings::Generator::_TriHw:
			nMin = max(1, m_nFixedSamples);
			break;
		default:
			_SampleRange( Settings.Gen.Wave, nMin, nMax );
	}
	return (float)CPUCLOCK / MIN_TICKS / nMin;
}

// Best timer setting for nTicks = CPUCLOCK / sample rate:
// prescaler nDiv (1..65536) and reload nCnt (nMinCnt..65536)
/*static*/ void CCoreGenerator::_Timer(float fTicks, int nMinCnt, int& nDiv, int& nCnt)
{
#ifdef DS203
	nDiv = (int)(fTicks / 65536.0f) + 1;
	if ( nDiv > 65536 )
		nDiv = 65536;
	if ( Settings.Gen.Wave == CSettings::Generator::_Square )
	{
		// square: try more prescalers, the product of both can match better
		int nBestDiv = nDiv, nBestCnt = 0;
		float fBestErr = 1e30f;
		for ( int d = nDiv; d < nDiv + 256 && d <= 65536; d++ )
		{
			int c = (int)(fTicks / d + 0.5f);
			if ( c < nMinCnt )
				break;
			if ( c > 65536 )
				continue;
			float fErr = fTicks - (float)d * c;
			if ( fErr < 0 )
				fErr = -fErr;
			if ( fErr < fBestErr )
			{
				fBestErr = fErr;
				nBestDiv = d;
				nBestCnt = c;
				if ( fErr < 0.5f )
					break;
			}
		}
		if ( nBestCnt )
		{
			nDiv = nBestDiv;
			nCnt = nBestCnt;
			return;
		}
	}
	nCnt = (int)(fTicks / nDiv + 0.5f);
#else
	// DS213: BIOS uses fixed prescaler 20 for the DAC timer and takes the
	// sample rate in integer Hz
	nDiv = 20;
	int nHz = (int)(CPUCLOCK / fTicks + 0.5f);
	nHz = max(55, min(nHz, CPUCLOCK / 20 / 2));
	nCnt = (CPUCLOCK / 20) / nHz;
#endif
	if ( nCnt < nMinCnt )
		nCnt = nMinCnt;
	if ( nCnt > 65536 )
		nCnt = 65536;
}

// Timer for a buffer of (n * nUnit) samples repeated at fFreq, n in nMin..nMax
/*static*/ void CCoreGenerator::_PlanCounts(float fFreq, int nMin, int nMax, int nUnit, TPlan& plan)
{
	plan.nCycles = 1;
	// Most samples per period the DAC rate allows. Search downwards from it,
	// at first keeping at least half of it (signal quality), then down to a
	// quarter (but >= 24) if the frequency can not be matched well enough
	int nIdeal = (int)(CPUCLOCK / MIN_TICKS / (fFreq * nUnit));
	nIdeal = max(nMin, min(nIdeal, nMax));
	int nLow1 = max(nMin, nIdeal / 2);
	int nLow2 = max(nMin, min(nLow1, max(24, nIdeal / 4)));
	float fBestErr = 1e30f;
	for ( int nStage = 0; nStage < 2; nStage++ )
	{
		int nLow = nStage == 0 ? nLow1 : nLow2;
		for ( int n = nIdeal; n >= nLow; n-- )
		{
			float fTicks = CPUCLOCK / (fFreq * n * nUnit);
			if ( fTicks < MIN_TICKS && n > nMin )
				continue;
			int nDiv, nCnt;
			_Timer( fTicks, 2, nDiv, nCnt );
			float fReal = (float)CPUCLOCK / nDiv / nCnt / (n * nUnit);
			float fErr = (fReal - fFreq) / fFreq;
			if ( fErr < 0 )
				fErr = -fErr;
			if ( fErr < fBestErr * 0.9999f )
			{
				fBestErr = fErr;
				plan.nSamples = n * nUnit;
				plan.nDiv = nDiv;
				plan.nCnt = nCnt;
				plan.fReal = fReal;
			}
			if ( fErr <= 1e-4f )
				return;   // within 100 ppm, keep the most samples
		}
		if ( fBestErr <= 5e-4f )
			return;
	}
	if ( fBestErr < 1e30f )
		return;

	if ( fBestErr < 1e30f )
		return;
	// not reachable, fallback to the fastest setting
	plan.nSamples = nMin * nUnit;
	_Timer( MIN_TICKS, 2, plan.nDiv, plan.nCnt );
	plan.fReal = (float)CPUCLOCK / plan.nDiv / plan.nCnt / plan.nSamples;
}

/*static*/ void CCoreGenerator::_Plan(float fFreq, TPlan& plan)
{
	int nWave = Settings.Gen.Wave;
	float fMax = _MaxFrequency();
	if ( fFreq > fMax )
		fFreq = fMax;
	if ( fFreq < FREQ_MIN )
		fFreq = FREQ_MIN;

	plan.nCycles = 1;
	if ( nWave == CSettings::Generator::_Square )
	{
		plan.nSamples = 1;
#ifdef DS203
		_Timer( CPUCLOCK / fFreq, 3, plan.nDiv, plan.nCnt );
#else
		// DS213 BIOS chooses prescaler by frequency band
		int nHz = max(5, (int)(fFreq + 0.5f));
		int nPsc = 1;
		if ( nHz <= 10 )
			nPsc = 240;
		else if ( nHz <= 2000 )
			nPsc = 180;
		else if ( nHz <= 20000 )
			nPsc = 18;
		plan.nDiv = nPsc + 1;
		plan.nCnt = max(3, (int)(CPUCLOCK / plan.nDiv / nHz));
#endif
		plan.fReal = (float)CPUCLOCK / plan.nDiv / plan.nCnt;
		return;
	}

	int nMin, nMax;
	if ( nWave == CSettings::Generator::_Noise || nWave == CSettings::Generator::_TriHw )
		nMin = nMax = max(1, m_nFixedSamples);
	else
		_SampleRange( nWave, nMin, nMax );
	_PlanCounts( fFreq, nMin, nMax, 1, plan );

	if ( Settings.Gen.nDds && nMin != nMax && Settings.Gen.nModMode == ModOff )
	{
		TPlan dds;
		if ( _PlanDds( fFreq, nMin, dds ) &&
			 _Err(dds.fReal, fFreq) < _Err(plan.fReal, fFreq) * 0.5f )
			plan = dds;
	}
}

/*static*/ float CCoreGenerator::_Err(float fReal, float fFreq)
{
	float fErr = (fReal - fFreq) / fFreq;
	return fErr < 0 ? -fErr : fErr;
}

// Best fraction p/q close to x with q <= nMaxDen (continued fractions)
static void _BestRational(float x, int nMaxDen, int& p, int& q)
{
	int p0 = 0, q0 = 1, p1 = 1, q1 = 0;
	float y = x;
	for ( int i = 0; i < 32; i++ )
	{
		int a = (int)y;
		int p2 = a * p1 + p0, q2 = a * q1 + q0;
		if ( q2 > nMaxDen )
		{
			// semiconvergent may be closer than the last convergent
			int k = (nMaxDen - q0) / q1;
			int ps = k * p1 + p0, qs = k * q1 + q0;
			float e1 = x - (float)p1 / q1, es = x - (float)ps / qs;
			if ( (es < 0 ? -es : es) < (e1 < 0 ? -e1 : e1) )
				p1 = ps, q1 = qs;
			break;
		}
		p0 = p1; q0 = q1; p1 = p2; q1 = q2;
		float fr = y - a;
		if ( fr < 1e-7f )
			break;
		y = 1.0f / fr;
	}
	p = p1;
	q = q1;
}

// DDS: the buffer holds K periods in L samples, f = Fs * K / L. Many more
// frequencies are reachable than with one period per buffer. The waveform
// is the same, consecutive periods are only sampled at different phases.
/*static*/ bool CCoreGenerator::_PlanDds(float fFreq, int nMinPeriod, TPlan& plan)
{
	int nIdeal = (int)(CPUCLOCK / MIN_TICKS / fFreq);
	if ( nIdeal > PeriodSamples )
		nIdeal = PeriodSamples;
	if ( nIdeal < nMinPeriod )
		return false;
	int nT0 = (int)(CPUCLOCK / (fFreq * nIdeal));
	if ( nT0 < MIN_TICKS )
		nT0 = MIN_TICKS;
	if ( nT0 > 65536 - 64 )
		return false;   // low frequencies are exact without DDS

	float fBestErr = 1e30f;
	for ( int t = nT0; t < nT0 + 64; t++ )
	{
		int nDiv, nCnt;
		_Timer( (float)t, 2, nDiv, nCnt );
		float fRate = (float)CPUCLOCK / nDiv / nCnt;
		int k, l;
		_BestRational( fFreq / fRate, MaxSamples, k, l );
		if ( k < 1 || l < k * nMinPeriod || l > MaxSamples )
			continue;
		float fReal = fRate * k / l;
		float fErr = _Err( fReal, fFreq );
		if ( fErr < fBestErr )
		{
			fBestErr = fErr;
			plan.nDiv = nDiv;
			plan.nCnt = nCnt;
			plan.nSamples = l;
			plan.nCycles = k;
			plan.fReal = fReal;
		}
	}
	return fBestErr < 1e30f;
}

/*static*/ float CCoreGenerator::GetFrequency()
{
	if ( Settings.Gen.Wave == CSettings::Generator::_Dc )
		return 0;
	return m_fReal;
}

/*static*/ float CCoreGenerator::AutoStep(float fFreq)
{
	// three significant digits
	float fStep = 0.1f;
	while ( fStep * 1000.0f <= fFreq )
		fStep *= 10.0f;
	return fStep;
}

/*static*/ void CCoreGenerator::StepFrequency(int nDir, float fStep)
{
	float fFreq = GetFrequency();
	if ( fStep <= 0 )
		fStep = AutoStep( fFreq );

	// snap to the step grid first
	float fBase = (int)(fFreq / fStep + 0.5f) * fStep;
	TPlan plan;
	float fTarget = fBase + nDir * fStep;
	_Plan( fTarget, plan );

	if ( nDir > 0 ? plan.fReal <= fFreq : plan.fReal >= fFreq )
	{
		// timer resolution coarser than the step: find the nearest
		// achievable frequency in that direction (exponential + bisection)
		float fLo = fTarget, fHi = 0;
		float fMul = 2;
		for ( int i = 0; i < 40; i++ )
		{
			float fTry = fBase + nDir * fStep * fMul;
			if ( fTry < FREQ_MIN || fTry > _MaxFrequency() * 1.01f )
				break;
			_Plan( fTry, plan );
			if ( nDir > 0 ? plan.fReal > fFreq : plan.fReal < fFreq )
			{
				fHi = fTry;
				break;
			}
			fLo = fTry;
			fMul *= 2;
		}
		if ( fHi == 0 )
			return; // range limit
		for ( int i = 0; i < 24; i++ )
		{
			float fMid = (fLo + fHi) / 2;
			_Plan( fMid, plan );
			if ( nDir > 0 ? plan.fReal > fFreq : plan.fReal < fFreq )
				fHi = fMid;
			else
				fLo = fMid;
		}
		// keep the target itself, planning is deterministic for it
		fTarget = fHi;
	}

	Settings.Gen.nFrequency = fTarget;
	Update();
}

/*static*/ void CCoreGenerator::_ApplyAnalog(const TPlan& plan)
{
	Settings.Gen.nPsc = plan.nDiv - 1;
	Settings.Gen.nArr = plan.nCnt - 1;
	Settings.Gen.nSamples = plan.nSamples;
	m_fReal = plan.fReal;
	// lets BIOS select analog mode and remember its state
	BIOS::DAC::SetFrequency( max(1, CPUCLOCK / (plan.nDiv * plan.nCnt)) );
#ifdef DS203
	// then set the exact timing directly: TIM7 paces the DMA to the DAC
	REG32(0x4000142C) = plan.nCnt - 1;      // TIM7_ARR
	REG32(0x40001428) = plan.nDiv - 1;      // TIM7_PSC
	REG32(0x40001414) = 1;                  // TIM7_EGR = UG, load prescaler
#endif
}

/*static*/ void CCoreGenerator::_ApplySquare(const TPlan& plan)
{
	Settings.Gen.nPsc = plan.nDiv - 1;
	Settings.Gen.nArr = plan.nCnt - 1;
	Settings.Gen.nSamples = 1;
	m_fReal = plan.fReal;
	BIOS::DAC::SetFrequency( max(1, (int)(plan.fReal + 0.5f)) );
	BIOS::DAC::SetDuty( Settings.Gen.nDuty );
	SetDuty( Settings.Gen.nDuty );
#ifdef DS203
	// TIM4 channel 1 drives PB6, set the exact timing directly
	REG32(0x4000082C) = plan.nCnt - 1;      // TIM4_ARR
	REG32(0x40000828) = plan.nDiv - 1;      // TIM4_PSC
	REG32(0x40000834) = plan.nCnt * (100 - Settings.Gen.nDuty) / 100; // TIM4_CCR1, same polarity as BIOS
	REG32(0x40000814) = 1;                  // TIM4_EGR = UG
#endif
}

/*static*/ int CCoreGenerator::_ModMode()
{
	int nWave = Settings.Gen.Wave;
	int nMode = Settings.Gen.nModMode;
	if ( nWave == CSettings::Generator::_Dc )
		return ModOff;
	// square and hardware waves have no sample buffer to shape
	if ( nMode == ModAm && ( nWave == CSettings::Generator::_Square ||
		 nWave == CSettings::Generator::_Noise || nWave == CSettings::Generator::_TriHw ) )
		return ModOff;
	return nMode;
}

void CCoreGenerator::Update()
{
	int nWaveIndex = Settings.Gen.Wave;
	int nMod = _ModMode();
	float fCarrier = Settings.Gen.nFrequency;
	TPlan plan;

	m_nSwMode = ModOff;
	m_fSwPhase = 0;
	m_nSwLast = BIOS::SYS::GetTick();
	if ( nMod == ModSweep )
	{
		// buffer and limits are planned for the highest frequency of the sweep
		fCarrier = max(Settings.Gen.fSweepFrom, Settings.Gen.fSweepTo);
	}

#ifdef DS203
	bool bHardware = nWaveIndex == CSettings::Generator::_Noise ||
		nWaveIndex == CSettings::Generator::_TriHw;
	if ( !bHardware )
		HwWave::Disable();
#endif

	switch (nWaveIndex)
	{
		case CSettings::Generator::_Dc:
		{
			// 0..0x10000 (0..100%) mapped to 0..0xfff. Value 0x1000 would not
			// fit 12 bit DAC register and would be output as zero
			_ConfigureDc( (Settings.Gen.nOffset * 0xfff + 0x8000) >> 16 );
			// selects analog output and starts DAC timer
			plan.nSamples = 1;
			plan.nCycles = 1;
			_Timer( CPUCLOCK / 1000.0f, 2, plan.nDiv, plan.nCnt );
			plan.fReal = 0;
			_ApplyAnalog( plan );
			return;
		}

		case CSettings::Generator::_Square:
			BIOS::DAC::SetMode(BIOS::DAC::EMode::Square, nullptr, 0);
#ifdef DS203
			HwWave::ReleaseAnalog();
#endif
			_Plan( fCarrier, plan );
			_ApplySquare( plan );
			_StartSoftware( nMod, plan );
			return;

		case CSettings::Generator::_Volatile:
			if ( m_nVolatileLen == 0 )
				CopyToVolatile( CSettings::Generator::_SinHq );
			break;
	}

#ifdef DS203
	if ( bHardware )
	{
		// amplitude is limited to 2^n-1 counts by the DAC
		int nMamp = 0;
		int nAmpl = min( 0xfff, (Settings.Gen.nScale * 0xfff) >> 16 );
		while ( nMamp < 11 && (4 << nMamp) - 1 <= nAmpl )
			nMamp++;
		int nPeak = (2 << nMamp) - 1;
		int nBase = ((Settings.Gen.nOffset * 0xfff) >> 16) - nPeak/2;
		nBase = max(0, min(nBase, 0xfff - nPeak));
		// triangle counts up and down once per period, noise updates every trigger
		m_nFixedSamples = nWaveIndex == CSettings::Generator::_TriHw ? 2*nPeak : 1;

		_Generate( nWaveIndex, 128 );   // preview only
		_ConfigureWave( m_pRamWave, m_nRamLen );
		_Plan( fCarrier, plan );
		_ApplyAnalog( plan );
		HwWave::Enable( nWaveIndex == CSettings::Generator::_Noise ? 1 : 2, nMamp, nBase );
		_StartSoftware( nMod, plan );
		return;
	}
#else
	if ( nWaveIndex == CSettings::Generator::_Noise || nWaveIndex == CSettings::Generator::_TriHw )
		m_nFixedSamples = PeriodSamples;
#endif

	if ( nMod == ModAm || nMod == ModFm )
	{
		// Exact modulation: one modulation period in the buffer with an integer
		// number of carrier periods, at least 8 samples each
		float fRate = Settings.Gen.fModRate;
		int nCycles = (int)(fCarrier / fRate + 0.5f);
		if ( fRate > 0 && nCycles >= 1 && nCycles * 8 <= MaxSamples )
		{
			int nMin, nMax;
			_SampleRange( nWaveIndex, nMin, nMax );
			nMax = max(nMin, min(nMax, MaxSamples / nCycles));
			if ( nCycles * nMin <= MaxSamples )
			{
				_PlanCounts( fCarrier / nCycles, nMin, nMax, nCycles, plan );
				plan.nCycles = nCycles;
				_Generate( nWaveIndex, plan.nSamples, nCycles, nMod, Settings.Gen.fModDepth * 0.01f );
				_ConfigureWave( m_pRamWave, m_nRamLen );
				_ApplyAnalog( plan );
				m_fReal = plan.fReal * nCycles;
				return;
			}
		}
		// modulation too slow for the buffer, done by Tick()
	}

	// software FM reaches carrier * (1 + deviation), keep the DAC rate valid
	if ( nMod == ModFm )
		fCarrier *= 1.0f + Settings.Gen.fModDepth * 0.01f;
	_Plan( fCarrier, plan );
	_Generate( nWaveIndex, plan.nSamples, plan.nCycles );
	_ConfigureWave( m_pRamWave, m_nRamLen );
	_ApplyAnalog( plan );
	_StartSoftware( nMod, plan );
}

// Slow modulations and sweep: Tick() changes the timer or the buffer
/*static*/ void CCoreGenerator::_StartSoftware(int nMod, const TPlan& plan)
{
	if ( nMod == ModOff )
		return;
	m_nSwMode = nMod;
	m_nSwSamples = plan.nSamples / max(1, plan.nCycles);
	m_fSwBase = Settings.Gen.nFrequency;
	if ( nMod == ModSweep )
		m_fSwBase = Settings.Gen.fSweepFrom;
}

// 2^x for the sweep, x in -32..32
static float _Exp2(float x)
{
	int n = (int)x;
	if ( x < n )
		n--;
	float f = x - n;   // 0..1, polynomial fit of 2^f, error < 1e-4
	float r = 1.0f + f * (0.6951786f + f * (0.2261677f + f * 0.0780463f));
	while ( n > 0 ) { r *= 2.0f; n--; }
	while ( n < 0 ) { r *= 0.5f; n++; }
	return r;
}

static float _Log2(float x)
{
	int n = 0;
	while ( x >= 2.0f ) { x *= 0.5f; n++; }
	while ( x < 1.0f ) { x *= 2.0f; n--; }
	// x in 1..2, log2 by series of ln((1+y)/(1-y))
	float y = (x - 1.0f) / (x + 1.0f), y2 = y * y;
	return n + 2.8853901f * y * (1.0f + y2 * (0.3333333f + y2 * (0.2f + y2 * 0.1428571f)));
}

// Changes the frequency without touching the buffer. Prescaler and reload
// are preloaded by the timers, the change happens at the end of a period
/*static*/ void CCoreGenerator::_Retime(float fFreq)
{
	if ( fFreq < FREQ_MIN )
		fFreq = FREQ_MIN;
	bool bSquare = Settings.Gen.Wave == CSettings::Generator::_Square;
	int nDiv, nCnt;
	if ( bSquare )
	{
		if ( fFreq > SQ_MAX_HZ )
			fFreq = SQ_MAX_HZ;
#ifdef DS203
		_Timer( CPUCLOCK / fFreq, 3, nDiv, nCnt );
		REG32(0x4000082C) = nCnt - 1;
		REG32(0x40000828) = nDiv - 1;
		REG32(0x40000834) = nCnt * (100 - Settings.Gen.nDuty) / 100;
#else
		BIOS::DAC::SetFrequency( (int)(fFreq + 0.5f) );
#endif
		return;
	}
	float fTicks = CPUCLOCK / (fFreq * m_nSwSamples);
	if ( fTicks < MIN_TICKS )
		fTicks = MIN_TICKS;
#ifdef DS203
	_Timer( fTicks, 2, nDiv, nCnt );
	REG32(0x4000142C) = nCnt - 1;
	REG32(0x40001428) = nDiv - 1;
#else
	BIOS::DAC::SetFrequency( (int)(CPUCLOCK / fTicks + 0.5f) );
#endif
}

/*static*/ void CCoreGenerator::Tick()
{
	if ( m_nSwMode == ModOff )
		return;
	uint32_t nNow = BIOS::SYS::GetTick();
	int nElapsed = nNow - m_nSwLast;
	// AM rewrites the whole buffer, do it less often
	if ( nElapsed < (m_nSwMode == ModAm ? 20 : 2) )
		return;
	m_nSwLast = nNow;
	float fDt = nElapsed * 0.001f;

	if ( m_nSwMode == ModSweep )
	{
		float fTime = Settings.Gen.fSweepTime;
		if ( fTime < 0.01f )
			fTime = 0.01f;
		m_fSwPhase += fDt / fTime;
		if ( m_fSwPhase >= 1.0f )
			m_fSwPhase -= (int)m_fSwPhase;
		float fFrom = Settings.Gen.fSweepFrom, fTo = Settings.Gen.fSweepTo;
		float fFreq;
		if ( Settings.Gen.nSweepLog && fFrom > 0 && fTo > 0 )
			fFreq = fFrom * _Exp2( _Log2( fTo / fFrom ) * m_fSwPhase );
		else
			fFreq = fFrom + (fTo - fFrom) * m_fSwPhase;
		_Retime( fFreq );
		m_fReal = fFreq;
		return;
	}

	m_fSwPhase += fDt * Settings.Gen.fModRate;
	if ( m_fSwPhase >= 1.0f )
		m_fSwPhase -= (int)m_fSwPhase;
	int nSin = _Sin( (uint32_t)(m_fSwPhase * 65536.0f) );   // +-2047
	float fDepth = Settings.Gen.fModDepth * 0.01f;

	if ( m_nSwMode == ModFm )
	{
		_Retime( m_fSwBase * (1.0f + fDepth * nSin * (1.0f / 2047.0f)) );
		return;
	}
	if ( m_nSwMode == ModAm )
	{
		// envelope (1 + m sin) / (1 + m)
		float fEnv = (1.0f + fDepth * nSin * (1.0f / 2047.0f)) / (1.0f + fDepth);
		_Generate( Settings.Gen.Wave, m_nRamLen, 1, ModOff, 0, (int)(Settings.Gen.nScale * fEnv) );
	}
}

int CCoreGenerator::GetDuty()
{
	return Settings.Gen.nDuty;
}

void CCoreGenerator::SetDuty(int nPercent)
{
	Settings.Gen.nCcr = (Settings.Gen.nArr) * nPercent / 100;
}

void CCoreGenerator::_ConfigureDc( int dc )
{
  static ui16 sData;
  sData = min(dc, BIOS::DAC::SampleMaxValue);
  _ConfigureWave( &sData, 1 );
}

void CCoreGenerator::_ConfigureWave( uint16_t* p, int n)
{
  BIOS::DAC::SetMode(BIOS::DAC::EMode::Buffer, p, n);
#ifdef DS203
  HwWave::EnsureAnalog();
#endif
}
