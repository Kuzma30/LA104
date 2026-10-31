#include <library.h>
#include "FpgaBus.h"

namespace FpgaBus
{
#if defined(DS203)
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }
    static inline volatile uint16_t& Bus() { return *(volatile uint16_t*)0x64000000; }

    enum : uint32_t {
        GPIOA_BSRR = 0x40010810, GPIOA_BRR = 0x40010814,
        GPIOC_BSRR = 0x40011010, GPIOC_BRR = 0x40011014,
        SCK = 1 << 6, SDA = 1 << 7,             // GPIOA
        C_D = 1 << 4, H_L = 1 << 5              // GPIOC
    };

    static inline void _Sync()
    {
        __asm volatile ("dsb" ::: "memory");    // pins set before the bus access
    }

    // SendByte of SYS
    static void _SendByte(uint8_t nData)
    {
        Reg(GPIOA_BSRR) = SCK;
        for ( int i = 0; i < 8; i++ )
        {
            Reg((nData << i) & 0x80 ? GPIOA_BSRR : GPIOA_BRR) = SDA;
            Reg(GPIOA_BRR) = SCK;
            Reg(GPIOA_BSRR) = SCK;
        }
        Reg(GPIOA_BRR) = SDA;
        Reg(GPIOA_BRR) = SCK;
        Reg(GPIOA_BSRR) = SDA;      // latches the byte
    }

    bool Available()
    {
        return true;
    }

    void SetParam(uint8_t nAddr, uint8_t nValue)
    {
        Reg(GPIOC_BSRR) = H_L;
        _SendByte( nAddr );
        Reg(GPIOC_BRR) = H_L;
        _SendByte( nValue );
    }

    void SetDepth(int nSamples)
    {
        if ( nSamples < 16 ) nSamples = 16;
        if ( nSamples > 4095 ) nSamples = 4095;
        SetParam( 6, nSamples & 0xff );
        SetParam( 7, nSamples >> 8 );
    }

    uint16_t Status()
    {
        Reg(GPIOC_BRR) = H_L | C_D;
        _Sync();
        return Bus();
    }

    bool ReadSample(uint32_t& nSample)
    {
        // the high word carries the status: reading it does not move the
        // read pointer, the low word (H_L high) does
        uint32_t nHigh = Status();
        if ( nHigh & StEmpty )
            return false;
        Reg(GPIOC_BSRR) = H_L;
        _Sync();
        uint32_t nLow = Bus();
        nSample = (nHigh << 16) | nLow;
        return true;
    }

    uint16_t ReadSelected(uint8_t nSelect)
    {
        SetParam( 5, nSelect );
        Reg(GPIOC_BSRR) = C_D;
        _Sync();
        uint16_t n = Bus();
        Reg(GPIOC_BRR) = C_D;
        return n;
    }
#else
    bool Available() { return false; }
    void SetParam(uint8_t, uint8_t) {}
    void SetDepth(int) {}
    uint16_t Status() { return 0; }
    bool ReadSample(uint32_t&) { return false; }
    uint16_t ReadSelected(uint8_t) { return 0; }
#endif
}
