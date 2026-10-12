#include "CorePid.h"
#include <MainWnd.h>

#ifndef WAVEOUT_FULLSCALE_MV
#define WAVEOUT_FULLSCALE_MV 2250
#endif

namespace CorePid
{
	static CPid s_pid;
	static int s_nMode = Off;
	static uint32_t s_nLast = 0;
	static uint32_t s_nSteps = 0;
	static float s_fMeasured = 0;
	static float s_fPlant = 0;      // simulated plant output

	// time constant of the simulated plant (first order, 1/(s*tau+1))
	static const float SimTau = 0.5f;

	float OutputMax()
	{
		return WAVEOUT_FULLSCALE_MV / 1000.0f;
	}

	const CPid& Pid() { return s_pid; }
	float Measured() { return s_fMeasured; }
	uint32_t Steps() { return s_nSteps; }

	bool Stalled()
	{
		return Settings.Pid.Mode == Run && Settings.Trig.State == CSettings::Trigger::_Stop;
	}

	// average of the newest samples of CH1 in volts
	static float _MeasureCh1()
	{
		int nValid = Sampler::GetValid();
		int nFirst = max(Settings.Time.InvalidFirst, nValid - 256);
		if ( nValid - nFirst < 8 )
			return s_fMeasured;
		int nSum = 0;
		for ( int i = nFirst; i < nValid; i++ )
			nSum += Sampler::GetAt(i) & 0xff;
		CSettings::Calibrator::FastCalc fast;
		Settings.CH1Calib.Prepare( &Settings.CH1, fast );
		float fVolts = Settings.CH1Calib.Voltage( fast, (float)nSum / (nValid - nFirst) );
		return fVolts * CSettings::AnalogChannel::pfValueProbe[ Settings.CH1.Probe ];
	}

	void Restart()
	{
		int nMode = Settings.Pid.Mode;
		if ( nMode != Run && s_nMode == Run )
			CCoreGenerator::PidRelease();
		s_nMode = nMode;
		s_nLast = 0;
		s_fPlant = 0;
		s_fMeasured = nMode == Run ? _MeasureCh1() : 0;
		s_pid.Reset( 0 );
	}

	void Tick()
	{
		if ( Settings.Pid.Mode != s_nMode )
			Restart();
		if ( s_nMode == Off )
			return;

		uint32_t nNow = BIOS::SYS::GetTick();
		uint32_t nPeriod = (uint32_t)Settings.Pid.fPeriod;
		if ( s_nLast && nNow - s_nLast < nPeriod )
			return;
		float fDt = s_nLast ? (nNow - s_nLast) * 0.001f : nPeriod * 0.001f;
		s_nLast = nNow;

		if ( Stalled() )
		{
			// no new captures: hold the output instead of regulating on old data
			if ( MainWnd.m_wndPidGraph.IsVisible() )
				MainWnd.m_wndPidGraph.AddPoint();
			return;
		}

		s_fMeasured = s_nMode == Run ? _MeasureCh1() : s_fPlant;

		float fOut = s_pid.Step( Settings.Pid.fTarget, s_fMeasured, fDt,
			Settings.Pid.fKp, Settings.Pid.fKi, Settings.Pid.fKd, 0, OutputMax() );

		if ( s_nMode == Run )
		{
			int nDac = (int)(fOut / OutputMax() * BIOS::DAC::SampleMaxValue + 0.5f);
			CCoreGenerator::PidOutput( nDac );
		} else
		{
			float fAlpha = fDt / (SimTau + fDt);
			s_fPlant += (fOut - s_fPlant) * fAlpha;
		}
		s_nSteps++;

		if ( MainWnd.m_wndPidGraph.IsVisible() )
			MainWnd.m_wndPidGraph.AddPoint();
	}
}
