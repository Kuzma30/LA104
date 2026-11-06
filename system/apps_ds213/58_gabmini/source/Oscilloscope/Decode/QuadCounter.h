#pragma once
#include <stdint.h>

// Quadrature decoder of the oscilloscope FPGA (DS203, FPGA 2.64 from
// system/fpga_ds203/scope). A, B and the index Z can be on any input:
// CH1 / CH2 (analog, compared with a threshold) or CH3 / CH4 (digital).
// It counts all the time, independently of the acquisition, the
// oscilloscope works as usual.
//
// - A and B on CH3 and CH4: counted by the input pins, the timebase does
//   not matter
// - otherwise: counted in the sampling clock, the steps must be slower than
//   the sample rate of the timebase, faster steps are counted as errors
//
// The values are x4 steps (every edge of A and B), forward = A leads B.
// The total position is changed only by Reset(), Z only starts a new
// revolution: position in the revolution = position - position at the last Z.
namespace QuadCounter
{
    enum EStatus { Off, Running, NoDecoder };
    enum { Ch1, Ch2, Ch3, Ch4, ChOff };

    bool Available();               // DS203 only
    bool Start();                   // false when the FPGA has no quadrature decoder
    void Stop();                    // also switches Z off in the FPGA
    bool Active();
    EStatus Status();

    // inputs (Ch1..Ch4, Z also ChOff), Z polarity and the thresholds of the
    // analog channels (raw sample value 0..255). Other inputs than before
    // start the counting again.
    void Configure(int nA, int nB, int nZ, bool bZActiveLow,
                   int nThr1, int nHys1, int nThr2, int nHys2);
    bool ByPins();                  // A and B on CH3 and CH4

    void Reset();                   // total position, Z and error counters to 0
    void Tick();                    // from the main loop, reads the FPGA every 100 ms

    int64_t Position();             // since Reset()
    uint32_t ZCount();              // Z pulses since Reset()
    uint32_t Errors();              // steps faster than the sampling (since Reset())
    uint32_t LastErrorTick();       // BIOS::SYS::GetTick() of the last new error
    bool InRev(int64_t& nSteps);    // since the last Z, false before the first Z
    bool PerRev(int32_t& nSteps);   // steps between the last two Z (signed)
    bool Rate(float& fSteps);       // steps per second over 1 s (signed)
}
