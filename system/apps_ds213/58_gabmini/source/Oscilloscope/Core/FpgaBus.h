#pragma once
#include <stdint.h>

// Direct access to the FPGA of the DS203, the same signals as SYS uses
// (system/fpga_ds203/stock_v25): serial register writes on PA6 (SCK) /
// PA7 (SDA) with H_L (PC5) selecting address or data, reads at 0x64000000
// with H_L / C_D (PC4) selecting the word.
//
// Registers (FPGA 2.5+): 0 trigger mode (also sets Depth to 4095), 1 Vt,
// 2/3 Tt, 4 control, 5 select, 6/7 depth (samples until Full),
// 8/9 pre-sampling, 0x0A..0x0D delay, 0x0E counters (2.62).
// FPGA 2.68: 0x0A/0x0B peak detection K - 1, 0x0C/0x0D Tt bits 16..31
// (the delay is removed), Select 0x1B ID 0xC0D8; cleared by register 0.
namespace FpgaBus
{
    bool Available();                       // DS203 only

    void SetParam(uint8_t nAddr, uint8_t nValue);
    // samples written after the restart until the FIFO reports Full
    void SetDepth(int nSamples);

    // status word: bit 0..1 CH3/CH4, 2 ready (triggered), 3 full,
    // 4 empty, 5 start, 15..6 version
    enum { StReady = 1 << 2, StFull = 1 << 3, StEmpty = 1 << 4 };
    uint16_t Status();

    // one sample (as BIOS::ADC::Get) when the FIFO is not empty
    bool ReadSample(uint32_t& nSample);

    // register selected by 'select', read with C_D high
    uint16_t ReadSelected(uint8_t nSelect);

    // FPGA 2.68 or newer with peak detection (checked once)
    bool PeakAvailable();
    // peak detection: nK fast samples give one pair (min, max), 0 = off;
    // the time threshold of the pulse triggers in fast samples (32 bits).
    // Written after the trigger mode (register 0 clears both).
    void SetPeak(int nK, uint32_t nTime);
}
