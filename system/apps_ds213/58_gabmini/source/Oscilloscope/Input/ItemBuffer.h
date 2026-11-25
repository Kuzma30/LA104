#ifndef __MENUBUFFER_H__
#define __MENUBUFFER_H__

#include <Oscilloscope/Core/CoreOscilloscope.h>

// Buffer length: whole sample buffer (Window moves in several screens) or one
// screen (much faster refresh). Persistence in T-Y and phosphor need the
// memory after one screen for their history, they use one screen as well.
// Peak: peak detection of FPGA 2.68 (2048 samples, each the band between
// the minimum and the maximum of its period); "Buf:Pk*" where it does not
// work (time base faster than 5 us/div, roll mode, older FPGA): Full then.
class CItemBuffer : public CWndMenuItem
{
public:
	virtual void Create(CWnd *pParent) 
	{
		CWndMenuItem::Create( "Buf:Full", RGB565(ffffff), 1, pParent);
	}

	virtual void OnPaint() override
	{
		bool bScreen = CCoreOscilloscope::BufferLength() < Sampler::Samples;
		bool bForced = bScreen && Settings.Time.Buffer != CSettings::TimeBase::_BufScreen;
		if ( Settings.Time.Buffer == CSettings::TimeBase::_BufPeak )
			m_pszId = CCoreOscilloscope::PeakFactor() > 0 ? "Buf:Peak" : "Buf:Pk*";
		else
			m_pszId = !bScreen ? "Buf:Full" : bForced ? "Buf:Scr*" : "Buf:Scr";
		CWndMenuItem::OnPaint();
	}

	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right || nKey == BIOS::KEY::Enter )
		{
			// Full -> Screen -> Peak (Left: backwards)
			int n = (int)Settings.Time.Buffer + ( nKey == BIOS::KEY::Left ? 2 : 1 );
			Settings.Time.Buffer = (CSettings::TimeBase::EBuffer)( n % 3 );
			Invalidate();
			SendMessage(m_pParent, ToWord('b', 'u'), 0);
			return;
		}
		CWnd::OnKey( nKey );
	}
};

#endif
