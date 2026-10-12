#pragma once
#include <stdint.h>

// PID regulator: CH1 is the measured value, Wave Out (DC level) the output.
// Ported from the PID module of gabonator/DS203 with these changes:
//  - dt is the real time between two steps (it was a constant that did not
//    match the timer period)
//  - anti-windup: the integral stops growing while the output is saturated
//    and it is kept in the output units (changing Ki does not make a jump)
//  - derivative of the measured value, not of the error, so changing the
//    set point does not kick the output
//  - the measured value is the average of the newest samples of the scope
//    buffer (the regulator sees the latest capture, not its middle)
class CPid
{
public:
	float m_fError{0};
	float m_fP{0}, m_fI{0}, m_fD{0};
	float m_fOutput{0};

	void Reset(float fOutput)
	{
		m_fI = fOutput;     // bumpless start from the current output
		m_bFirst = true;
		m_fError = m_fP = m_fD = 0;
		m_fOutput = fOutput;
	}

	float Step(float fTarget, float fMeasured, float fDt, float fKp, float fKi, float fKd,
		float fMin, float fMax)
	{
		m_fError = fTarget - fMeasured;
		m_fP = fKp * m_fError;
		m_fD = (m_bFirst || fDt <= 0) ? 0 : -fKd * (fMeasured - m_fPrevMeasured) / fDt;

		float fI = m_fI + fKi * m_fError * fDt;
		float fOut = m_fP + fI + m_fD;
		// do not integrate further into the saturation
		if ( (fOut > fMax && m_fError > 0) || (fOut < fMin && m_fError < 0) )
			fI = m_fI;
		m_fI = _Clamp(fI, fMin, fMax);

		m_fOutput = _Clamp(m_fP + m_fI + m_fD, fMin, fMax);
		m_fPrevMeasured = fMeasured;
		m_bFirst = false;
		return m_fOutput;
	}

private:
	bool m_bFirst{true};
	float m_fPrevMeasured{0};

	static float _Clamp(float f, float fMin, float fMax)
	{
		return f < fMin ? fMin : f > fMax ? fMax : f;
	}
};

namespace CorePid
{
	enum EMode { Off, Simulation, Run, ModeMax = Run };

	// call from the main loop, runs the regulator with the set period
	void Tick();
	// mode changed or regulator restarted
	void Restart();

	const CPid& Pid();
	float Measured();
	float OutputMax();          // Wave Out full scale, V
	bool Stalled();             // scope stopped, no new measurements
	uint32_t Steps();           // incremented with every regulator step
}
