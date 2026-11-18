#pragma once
#include <stdint.h>

// LA104: automatic measurements on the sample buffer (the whole valid part,
// from Time.InvalidFirst), for the tab Meas.
namespace MeasCore
{
	enum EType
	{
		Vpp, Vmax, Vmin, Vavg, Vrms,              // analog channels only
		Freq, Period, Duty, WidthH, WidthL,
		Rise, Fall,                               // 10..90 %, analog only
		Delay, Phase,                             // CH1 -> CH2 (or CH2 -> CH1)
		FreqHw,                                   // edge counters of the FPGA (2.66+)
		TypeCount
	};
	enum ESource { CH1, CH2, CH3, CH4, SourceCount };

	const char* Name( int nType );
	const char* SourceName( int nSource );
	bool Allowed( int nType, int nSource );     // e.g. no voltage on CH3

	// the value in V, Hz, s, %, deg; false when it can not be measured
	bool Measure( int nType, int nSource, float& fValue );

	// "1.23kHz", "-45.6mV", "12.5%", "90.1deg" (at most 9 characters)
	void Format( int nType, float fValue, char* pOut );
	void FormatSI( float fValue, const char* pUnit, char* pOut );

	// sampling rate of the current time base
	float SampleRate();

	// gate of the FPGA counters (FreqHw), call often from the main loop
	void Tick();
}
