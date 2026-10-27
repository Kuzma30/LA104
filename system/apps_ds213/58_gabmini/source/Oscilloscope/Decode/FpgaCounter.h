#pragma once
#include <stdint.h>

// 32 bit edge counters of inputs C and D in the oscilloscope FPGA (DS203,
// FPGA 2.62 from system/fpga_ds203/scope). The counters run all the time,
// independently of the sampling, the oscilloscope works as usual.
//
// Registers are written like SYS Set_Param does it (serial PA6/PA7, H_L PC5)
// and read over the bus with C_D (PC4) high. The FPGA keeps rising and
// falling edges separately, both edges = rising + falling. The 32 bit values
// are extended to 64 bits in software.
namespace FpgaCounter
{
    enum EChannel { C, D };
    enum EStatus { Off, Running, NoCounters };

    bool Available();               // DS203 only
    bool Start(int nEdge);          // false when the FPGA has no counters
    void SetEdge(int nEdge);        // Decoder::ECountEdge
    void Stop();
    bool Active();
    EStatus Status();
    int FpgaVersion();              // from the status word, 262 = with counters

    void Reset();                   // clears the counters in the FPGA
    uint64_t Value(int nChannel);
    // edges per second over 1 s, timed by SysTick
    bool Rate(int nChannel, float& fRate);
    void Tick();                    // from the main loop
}
