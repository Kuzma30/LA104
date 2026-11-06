#pragma once
#include <stdint.h>

// Quadrature decoder of the oscilloscope FPGA (DS203, FPGA 2.63 from
// system/fpga_ds203/scope): encoder A on input C, B on input D, the index
// Z compared with a threshold on CH1 (CH_A) or CH2 (CH_B). It counts all
// the time, independently of the sampling, the oscilloscope works as usual.
//
// The values are x4 steps (every edge of A and B), forward = A leads B.
// The total position is changed only by Reset(), Z only starts a new
// revolution: position in the revolution = position - position at the last Z.
namespace QuadCounter
{
    enum EStatus { Off, Running, NoDecoder };
    enum EZSource { ZOff, ZCh1, ZCh2 };

    bool Available();               // DS203 only
    bool Start();                   // false when the FPGA has no quadrature decoder
    void Stop();                    // also switches Z off in the FPGA
    bool Active();
    EStatus Status();

    // Z source, polarity and threshold (raw sample value 0..255)
    void SetZ(int nSource, bool bActiveLow, int nThreshold, int nHysteresis);

    void Reset();                   // total position and Z counter to 0
    void Tick();                    // from the main loop, reads the FPGA every 100 ms

    int64_t Position();             // since Reset()
    uint32_t ZCount();              // Z pulses since Reset()
    bool InRev(int64_t& nSteps);    // since the last Z, false before the first Z
    bool PerRev(int32_t& nSteps);   // steps between the last two Z (signed)
    bool Rate(float& fSteps);       // steps per second over 1 s (signed)
}
