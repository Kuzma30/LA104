#ifndef __MENUBUFFER_H__
#define __MENUBUFFER_H__

#include <Oscilloscope/Core/CoreOscilloscope.h>

// Buffer length: whole sample buffer (Window moves in several screens) or one
// screen (much faster refresh). Persistence in T-Y and phosphor need the
// memory after one screen for their history, they use one screen as well.
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
		bool bForced = bScreen && Settings.Time.Buffer == CSettings::TimeBase::_BufFull;
		m_pszId = !bScreen ? "Buf:Full" : bForced ? "Buf:Scr*" : "Buf:Scr";
		CWndMenuItem::OnPaint();
	}

	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right || nKey == BIOS::KEY::Enter )
		{
			Settings.Time.Buffer = Settings.Time.Buffer == CSettings::TimeBase::_BufFull ?
				CSettings::TimeBase::_BufScreen : CSettings::TimeBase::_BufFull;
			Invalidate();
			SendMessage(m_pParent, ToWord('b', 'u'), 0);
			return;
		}
		CWnd::OnKey( nKey );
	}
};

#endif
