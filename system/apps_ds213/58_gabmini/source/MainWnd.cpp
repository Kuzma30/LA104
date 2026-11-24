#include "MainWnd.h"
#include <Oscilloscope/Core/Roll.h>
#include "Oscilloscope/Core/AutoSet.h"

CMainWnd* CMainWnd::m_pInstance = nullptr;
CSettings m_Settings; // TODO:!

long m_lLastAcquired = 0;
long m_lLastRequested = 0;
long m_lHoldUntil = 0; // non zero while waiting for trigger hold-off to expire

void CMainWnd::Create()
{	
	m_pInstance = this;
	CWnd::Create("CMainWnd", WsVisible | WsListener, CRect(0, 0, BIOS::LCD::Width, BIOS::LCD::Height), NULL );
	mWndToolBar.Create(this);
	m_wndGraph.Create( this, WsHidden | WsNoActivate );

	// hidden as the other tabs: the tab opened last time is shown below
	// ('g','i'); a visible Input stayed under it (its Trig item below Tools)
	m_wndMenuInput.Create( this, WsHidden );
	m_wndMenuCursor.Create( this, WsHidden );
	m_wndMenuDisplay.Create( this, WsHidden );
	m_wndMenuDecode.Create( this, WsHidden );
	m_wndMenuMeas.Create( this, WsHidden );
	m_wndMenuTools.Create( this, WsHidden );
	m_wndMenuSystem.Create( this, WsHidden );

	// signal generator, the output keeps running in all tabs
	m_wndSignalGraph.Create( this, WsHidden | WsNoActivate );
	m_wndMenuGenerator.Create( this, WsHidden );
	m_wndMenuGeneratorMod.Create( this, WsHidden );
	m_wndMenuGeneratorEdit.Create( this, WsHidden );
	m_wndPidGraph.Create( this, WsHidden | WsNoActivate );
	m_wndMenuPid.Create( this, WsHidden );

	m_wndLReferences.Create( this, WsHidden );
	m_wndTReferences.Create( this, WsHidden );
	
	m_wndZoomBar.Create(this, WsHidden, &m_wndGraph);

	// tab opened last time (saved settings), Input otherwise
	int nMenuItem = 1;
	const CWndToolBar::CBarItem* pItems = CWndToolBar::GetMenuItems();
	for ( int i = 0; pItems[i].m_eType != CWndToolBar::CBarItem::IEnd; i++ )
		if ( i == Settings.Runtime.m_nMenuItem && pItems[i].m_eType == CWndToolBar::CBarItem::ISub )
			nMenuItem = i;
	SendMessage( &mWndToolBar, ToWord('g', 'i'), nMenuItem);
	if ( nMenuItem == 1 )
		m_wndMenuInput.SetFocus();


#if defined(DS203)
	// Start in scan mode
	CCoreOscilloscope::ConfigureTrigger();
	CCoreOscilloscope::ConfigureAdc();
	BIOS::ADC::Restart(0);
#elif defined(DS213)
        BIOS::ADC::Enable( true );
	CCoreOscilloscope::ConfigureAdc();
	CCoreOscilloscope::ConfigureTrigger();
#else
#error Unknown target
#endif
}

/*virtual*/ void CMainWnd::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
	if ( pSender == &mWndToolBar )
	{
		if ( code == ToWord('L', 'D') && data )	// Layout disable
		{
			CWnd* pLayout = (CWnd*)data;
			SendMessage( pLayout, code, 0 );
			pLayout->ShowWindow( false );
		}
		if ( code == ToWord('L', 'E') && data )	// Layout enable
		{
			CWnd* pLayout = (CWnd*)data;
			SendMessage( pLayout, code, 0 );
			pLayout->ShowWindow( true );
		}
		if ( code == ToWord('L', 'E') && !data )
		{
			// Entered root menu, exit app
			mRunning = false;
		}
		if ( code == ToWord('L', 'R') )	// Layout reset
		{
			Invalidate();
		}
		return;
	}
}

/*virtual*/ void CMainWnd::OnPaint()
{
	//BIOS::LCD::Clear(RGB565(000000));
	GUI::Background(CRect(0, 0, BIOS::LCD::Width, BIOS::LCD::Height), RGB565(101010), RGB565(404040));
}

bool CMainWnd::IsRunning()
{
	return mRunning;
}

void CMainWnd::ToggleRunStop()
{
	if ( Settings.Trig.State == CSettings::Trigger::_Stop )
	{
		// continue, Single waits for the next trigger
		Settings.Trig.State = Settings.Trig.Sync == CSettings::Trigger::_Single ?
			CSettings::Trigger::_Wait : CSettings::Trigger::_Run;
		m_lHoldUntil = 0;
		BIOS::ADC::Restart(0);
		m_lLastRequested = m_lLastAcquired = BIOS::SYS::GetTick();
	} else
		Settings.Trig.State = CSettings::Trigger::_Stop;

	if ( m_wndMenuInput.m_itmTrig.IsVisible() )
		m_wndMenuInput.m_itmTrig.Invalidate();
	if ( m_wndGraph.IsVisible() )
		m_wndGraph.Invalidate();
}

// F3: automatic V/div, position, time base and trigger (Oscilloscope/Core/AutoSet)
void CMainWnd::AutoSet()
{
	CRect rc( m_wndGraph.m_rcClient.left + 4, m_wndGraph.m_rcClient.top + 4,
		m_wndGraph.m_rcClient.left + 4 + 110, m_wndGraph.m_rcClient.top + 4 + 16 );
	BIOS::LCD::Bar( rc, RGB565(000000) );
	UI::Print( rc.left + 4, rc.top, RGB565(ffff00), RGBTRANS, "AUTO..." );

	AutoSet::EResult result = AutoSet::Run();

	m_lHoldUntil = 0;
	m_lLastAcquired = m_lLastRequested = BIOS::SYS::GetTick();
	m_wndGraph.ClearAverage();
	m_wndGraph.ClearPersist();
	// ranges, time base and trigger changed: redraw everything
	WindowMessage( CWnd::WmPaint );

	if ( result == AutoSet::NoSignal )
	{
		BIOS::LCD::Bar( rc, RGB565(000000) );
		UI::Print( rc.left + 4, rc.top, RGB565(ff4040), RGBTRANS, "AUTO: no signal" );
	}
}

/*virtual*/ void CMainWnd::WindowMessage(int nMsg, int nParam)
{
	// acquisition is paused while the list of decoded data is shown, so the
	// list stays consistent with the sample buffer
	// persistence keeps its history after one screen of samples: changing
	// Persist or the display mode (Disp menu) may change the length of the
	// acquisition, the FPGA depth is set again
	if ( nMsg == WmTick && CCoreOscilloscope::BufferLength() != Sampler::Length() )
	{
		CCoreOscilloscope::ConfigureTrigger();
		m_wndZoomBar.Invalidate();
		m_wndTReferences.Invalidate();
		if ( m_wndMenuInput.m_itmBuffer.IsVisible() )
			m_wndMenuInput.m_itmBuffer.Invalidate();
	}
	// battery icon of the toolbar (reads the voltage every 5 s)
	if ( nMsg == WmTick && mWndToolBar.IsVisible() )
		mWndToolBar.UpdateBattery();
	// the battery voltage of the System tab, every 2 s
	static long s_lSysPaint = 0;
	if ( nMsg == WmTick && m_wndMenuSystem.IsVisible() && BIOS::SYS::GetTick() - s_lSysPaint > 2000 )
	{
		s_lSysPaint = BIOS::SYS::GetTick();
		m_wndMenuSystem.m_itmSystem.Invalidate();
	}
	// trigger state label (Norm / Single), twice a second
	static long s_lTrigPaint = 0;
	if ( nMsg == WmTick && m_wndGraph.IsVisible() && BIOS::SYS::GetTick() - s_lTrigPaint > 500 )
	{
		s_lTrigPaint = BIOS::SYS::GetTick();
		m_wndGraph.PaintTrigState();
	}


	if ( nMsg == WmTick && !DecodeUi::Hold() && CCoreOscilloscope::Rolling() )
	{
		// roll: samples are shown as they come, the screen is redrawn at
		// most 10 times per second
		static bool bDirty = false;
		static long lLastDraw = 0;
		long lNow = BIOS::SYS::GetTick();
		if ( Settings.Trig.State != CSettings::Trigger::_Stop && Roll::Tick() )
			bDirty = true;
		if ( bDirty && lNow - lLastDraw >= 100 )
		{
			bDirty = false;
			lLastDraw = lNow;
			m_lLastAcquired = lNow;
			CWnd::WindowMessage( CWnd::WmBroadcast, ToWord('d', 'g') );
		}
	} else
	if ( nMsg == WmTick && !DecodeUi::Hold() )
	{
		BIOS::ADC::EState state = BIOS::ADC::GetState();

		bool ready = state != BIOS::ADC::EState::Offline && state != BIOS::ADC::EState::Busy;
#ifdef DS203
		bool triggered = state == BIOS::ADC::EState::Full;
#else
		bool triggered = state == BIOS::ADC::EState::Triggered;
#endif

		long lNow = BIOS::SYS::GetTick();
		bool bStopped = Settings.Trig.State == CSettings::Trigger::_Stop;

		if ( m_lHoldUntil && lNow - m_lHoldUntil >= 0 )
		{
			m_lHoldUntil = 0;
			BIOS::ADC::Restart(0);
			m_lLastRequested = lNow;
		}

		// None: no trigger condition, the FPGA fills the buffer right after
		// the restart: every full buffer is shown (free running)
		if ( triggered && !bStopped && !m_lHoldUntil )
		{
			m_lLastAcquired = lNow;
			Sampler::Copy();
			// the edge at the trigger position (the FPGA mark moves at 72 MS/s)
			if ( Settings.Trig.Type == CSettings::Trigger::_EdgeLH || Settings.Trig.Type == CSettings::Trigger::_EdgeHL )
				Sampler::AlignTrigger( (int)Settings.Trig.Source, Settings.Trig.Type == CSettings::Trigger::_EdgeLH, Settings.Trig.nLevel );
			if ( Settings.Trig.Sync == CSettings::Trigger::_Single )
			{
				// keep the captured waveform, any trigger change re-arms
				Settings.Trig.State = CSettings::Trigger::_Stop;
				if ( m_wndMenuInput.m_itmTrig.IsVisible() )
					m_wndMenuInput.m_itmTrig.Invalidate();
			} else
			if ( Settings.Trig.nHoldOff > 0 )
			{
				m_lHoldUntil = lNow + Settings.Trig.nHoldOff;
				if ( !m_lHoldUntil )
					m_lHoldUntil = 1;
			} else
			{
				BIOS::ADC::Restart(0);
				m_lLastRequested = lNow;
			}

			CWnd::WindowMessage( CWnd::WmBroadcast, ToWord('d', 'g') );
		}

	if ( ready && Settings.Trig.Sync == CSettings::Trigger::_Auto && !m_lHoldUntil && !bStopped )
	{
                // we dont need to wait for the screen to be fully reloaded - on slow sampling modes
		int screenDuration = Settings.Runtime.m_nScreenDuration*3/2+20;
		// Fall back to untriggered display only when the trigger had a fair chance.
		// Triggered transfer is reported when the whole buffer is filled, that takes
		// NumSamples/300 screens. With the fixed 500 ms timeout, every timebase slower
		// than ~1 ms/div restarted the acquisition before it could ever trigger and
		// Auto behaved exactly like Scan. Very slow timebases still fall back after
		// 3 s so that the screen keeps updating (roll-like behaviour).
		int nAutoTimeout = Settings.Runtime.m_nScreenDuration * (Sampler::Length() / 300) + 100;
		nAutoTimeout = max(500, min(nAutoTimeout, 3000));
		if ( lNow - m_lLastAcquired > nAutoTimeout && lNow - m_lLastRequested > screenDuration)
		{
// If we enter this point, we will lost buffer-full synchronization - because we immediatelly 
// request new transfer after the wptr reaches end of screen. So the "FULL" flag will be never
// set
// only if we will wait for full buffer, but that would reduce redraw time
#ifdef DS203
// + invalid first + scroll offset
			bool bScreenReady = BIOS::ADC::GetPointer() > (300 + Settings.Time.InvalidFirst);
//			bool canProcess = true;
#else
			bool bScreenReady = true;
//			int nScreenDuration = Settings.Runtime.m_nScreenDuration*2;
//			bool bScreenReady = BIOS::SYS::GetTick() - m_lLastRequested > nScreenDuration;
//                        bool canProcess = bScreenReady;
#endif
//			if (bScreenReady)
			{
         			// the acquisition continues when it is not restarted below,
         			// the next copy reads the same samples again
         			Sampler::Copy( bScreenReady );

         			// redraw the screen even when the sampler is not full
         			WindowMessage( CWnd::WmBroadcast, ToWord('d', 'g') );
         		
         			// force restart if the write pointer is behind current window
         			// restart only when the write ptr is behind screen - slow signals
         			if ( bScreenReady )
         			{
         				BIOS::ADC::Restart(0);
         				m_lLastRequested = BIOS::SYS::GetTick();
         			} 
			}
		}
	}

/*
		if ( (Settings.Trig.Sync != CSettings::Trigger::_None) && BIOS::ADC::Enabled() && BIOS::ADC::Ready() )
		{
			// ADC::Ready means that the write pointer is at the end of buffer, we can restart sampler
			BIOS::ADC::Copy( BIOS::ADC::GetCount() );
			BIOS::ADC::Restart();
			Resample();
		
			// trig stuff
			m_lLastAcquired = BIOS::SYS::GetTick();
			if ( BIOS::ADC::Enabled() && Settings.Trig.Sync == CSettings::Trigger::_Single )
			{
				BIOS::ADC::Enable( false );
				Settings.Trig.State = CSettings::Trigger::_Stop;
				if ( m_wndMenuInput.m_itmTrig.IsVisible() )
					m_wndMenuInput.m_itmTrig.Invalidate();
					}
		
			// broadcast message for windows that process waveform data
			WindowMessage( CWnd::WmBroadcast, ToWord('d', 'g') );
		}*/
	}
	CWnd::WindowMessage(nMsg, nParam);
}
