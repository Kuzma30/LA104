#include <library.h>
#include "MainWnd.h"
#include "SettingsStore.h"
#include "Oscilloscope/Meas/MeasCore.h"
#include "Framework/UiText.h"
#include "../../common/sleep.h"

// a signal in the last acquisition: CH1 / CH2 (on) vary by more than half
// a division, or CH3 / CH4 (on) change
static bool SignalPresent()
{
    int nEnd = Sampler::GetValidRaw();
    bool b1 = Settings.CH1.Enabled == CSettings::AnalogChannel::_YES;
    bool b2 = Settings.CH2.Enabled == CSettings::AnalogChannel::_YES;
    bool b3 = Settings.CH3.Enabled == CSettings::DigitalChannel::_YES;
    bool b4 = Settings.CH4.Enabled == CSettings::DigitalChannel::_YES;
    const uint8_t* p = Sampler::GetBuffer();
    int min1 = 255, max1 = 0, min2 = 255, max2 = 0;
    uint8_t d0 = 0xff;
    for ( int i = Settings.Time.InvalidFirst; i < nEnd; i += 2 )
    {
        const uint8_t* s = p + i * Sampler::BytesPerSample;
        if ( s[0] < min1 ) min1 = s[0];
        if ( s[0] > max1 ) max1 = s[0];
        if ( s[1] < min2 ) min2 = s[1];
        if ( s[1] > max2 ) max2 = s[1];
        uint8_t d = s[2] & ( (b3 ? 1 : 0) | (b4 ? 2 : 0) );
        if ( d0 != 0xff && d != d0 )
            return true;
        d0 = d;
    }
    return ( b1 && max1 - min1 > 12 ) || ( b2 && max2 - min2 > 12 );
}

CMainWnd app;

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{    
    // the translation (LANG.CFG, LANG/<code>/OSC.TXT); scratch: the sample
    // memory, not used yet
    {
        int n;
        uint8_t* scratch = Sampler::Scratch(n);
        Lang::Init("OSC", scratch, n);
        memset(scratch, 0, n);
    }
    SettingsStore::Load();
    Sleep::Load();
    app.Create();
    // zero calibration of the channels (Tools: Zero cal)
    Tools::LoadCalibration();
    app.WindowMessage( CWnd::WmPaint );

    BIOS::KEY::EKey key;
    while (app.IsRunning())
    {
	    key = BIOS::KEY::GetKey();
        // the display sleeps (no key, no signal): a key only wakes it
        if (key != BIOS::KEY::None)
        {
            if (Sleep::Wake())
                key = BIOS::KEY::None;
        }
        {
            // every new acquisition, and every 2 s (roll mode)
            static uint32_t s_nSeq = 0, s_lCheck = 0;
            if (Sampler::GetSequence() != s_nSeq || BIOS::SYS::GetTick() - s_lCheck > 2000)
            {
                s_nSeq = Sampler::GetSequence();
                s_lCheck = BIOS::SYS::GetTick();
                if (SignalPresent())
                    Sleep::Activity();
            }
            Sleep::Tick();
        }
        // F1 (first button) runs / stops the acquisition, its former role
        // (Enter, BIOS::KEY::Enter is F1) moved to F4
        if (key == BIOS::KEY::F1)
        {
            app.ToggleRunStop();
            key = BIOS::KEY::None;
        }
        else if (key == BIOS::KEY::F4)
            key = BIOS::KEY::Enter;
        // F3: automatic setup while the oscilloscope is shown (the trigger
        // item keeps F3 for its source)
        else if (key == BIOS::KEY::F3 && app.m_wndGraph.IsVisible() &&
            !app.m_wndMenuInput.m_itmTrig.HasFocus())
        {
            app.AutoSet();
            key = BIOS::KEY::None;
        }
        if (key != BIOS::KEY::None)
            app.WindowMessage(CWnd::WmKey, key);
		
        app.WindowMessage(CWnd::WmTick);
        // slow generator modulations and sweep
        CCoreGenerator::Tick();
        // PID regulator keeps running in all tabs
        CorePid::Tick();
        // pulse counter (hardware counter on Wave Out)
        DecodeUi::Tick();
        // gate of the FPGA edge counters (measurement FreqHW)
        MeasCore::Tick();
        SettingsStore::Tick();
    }
    DecodeUi::Shutdown();
    SettingsStore::Save();
    
    app.Destroy();
    return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
    BIOS::DBG::Print("Assertion failed in %s [%d]: %s\n", file, line, cond);
//    while (1);
}

