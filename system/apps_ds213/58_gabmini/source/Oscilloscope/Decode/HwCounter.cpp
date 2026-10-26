#include <library.h>
#include "HwCounter.h"
#include <Generator/Core/CoreGenerator.h>

namespace HwCounter
{
#if defined(DS203)
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }

    enum : uint32_t {
        TIM4 = 0x40000800, TIM5 = 0x40000C00,
        CR1 = 0x00, CR2 = 0x04, SMCR = 0x08, DIER = 0x0C, SR = 0x10, EGR = 0x14,
        CCMR1 = 0x18, CCER = 0x20, CNT = 0x24, PSC = 0x28, ARR = 0x2C, CCR1 = 0x34,
        RCC_APB1ENR = 0x4002101C, RCC_TIM5EN = 1 << 3,
        GPIOA_CRL = 0x40010800, GPIOB_CRL = 0x40010C00,
        DAC_CR = 0x40007400, TIM7_CR1 = 0x40001400, DMA2_CCR4 = 0x40020444,
        SYST_LOAD = 0xE000E014, SYST_VAL = 0xE000E018, SCB_ICSR = 0xE000ED04,
        PENDSTSET = 1 << 26
    };

    struct TSaved
    {
        uint32_t tim4[9];
        uint32_t pa4, pb6, dacCr, tim7Cr1, dmaCcr, rcc;
    };
    static const uint8_t g_tim4Regs[9] = {CR1, CR2, SMCR, DIER, CCMR1, CCER, PSC, ARR, CCR1};

    static TSaved s_saved;
    static bool s_bActive = false;
    static int s_nEdge = 0;

    static uint32_t s_nLast32 = 0;      // last hardware value, to detect wrap
    static uint32_t s_nHigh = 0;        // software extension (bits 32..63)
    static uint64_t s_nBase = 0;        // value at the last reset

    static uint64_t s_nGateCount = 0;
    static uint64_t s_nGateClocks = 0;
    static uint32_t s_nGateTick = 0;
    static float s_fRate = 0;
    static bool s_bRate = false;

    static uint32_t _Read32()
    {
        // TIM5 is incremented a few clocks after TIM4 wraps, read it twice
        uint32_t nHi1 = Reg(TIM5 + CNT) & 0xffff;
        uint32_t nLo = Reg(TIM4 + CNT) & 0xffff;
        for ( volatile int i = 0; i < 2; i++ )
            ;
        uint32_t nHi2 = Reg(TIM5 + CNT) & 0xffff;
        if ( nHi1 != nHi2 )
            nHi1 = nLo < 0x8000 ? nHi2 : nHi1;  // wrap before / after reading TIM4
        return (nHi1 << 16) | nLo;
    }

    static uint64_t _Read64()
    {
        uint32_t n = _Read32();
        if ( n < s_nLast32 )
            s_nHigh++;
        s_nLast32 = n;
        return ((uint64_t)s_nHigh << 32) | n;
    }

    // counter value and time in SysTick clocks taken at the same moment
    static void _Snapshot(uint64_t& nCount, uint64_t& nClocks)
    {
        uint32_t nPrimask;
        __asm volatile ("mrs %0, primask\n cpsid i" : "=r"(nPrimask) :: "memory");
        uint32_t nTick = BIOS::SYS::GetTick();
        uint32_t nVal = Reg(SYST_VAL);
        if ( Reg(SCB_ICSR) & PENDSTSET )
        {
            // SysTick wrapped, its interrupt did not run yet
            nVal = Reg(SYST_VAL);
            nTick++;
        }
        nCount = _Read64();
        __asm volatile ("msr primask, %0" :: "r"(nPrimask) : "memory");
        uint32_t nLoad = Reg(SYST_LOAD) & 0xffffff;
        nClocks = (uint64_t)nTick * (nLoad + 1) + (nLoad - (nVal & 0xffffff));
    }

    static void _StartGate()
    {
        _Snapshot( s_nGateCount, s_nGateClocks );
        s_nGateTick = BIOS::SYS::GetTick();
        s_bRate = false;
    }

    bool Available()
    {
        return true;
    }

    void Start(int nEdge)
    {
        if ( s_bActive )
            Stop();
        s_nEdge = nEdge;

        for ( int i = 0; i < 9; i++ )
            s_saved.tim4[i] = Reg(TIM4 + g_tim4Regs[i]);
        s_saved.pa4 = Reg(GPIOA_CRL) & (0xfu << 16);
        s_saved.pb6 = Reg(GPIOB_CRL) & (0xfu << 24);
        s_saved.dacCr = Reg(DAC_CR);
        s_saved.tim7Cr1 = Reg(TIM7_CR1);
        s_saved.dmaCcr = Reg(DMA2_CCR4);
        s_saved.rcc = Reg(RCC_APB1ENR) & RCC_TIM5EN;

        // release the connector: no DAC output, no square wave
        CCoreGenerator::Suspend(true);
        Reg(DMA2_CCR4) &= ~1u;
        Reg(TIM7_CR1) &= ~1u;
        Reg(DAC_CR) &= ~0x1fffu;
        Reg(GPIOA_CRL) = (Reg(GPIOA_CRL) & ~(0xfu << 16)) | (0x4u << 16); // PA4 floating input
        Reg(GPIOB_CRL) = (Reg(GPIOB_CRL) & ~(0xfu << 24)) | (0x4u << 24); // PB6 floating input

        // TIM4: clock from TI1 (PB6), no prescaler, update -> TRGO
        Reg(TIM4 + CR1) = 0;
        Reg(TIM4 + DIER) = 0;
        Reg(TIM4 + CCER) = 0;
        Reg(TIM4 + CCMR1) = (Reg(TIM4 + CCMR1) & 0xff00) | 0x01;   // CC1S = TI1, no filter
        if ( nEdge == 1 )
            Reg(TIM4 + CCER) = 1 << 1;                           // CC1P: falling edge
        // TS = 100 (TI1 edge detector, both edges) or 101 (TI1FP1), SMS = 111
        Reg(TIM4 + SMCR) = ((nEdge == 2 ? 4u : 5u) << 4) | 7;
        Reg(TIM4 + PSC) = 0;
        Reg(TIM4 + ARR) = 0xffff;
        Reg(TIM4 + CR2) = 2 << 4;                                // MMS = update
        Reg(TIM4 + EGR) = 1;                                     // load PSC, CNT = 0
        Reg(TIM4 + SR) = 0;

        // TIM5: counts the overflows of TIM4 (ITR2 = TIM4)
        Reg(RCC_APB1ENR) |= RCC_TIM5EN;
        Reg(TIM5 + CR1) = 0;
        Reg(TIM5 + DIER) = 0;
        Reg(TIM5 + PSC) = 0;
        Reg(TIM5 + ARR) = 0xffff;
        Reg(TIM5 + SMCR) = (2u << 4) | 7;
        Reg(TIM5 + EGR) = 1;
        Reg(TIM5 + CNT) = 0;
        Reg(TIM5 + SR) = 0;
        Reg(TIM5 + CR1) = 1;

        Reg(TIM4 + CNT) = 0;
        Reg(TIM4 + CR1) = 1;

        s_bActive = true;
        s_nLast32 = 0;
        s_nHigh = 0;
        s_nBase = 0;
        _StartGate();
    }

    void Stop()
    {
        if ( !s_bActive )
            return;
        s_bActive = false;
        s_bRate = false;

        Reg(TIM5 + CR1) = 0;
        Reg(TIM5 + SMCR) = 0;
        if ( !s_saved.rcc )
            Reg(RCC_APB1ENR) &= ~RCC_TIM5EN;

        Reg(TIM4 + CR1) = 0;
        for ( int i = 1; i < 9; i++ )
            Reg(TIM4 + g_tim4Regs[i]) = s_saved.tim4[i];
        Reg(TIM4 + EGR) = 1;
        Reg(TIM4 + SR) = 0;
        Reg(TIM4 + CR1) = s_saved.tim4[0];

        Reg(GPIOB_CRL) = (Reg(GPIOB_CRL) & ~(0xfu << 24)) | s_saved.pb6;
        Reg(GPIOA_CRL) = (Reg(GPIOA_CRL) & ~(0xfu << 16)) | s_saved.pa4;
        Reg(DAC_CR) = s_saved.dacCr;
        Reg(TIM7_CR1) = s_saved.tim7Cr1;
        Reg(DMA2_CCR4) = s_saved.dmaCcr;

        // the generator sets up its output again
        CCoreGenerator::Suspend(false);
    }

    bool Active()
    {
        return s_bActive;
    }

    int Edge()
    {
        return s_nEdge;
    }

    void Reset()
    {
        if ( !s_bActive )
            return;
        s_nBase = _Read64();
    }

    uint64_t Value()
    {
        if ( !s_bActive )
            return 0;
        return _Read64() - s_nBase;
    }

    bool Rate(float& fRate)
    {
        fRate = s_fRate;
        return s_bActive && s_bRate;
    }

    void Tick()
    {
        if ( !s_bActive )
            return;
        if ( BIOS::SYS::GetTick() - s_nGateTick < 1000 )
        {
            _Read64();
            return;
        }
        uint64_t nCount, nClocks;
        _Snapshot( nCount, nClocks );
        uint32_t nLoad = Reg(SYST_LOAD) & 0xffffff;
        uint64_t dClocks = nClocks - s_nGateClocks;
        if ( dClocks > 0 )
        {
            // SysTick runs one interrupt per millisecond
            float fSeconds = (float)dClocks / ((float)(nLoad + 1) * 1000.0f);
            s_fRate = (float)(nCount - s_nGateCount) / fSeconds;
            s_bRate = true;
        }
        s_nGateCount = nCount;
        s_nGateClocks = nClocks;
        s_nGateTick = BIOS::SYS::GetTick();
    }
#else
    bool Available() { return false; }
    void Start(int) {}
    void Stop() {}
    bool Active() { return false; }
    int Edge() { return 0; }
    void Reset() {}
    uint64_t Value() { return 0; }
    bool Rate(float& fRate) { fRate = 0; return false; }
    void Tick() {}
#endif
}
