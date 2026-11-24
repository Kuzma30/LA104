#pragma once
// Battery voltage of the DS203 (one Li-ion cell): PC2 through a divider
// 1:2 (schematic HW 2.6x). The SYS attribute V_BATTERY reports nothing
// useful under the LA104 OS (0.08 V), so the voltage is read here with ADC2,
// channel 12 (PC2), against VDDA = 3.3 V. Not used by the OS or the
// applications otherwise.
#include <stdint.h>

namespace Battery
{
  enum { VddaMv = 3300, Divider = 2 };

#if defined(_ARM) && defined(DS203)
  inline volatile uint32_t& _R(uint32_t a) { return *(volatile uint32_t*)a; }
  enum : uint32_t {
    RCC_CFGR = 0x40021004, RCC_APB2ENR = 0x40021018,
    GPIOC_CRL = 0x40011000,
    ADC2 = 0x40012800, SR = 0x00, CR1 = 0x04, CR2 = 0x08, SMPR1 = 0x0C, SQR1 = 0x2C, SQR3 = 0x34, DR = 0x4C
  };

  inline void _Wait(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) {} }

  inline void _Init()
  {
    static bool bInit = false;
    if (bInit)
      return;
    bInit = true;
    _R(RCC_APB2ENR) |= (1 << 10) | (1 << 4);          // ADC2, GPIOC
    // ADC clock at most 14 MHz: PCLK2 / 6
    uint32_t cfgr = _R(RCC_CFGR);
    if (((cfgr >> 14) & 3) < 2)
      _R(RCC_CFGR) = (cfgr & ~(3u << 14)) | (2u << 14);
    _R(GPIOC_CRL) &= ~(0xfu << 8);                     // PC2 analog input
    _R(ADC2 + CR1) = 0;
    _R(ADC2 + CR2) = 1;                                // ADON
    _Wait(1000);
    _R(ADC2 + CR2) |= 1 << 3;                          // RSTCAL
    for (int i = 0; i < 100000 && (_R(ADC2 + CR2) & (1 << 3)); i++) {}
    _R(ADC2 + CR2) |= 1 << 2;                          // CAL
    for (int i = 0; i < 100000 && (_R(ADC2 + CR2) & (1 << 2)); i++) {}
    _R(ADC2 + SMPR1) = (_R(ADC2 + SMPR1) & ~(7u << 6)) | (7u << 6);   // ch 12: 239.5 cycles
    _R(ADC2 + SQR1) = 0;                               // one conversion
    _R(ADC2 + SQR3) = 12;
  }

  // the voltage of the battery in mV (average of 16 conversions)
  inline int ReadMv()
  {
    _Init();
    uint32_t sum = 0;
    for (int n = 0; n < 16; n++)
    {
      _R(ADC2 + SR) = 0;
      _R(ADC2 + CR2) |= 1;                             // ADON again: start
      int i = 0;
      while (!(_R(ADC2 + SR) & 2) && i++ < 100000) {}
      sum += _R(ADC2 + DR) & 0xfff;
    }
    return (int)(sum * VddaMv * Divider / (16 * 4095));
  }
#else
  inline int ReadMv() { return 3900; }
#endif
}
