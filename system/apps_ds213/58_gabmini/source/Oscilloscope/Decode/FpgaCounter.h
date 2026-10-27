#pragma once
#include <stdint.h>

// 32 bit counters of inputs C and D in a custom FPGA image (DS203).
//
// The image CNT_FPGA.BIN (system/fpga_ds203/counter) is loaded from the disk
// in place of the oscilloscope image, the original image is loaded back from
// the flash by Stop(). Nothing is sampled while the counter image runs.
// The 32 bit hardware values are extended to 64 bits in software.
namespace FpgaCounter
{
    enum EChannel { C, D, T };
    enum EStatus { Off, Running, NoFile, ConfigError, NoId, NoClock };

    bool Available();               // DS203 only
    bool Start(int nEdge);          // Decoder::ECountEdge for both inputs
    void SetEdge(int nEdge);
    void Stop();                    // original image back, scope reconfigured
    bool Active();
    EStatus Status();               // result of the last Start / clock check

    void Reset();                   // clears C and D
    uint64_t Value(int nChannel);
    // edges per second, measured by the FPGA clock over 1 s
    bool Rate(int nChannel, float& fRate);
    void Tick();                    // from the main loop
}
