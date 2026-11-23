#include <library.h>
#include "MainWnd.h"
#include "SettingsStore.h"
#include "Oscilloscope/Meas/MeasCore.h"
#include "Framework/UiText.h"

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
    app.Create();
    // zero calibration of the channels (Tools: Zero cal)
    Tools::LoadCalibration();
    app.WindowMessage( CWnd::WmPaint );

    BIOS::KEY::EKey key;
    while (app.IsRunning())
    {
	    key = BIOS::KEY::GetKey();
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

