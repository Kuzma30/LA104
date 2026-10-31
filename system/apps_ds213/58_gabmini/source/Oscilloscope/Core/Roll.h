#pragma once
#include <stdint.h>

// Roll mode (Scan at 50 ms/div and slower, DS203): samples are read from the
// FPGA FIFO as soon as they are written (the empty flag tells when to stop)
// and appended to the shown window like on a chart recorder, instead of
// waiting until the whole buffer is filled (4096 samples are 136 s at 1 s/div).
//
// The FPGA stops writing after 4095 samples since the restart, the
// acquisition is restarted before that; after a restart the first read is
// stale and the next 151 samples are the pre-trigger part written before the
// restart, which was already read, so they are skipped.
namespace Roll
{
    void Reset();
    // reads the available samples, returns true when some were added
    bool Tick();
}
