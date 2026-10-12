#include <library.h>
#include "MainWnd.h"
#include "SettingsStore.h"

CMainWnd app;

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{    
    SettingsStore::Load();
    app.Create();
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
        if (key != BIOS::KEY::None)
            app.WindowMessage(CWnd::WmKey, key);
		
        app.WindowMessage(CWnd::WmTick);
        // slow generator modulations and sweep
        CCoreGenerator::Tick();
        // PID regulator keeps running in all tabs
        CorePid::Tick();
        SettingsStore::Tick();
    }
    SettingsStore::Save();
    
    app.Destroy();
    return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
    BIOS::DBG::Print("Assertion failed in %s [%d]: %s\n", file, line, cond);
//    while (1);
}

