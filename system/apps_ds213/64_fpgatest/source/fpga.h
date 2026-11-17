#pragma once
// Access to the registers of the DS203 FPGA 2.64 (edge counters, quadrature
// decoder), see system/fpga_ds203/readme.md.
//
//   write:  Set_Param(addr, value): H_L = 1, serial byte addr, H_L = 0,
//           serial byte value (SCK = PA6, SDA = PA7, MSB first, the byte
//           is taken at the rising edge of SDA while SCK is low)
//   read:   register 0x05 (Select) chooses the word, then C_D (PC4) = 1,
//           H_L (PC5) = 0 and a 16 bit read of the FPGA bank 0x64000000
//
// The same lines are used by SYS and by BIOS::ADC (FIFO reads: H_L toggles,
// C_D = 0). After every access C_D is 0 and H_L is 1 again.

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

  inline int32_t Sign24(uint32_t v)
  {
    v &= 0xffffff;
    return (v & 0x800000) ? (int32_t)(v | 0xff000000) : (int32_t)v;
  }

  // ---- edge counters (32 bit): 0 C rising, 1 C falling, 2 D rising, 3 D falling
  inline uint32_t Edges(int which)
  {
    uint8_t sel = 0x10 + 2 * which;
    uint32_t lo = Read(sel);           // latches the whole value
    uint32_t hi = Read(sel + 1);
    return Gray2Bin((hi << 16) | lo);
  }

  inline void ClearEdges()
  {
    SetParam(0x0E, 1);
    SetParam(0x0E, 0);
  }

  // ---- quadrature, counted by the pins (A = CH_C, B = CH_D)
  struct Quad
  {
    int32_t pos;        // sum of the four up/down counters (24 bit)
    uint32_t part[4];   // AR, AF, BR, BF
  };

  // base 0x20: now, 0x28: captured at the last Z
  inline Quad ReadQuad(uint8_t base)
  {
    uint16_t w[8];
    for (int i = 0; i < 8; i++)
      w[i] = Read(base + i);
    Quad q;
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++)
    {
      q.part[i] = Gray2Bin(((uint32_t)w[2 * i + 1] << 16) | w[2 * i]) & 0xffffff;
      sum += q.part[i];
    }
    q.pos = Sign24(sum);
    return q;
  }

  // Z counter (read after ReadQuad(0x28), which latches it)
  inline uint32_t ZCount()
  {
    return Gray2Bin(Read(0x30));
  }

  // ---- quadrature decoder in the sampling clock MCI
  struct Sync
  {
    int32_t pos;
    uint32_t err;       // 8 bit
    int32_t atZ;
    uint32_t z;         // 16 bit
  };

  inline Sync ReadSync()
  {
    uint16_t w[6];
    for (int i = 0; i < 6; i++)
      w[i] = Read(0x38 + i);           // 0x38 latches the set
    Sync s;
    s.pos = Sign24(Gray2Bin(((uint32_t)w[1] << 16) | w[0]));
    s.err = Gray2Bin(w[2]) & 0xff;
    s.atZ = Sign24(Gray2Bin(((uint32_t)w[4] << 16) | w[3]));
    s.z = Gray2Bin(w[5]) & 0xffff;
    return s;
  }

  // control register 0x0F: bit 1..0 A, 3..2 B, 5..4 Z (0 CH_A, 1 CH_B,
  // 2 CH_C, 3 CH_D), bit 6 Z on, bit 7 Z active low
  inline uint8_t QuadCtrl(int a, int b, int z, bool zOn, bool zLow)
  {
    return (a & 3) | ((b & 3) << 2) | ((z & 3) << 4) | (zOn ? 0x40 : 0) | (zLow ? 0x80 : 0);
  }
  const uint8_t QuadCtrlDefault = 0x0E;   // power up: A = CH_C, B = CH_D, Z off

  inline void SetThresholds(uint8_t thrA, uint8_t hysA, uint8_t thrB, uint8_t hysB)
  {
    SetParam(0x10, thrA);
    SetParam(0x11, hysA);
    SetParam(0x12, thrB);
    SetParam(0x13, hysB);
  }
}

// Square wave / pulses on Wave Out (PB6, TIM4 channel 1), programmed
// directly. PA4 (DAC, connected to PB6 on the board) is released by
// BIOS::DAC::SetMode(LogicLow) at the start.
namespace Wave
{
  using namespace Fpga::Reg;

  const uint32_t Tim4 = 0x40000800;
  enum { Cr1 = 0x00, Ccmr1 = 0x18, Ccer = 0x20, Cnt = 0x24, Psc = 0x28, Arr = 0x2C, Ccr1 = 0x34, Egr = 0x14 };

  inline void PinGpio()   // push-pull output
  {
    R(GpioB + Crl) = (R(GpioB + Crl) & ~0x0f000000) | 0x03000000;
  }
  inline void PinTimer()  // alternate function push-pull
  {
    R(GpioB + Crl) = (R(GpioB + Crl) & ~0x0f000000) | 0x0b000000;
  }

  inline void Low()
  {
    R(GpioB + Brr) = 1 << 6;
    PinGpio();
    R(Tim4 + Cr1) = 0;
  }

  // returns the real frequency (mHz resolution is not needed: Hz * 1000 / 1000)
  inline uint32_t PlanFrequency(uint32_t freq, uint32_t& psc, uint32_t& arr)
  {
    uint32_t div = 72000000UL / freq;
    psc = (div + 65535) / 65536;
    if (psc < 1) psc = 1;
    arr = (72000000UL / psc + freq / 2) / freq;
    if (arr < 2) arr = 2;
    if (arr > 65536) arr = 65536;
    psc -= 1;
    arr -= 1;
    return 72000000UL / (psc + 1) / (arr + 1);
  }

  // square 50 %, starts from low (the pin is low before)
  inline uint32_t Square(uint32_t freq)
  {
    uint32_t psc, arr;
    uint32_t real = PlanFrequency(freq, psc, arr);
    R(0x4002101C) |= 1 << 2;           // RCC_APB1ENR: TIM4
    R(Tim4 + Cr1) = 0;
    R(Tim4 + Psc) = psc;
    R(Tim4 + Arr) = arr;
    R(Tim4 + Ccr1) = (arr + 1) / 2;
    R(Tim4 + Ccmr1) = (R(Tim4 + Ccmr1) & ~0xff) | 0x68;   // PWM mode 1, preload
    R(Tim4 + Ccer) |= 1;
    R(Tim4 + Cnt) = 0;
    R(Tim4 + Egr) = 1;
    R(Tim4 + Cr1) = 0x81;
    PinTimer();
    return real;
  }

  // n pulses by the CPU (interrupts off), about 1 MHz
  inline void Pulses(uint32_t n)
  {
    R(GpioB + Brr) = 1 << 6;
    PinGpio();
    __asm volatile ("cpsid i" ::: "memory");
    for (uint32_t i = 0; i < n; i++)
    {
      R(GpioB + Bsrr) = 1 << 6;
      for (volatile int j = 0; j < 3; j++);
      R(GpioB + Brr) = 1 << 6;
      for (volatile int j = 0; j < 3; j++);
    }
    __asm volatile ("cpsie i" ::: "memory");
  }
}
