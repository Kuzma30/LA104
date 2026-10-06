#include "platform.h"
#include "DS213HwDriver.h"

extern HwDrvDef  Hw;


void SetLogic()
{
  #define Mask32(adr, mask, val) *((volatile uint32_t*)adr) &= ~mask; *((volatile uint32_t*)adr) |= val;
  #define Write32(adr, val) *((volatile uint32_t*)adr) = val;

  Hw.pDevInit(SO_DGTL);

  // PA2_T23 TIM2_CR1
  Write32(0x40000000, 0x80);

  // SetGpioState<GPIOA_BASE, 2, Output50MHz | OutputPushPull>();
  Mask32(0x40010800, 0x0f00, 0x0300);
}

void SetLogicHigh()
{
  // SetGpioLevel<GPIOA_BASE, 2, true>();
  Write32(0x40010810, 1<<2);
}

void SetLogicLow()
{
  // SetGpioLevel<GPIOA_BASE, 2, false>();
  Write32(0x40010814, 1<<2);
}

namespace BIOS
{
  namespace DAC
  {
    void* wave = nullptr;
    int arr = 1;
    int psc = 1;
    int ccr = 1;
    int samples = 1;
    int duty = 50;

    #define Hz *1
    #define KHz *1000
    #define MHz *1000000

    void SetFrequency(int freqHz) 
    {
      if (!wave)
      {
        psc = 1;
        if ( freqHz <= 10 Hz )
          psc = 240;
        else if ( freqHz <= 2 KHz )
          psc = 180;
        else if ( freqHz <= 20 KHz )
          psc = 18;

        arr = 72000000UL / (psc + 1) / freqHz - 1;
        if (arr<2)
          arr = 2;
        if (arr>0xffff)
          arr = 0xffff;
        ccr = arr * (100-duty) / 100;

        *Hw.pFout_TIM_PSC = psc;
        *Hw.pFout_TIM_ARR = arr;
        *Hw.pFout_TIM_CCR = ccr;
        return;
      }

      // timer divides by PSC+1, 19 gives the 72MHz/20 sample clock GetFrequency assumes
      psc = 19;
      arr = 72000000 / 20 / freqHz - 1;
      if (arr<1)
        arr = 1;
      if (arr>0xffff)
        arr = 0xffff;

      *Hw.pFout_DMA_PSC = psc;
      *Hw.pFout_DMA_ARR = arr;
    }

    void SetDuty(int dutyPercent) 
    {
      duty = dutyPercent;
      ccr = arr * (100-duty) / 100;
      *Hw.pFout_TIM_CCR = ccr;
    }

    int GetFrequency() 
    {
      if (!wave)
        return 72000000UL / (arr + 1) / (psc + 1);

      return 72000000UL / 20 / ( arr + 1 ); 
    }

    int GetDuty() 
    {
      return ccr * 100 / arr;
    }

    void SetMode(EMode mode, uint16_t* buffer, int length)
    {
      switch (mode)
      {
        case EMode::Square:
          Hw.pDevInit(SGNLOUT);
          Hw.pDevInit(SO_DGTL);
          wave = nullptr;
          samples = 0;
          break;
        case EMode::Buffer:
          if (wave != buffer || length != samples)
          {
            Hw.pDevInit(SGNLOUT);
            Hw.pDevInit(SO_ANLG);
            Hw.pFout_DMA(DISABLE);
            *Hw.pFout_DMA_CNT = length;
            *Hw.pFout_DMA_CMA = (uint32_t)buffer;
            *Hw.pFout_DMA_PSC = psc;
            *Hw.pFout_DMA_ARR = arr;
            Hw.pFout_DMA(ENABLE);

            samples = length;
            wave = buffer;
          }
        break;
        case EMode::LogicHigh:
          wave = nullptr;
          samples = 0;
          SetLogic();
          SetLogicHigh();
        break;
        case EMode::LogicLow:
          wave = nullptr;
          samples = 0;
          SetLogic();
          SetLogicLow();
        break;
        default:
          _ASSERT(!"Not supported");
      }
    }
  }
}
