#pragma once
// Battery voltage of the DS203 (one Li-ion cell): PC2 through a divider
// 1:2 (schematic HW 2.6x). The SYS attribute V_BATTERY reports nothing
// useful under the LA104 OS (0.08 V), so the voltage is read here with ADC1:
// channel 12 (PC2) against the internal reference (channel 17, 1.20 V
// typical), which does not depend on the supply of the ADC. ADC1 is not
// used by the OS or the applications otherwise.
#include <stdint.h>

namespace Battery
{
  enum { VrefintMv = 1200, Divider = 2 };

#if defined(_ARM) && defined(DS203)
  inline volatile uint32_t& _R(uint32_t a) { return *(volatile uint32_t*)a; }
  enum : uint32_t {
    RCC_CFGR = 0x40021004, RCC_APB2ENR = 0x40021018,
    GPIOC_CRL = 0x40011000,
    ADC1 = 0x40012400, SR = 0x00, CR1 = 0x04, CR2 = 0x08, SMPR1 = 0x0C, SQR1 = 0x2C, SQR3 = 0x34, DR = 0x4C
  };

  inline void _Wait(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) {} }

  inline void _Init()
  {
    static bool bInit = false;
    if (bInit)
      return;
    bInit = true;
    _R(RCC_APB2ENR) |= (1 << 9) | (1 << 4);           // ADC1, GPIOC
    // ADC clock at most 14 MHz: PCLK2 / 6
    uint32_t cfgr = _R(RCC_CFGR);
    if (((cfgr >> 14) & 3) < 2)
      _R(RCC_CFGR) = (cfgr & ~(3u << 14)) | (2u << 14);
    _R(GPIOC_CRL) &= ~(0xfu << 8);                     // PC2 analog input
    _R(ADC1 + CR1) = 0;
    _R(ADC1 + CR2) = 1 | (1u << 23);                   // ADON, TSVREFE (Vrefint)
    _Wait(2000);
    _R(ADC1 + CR2) |= 1 << 3;                          // RSTCAL
    for (int i = 0; i < 100000 && (_R(ADC1 + CR2) & (1 << 3)); i++) {}
    _R(ADC1 + CR2) |= 1 << 2;                          // CAL
    for (int i = 0; i < 100000 && (_R(ADC1 + CR2) & (1 << 2)); i++) {}
    // channels 12 (PC2) and 17 (Vrefint): 239.5 cycles
    _R(ADC1 + SMPR1) |= (7u << 6) | (7u << 21);
    _R(ADC1 + SQR1) = 0;                               // one conversion
  }

  inline uint32_t _Sum(int ch)
  {
    _R(ADC1 + SQR3) = ch;
    uint32_t sum = 0;
    for (int n = 0; n < 16; n++)
    {
      _R(ADC1 + SR) = 0;
      _R(ADC1 + CR2) |= 1;                             // ADON again: start
      int i = 0;
      while (!(_R(ADC1 + SR) & 2) && i++ < 100000) {}
      sum += _R(ADC1 + DR) & 0xfff;
    }
    return sum;
  }

  // the voltage of the battery in mV: PC2 against the internal reference
  // (1.20 V), so the supply of the ADC does not matter
  inline int ReadMv()
  {
    _Init();
    uint32_t ref = _Sum(17), bat = _Sum(12);
    if (ref == 0)
      return 0;
    return (int)((uint64_t)bat * VrefintMv * Divider / ref);
  }
#else
  inline int ReadMv() { return 3900; }
#endif
}
