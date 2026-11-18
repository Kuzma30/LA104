#pragma once
// Registers of the DS203 FPGA 2.66+ (edge counters), the same access as
// system/apps_ds213/64_fpgatest/source/fpga.h:
//   write: Set_Param(addr, value) over SCK = PA6 / SDA = PA7, H_L = PC5
//   read:  register 0x05 (Select) chooses the word, C_D (PC4) = 1, H_L = 0
// The lines are shared with SYS and BIOS::ADC; after every access C_D is 0
// and H_L is 1 again. Used only from the main loop, never inside an ADC read.

#include <stdint.h>

namespace Fpga
{
  namespace Reg
  {
    enum : uint32_t
    {
      GpioA = 0x40010800, GpioB = 0x40010C00, GpioC = 0x40011000,
      Crl = 0x00, Idr = 0x08, Odr = 0x0C, Bsrr = 0x10, Brr = 0x14,
    };
    inline volatile uint32_t& R(uint32_t a) { return *(volatile uint32_t*)a; }
  }

  const uint32_t PinSck = 1 << 6;   // PA6
  const uint32_t PinSda = 1 << 7;   // PA7
  const uint32_t PinCd  = 1 << 4;   // PC4 (ClrR on the schematic)
  const uint32_t PinHl  = 1 << 5;   // PC5

  inline void Delay()
  {
    // ~0.2 us at 72 MHz, the lines are slow (the FPGA takes SCK / SDA as clocks)
    for (volatile int i = 0; i < 4; i++);
  }

  inline void Set(uint32_t port, uint32_t pin, bool v)
  {
    Reg::R(port + (v ? Reg::Bsrr : Reg::Brr)) = pin;
    Delay();
  }

  inline void Init()
  {
    using namespace Reg;
    // PC4 is driven by SYS already; make sure it is an output (push-pull, 50 MHz)
    uint32_t crl = R(GpioC + Crl);
    if (((crl >> 16) & 3) == 0)
      R(GpioC + Crl) = (crl & ~0x000f0000) | 0x00030000;
    Set(GpioC, PinCd, false);
    Set(GpioC, PinHl, true);
  }

  inline void SendByte(uint8_t d)
  {
    using namespace Reg;
    Set(GpioA, PinSck, true);
    for (int i = 0; i < 8; i++)
    {
      Set(GpioA, PinSda, (d << i) & 0x80);
      Set(GpioA, PinSck, false);
      Set(GpioA, PinSck, true);
    }
    Set(GpioA, PinSda, false);
    Set(GpioA, PinSck, false);
    Set(GpioA, PinSda, true);       // rising edge of SDA with SCK low: take the byte
  }

  inline void SetParam(uint8_t addr, uint8_t value)
  {
    using namespace Reg;
    Set(GpioC, PinHl, true);
    SendByte(addr);
    Set(GpioC, PinHl, false);
    SendByte(value);
    Set(GpioC, PinHl, true);
  }

  inline uint16_t BusRead()
  {
    return *(volatile uint16_t*)0x64000000;
  }

  // word selected by register 0x05
  inline uint16_t Read(uint8_t select)
  {
    using namespace Reg;
    SetParam(0x05, select);
    Set(GpioC, PinCd, true);
    Set(GpioC, PinHl, false);
    uint16_t v = BusRead();
    Set(GpioC, PinCd, false);
    Set(GpioC, PinHl, true);
    return v;
  }

  // status word (C_D = 0, H_L = 0): version in bits 15..6
  inline uint16_t Status()
  {
    using namespace Reg;
    Set(GpioC, PinCd, false);
    Set(GpioC, PinHl, false);
    uint16_t v = BusRead();
    Set(GpioC, PinHl, true);
    return v;
  }

  inline uint32_t Gray2Bin(uint32_t g)
  {
    g ^= g >> 16;
    g ^= g >> 8;
    g ^= g >> 4;
    g ^= g >> 2;
    g ^= g >> 1;
    return g;
  }

}
