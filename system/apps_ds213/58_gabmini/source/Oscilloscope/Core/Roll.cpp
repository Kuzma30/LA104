#include <Framework.h>
#include <Settings.h>
#include <Oscilloscope/Controls/GraphBase.h>
#include "Roll.h"
#include "FpgaBus.h"
#include "CoreOscilloscope.h"

namespace Roll
{
    enum {
        SkipAfterRestart = 1 + 151,
        RestartAfter = 3800,        // FIFO is full after 4095 samples
        MaxPerTick = 64
    };

    static int s_nSkip = 0;
    static int s_nSinceRestart = 0;
    static uint32_t s_nGeneration = 0;
    static bool s_bStarted = false;

    static void _Restart()
    {
        BIOS::ADC::Restart(0);
        s_nSkip = SkipAfterRestart;
        s_nSinceRestart = 0;
    }

    void Reset()
    {
        _Restart();
        Sampler::RollStart( Settings.Time.Shift );
        s_nGeneration = CCoreOscilloscope::ConfigGeneration();
        s_bStarted = true;
    }

    bool Tick()
    {
        // new settings restart the acquisition, the shown window starts again
        if ( !s_bStarted || s_nGeneration != CCoreOscilloscope::ConfigGeneration() )
            Reset();

        uint32_t arrNew[MaxPerTick];
        int nNew = 0;
        uint32_t nSample;
        bool bEmpty = false;
        while ( nNew < MaxPerTick )
        {
            if ( !FpgaBus::ReadSample( nSample ) )
            {
                bEmpty = true;
                break;
            }
            s_nSinceRestart++;
            if ( s_nSkip > 0 )
            {
                s_nSkip--;
                continue;
            }
            arrNew[nNew++] = nSample;
        }

        if ( FpgaBus::Status() & FpgaBus::StFull )
        {
            // not read in time (stopped, busy): continue with a gap
            _Restart();
        } else
        if ( bEmpty && s_nSinceRestart >= RestartAfter )
        {
            // everything written so far was read, restart before the FIFO stops
            _Restart();
        }

        if ( nNew > 0 )
            Sampler::RollAppend( arrNew, nNew, Settings.Time.Shift, CWndGraph::MaxX );
        return nNew > 0;
    }
}
