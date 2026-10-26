#pragma once
#include <stdint.h>

// Hardware pulse counter on the Wave Out connector (DS203).
//
// Inputs C and D are sampled by the FPGA only, the timers of the STM32 can
// not see them. The Wave Out connector is wired to PA4 (DAC) and PB6, and PB6
// is the input 1 of timer TIM4. While counting, the generator is suspended,
// PA4 and PB6 are inputs, TIM4 counts the edges (external clock mode 1) and
// its overflows are counted by TIM5 (TIM4 TRGO -> ITR2), so the hardware
// counter has 32 bits and counts without gaps, independently of the sampling.
// The 32 bit value is extended to 64 bits in software.
//
// Input level 0..3.3 V (PA4 is not 5 V tolerant).
namespace HwCounter
{
    bool Available();               // false on other devices than DS203
    void Start(int nEdge);          // Decoder::ECountEdge
    void Stop();                    // restores the timers and the generator
    bool Active();
    int Edge();

    void Reset();
    uint64_t Value();
    // edges per second measured with 1 s gate, false until the first gate ends
    bool Rate(float& fRate);
    // call often (at least every minute): overflow of the 32 bit counter and
    // the frequency gate
    void Tick();
}
