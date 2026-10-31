#include "CoreOscilloscope.h"
#include <Framework.h>
#include <Settings.h>
#include "FpgaBus.h"
#include <Oscilloscope/Controls/GraphBase.h>

static uint32_t s_nConfigGeneration = 0;

/*static*/ uint32_t CCoreOscilloscope::ConfigGeneration()
{
	return s_nConfigGeneration;
}

/*static*/ bool CCoreOscilloscope::Rolling()
{
	return FpgaBus::Available() && Settings.Trig.Sync == CSettings::Trigger::_Scan &&
		Settings.Time.Resolution >= CSettings::TimeBase::_50ms;
}

/*static*/ int CCoreOscilloscope::BufferLength()
{
	if ( Rolling() )
		return Sampler::Samples;
	bool bHistory = Settings.Disp.Persist == CSettings::Display::_PerPhosphor ||
		( Settings.Disp.Persist == CSettings::Display::_PerYes && Settings.Disp.Axes == CSettings::Display::_TY );
	if ( Settings.Time.Buffer == CSettings::TimeBase::_BufScreen || bHistory )
		return Sampler::ScreenSamples;
	return Sampler::Samples;
}

/*static*/ void CCoreOscilloscope::ApplyBuffer()
{
	// one screen: the FPGA stops writing (Full) after the screen
	int nLength = BufferLength();
	Sampler::SetLength( nLength );
	FpgaBus::SetDepth( nLength >= Sampler::Samples ? 4095 : nLength );
	int nMaxShift = nLength - CWndGraph::MaxX;
	if ( Settings.Time.Shift > nMaxShift )
		Settings.Time.Shift = nMaxShift;
	if ( Settings.Time.Shift < Settings.Time.InvalidFirst )
		Settings.Time.Shift = Settings.Time.InvalidFirst;
}

/*static*/ void CCoreOscilloscope::ConfigureAdc()
{
	/*
	const static ui16 arrPsc[] = 
	{ 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 1-1, 10-1, 10-1, 10-1, 100-1, 100-1, 
	100-1, 1000-1, 1000-1, 1000-1, 1000-1, 1000-1 };
	const static ui16 arrArr[] = 
	{ 1-1, 1-1, 2-1, 3-1, 5-1, 12-1, 24-1, 48-1, 120-1, 240-1, 480-1, 120-1, 240-1, 480-1,
	120-1, 240-1, 480-1, 120-1, 240-1, 480-1, 1200-1, 1000-1};
	const static int arrLen[] = 
	{ 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 2048, 2048, 2048, 2048, 
	1024, 1024, 1024, 512, 512, 512 };
	*/
//	_ASSERT( COUNT(arrPsc) == COUNT(arrArr) && COUNT(arrPsc) == CSettings::TimeBase::_ResolutionMax+1 );

	ui8 nACouple = Settings.CH1.Coupling == CSettings::AnalogChannel::_DC ? 0 : 1;
	ui8 nARange = (ui8)(NATIVEENUM)Settings.CH1.Resolution;
	ui16 nAOffset = Settings.CH1.u16Position;
	ui8 nBCouple = Settings.CH2.Coupling == CSettings::AnalogChannel::_DC ? 0 : 1;
	ui8 nBRange = (ui8)(NATIVEENUM)Settings.CH2.Resolution;
	ui16 nBPosition = Settings.CH2.u16Position;

	float fTimePerDiv = CSettings::TimeBase::pfValueResolution[ (NATIVEENUM)Settings.Time.Resolution ];
	//ui16 nTimePsc = arrPsc[ (NATIVEENUM)Settings.Time.Resolution ];
	//ui16 nTimeArr = arrArr[ (NATIVEENUM)Settings.Time.Resolution ];
	
#ifdef DS203
	BIOS::ADC::Enable(true);
#endif
	BIOS::ADC::ConfigureInput( BIOS::ADC::EInput::CH1, (BIOS::ADC::ECouple)nACouple, (BIOS::ADC::EResolution)nARange, nAOffset*4);
	BIOS::ADC::ConfigureInput( BIOS::ADC::EInput::CH2, (BIOS::ADC::ECouple)nBCouple, (BIOS::ADC::EResolution)nBRange, nBPosition*4);
	BIOS::ADC::ConfigureTimebase(fTimePerDiv);
#ifdef DS203
	if ( Settings.Time.Resolution == CSettings::TimeBase::_1s )
	{
		// the timebase table of the OS has ARR 999 for 1 s/div (72 Hz), the
		// right one is 2399 (30 Hz, 30 samples per division) as in SYS
		*(volatile uint32_t*)0x40012C28 = 1000-1;  // TIM1_PSC
		*(volatile uint32_t*)0x40012C2C = 2400-1;  // TIM1_ARR
		*(volatile uint32_t*)0x40012C34 = 1200;    // TIM1_CCR1
	}
#endif
//	BIOS::ADC::ConfigureBuffer( arrLen[ (NATIVEENUM)Settings.Time.Resolution ] );
	ApplyBuffer();
	BIOS::ADC::Restart(0);

	UpdateConstants();
	s_nConfigGeneration++;
}

/*static*/ void CCoreOscilloscope::ConfigureTrigger()
{
	if ( Settings.Trig.Sync == CSettings::Trigger::_None || Settings.Trig.Sync == CSettings::Trigger::_Scan )
	{
		BIOS::ADC::ConfigureTrigger(0, 0, BIOS::ADC::ETriggerType::None, BIOS::ADC::EInput::CH1 );
	} else
	{
		BIOS::ADC::ConfigureTrigger(Settings.Trig.nTime, Settings.Trig.nLevel*4, 
			(BIOS::ADC::ETriggerType)Settings.Trig.Type, (BIOS::ADC::EInput)Settings.Trig.Source);
	}
	// writing the trigger mode above set the depth back to the whole buffer
	ApplyBuffer();

	// FPGA uses the new trigger condition only for a new acquisition. Without the
	// restart (Norm/Single waiting for the old condition) the change appeared only
	// after some other setting restarted the ADC
	BIOS::ADC::Restart(0);
	s_nConfigGeneration++;
}

/*static*/ void CCoreOscilloscope::UpdateConstants()
{
	Settings.Runtime.m_fTimeRes = CSettings::TimeBase::pfValueResolution[ Settings.Time.Resolution ];
	Settings.Runtime.m_nScreenDuration = (int)(Settings.Runtime.m_fTimeRes*1000*10);

	Settings.Runtime.m_fCH1Res  = 
		CSettings::AnalogChannel::pfValueResolution[ Settings.CH1.Resolution ] *
		CSettings::AnalogChannel::pfValueProbe[ Settings.CH1.Probe ];
	Settings.Runtime.m_fCH2Res  = 
		CSettings::AnalogChannel::pfValueResolution[ Settings.CH2.Resolution ] *
		CSettings::AnalogChannel::pfValueProbe[ Settings.CH2.Probe ];
}
